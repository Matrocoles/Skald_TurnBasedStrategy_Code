#include "Skald_GameMode.h"
#include "Algo/RandomShuffle.h"
#include "Algo/Sort.h"
#include "Camera/CameraComponent.h"
#include "Components/AudioComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GridBattleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Skald.h"
#include "Skald_EnumUtils.h"
#include "SkaldLogging.h"
#include "SkaldSaveGame.h"
#include "Misc/Char.h"
#if defined(WITH_AUTOMATION_TESTS) && WITH_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif
#include "Skald_AIController.h"
#include "Skald_BattleGameMode.h"
#include "Skald_GameInstance.h"
#include "SkaldDiceManager.h"
#include "Skald_GameState.h"
#include "Skald_PropertyAccess.h"
#include "Skald_PlayerCharacter.h"
#include "Skald_PlayerController.h"
#include "Skald_PlayerState.h"
#include "Skald_TurnManager.h"
#include "Sound/SoundBase.h"
#include "Territory.h"
#include "TimerManager.h"
#include "UI/SkaldMainHUDWidget.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"
#include "WorldMap.h"
#include "Containers/Set.h"

namespace {
constexpr float StartGameTimeout = 10.f;
constexpr int32 StartingResources = 100;
constexpr float RetryInitDelay = 0.01f;
constexpr float ArmyPlacementAutoAdvanceDelay = 0.15f;
const TArray<FString> FantasyAINames = {
    TEXT("Aeloria Swiftwind"), TEXT("Borin Stonefist"),
    TEXT("Caelynn Starwhisper"), TEXT("Durgan Ironmantle"),
    TEXT("Elowen Nightbloom"), TEXT("Faelar Moonshadow"),
    TEXT("Garrick Stormspear"), TEXT("Halia Emberforge"),
    TEXT("Ilyrion Dawnseeker"), TEXT("Jorvan Frostguard"),
    TEXT("Kaelin Ravensong"), TEXT("Lirra Sunveil"),
    TEXT("Maelor Runebound"), TEXT("Nyssa Thornheart"),
    TEXT("Orin Blackflame"), TEXT("Pyria Windwalker"),
    TEXT("Quorin Silvercrest"), TEXT("Rhydan Wolfshield"),
    TEXT("Sylas Duskreaver"), TEXT("Thalia Brightblade"),
    TEXT("Vaelis Stormbinder"), TEXT("Wrenna Shadeleaf"),
    TEXT("Xandar Starforge"), TEXT("Ysolde Whisperwind"),
    TEXT("Zarek Ironbloom")};
// Instance variables moved into ASkaldGameMode to avoid cross-instance
// interference; see header for declarations.
} // namespace

using Skald::PropertyAccess::ReadBoolProperty;
using Skald::PropertyAccess::ReadIntProperty;
using Skald::PropertyAccess::WriteBoolProperty;
using Skald::PropertyAccess::WriteIntProperty;

ASkaldGameMode::ASkaldGameMode() {
  GameStateClass = ASkaldGameState::StaticClass();
  PlayerStateClass = ASkaldPlayerState::StaticClass();
  PlayerControllerClass = ASkaldPlayerController::StaticClass();
  DefaultPawnClass = ASkald_PlayerCharacter::StaticClass();

  TurnManager = nullptr;
  TurnManagerClass = ATurnManager::StaticClass();
  WorldMap = nullptr;
  bTurnsStarted = false;
  bWorldInitialized = false;
  bAIPlayersSpawned = false;
  bPendingInitialSnapshot = false;
  AIControllerClass = ASkaldAIController::StaticClass();

  NextSiegeID = 1;

  bAwaitingStrategicInitiativeInput = false;
  bStrategicInitiativePromptIssued = false;
}

FString ASkaldGameMode::InitNewPlayer(APlayerController *NewPlayer,
                                      const FUniqueNetIdRepl &UniqueId,
                                      const FString &Options,
                                      const FString &Portal) {
  const FString Error =
      Super::InitNewPlayer(NewPlayer, UniqueId, Options, Portal);
  if (!Error.IsEmpty()) {
    return Error;
  }

  ASkaldPlayerController *PC = Cast<ASkaldPlayerController>(NewPlayer);
  ASkaldPlayerState *PS = PC ? PC->GetPlayerState<ASkaldPlayerState>() : nullptr;
  if (!PS) {
    return Error;
  }

  FString RequestedName = UGameplayStatics::ParseOption(Options, TEXT("Name"));
  if (RequestedName.IsEmpty()) {
    RequestedName = UGameplayStatics::ParseOption(Options, TEXT("PlayerName"));
  }
  RequestedName.TrimStartAndEndInline();

  if (!RequestedName.IsEmpty()) {
    PS->PlayerDisplayName = RequestedName;
    if (PS->GetPlayerName().IsEmpty() || PS->GetPlayerName() != RequestedName) {
      PS->SetPlayerName(RequestedName);
    }
  } else {
    PS->EnsureDefaultPlayerName();
  }

  return Error;
}

void ASkaldGameMode::InitGame(const FString &Map, const FString &Options,
                              FString &Error) {
  Super::InitGame(Map, Options, Error);

  USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>();
  MinPlayerCount = 1;
  if (GI && GI->bIsMultiplayer) {
    const int32 ExpectedHumans = FMath::Max(1, GI->ExpectedLobbyPlayerCount);
    MinPlayerCount = ExpectedHumans;
  }

  // Reset transient state in case the same GameMode instance is reused after
  // travelling back from a battle. Any lingering timers or cached pointers can
  // prevent the overworld from reinitialising correctly which in turn blocks
  // the overview map from loading its territory snapshot.
  TurnManager = nullptr;
  WorldMap = nullptr;
  bTurnsStarted = false;
  bWorldInitialized = false;
  bAIPlayersSpawned = false;
  bPendingInitialSnapshot = false;
  PlacementIndex = 0;
  bArmyPlacementFailsafeTriggered = false;
  ArmyPlacementLeader.Reset();
  PendingControllers.Reset();
  PendingStrategicInitiativePlayers.Reset();
  bAwaitingStrategicInitiativeInput = false;
  bStrategicInitiativePromptIssued = false;
  StrategicInitiativeRound = 0;
  StrategicInitiativeRoundByPlayer.Empty();

  if (UWorld *World = GetWorld()) {
    FTimerManager &TimerManager = World->GetTimerManager();
    TimerManager.ClearTimer(RetryInitTimerHandle);
    TimerManager.ClearTimer(WorldMapRetryHandle);
    TimerManager.ClearTimer(StartGameTimerHandle);
    TimerManager.ClearTimer(ArmyPlacementAutoAdvanceHandle);
    TimerManager.ClearTimer(ArmyPlacementFailsafeHandle);
    TimerManager.ClearTimer(ArmyPlacementStartupRetryHandle);
  } else {
    RetryInitTimerHandle.Invalidate();
    WorldMapRetryHandle.Invalidate();
    StartGameTimerHandle.Invalidate();
    ArmyPlacementAutoAdvanceHandle.Invalidate();
    ArmyPlacementFailsafeHandle.Invalidate();
    ArmyPlacementStartupRetryHandle.Invalidate();
  }

  if (GI) {
    GI->SetTravelPending(false);
  }
}

void ASkaldGameMode::BeginPlay() {
  Super::BeginPlay();

  CleanupStalePlayerStates();
  PlayerDataArray.Empty();

  for (FConstPlayerControllerIterator It =
           GetWorld()->GetPlayerControllerIterator();
       It; ++It) {
    if (ASkaldPlayerController *PC = Cast<ASkaldPlayerController>(*It)) {
      RegisterPlayer(PC);
    }
  }

  if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
    if (GI->LoadedSaveGame) {
      ApplyLoadedGame(GI->LoadedSaveGame);
      GI->LoadedSaveGame = nullptr;
      return;
    }
  }

  // Defer AI population and world initialization until players lock in.
  RefreshHUDs();
}

void ASkaldGameMode::PostLogin(APlayerController *NewPlayer) {
  Super::PostLogin(NewPlayer);

  ASkaldPlayerController *PC = Cast<ASkaldPlayerController>(NewPlayer);
  if (!PC) {
    return;
  }

  if (bWorldInitialized) {
    PC->ClientMessage(TEXT("Game already in progress."));
    PC->Destroy();
    return;
  }

  RegisterPlayer(PC);
}

void ASkaldGameMode::Logout(AController *Exiting) {
  ASkaldPlayerController *PC = Cast<ASkaldPlayerController>(Exiting);
  int32 PlayerID = 0;
  bool bHasValidPlayerState = false;
  if (PC) {
    PendingControllers.Remove(PC);
    if (ASkaldPlayerState *PS = PC->GetPlayerState<ASkaldPlayerState>()) {
      PlayerID = PS->GetPlayerId();
      bHasValidPlayerState = true;

      RemovePendingStrategicInitiativePlayer(PS);
    }
  }

  Super::Logout(Exiting);

  HandlePendingStrategicInitiativeUpdate();

  if (bHasValidPlayerState) {
    PlayerDataArray.RemoveAll([PlayerID](const FS_PlayerData &Data) {
      return Data.PlayerID == PlayerID;
    });
  }

  if (TurnManager) {
    // Remove the exiting controller from the turn manager's list.
    TurnManager->SortControllersByInitiative();
  }

  RefreshHUDs();
  if (!bWorldInitialized) {
    TryInitializeWorldAndStart();
  }
}

void ASkaldGameMode::HandleSeamlessTravelPlayer(AController *&C) {
  Super::HandleSeamlessTravelPlayer(C);

  CleanupStalePlayerStates();

  if (ASkaldPlayerController *PC = Cast<ASkaldPlayerController>(C)) {
    RegisterPlayer(PC);
  }
}

void ASkaldGameMode::RegisterPlayer(ASkaldPlayerController *PC) {
  // Bail out if the controller has been destroyed or is otherwise invalid.
  if (!IsValid(PC)) {
    PendingControllers.Remove(PC);
    return;
  }
  USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>();
  const bool bIsMultiplayer = GI && GI->bIsMultiplayer;

  ASkaldGameState *GS = GetGameState<ASkaldGameState>();
  ASkaldPlayerState *PS = PC->GetPlayerState<ASkaldPlayerState>();

  if (!GS) {
    // During seamless travel the GameState may not have finished
    // initialising when RegisterPlayer is first invoked. Keep the controller
    // pending and retry on the next tick so we always process it once the
    // replicated state is available instead of silently dropping it.
    PendingControllers.AddUnique(PC);

    FTimerDelegate RetryDelegate = FTimerDelegate::CreateUObject(
        this, &ASkaldGameMode::RegisterPlayer, PC);
    GetWorldTimerManager().SetTimerForNextTick(RetryDelegate);
    return;
  }

  // If a PlayerState already exists for this controller, reuse it instead of
  // keeping a second one that may have been created by InitPlayerState.
  for (APlayerState *ExistingPSBase : GS->PlayerArray) {
    if (ExistingPSBase && ExistingPSBase->GetOwner() == PC &&
        ExistingPSBase != PS) {
      if (PS) {
        GS->RemovePlayerState(PS);
        PS->Destroy();
      }
      PS = Cast<ASkaldPlayerState>(ExistingPSBase);
      PC->PlayerState = PS;
      break;
    }
  }

  if (!PS) {
    // Player state replication can lag behind controller creation in both
    // singleplayer and multiplayer sessions. Keep the controller pending and
    // retry registration on the next tick until the state becomes available.
    PendingControllers.AddUnique(PC);

    if (UWorld *World = GetWorld()) {
      FTimerDelegate RetryDelegate = FTimerDelegate::CreateUObject(
          this, &ASkaldGameMode::RegisterPlayer, PC);
      World->GetTimerManager().SetTimerForNextTick(RetryDelegate);
    }
    return;
  }

  // Player state is valid; perform normal registration.
  PendingControllers.Remove(PC);

  PS->bIsAI = PC->IsA<ASkaldAIController>();

  // Ensure the controller has a stable player identifier before continuing.
  // Multiplayer clients can begin interacting with the world map before the
  // initial PlayerState replication completes, which leaves the StablePlayerId
  // unset and prevents UI/selection logic from routing to the correct owner.
  PS->RefreshStablePlayerId();

  if (!GS->PlayerArray.Contains(PS)) {
    GS->AddPlayerState(PS);
  }

  if (!TurnManager) {
    TurnManager = ResolveTurnManager();
  }

  if (TurnManager) {
    if (!TurnManager->GetControllers().Contains(PC)) {
      TurnManager->RegisterController(PC);
      UE_LOG(LogSkald, Log, TEXT("RegisterPlayer: ControllerCount=%d"),
             TurnManager->GetControllerCount());
    }
  } else {
    // Defer final registration until the turn manager is available. Ensure
    // the controller remains in the pending list so we can retry once the
    // manager finishes spawning in both single- and multiplayer sessions.
    PendingControllers.AddUnique(PC);

    // Retry on the next tick so late-spawned managers still capture the
    // controller without requiring manual refreshes.
    FTimerDelegate RetryDelegate =
        FTimerDelegate::CreateUObject(this, &ASkaldGameMode::RegisterPlayer,
                                      PC);
    GetWorldTimerManager().SetTimerForNextTick(RetryDelegate);
    return;
  }

  if (PlayerDataArray.Num() < GS->PlayerArray.Num()) {
    PlayerDataArray.SetNum(GS->PlayerArray.Num());
  }

  if (GI && (PS->Faction == ESkaldFaction::None || PS->PlayerDisplayName.IsEmpty())) {
    FS_PlayerData LobbyData;
    const FString ExistingIdentity = !PS->PlayerDisplayName.IsEmpty()
                                         ? PS->PlayerDisplayName
                                         : PS->GetPlayerName();
    if (GI->ConsumePendingLobbyPlayerData(PS->GetPlayerId(), ExistingIdentity,
                                          LobbyData)) {
      if (PS->Faction == ESkaldFaction::None &&
          LobbyData.Faction != ESkaldFaction::None) {
        PS->Faction = LobbyData.Faction;
        GI->TakenFactions.AddUnique(LobbyData.Faction);
      }

      if (PS->PlayerDisplayName.IsEmpty() && !LobbyData.DisplayName.IsEmpty()) {
        PS->PlayerDisplayName = LobbyData.DisplayName;
      }

      if (!LobbyData.PlayerName.IsEmpty()) {
        const FString DesiredName = LobbyData.PlayerName;
        if (PS->GetPlayerName().IsEmpty() || PS->GetPlayerName() != DesiredName) {
          PS->SetPlayerName(DesiredName);
        }
      } else if (!PS->PlayerDisplayName.IsEmpty() &&
                 PS->GetPlayerName() != PS->PlayerDisplayName) {
        PS->SetPlayerName(PS->PlayerDisplayName);
      }
    }
  }

  if (GI && PC && PC->IsLocalController()) {
    if (PS->Faction == ESkaldFaction::None) {
      PS->Faction = GI->Faction;
    }

    if (PS->PlayerDisplayName.IsEmpty()) {
      PS->PlayerDisplayName = GI->DisplayName;
    }
  }

  if (PS->PlayerDisplayName.IsEmpty()) {
    FString ResolvedName = PS->GetPlayerName();
    if (ResolvedName.IsEmpty()) {
      ResolvedName = FString::Printf(TEXT("Player %d"), PS->GetPlayerId());
    }
    if (ResolvedName.IsEmpty()) {
      ResolvedName = TEXT("Player");
    }

    PS->PlayerDisplayName = ResolvedName;
  }

  if (PS->GetPlayerName().IsEmpty() || PS->GetPlayerName() != PS->PlayerDisplayName) {
    FString EffectiveName = PS->PlayerDisplayName;
    if (EffectiveName.IsEmpty()) {
      EffectiveName = TEXT("Player");
    }

    PS->SetPlayerName(EffectiveName);
  }

  const int32 Index = GS->PlayerArray.IndexOfByKey(PS);
  if (PlayerDataArray.IsValidIndex(Index)) {
    PlayerDataArray[Index].PlayerID = PS->GetPlayerId();
    PlayerDataArray[Index].PlayerName =
        PS->GetResolvedPlayerName(TEXT("RegisterPlayer"));
    PlayerDataArray[Index].DisplayName = PS->PlayerDisplayName;
    PlayerDataArray[Index].IsAI = PS->bIsAI;
    PlayerDataArray[Index].Faction = PS->Faction;
    PlayerDataArray[Index].Resources = PS->Resources;
  }

  // Notify listeners that player data has changed and refresh HUDs on the
  // next tick once replication has a chance to update clients.
  GS->OnPlayersUpdated.Broadcast();
  FTimerDelegate RefreshDelegate =
      FTimerDelegate::CreateUObject(this, &ASkaldGameMode::RefreshHUDs);
  GetWorldTimerManager().SetTimerForNextTick(RefreshDelegate);

  if (!bWorldInitialized) {
    TryInitializeWorldAndStart();
  }
}

void ASkaldGameMode::CleanupStalePlayerStates() {
  ASkaldGameState *GS = GetGameState<ASkaldGameState>();
  if (!GS) {
    return;
  }

  USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>();
  const bool bIsBattleMap = GI && GI->bIsInBattleMap;
  const FS_BattlePayload BattleSnapshot =
      bIsBattleMap ? GI->PendingBattle : FS_BattlePayload();

  bool bRemovedAny = false;
  for (int32 i = GS->PlayerArray.Num() - 1; i >= 0; --i) {
    APlayerState *BasePS = GS->PlayerArray[i];
    AController *OwningController =
        BasePS ? Cast<AController>(BasePS->GetOwner()) : nullptr;
    if (!IsValid(OwningController)) {
      ASkaldPlayerState *SkaldPS = Cast<ASkaldPlayerState>(BasePS);
      const bool bPreserveForBattle =
          bIsBattleMap && SkaldPS &&
          (SkaldPS->GetPlayerId() == BattleSnapshot.AttackerPlayerID ||
           SkaldPS->GetPlayerId() == BattleSnapshot.DefenderPlayerID);
      if (bPreserveForBattle) {
        continue;
      }

      GS->PlayerArray.RemoveAt(i);
      if (SkaldPS) {
        GS->Players.RemoveSwap(SkaldPS);
      }
      bRemovedAny = true;
    }
  }

  if (bRemovedAny) {
    GS->OnPlayersUpdated.Broadcast();
  }

  HandlePendingStrategicInitiativeUpdate();
}

void ASkaldGameMode::PopulateAIPlayers() {
  ASkaldGameState *GS = GetGameState<ASkaldGameState>();
  USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>();
  if (!GS || !GI || !AIControllerClass) {
    return;
  }

  bool bHasHuman = false;
  int32 ExistingAI = 0;
  TArray<ASkaldPlayerState *> AIStates;
  for (APlayerState *ExistingPS : GS->PlayerArray) {
    if (ASkaldPlayerState *EPS = Cast<ASkaldPlayerState>(ExistingPS)) {
      if (EPS->bIsAI) {
        ++ExistingAI;
        AIStates.Add(EPS);
      } else {
        bHasHuman = true;
      }
    }
  }
  if (!bHasHuman) {
    return;
  }

  const int32 TargetAI = FMath::Max(GI->AIPlayersToSpawn, 0);

  // Trim any excess AI players to respect the configured total.
  if (ExistingAI > TargetAI) {
    for (int32 Index = AIStates.Num() - 1; Index >= 0 && ExistingAI > TargetAI;
         --Index) {
      ASkaldPlayerState *ExcessPS = AIStates[Index];
      if (AController *ControllerOwner =
              Cast<AController>(ExcessPS->GetOwner())) {
        ControllerOwner->Destroy();
      }
      GS->RemovePlayerState(ExcessPS);
      GS->Players.RemoveSwap(ExcessPS);
      PlayerDataArray.RemoveAll([ExcessPS](const FS_PlayerData &Data) {
        return Data.PlayerID == ExcessPS->GetPlayerId();
      });
      GI->TakenFactions.Remove(ExcessPS->Faction);
      --ExistingAI;
    }
    GS->OnPlayersUpdated.Broadcast();
  }

  TArray<FSkaldAIPlayerConfig> PendingLobbyAI;
  if (GI)
  {
    PendingLobbyAI = GI->PendingLobbyAIPlayers;
  }

  const int32 MaxSpawnAttempts = TargetAI * 2;
  int32 SpawnAttempts = 0;
  TArray<ASkaldPlayerState *> NewlySpawnedAI;

  TSet<ESkaldFaction> UsedFactions;
  for (APlayerState *ExistingPS : GS->PlayerArray) {
    if (ASkaldPlayerState *EPS = Cast<ASkaldPlayerState>(ExistingPS)) {
      if (EPS->Faction != ESkaldFaction::None) {
        UsedFactions.Add(EPS->Faction);
      }
    }
  }

  TArray<ESkaldFaction> ReservedFactions;
  TSet<ESkaldFaction> RemainingReserved;
  for (ESkaldFaction Faction : GI->TakenFactions) {
    if (Faction == ESkaldFaction::None || UsedFactions.Contains(Faction)) {
      continue;
    }

    ReservedFactions.Add(Faction);
    RemainingReserved.Add(Faction);
    UsedFactions.Add(Faction);
  }

  TArray<ESkaldFaction> Available;
  if (UEnum *Enum = StaticEnum<ESkaldFaction>()) {
    for (int32 i = 0; i < Enum->NumEnums(); ++i) {
      if (Skald::EnumUtils::IsHiddenEntry(Enum, i)) {
        continue;
      }

      const FString EnumName = Enum->GetNameStringByIndex(i);
      if (EnumName.EndsWith(TEXT("_MAX"))) {
        continue;
      }
      ESkaldFaction Fac =
          static_cast<ESkaldFaction>(Enum->GetValueByIndex(i));
      if (Fac != ESkaldFaction::None && !UsedFactions.Contains(Fac)) {
        Available.Add(Fac);
      }
    }
  }

  auto NormaliseName = [](const FString &InName) {
    FString Result = InName;
    Result.TrimStartAndEndInline();
    Result.ToLowerInline();
    return Result;
  };

  const FSkaldTravelState *TravelStatePtr = nullptr;
  if (GI) {
    const FSkaldTravelState &State = GI->GetTravelState();
    if (State.bValid) {
      TravelStatePtr = &State;
    }
  }

  TMap<int32, const FS_PlayerData *> RestorableAIById;
  TMap<FString, const FS_PlayerData *> RestorableAIByName;
  TSet<const FS_PlayerData *> PendingRestorations;
  if (TravelStatePtr) {
    for (const FS_PlayerData &Snapshot : TravelStatePtr->PlayerSnapshots) {
      if (!Snapshot.IsAI) {
        continue;
      }

      const FS_PlayerData *SnapshotPtr = &Snapshot;
      PendingRestorations.Add(SnapshotPtr);
      if (Snapshot.PlayerID > 0 && !RestorableAIById.Contains(Snapshot.PlayerID)) {
        RestorableAIById.Add(Snapshot.PlayerID, SnapshotPtr);
      }

      const FString NormalisedName =
          NormaliseName(!Snapshot.DisplayName.IsEmpty() ? Snapshot.DisplayName
                                                        : Snapshot.PlayerName);
      if (!NormalisedName.IsEmpty() && !RestorableAIByName.Contains(NormalisedName)) {
        RestorableAIByName.Add(NormalisedName, SnapshotPtr);
      }
    }
  }

  if (TravelStatePtr) {
    for (ASkaldPlayerState *ExistingAIState : AIStates) {
      if (!ExistingAIState) {
        continue;
      }

      const FS_PlayerData *MatchingSnapshot = nullptr;
      if (ExistingAIState->GetPlayerId() > 0) {
        if (const FS_PlayerData *const *FoundById =
                RestorableAIById.Find(ExistingAIState->GetPlayerId())) {
          MatchingSnapshot = *FoundById;
        }
      }

      if (!MatchingSnapshot) {
        const FString NormalisedExisting =
            NormaliseName(ExistingAIState->PlayerDisplayName);
        if (!NormalisedExisting.IsEmpty()) {
          if (const FS_PlayerData *const *FoundByName =
                  RestorableAIByName.Find(NormalisedExisting)) {
            MatchingSnapshot = *FoundByName;
          }
        }
      }

      if (MatchingSnapshot) {
        PendingRestorations.Remove(MatchingSnapshot);
      }
    }
  }

  TArray<const FS_PlayerData *> OrderedRestorations;
  if (TravelStatePtr && PendingRestorations.Num() > 0) {
    for (const FS_PlayerData &Snapshot : TravelStatePtr->PlayerSnapshots) {
      const FS_PlayerData *SnapshotPtr = &Snapshot;
      if (PendingRestorations.Contains(SnapshotPtr)) {
        OrderedRestorations.Add(SnapshotPtr);
      }
    }

    OrderedRestorations.Sort([](const FS_PlayerData &A,
                                const FS_PlayerData &B) {
      auto DesiredIndex = [](const FS_PlayerData &Snapshot) {
        if (Snapshot.DesiredControllerIndex >= 0) {
          return Snapshot.DesiredControllerIndex;
        }
        if (Snapshot.DesiredTurnIndex >= 0) {
          return Snapshot.DesiredTurnIndex;
        }
        return TNumericLimits<int32>::Max();
      };

      const int32 AIndex = DesiredIndex(A);
      const int32 BIndex = DesiredIndex(B);
      if (AIndex == BIndex) {
        const int32 AId = (A.PlayerID > 0) ? A.PlayerID
                                           : TNumericLimits<int32>::Max();
        const int32 BId = (B.PlayerID > 0) ? B.PlayerID
                                           : TNumericLimits<int32>::Max();
        return AId < BId;
      }
      return AIndex < BIndex;
    });
  }

  int32 RestoreCursor = 0;

  TSet<FString> UsedDisplayNames;
  for (APlayerState *ExistingPSBase : GS->PlayerArray) {
    if (!ExistingPSBase) {
      continue;
    }

    if (ASkaldPlayerState *ExistingSkaldState =
            Cast<ASkaldPlayerState>(ExistingPSBase)) {
      if (!ExistingSkaldState->PlayerDisplayName.IsEmpty()) {
        UsedDisplayNames.Add(ExistingSkaldState->PlayerDisplayName);
      }
    } else {
      const FString ExistingName = ExistingPSBase->GetPlayerName();
      if (!ExistingName.IsEmpty()) {
        UsedDisplayNames.Add(ExistingName);
      }
    }
  }

  TArray<FString> AvailableNames;
  AvailableNames.Reserve(FantasyAINames.Num());
  for (const FString &CandidateName : FantasyAINames) {
    if (!UsedDisplayNames.Contains(CandidateName)) {
      AvailableNames.Add(CandidateName);
    }
  }

  auto AcquireRandomName = [&](int32 SeedCount) -> FString {
    if (AvailableNames.Num() > 0) {
      const int32 NameIndex =
          GI ? GI->CombatRandomStream.RandRange(0, AvailableNames.Num() - 1)
             : FMath::RandRange(0, AvailableNames.Num() - 1);
      const FString Chosen = AvailableNames[NameIndex];
      AvailableNames.RemoveAtSwap(NameIndex);
      return Chosen;
    }

    int32 Suffix = SeedCount;
    FString Generated;
    do {
      Generated = FString::Printf(TEXT("AI_%d"), Suffix++);
    } while (UsedDisplayNames.Contains(Generated));
    return Generated;
  };

  auto RemoveNameFromPool = [&](const FString &Name) {
    if (Name.IsEmpty()) {
      return;
    }
    AvailableNames.Remove(Name);
  };

  auto ResolveSnapshotName = [&](const FS_PlayerData *Snapshot) -> FString {
    if (!Snapshot) {
      return FString();
    }
    if (!Snapshot->DisplayName.IsEmpty()) {
      return Snapshot->DisplayName;
    }
    return Snapshot->PlayerName;
  };

  auto ConsumeRestorationSnapshot = [&](const FS_PlayerData *Snapshot) {
    if (!Snapshot) {
      return;
    }
    PendingRestorations.Remove(Snapshot);
  };

  auto ApplySnapshotState = [&](ASkaldPlayerState *State,
                                const FS_PlayerData *Snapshot) {
    if (!State || !Snapshot) {
      return;
    }
    if (Snapshot->PlayerID > 0 && State->GetPlayerId() != Snapshot->PlayerID) {
      State->SetPlayerId(Snapshot->PlayerID);
    }
    State->Resources = Snapshot->Resources;
    State->IsEliminated = Snapshot->IsEliminated;
  };

  auto ResolveSnapshotFaction = [&](const FS_PlayerData *Snapshot) {
    if (!Snapshot) {
      return ESkaldFaction::None;
    }
    return Snapshot->Faction;
  };

  while (ExistingAI < TargetAI && SpawnAttempts++ < MaxSpawnAttempts) {
    FTransform SpawnTransform = FTransform::Identity;
    ASkaldPlayerController *AIController = Cast<ASkaldPlayerController>(
        GetWorld()->SpawnActorDeferred<APlayerController>(
            AIControllerClass, SpawnTransform, nullptr, nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn));
    if (!AIController) {
      break;
    }

    AIController->FinishSpawning(SpawnTransform);
    AIController->InitPlayerState();

    ASkaldPlayerState *AIState =
        AIController->GetPlayerState<ASkaldPlayerState>();
    if (!AIState) {
      AIController->Destroy();
      break;
    }

    // If this controller already has a PlayerState registered with the
    // GameState, reuse it instead of creating a duplicate. This can happen if
    // PopulateAIPlayers is called again for an existing AI controller.
    for (APlayerState *ExistingPSBase : GS->PlayerArray) {
      if (ExistingPSBase && ExistingPSBase->GetOwner() == AIController &&
          ExistingPSBase != AIState) {
        GS->RemovePlayerState(AIState);
        AIState->Destroy();
        AIState = Cast<ASkaldPlayerState>(ExistingPSBase);
        AIController->PlayerState = AIState;
        break;
      }
    }

    AIState->bIsAI = true;



    const FS_PlayerData *RestorationSnapshot = nullptr;
    if (OrderedRestorations.IsValidIndex(RestoreCursor)) {
      RestorationSnapshot = OrderedRestorations[RestoreCursor++];
      ConsumeRestorationSnapshot(RestorationSnapshot);
    }

    FSkaldAIPlayerConfig LobbyPreset;
    bool bUsingLobbyPreset = false;
    if (!RestorationSnapshot && PendingLobbyAI.Num() > 0) {
      LobbyPreset = PendingLobbyAI[0];
      PendingLobbyAI.RemoveAt(0);
      bUsingLobbyPreset = true;
    }

    FString SelectedName = ResolveSnapshotName(RestorationSnapshot);
    if (SelectedName.IsEmpty() && bUsingLobbyPreset && !LobbyPreset.DisplayName.IsEmpty()) {
      SelectedName = LobbyPreset.DisplayName;
    }
    if (SelectedName.IsEmpty()) {
      SelectedName = AcquireRandomName(GS->PlayerArray.Num() + ExistingAI);
    }

    RemoveNameFromPool(SelectedName);
    UsedDisplayNames.Add(SelectedName);

    AIState->PlayerDisplayName = SelectedName;
    AIState->SetPlayerName(AIState->PlayerDisplayName);

    if (RestorationSnapshot) {
      ApplySnapshotState(AIState, RestorationSnapshot);
    }

    ESkaldFaction AssignedFaction = ResolveSnapshotFaction(RestorationSnapshot);
    if (AssignedFaction != ESkaldFaction::None) {
      ReservedFactions.Remove(AssignedFaction);
      RemainingReserved.Remove(AssignedFaction);
      Available.Remove(AssignedFaction);
    } else if (bUsingLobbyPreset && LobbyPreset.Faction != ESkaldFaction::None) {
      AssignedFaction = LobbyPreset.Faction;
      ReservedFactions.Remove(AssignedFaction);
      RemainingReserved.Remove(AssignedFaction);
      Available.Remove(AssignedFaction);
    } else if (ReservedFactions.Num() > 0) {
      AssignedFaction = ReservedFactions[0];
      ReservedFactions.RemoveAt(0);
      RemainingReserved.Remove(AssignedFaction);
    } else if (Available.Num() > 0) {
      const int32 FactionIndex =
          GI->CombatRandomStream.RandRange(0, Available.Num() - 1);
      AssignedFaction = Available[FactionIndex];
      Available.RemoveAtSwap(FactionIndex);
    }

    if (AssignedFaction == ESkaldFaction::None) {
      UE_LOG(LogSkald, Error,
             TEXT("PopulateAIPlayers: no available factions for AI"));
      AIController->Destroy();
      break;
    }

    AIState->Faction = AssignedFaction;
    UsedFactions.Add(AssignedFaction);
    GI->TakenFactions.AddUnique(AIState->Faction);

    RegisterPlayer(AIController);

    NewlySpawnedAI.Add(AIState);
    ++ExistingAI;

    if (!AIController->GetPawn() && DefaultPawnClass) {
      FActorSpawnParameters PawnParams;
      PawnParams.SpawnCollisionHandlingOverride =
          ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
      if (APawn *Pawn = GetWorld()->SpawnActor<APawn>(
              DefaultPawnClass, FVector::ZeroVector, FRotator::ZeroRotator,
              PawnParams)) {
        AIController->Possess(Pawn);
      }
    }

    if (ASkald_BattleGameMode *BattleGM = Cast<ASkald_BattleGameMode>(this)) {
      if (AIController->GetPawn()) {
        TArray<AController *> ControllersToRelocate;
        ControllersToRelocate.Add(AIController);
        BattleGM->RelocateControllersNearBattleGrid(ControllersToRelocate);
      }
    }
  }

  if (RemainingReserved.Num() > 0) {
    for (ESkaldFaction Faction : RemainingReserved) {
      GI->TakenFactions.Remove(Faction);
    }
  }

  for (ASkaldPlayerState *NewAIState : NewlySpawnedAI) {
    HandlePlayerLockedIn(NewAIState);
  }

  if (ExistingAI < TargetAI) {
    UE_LOG(LogSkald, Warning,
           TEXT("PopulateAIPlayers spawned only %d/%d AI players after %d "
                "attempts"),
           ExistingAI, TargetAI, SpawnAttempts);
  }

  if (GI)
  {
    GI->PendingLobbyAIPlayers.Reset();
  }
}

void ASkaldGameMode::HandlePlayerLockedIn(ASkaldPlayerState *PS) {
  if (!PS || bWorldInitialized || PS->bHasLockedIn) {
    return;
  }

  PS->bHasLockedIn = true;

  UE_LOG(LogSkald, Log, TEXT("HandlePlayerLockedIn: Player=%s bIsAI=%d"),
         *PS->GetResolvedPlayerName(TEXT("HandlePlayerLockedIn")),
         PS->bIsAI ? 1 : 0);
  ASkaldGameState *GS = GetGameState<ASkaldGameState>();
  UE_LOG(LogSkald, Log,
         TEXT("HandlePlayerLockedIn: TurnManager=%s ControllerCount=%d "
              "PlayerCount=%d"),
         TurnManager ? *TurnManager->GetName() : TEXT("null"),
         TurnManager ? TurnManager->GetControllerCount() : 0,
         GS ? GS->PlayerArray.Num() : 0);

  USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>();
  const bool bIsBattleMap = GI && GI->bIsInBattleMap;

  if (WorldMap && !IsValid(WorldMap)) {
    WorldMap = nullptr;
  }

  if (!WorldMap && !bIsBattleMap) {
    if (!TryResolveWorldMap()) {
      UE_LOG(LogSkald, Verbose,
             TEXT("HandlePlayerLockedIn: WorldMap not yet available; deferring initialization."));
      RequestWorldMapRetry();
    }
  }

  if (GI && !bIsBattleMap) {
    if (GI->bResumeTurns) {
      UE_LOG(LogSkald, Verbose,
             TEXT("HandlePlayerLockedIn: Skipping snapshot capture while travel state is pending resume."));
    } else if (bWorldInitialized) {
      GI->CacheWorldMapSnapshot(GetWorld());
    } else {
      bPendingInitialSnapshot = true;
      UE_LOG(LogSkald, Verbose,
             TEXT("HandlePlayerLockedIn: Deferring overworld snapshot until after initiative and world initialization."));
    }
  }

  if (!TurnManager) {
    TurnManager = ResolveTurnManager();
  }

  FS_PlayerData *PlayerData =
      PlayerDataArray.FindByPredicate([PS](const FS_PlayerData &Data) {
        return Data.PlayerID == PS->GetPlayerId();
      });
  if (PlayerData) {
    PlayerData->PlayerName =
        PS->GetResolvedPlayerName(TEXT("RegisterPlayer_PlayerData"));
    PlayerData->Faction = PS->Faction;
  }

  RefreshHUDs();
  TryInitializeWorldAndStart();
}

bool ASkaldGameMode::TryResolveWorldMap() {
  if (WorldMap && !IsValid(WorldMap)) {
    WorldMap = nullptr;
  }

  if (WorldMap) {
    return true;
  }

  UWorld *World = GetWorld();
  if (!World) {
    return false;
  }

  if (AActor *Actor =
          UGameplayStatics::GetActorOfClass(World, AWorldMap::StaticClass())) {
    if (AWorldMap *Found = Cast<AWorldMap>(Actor)) {
      if (IsValid(Found)) {
        WorldMap = Found;
        return true;
      }
    }
  }

  return false;
}

void ASkaldGameMode::RequestWorldMapRetry() {
  if (WorldMap) {
    return;
  }

  if (!GetWorld()) {
    return;
  }

  if (!GetWorldTimerManager().IsTimerActive(WorldMapRetryHandle)) {
    GetWorldTimerManager().SetTimer(WorldMapRetryHandle, this,
                                    &ASkaldGameMode::HandleWorldMapRetry,
                                    RetryInitDelay, false);
  }

  FTimerDelegate ImmediateRetry =
      FTimerDelegate::CreateUObject(this, &ASkaldGameMode::HandleWorldMapRetry);
  GetWorldTimerManager().SetTimerForNextTick(ImmediateRetry);
}

void ASkaldGameMode::HandleWorldMapRetry() {
  USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>();
  const bool bIsBattleMap = GI && GI->bIsInBattleMap;
  if (bIsBattleMap) {
    GetWorldTimerManager().ClearTimer(WorldMapRetryHandle);
    return;
  }

  if (TryResolveWorldMap()) {
    GetWorldTimerManager().ClearTimer(WorldMapRetryHandle);
    FTimerDelegate RetryInit = FTimerDelegate::CreateUObject(
        this, &ASkaldGameMode::TryInitializeWorldAndStart);
    GetWorldTimerManager().SetTimerForNextTick(RetryInit);
  } else {
    GetWorldTimerManager().SetTimer(WorldMapRetryHandle, this,
                                    &ASkaldGameMode::HandleWorldMapRetry,
                                    RetryInitDelay, false);
  }
}

void ASkaldGameMode::BeginPreBattleSelection(ASkaldPlayerState *A,
                                             ASkaldPlayerState *D,
                                             int32 ABudget, int32 DBudget) {
  if (!HasAuthority()) {
    return;
  }

  if (A) {
    A->PendingArmyBudget = ABudget;
    A->PendingArmy.Reset();
    A->bArmyLockedIn = false;
    if (ASkaldPlayerController *APC =
            Cast<ASkaldPlayerController>(A->GetOwner())) {
      APC->Client_ShowFighterSelection(ABudget, A->Faction);
    }
  }

  if (D) {
    D->PendingArmyBudget = DBudget;
    D->PendingArmy.Reset();
    D->bArmyLockedIn = false;
    if (ASkaldPlayerController *DPC =
            Cast<ASkaldPlayerController>(D->GetOwner())) {
      DPC->Client_ShowFighterSelection(DBudget, D->Faction);
    }
  }
}

void ASkaldGameMode::HandleBattleEnded(ESkaldFaction Winner, int32 AttackerCasualties, int32 DefenderCasualties)
{
  if (ASkaldGameState* GS = GetGameState<ASkaldGameState>())
  {
    GS->LastBattleWinner = Winner;
    GS->LastAttackerCasualties = AttackerCasualties;
    GS->LastDefenderCasualties = DefenderCasualties;
    GS->NotifyBattleSummaryChanged();
  }
}

void ASkaldGameMode::RefreshHUDs() {
  ASkaldGameState *GS = GetGameState<ASkaldGameState>();
  if (!GS) {
    return;
  }

  if (const UWorld *World = GetWorld()) {
    // HUD refresh can be triggered during world teardown (e.g. on travel), so
    // avoid creating any new widgets while the world is being destroyed.
    if (World->bIsTearingDown) {
      UE_LOG(LogSkald, Verbose,
             TEXT("[HUD] Skipping RefreshHUDs because world is tearing down"));
      return;
    }
  }

  // Ensure all AI players have valid names before refreshing any HUDs.
  for (APlayerState *PSBase : GS->PlayerArray) {
    if (ASkaldPlayerState *SPS = Cast<ASkaldPlayerState>(PSBase)) {
      if (SPS->bIsAI && SPS->PlayerDisplayName.IsEmpty()) {
        UE_LOG(LogSkald, Error,
               TEXT("AI PlayerState missing display name before RefreshHUDs"));
        ensure(!SPS->PlayerDisplayName.IsEmpty());
      }
    }
  }

  TArray<FS_PlayerData> AllPlayers;
  for (APlayerState *PSBase : GS->PlayerArray) {
    if (ASkaldPlayerState *SPS = Cast<ASkaldPlayerState>(PSBase)) {
      FS_PlayerData Data;
      Data.PlayerID = SPS->GetPlayerId();
      Data.PlayerName =
          SPS->GetResolvedPlayerName(TEXT("RefreshHUDs_Player"));
      Data.IsAI = SPS->bIsAI;
      Data.Faction = SPS->Faction;
      Data.Resources = SPS->Resources;
      AllPlayers.Add(Data);
    }
  }

  for (FConstPlayerControllerIterator It =
           GetWorld()->GetPlayerControllerIterator();
       It; ++It) {
    if (ASkaldPlayerController *RefreshPC = Cast<ASkaldPlayerController>(*It)) {
      if (USkaldMainHUDWidget *HUD = RefreshPC->GetHUDWidget()) {
        HUD->RefreshPlayerList(AllPlayers);
      }
    }
  }
}

void ASkaldGameMode::UpdatePlayerResources(ASkaldPlayerState *Player) {
  if (!Player) {
    return;
  }

  FS_PlayerData *PlayerData =
      PlayerDataArray.FindByPredicate([Player](const FS_PlayerData &Data) {
        return Data.PlayerID == Player->GetPlayerId();
      });
  if (PlayerData) {
    PlayerData->Resources = Player->Resources;
  }
}

void ASkaldGameMode::NormalizePlayerStateIds() {
  ASkaldGameState *GS = GetGameState<ASkaldGameState>();
  if (!GS) {
    return;
  }

  PlayerDataArray.SetNum(GS->PlayerArray.Num());

  TSet<int32> ReservedPlayerIds;
  TSet<int32> AssignedPlayerIds;
  int32 NextCandidateId = 1;

  auto NormaliseName = [](const FString &InName) {
    FString Result = InName;
    Result.TrimStartAndEndInline();
    Result.ToLowerInline();
    return Result;
  };

  USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>();
  const FSkaldTravelState *TravelStatePtr =
      GI ? &GI->GetTravelState() : nullptr;

  TMap<FString, int32> DesiredIdByName;
  TArray<int32> DesiredHumanIds;
  TArray<int32> DesiredAIIds;

  if (TravelStatePtr && TravelStatePtr->bValid &&
      TravelStatePtr->PlayerSnapshots.Num() > 0) {
    for (const FS_PlayerData &Snapshot : TravelStatePtr->PlayerSnapshots) {
      if (Snapshot.PlayerID <= 0) {
        continue;
      }

      ReservedPlayerIds.Add(Snapshot.PlayerID);
      TArray<int32> &Pool = Snapshot.IsAI ? DesiredAIIds : DesiredHumanIds;
      Pool.AddUnique(Snapshot.PlayerID);

      if (!Snapshot.PlayerName.IsEmpty()) {
        const FString Normalised = NormaliseName(Snapshot.PlayerName);
        if (!Normalised.IsEmpty() &&
            !DesiredIdByName.Contains(Normalised)) {
          DesiredIdByName.Add(Normalised, Snapshot.PlayerID);
        }
      }
    }
  }

  for (APlayerState *PSBase : GS->PlayerArray) {
    if (const ASkaldPlayerState *ExistingPS = Cast<ASkaldPlayerState>(PSBase)) {
      const int32 ExistingId = ExistingPS->GetPlayerId();
      if (ExistingId > 0) {
        ReservedPlayerIds.Add(ExistingId);
      }
    }
  }

  auto RemoveFromPool = [](TArray<int32> &Pool, int32 Value) {
    const int32 Index = Pool.Find(Value);
    if (Index != INDEX_NONE) {
      Pool.RemoveAtSwap(Index);
    }
  };

  auto AcquirePlayerId = [&](ASkaldPlayerState *PS) -> int32 {
    if (!PS) {
      return 0;
    }

    FString DisplayName = NormaliseName(PS->PlayerDisplayName);
    if (DisplayName.IsEmpty()) {
      DisplayName = NormaliseName(PS->GetPlayerName());
    }

    int32 DesiredId = 0;
    if (!DisplayName.IsEmpty()) {
      if (int32 *FoundId = DesiredIdByName.Find(DisplayName)) {
        DesiredId = *FoundId;
        DesiredIdByName.Remove(DisplayName);
      }
    }

    TArray<int32> &Pool = PS->bIsAI ? DesiredAIIds : DesiredHumanIds;
    if (DesiredId == 0) {
      for (int32 Index = 0; Index < Pool.Num(); ++Index) {
        const int32 Candidate = Pool[Index];
        if (!AssignedPlayerIds.Contains(Candidate)) {
          DesiredId = Candidate;
          Pool.RemoveAtSwap(Index);
          break;
        }
      }
    } else {
      RemoveFromPool(Pool, DesiredId);
    }

    if (DesiredId > 0 && !AssignedPlayerIds.Contains(DesiredId)) {
      AssignedPlayerIds.Add(DesiredId);
      ReservedPlayerIds.Add(DesiredId);
      if (PS->bIsAI) {
        RemoveFromPool(DesiredHumanIds, DesiredId);
      } else {
        RemoveFromPool(DesiredAIIds, DesiredId);
      }
      return DesiredId;
    }

    const int32 ExistingId = PS->GetPlayerId();
    if (ExistingId > 0 && !AssignedPlayerIds.Contains(ExistingId)) {
      AssignedPlayerIds.Add(ExistingId);
      ReservedPlayerIds.Add(ExistingId);
      RemoveFromPool(DesiredHumanIds, ExistingId);
      RemoveFromPool(DesiredAIIds, ExistingId);
      for (auto It = DesiredIdByName.CreateIterator(); It; ++It) {
        if (It.Value() == ExistingId) {
          It.RemoveCurrent();
          break;
        }
      }
      return ExistingId;
    }

    int32 CandidateId = NextCandidateId;
    while (ReservedPlayerIds.Contains(CandidateId) ||
           AssignedPlayerIds.Contains(CandidateId)) {
      ++CandidateId;
    }

    NextCandidateId = CandidateId + 1;
    AssignedPlayerIds.Add(CandidateId);
    ReservedPlayerIds.Add(CandidateId);
    return CandidateId;
  };

  for (int32 i = 0; i < GS->PlayerArray.Num(); ++i) {
    if (ASkaldPlayerState *PS = Cast<ASkaldPlayerState>(GS->PlayerArray[i])) {
      const int32 AssignedId = AcquirePlayerId(PS);
      if (PS->GetPlayerId() != AssignedId) {
        PS->SetPlayerId(AssignedId);
      }

      if (PlayerDataArray.IsValidIndex(i)) {
        PlayerDataArray[i].PlayerID = AssignedId;
      }

      if (ASkaldPlayerController *OwningController =
              Cast<ASkaldPlayerController>(PS->GetOwner())) {
        if (USkaldMainHUDWidget *HUD = OwningController->GetHUDWidget()) {
          HUD->LocalPlayerID = AssignedId;
          HUD->SyncPhaseButtons(HUD->CurrentPlayerID == HUD->LocalPlayerID);
        }
      }
    }
  }

  if (TurnManager && GI && TravelStatePtr && TravelStatePtr->bValid) {
    TurnManager->RestoreControllerOrderFromSnapshots(
        TravelStatePtr->PlayerSnapshots);
  }
}

void ASkaldGameMode::TryInitializeWorldAndStart() {
  ASkaldGameState *GS = GetGameState<ASkaldGameState>();
  if (!GS || !TurnManager) {
    return;
  }

  if (bWorldInitialized && bTurnsStarted) {
    HandleWorldInitializationComplete();
    GetWorldTimerManager().ClearTimer(RetryInitTimerHandle);
    return;
  }

  // Purge any stale controller references before we evaluate player counts. A
  // player may have disconnected leaving behind a PlayerState with an owning
  // controller that is pending kill. Ensuring the turn manager drops these
  // entries keeps controller counts accurate when calculating readiness.
  TurnManager->SortControllersByInitiative();

  // Register any controllers that joined before the turn manager was ready.
  for (int32 Index = PendingControllers.Num() - 1; Index >= 0; --Index) {
    RegisterPlayer(PendingControllers[Index]);
  }

  USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>();
  // Normalize player identifiers before evaluating restoration so cached
  // snapshots that reference previous IDs can resolve the new PlayerState
  // instances spawned after travel.
  NormalizePlayerStateIds();

  const bool bHasPendingTravelSnapshot =
      GI && GI->GetPendingTravelSnapshot().Num() > 0;
  const bool bHasCachedWorldSnapshot =
      GI && GI->CachedWorldMapTerritories.Num() > 0;
  const bool bHasRestorableSnapshot =
      bHasPendingTravelSnapshot || bHasCachedWorldSnapshot;
  bool bWantsResume = GI && GI->bResumeTurns;
  if (GI && !bWantsResume && !bHasRestorableSnapshot &&
      (GI->SavedTurnIndex != 0 ||
       GI->SavedTurnPhase != ETurnPhase::Reinforcement)) {
    GI->SavedTurnIndex = 0;
    GI->SavedTurnPlayerId = 0;
    GI->SavedTurnPhase = ETurnPhase::Reinforcement;
  }

  if (GI && !GI->bIsMultiplayer) {
    TArray<ASkaldPlayerState *> AutoLockPlayers;
    for (APlayerState *PSBase : GS->PlayerArray) {
      ASkaldPlayerState *PS = Cast<ASkaldPlayerState>(PSBase);
      if (PS && !PS->bIsAI && !PS->bHasLockedIn) {
        AutoLockPlayers.Add(PS);
      }
    }

    for (ASkaldPlayerState *PS : AutoLockPlayers) {
      HandlePlayerLockedIn(PS);
    }
  }

  if (!bAIPlayersSpawned) {
    bAIPlayersSpawned = true;
    PopulateAIPlayers();
  }

  // Ensure local human controllers have their HUD widgets initialized before
  // proceeding with world initialization. Retry on the next tick if any are
  // still pending.
  for (FConstPlayerControllerIterator It =
           GetWorld()->GetPlayerControllerIterator();
       It; ++It) {
    if (ASkaldPlayerController *PC = Cast<ASkaldPlayerController>(*It)) {
      ASkaldPlayerState *PS = PC->GetPlayerState<ASkaldPlayerState>();
      const bool bIsAI = PS && PS->bIsAI;
      if (!bIsAI && PC->IsLocalController() && !PC->GetHUDWidget()) {
        FTimerDelegate RetryInit = FTimerDelegate::CreateUObject(
            this, &ASkaldGameMode::TryInitializeWorldAndStart);
        GetWorldTimerManager().ClearTimer(RetryInitTimerHandle);
        GetWorldTimerManager().SetTimer(RetryInitTimerHandle, RetryInit,
                                        RetryInitDelay, false);
        GetWorldTimerManager().SetTimerForNextTick(RetryInit);
        return;
      }
    }
  }

  const bool bNeedsSnapshotRestore =
      !bWorldInitialized && (bWantsResume || bHasRestorableSnapshot);

  if (bNeedsSnapshotRestore) {
    const bool bRestored = GI && GI->RestoreWorldFromSnapshot(GetWorld());
    if (!bRestored) {
      const bool bShouldRetryRestore =
          GI && (GI->bResumeTurns || GI->GetPendingTravelSnapshot().Num() > 0 ||
                 GI->CachedWorldMapTerritories.Num() > 0);
      if (bShouldRetryRestore) {
        FTimerDelegate RetryInit = FTimerDelegate::CreateUObject(
            this, &ASkaldGameMode::TryInitializeWorldAndStart);
        GetWorldTimerManager().ClearTimer(RetryInitTimerHandle);
        GetWorldTimerManager().SetTimer(RetryInitTimerHandle, RetryInit,
                                        RetryInitDelay, false);
        GetWorldTimerManager().SetTimerForNextTick(RetryInit);
        return;
      }
      bWantsResume = GI && GI->bResumeTurns;
    } else {
      bWorldInitialized = true;
      bWantsResume = GI && GI->bResumeTurns;
      HandleWorldInitializationComplete();
    }
  }

  UE_LOG(LogSkald, Log,
         TEXT("TryInitializeWorldAndStart: TurnManager=%s PlayerCount=%d"),
         TurnManager ? *TurnManager->GetName() : TEXT("null"),
         GS->PlayerArray.Num());

  TArray<ASkaldPlayerController *> RegisteredControllers =
      TurnManager ? TurnManager->GetControllers()
                  : TArray<ASkaldPlayerController *>();

  const int32 ExpectedControllerCount =
      FMath::Max(ResolveExpectedControllerCount(), MinPlayerCount);
  const int32 RegisteredControllerCount =
      TurnManager ? TurnManager->GetControllerCount() : 0;
  const bool bWaitingOnControllers =
      PendingControllers.Num() > 0 ||
      (ExpectedControllerCount > 0 &&
       RegisteredControllerCount < ExpectedControllerCount);

  if (bWaitingOnControllers) {
    UE_LOG(LogSkald, Log,
           TEXT("TryInitializeWorldAndStart: waiting for controllers Registered=%d Expected=%d Pending=%d"),
           RegisteredControllerCount, ExpectedControllerCount,
           PendingControllers.Num());

    if (GEngine && ExpectedControllerCount > 0) {
      GEngine->AddOnScreenDebugMessage(
          -1, 4.f, FColor::Yellow,
          FString::Printf(TEXT("Waiting for controllers: %d/%d ready"),
                          RegisteredControllerCount,
                          ExpectedControllerCount));
    }

    FTimerDelegate RetryInit = FTimerDelegate::CreateUObject(
        this, &ASkaldGameMode::TryInitializeWorldAndStart);
    GetWorldTimerManager().ClearTimer(RetryInitTimerHandle);
    GetWorldTimerManager().SetTimer(RetryInitTimerHandle, RetryInit,
                                    RetryInitDelay, false);
    GetWorldTimerManager().SetTimerForNextTick(RetryInit);
    return;
  }
  TSet<ASkaldPlayerController *> UniqueControllers;
  TSet<ASkaldPlayerController *> UniqueHumanControllers;
  bool bNeedsRetry = false;
  for (int32 Index = GS->PlayerArray.Num() - 1; Index >= 0; --Index) {
    ASkaldPlayerState *PS = Cast<ASkaldPlayerState>(GS->PlayerArray[Index]);
    ASkaldPlayerController *OwningController =
        PS ? Cast<ASkaldPlayerController>(PS->GetOwner()) : nullptr;
    if (!IsValid(OwningController)) {
      UE_LOG(LogSkald, Warning,
             TEXT("TryInitializeWorldAndStart: Removing PlayerState %s with no "
                  "controller"),
             *GetNameSafe(PS));
      GS->RemovePlayerState(PS);
      PlayerDataArray.RemoveAll([PS](const FS_PlayerData &Data) {
        return Data.PlayerID == PS->GetPlayerId();
      });
      bNeedsRetry = true;
      continue;
    }
    if (UniqueControllers.Contains(OwningController)) {
      UE_LOG(
          LogSkald, Warning,
          TEXT("TryInitializeWorldAndStart: Removing duplicate PlayerState %s "
               "for controller %s"),
          *GetNameSafe(PS), *GetNameSafe(OwningController));
      GS->RemovePlayerState(PS);
      PlayerDataArray.RemoveAll([PS](const FS_PlayerData &Data) {
        return Data.PlayerID == PS->GetPlayerId();
      });
      bNeedsRetry = true;
      continue;
    }
    UniqueControllers.Add(OwningController);
    if (PS && !PS->bIsAI) {
      UniqueHumanControllers.Add(OwningController);
    }
    if (!RegisteredControllers.Contains(OwningController)) {
      UE_LOG(LogSkald, Warning,
             TEXT("TryInitializeWorldAndStart: Requeuing controller %s"),
             *GetNameSafe(OwningController));
      PendingControllers.AddUnique(OwningController);
      FTimerDelegate RetryDelegate = FTimerDelegate::CreateUObject(
          this, &ASkaldGameMode::RegisterPlayer, OwningController);
      GetWorldTimerManager().SetTimerForNextTick(RetryDelegate);
      bNeedsRetry = true;
    }
  }

  if (bNeedsRetry) {
    NormalizePlayerStateIds();
    GS->OnPlayersUpdated.Broadcast();
    FTimerDelegate RefreshDelegate =
        FTimerDelegate::CreateUObject(this, &ASkaldGameMode::RefreshHUDs);
    GetWorldTimerManager().SetTimerForNextTick(RefreshDelegate);
    FTimerDelegate RetryInit = FTimerDelegate::CreateUObject(
        this, &ASkaldGameMode::TryInitializeWorldAndStart);
    GetWorldTimerManager().ClearTimer(RetryInitTimerHandle);
    GetWorldTimerManager().SetTimer(RetryInitTimerHandle, RetryInit,
                                    RetryInitDelay, false);
    GetWorldTimerManager().SetTimerForNextTick(RetryInit);
    return;
  }

  if (bWantsResume) {
    const bool bResumed =
        TurnManager && TurnManager->AttemptResumeSavedTurnState();
    if (!bResumed) {
      FTimerDelegate RetryInit = FTimerDelegate::CreateUObject(
          this, &ASkaldGameMode::TryInitializeWorldAndStart);
      GetWorldTimerManager().ClearTimer(RetryInitTimerHandle);
      GetWorldTimerManager().SetTimer(RetryInitTimerHandle, RetryInit,
                                      RetryInitDelay, false);
      GetWorldTimerManager().SetTimerForNextTick(RetryInit);
      return;
    }

    bWorldInitialized = true;
    bTurnsStarted = true;
    HandleWorldInitializationComplete();
    GetWorldTimerManager().ClearTimer(RetryInitTimerHandle);
    return;
  }

  // Player IDs can grow without bound across repeated sessions. Reassign
  // them to a compact, 1-based range so blueprints that index by ID never
  // read past the array length while keeping IDs strictly positive.
  NormalizePlayerStateIds();

  GS->OnPlayersUpdated.Broadcast();

  UE_LOG(LogSkald, Log,
         TEXT("TryInitializeWorldAndStart: Listing player lock states"));
  for (APlayerState *PSBase : GS->PlayerArray) {
    ASkaldPlayerState *PS = Cast<ASkaldPlayerState>(PSBase);
    ASkaldPlayerController *OwningController =
        PS ? Cast<ASkaldPlayerController>(PS->GetOwner()) : nullptr;
    UE_LOG(LogSkald, Log,
           TEXT("TryInitializeWorldAndStart: Player=%s IsAI=%s LockedIn=%s "
                "Controller=%s"),
           PS ? *PS->GetPlayerName() : TEXT("null"),
           PS && PS->bIsAI ? TEXT("true") : TEXT("false"),
           PS && PS->bHasLockedIn ? TEXT("true") : TEXT("false"),
           *GetNameSafe(OwningController));
  }

  bool bAllLockedIn = true;
  bool bAllHaveControllers = true;
  bool bAllHumansHaveFactions = true;
  bool bAllHumansHaveNames = true;
  for (APlayerState *PSBase : GS->PlayerArray) {
    ASkaldPlayerState *PS = Cast<ASkaldPlayerState>(PSBase);
    if (!PS || !PS->bHasLockedIn) {
      bAllLockedIn = false;
    }
    ASkaldPlayerController *OwningController =
        PS ? Cast<ASkaldPlayerController>(PS->GetOwner()) : nullptr;
    if (!OwningController ||
        !RegisteredControllers.Contains(OwningController)) {
      bAllHaveControllers = false;
      UE_LOG(
          LogSkald, Warning,
          TEXT("TryInitializeWorldAndStart: PlayerState %s missing controller"),
          *GetNameSafe(PS));
    }
    if (PS && !PS->bIsAI) {
      if (PS->Faction == ESkaldFaction::None) {
        bAllHumansHaveFactions = false;
      }
      const bool bHasDisplayName =
          !PS->PlayerDisplayName.IsEmpty() || !PS->GetPlayerName().IsEmpty();
      if (!bHasDisplayName) {
        bAllHumansHaveNames = false;
      }
    }
    if (!bAllLockedIn || !bAllHaveControllers) {
      break;
    }
  }

  const int32 CurrentControllerCount = UniqueControllers.Num();
  const int32 CurrentHumanCount = UniqueHumanControllers.Num();
  const bool bReadyToStart =
      bAllLockedIn && bAllHaveControllers && bAllHumansHaveFactions &&
      bAllHumansHaveNames &&
      CurrentHumanCount >= MinPlayerCount && TurnManager &&
      TurnManager->GetControllerCount() >= CurrentControllerCount;

  UE_LOG(
      LogSkald, Log,
      TEXT("TryInitializeWorldAndStart: bAllLockedIn=%s bAllHaveControllers=%s "
           "bAllHumansHaveFactions=%s bAllHumansHaveNames=%s CurrentHumanCount=%d CurrentControllerCount=%d "
           "ControllerCount=%d bReadyToStart=%s"),
      bAllLockedIn ? TEXT("true") : TEXT("false"),
      bAllHaveControllers ? TEXT("true") : TEXT("false"),
      bAllHumansHaveFactions ? TEXT("true") : TEXT("false"),
      bAllHumansHaveNames ? TEXT("true") : TEXT("false"), CurrentHumanCount,
      CurrentControllerCount,
      TurnManager ? TurnManager->GetControllerCount() : 0,
      bReadyToStart ? TEXT("true") : TEXT("false"));

  if (GEngine && CurrentHumanCount < MinPlayerCount) {
    GEngine->AddOnScreenDebugMessage(
        -1, 4.f, FColor::Yellow,
        FString::Printf(TEXT("Waiting for players: %d/%d ready"),
                        CurrentHumanCount, MinPlayerCount));
  }

  if (GEngine && (!bAllHumansHaveFactions || !bAllHumansHaveNames)) {
    FString Reason;
    if (!bAllHumansHaveFactions) {
      Reason = TEXT("faction selections");
    }
    if (!bAllHumansHaveNames) {
      if (!Reason.IsEmpty()) {
        Reason += TEXT(" & ");
      }
      Reason += TEXT("display names");
    }
    GEngine->AddOnScreenDebugMessage(
        -1, 4.f, FColor::Yellow,
        FString::Printf(TEXT("Waiting for lobby data: missing %s"), *Reason));
  }

  if (!bWorldInitialized && bReadyToStart) {
    if (!bStrategicInitiativePromptIssued && !bAwaitingStrategicInitiativeInput) {
      BeginStrategicInitiativePhase();
    }

    if (bAwaitingStrategicInitiativeInput) {
      return;
    }

    if (!bWorldInitialized) {
      return;
    }
  }

  if (TurnManager && TurnManager->HasTurnsStarted()) {
    bTurnsStarted = true;
  }

  if (bWorldInitialized && bReadyToStart && !bTurnsStarted && TurnManager &&
      !TurnManager->HasTurnsStarted() &&
      TurnManager->GetCurrentPhase() != ETurnPhase::ArmyPlacement &&
      TurnManager->GetControllerCount() > 0) {
    bTurnsStarted = true;
    TurnManager->SortControllersByInitiative();
    TurnManager->StartTurns();

    UE_LOG(LogSkald, Log, TEXT("TryInitializeWorldAndStart: Turns started"));
    if (GEngine) {
      GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Green,
                                       TEXT("Game started"));
    }
  }

  if (bWorldInitialized && bTurnsStarted) {
    GetWorldTimerManager().ClearTimer(RetryInitTimerHandle);
  }
}

void ASkaldGameMode::BeginStrategicInitiativePhase() {
  ASkaldGameState *GS = GetGameState<ASkaldGameState>();
  if (!GS) {
    return;
  }

  PendingStrategicInitiativePlayers.Empty();
  PendingStrategicInitiativeResolutionPlayers.Empty();
  PendingStrategicInitiativeAIPlayers.Empty();
  PendingStrategicInitiativeRolls.Empty();
  bAwaitingStrategicInitiativeInput = true;
  bStrategicInitiativePromptIssued = true;
  StrategicInitiativeRound = 1;
  StrategicInitiativeRoundByPlayer.Empty();

  EnsureStrategicInitiativeDiceBinding();
  if (USkaldDiceManager *DiceManager = ResolveDiceManager()) {
    DiceManager->SetHoldInitiativeDice(true);
  }
  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(StrategicInitiativeAIRollHandle);
  }

  for (APlayerState *PSBase : GS->PlayerArray) {
    if (ASkaldPlayerState *PS = Cast<ASkaldPlayerState>(PSBase)) {
      if (PS->InitiativeRoll > 0) {
        PS->InitiativeRoll = 0;
      }

      PendingStrategicInitiativeResolutionPlayers.Add(PS);
      StrategicInitiativeRoundByPlayer.Add(PS, StrategicInitiativeRound);

      if (!PS->bIsAI) {
        PendingStrategicInitiativePlayers.Add(PS);
      } else {
        PendingStrategicInitiativeAIPlayers.Add(PS);
      }
    }
  }

  for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator();
       It; ++It) {
    if (ASkaldPlayerController *PC = Cast<ASkaldPlayerController>(*It)) {
      ASkaldPlayerState *PS = PC->GetPlayerState<ASkaldPlayerState>();
      const bool bIsAI = PS && PS->bIsAI;
      if (!bIsAI) {
        PC->ClientPromptStrategicInitiative(StrategicInitiativeRound,
                                            /*RollValue*/ 0,
                                            /*bWonInitiative*/ false);
      }
    }
  }

  if (PendingStrategicInitiativePlayers.Num() == 0) {
    ScheduleNextStrategicInitiativeAIRoll();
  }
}

void ASkaldGameMode::RemovePendingStrategicInitiativePlayer(
    ASkaldPlayerState *PlayerState) {
  if (!bAwaitingStrategicInitiativeInput) {
    return;
  }

  if (PlayerState) {
    PendingStrategicInitiativePlayers.Remove(
        TWeakObjectPtr<ASkaldPlayerState>(PlayerState));
    RemovePendingStrategicInitiativeResolutionPlayer(PlayerState);
  }

  HandlePendingStrategicInitiativeUpdate();
}

void ASkaldGameMode::PrunePendingStrategicInitiativePlayers() {
  for (auto It = PendingStrategicInitiativePlayers.CreateIterator(); It; ++It) {
    if (!It->IsValid()) {
      It.RemoveCurrent();
    }
  }
}

void ASkaldGameMode::RemovePendingStrategicInitiativeResolutionPlayer(
    ASkaldPlayerState *PlayerState) {
  if (!PlayerState) {
    return;
  }

  PendingStrategicInitiativeResolutionPlayers.Remove(
      TWeakObjectPtr<ASkaldPlayerState>(PlayerState));

  PendingStrategicInitiativeAIPlayers.RemoveAll(
      [PlayerState](const TWeakObjectPtr<ASkaldPlayerState> &Entry) {
        return !Entry.IsValid() || Entry.Get() == PlayerState;
      });
}

void ASkaldGameMode::PrunePendingStrategicInitiativeResolutionPlayers() {
  for (auto It = PendingStrategicInitiativeResolutionPlayers.CreateIterator(); It;
       ++It) {
    if (!It->IsValid()) {
      It.RemoveCurrent();
    }
  }

  PendingStrategicInitiativeAIPlayers.RemoveAll(
      [](const TWeakObjectPtr<ASkaldPlayerState> &Entry) {
        return !Entry.IsValid() || (Entry.IsValid() && Entry.Get()->InitiativeRoll > 0);
      });
}

void ASkaldGameMode::HandlePendingStrategicInitiativeUpdate() {
  PrunePendingStrategicInitiativePlayers();
  PrunePendingStrategicInitiativeResolutionPlayers();

  if (!bAwaitingStrategicInitiativeInput) {
    return;
  }

  if (PendingStrategicInitiativePlayers.Num() == 0) {
    ScheduleNextStrategicInitiativeAIRoll();
  }

  if (PendingStrategicInitiativePlayers.Num() == 0 &&
      PendingStrategicInitiativeResolutionPlayers.Num() == 0 &&
      PendingStrategicInitiativeRolls.Num() == 0) {
    ResolveStrategicInitiativePhase();
  }
}

void ASkaldGameMode::ConfirmStrategicInitiativeRoll(
    ASkaldPlayerController *Controller) {
  if (!Controller || !bAwaitingStrategicInitiativeInput) {
    return;
  }

  ASkaldPlayerState *PS = Controller->GetPlayerState<ASkaldPlayerState>();
  if (!PS) {
    return;
  }

  if (PS->InitiativeRoll <= 0 && !IsStrategicInitiativeRollPending(PS)) {
    if (StartStrategicInitiativeRoll(PS, /*bUsePlayerTint*/ true)) {
      ScheduleNextStrategicInitiativeAIRoll();
      return;
    }

    const TArray<int32> EmptyResults;
    const int32 FallbackValue = ResolveStrategicInitiativeResult(EmptyResults);
    PS->InitiativeRoll = FallbackValue;
  }

  RemovePendingStrategicInitiativePlayer(PS);
}

bool ASkaldGameMode::IsStrategicInitiativeRollPending(
    const ASkaldPlayerState *PlayerState) const {
  if (!PlayerState) {
    return false;
  }

  for (const auto &Entry : PendingStrategicInitiativeRolls) {
    if (Entry.Value.Get() == PlayerState) {
      return true;
    }
  }

  return false;
}

bool ASkaldGameMode::StartStrategicInitiativeRoll(
    ASkaldPlayerState *PlayerState, bool bUsePlayerTint) {
  if (!PlayerState) {
    return false;
  }

  if (USkaldDiceManager *DiceManager = ResolveDiceManager()) {
    EnsureStrategicInitiativeDiceBinding();
    const int32 PlayerDice = bUsePlayerTint ? 1 : 0;
    const int32 EnemyDice = bUsePlayerTint ? 0 : 1;
    USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>();
    const FLinearColor FactionColor = GI
                                          ? GI->GetFactionColor(PlayerState->Faction)
                                          : USkaldGameInstance::GetDefaultFactionColor(
                                                PlayerState->Faction);
    const FLinearColor PlayerTint =
        bUsePlayerTint ? FactionColor : FLinearColor::Transparent;
    const FLinearColor EnemyTint =
        bUsePlayerTint ? FLinearColor::Transparent : FactionColor;
    const FGuid RollId = DiceManager->RollDice_D6(PlayerDice, EnemyDice, true,
                                                 PlayerTint, EnemyTint);
    if (RollId.IsValid()) {
      if (UWorld *World = GetWorld()) {
        ASkaldPlayerController::BroadcastPhysicalDiceRoll(
            World, RollId, PlayerDice, EnemyDice, true, PlayerTint, EnemyTint);
      }
      PendingStrategicInitiativeRolls.Add(RollId, PlayerState);
      return true;
    }

    UE_LOG(LogSkald, Warning,
           TEXT("Strategic initiative roll for %s failed to start; falling back to RNG."),
           *PlayerState->GetPlayerName());
  } else {
    UE_LOG(LogSkald, Warning,
           TEXT("Strategic initiative roll fallback: dice manager unavailable."));
  }

  return false;
}

void ASkaldGameMode::ScheduleNextStrategicInitiativeAIRoll() {
  if (!bAwaitingStrategicInitiativeInput) {
    return;
  }

  PrunePendingStrategicInitiativeResolutionPlayers();

  ASkaldPlayerState *NextAI = nullptr;
  for (const TWeakObjectPtr<ASkaldPlayerState> &Entry :
       PendingStrategicInitiativeAIPlayers) {
    if (ASkaldPlayerState *PS = Entry.Get()) {
      if (PS->InitiativeRoll <= 0 && !IsStrategicInitiativeRollPending(PS)) {
        NextAI = PS;
        break;
      }
    }
  }

  if (!NextAI) {
    if (UWorld *World = GetWorld()) {
      World->GetTimerManager().ClearTimer(StrategicInitiativeAIRollHandle);
    }
    return;
  }

  if (UWorld *World = GetWorld()) {
    FTimerManager &TimerManager = World->GetTimerManager();
    if (!TimerManager.IsTimerActive(StrategicInitiativeAIRollHandle)) {
      FTimerDelegate Delegate = FTimerDelegate::CreateUObject(
          this, &ASkaldGameMode::PerformStrategicInitiativeAIRoll,
          TWeakObjectPtr<ASkaldPlayerState>(NextAI));
      TimerManager.SetTimer(StrategicInitiativeAIRollHandle, Delegate,
                            FMath::Max(StrategicInitiativeAIRollDelay, 0.f),
                            /*bLoop*/ false);
    }
  }
}

void ASkaldGameMode::PerformStrategicInitiativeAIRoll(
    TWeakObjectPtr<ASkaldPlayerState> PlayerState) {
  if (!bAwaitingStrategicInitiativeInput) {
    return;
  }

  ASkaldPlayerState *PS = PlayerState.Get();
  if (!PS || PS->InitiativeRoll > 0 || IsStrategicInitiativeRollPending(PS)) {
    ScheduleNextStrategicInitiativeAIRoll();
    return;
  }

  if (StartStrategicInitiativeRoll(PS, /*bUsePlayerTint*/ false)) {
    return;
  }

  const TArray<int32> EmptyResults;
  const int32 FallbackValue = ResolveStrategicInitiativeResult(EmptyResults);
  PS->InitiativeRoll = FallbackValue;
  RemovePendingStrategicInitiativeResolutionPlayer(PS);
  HandlePendingStrategicInitiativeUpdate();
}

void ASkaldGameMode::ResolveStrategicInitiativePhase() {
  if (!bAwaitingStrategicInitiativeInput) {
    return;
  }

  ASkaldGameState *GS = GetGameState<ASkaldGameState>();
  if (!GS) {
    return;
  }

  TMap<int32, TArray<ASkaldPlayerState *>> InitiativeBuckets;
  for (APlayerState *PSBase : GS->PlayerArray) {
    if (ASkaldPlayerState *PS = Cast<ASkaldPlayerState>(PSBase)) {
      if (PS->InitiativeRoll > 0) {
        InitiativeBuckets.FindOrAdd(PS->InitiativeRoll).Add(PS);
      }
    }
  }

  TArray<ASkaldPlayerState *> PlayersNeedingReroll;
  for (const TPair<int32, TArray<ASkaldPlayerState *>> &Pair : InitiativeBuckets) {
    if (Pair.Value.Num() > 1) {
      PlayersNeedingReroll.Append(Pair.Value);
    }
  }

  if (PlayersNeedingReroll.Num() > 0) {
    ++StrategicInitiativeRound;
    if (StrategicInitiativeRound <= 0) {
      StrategicInitiativeRound = 1;
    }

    PendingStrategicInitiativePlayers.Empty();
    PendingStrategicInitiativeResolutionPlayers.Empty();
    PendingStrategicInitiativeAIPlayers.Empty();

    if (UWorld *World = GetWorld()) {
      World->GetTimerManager().ClearTimer(StrategicInitiativeAIRollHandle);
    }

    for (ASkaldPlayerState *PS : PlayersNeedingReroll) {
      if (!PS) {
        continue;
      }

      PS->InitiativeRoll = 0;
      PendingStrategicInitiativeResolutionPlayers.Add(PS);
      StrategicInitiativeRoundByPlayer.Add(PS, StrategicInitiativeRound);

      if (PS->bIsAI) {
        PendingStrategicInitiativeAIPlayers.Add(PS);
      } else {
        PendingStrategicInitiativePlayers.Add(PS);
      }
    }

    UE_LOG(LogSkald, Log,
           TEXT("ResolveStrategicInitiativePhase: %d players tied on initiative; "
                "starting reroll round %d."),
           PlayersNeedingReroll.Num(), StrategicInitiativeRound);

    if (GEngine) {
      GEngine->AddOnScreenDebugMessage(
          -1, 3.f, FColor::Yellow,
          FString::Printf(TEXT("Initiative tie detected. Reroll round %d."),
                          StrategicInitiativeRound));
    }

    for (ASkaldPlayerState *PS : PlayersNeedingReroll) {
      if (!PS || PS->bIsAI) {
        continue;
      }

      if (ASkaldPlayerController *PC = Cast<ASkaldPlayerController>(PS->GetOwner())) {
        PC->ClientPromptStrategicInitiative(StrategicInitiativeRound,
                                            /*RollValue*/ 0,
                                            /*bWonInitiative*/ false);
      }
    }

    if (PendingStrategicInitiativePlayers.Num() == 0) {
      ScheduleNextStrategicInitiativeAIRoll();
    }
    return;
  }

  UE_LOG(LogSkald, Log,
         TEXT("TryInitializeWorldAndStart: Initializing world after initiative "
              "confirmations"));

  bAwaitingStrategicInitiativeInput = false;

  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(StrategicInitiativeAIRollHandle);
  }

  if (USkaldDiceManager *DiceManager = ResolveDiceManager()) {
    DiceManager->SetHoldInitiativeDice(false);
  }

  const bool bInitialized = InitializeWorld();
  PendingStrategicInitiativePlayers.Empty();
  PendingStrategicInitiativeResolutionPlayers.Empty();
  PendingStrategicInitiativeAIPlayers.Empty();
  PendingStrategicInitiativeRolls.Empty();
  bStrategicInitiativePromptIssued = false;
  StrategicInitiativeRound = 0;
  StrategicInitiativeRoundByPlayer.Empty();

  if (bInitialized) {
    bWorldInitialized = true;
    BeginArmyPlacementPhase();
    HandleWorldInitializationComplete();
  }

  FTimerDelegate RetryInit =
      FTimerDelegate::CreateUObject(this, &ASkaldGameMode::TryInitializeWorldAndStart);
  GetWorldTimerManager().SetTimerForNextTick(RetryInit);
}

void ASkaldGameMode::NotifyStrategicInitiativeRoll(
    ASkaldPlayerController *Controller, int32 RoundNumber, int32 RollValue,
    bool bWonInitiative) {
  if (!Controller) {
    return;
  }

  int32 DisplayRound = RoundNumber;
  if (const ASkaldPlayerState *TargetState =
          Controller->GetPlayerState<ASkaldPlayerState>()) {
    if (const int32 *StoredRound =
            StrategicInitiativeRoundByPlayer.Find(TargetState)) {
      DisplayRound = *StoredRound;
    }
  }

  int32 EnemyRoll = 0;
  if (const ASkaldGameState *GS = GetGameState<ASkaldGameState>()) {
    const ASkaldPlayerState *TargetState =
        Controller->GetPlayerState<ASkaldPlayerState>();
    for (APlayerState *PSBase : GS->PlayerArray) {
      if (const ASkaldPlayerState *Other =
              Cast<ASkaldPlayerState>(PSBase)) {
        if (Other != TargetState) {
          EnemyRoll = FMath::Max(EnemyRoll, Other->InitiativeRoll);
        }
      }
    }
  }

  if (bStrategicInitiativePromptIssued) {
    Controller->ClientDisplayStrategicInitiativeResult(DisplayRound, RollValue,
                                                      EnemyRoll,
                                                      bWonInitiative);
  } else {
    Controller->ClientPromptStrategicInitiative(DisplayRound, RollValue,
                                               bWonInitiative);
  }
}

void ASkaldGameMode::EnsureStrategicInitiativeDiceBinding() {
  if (USkaldDiceManager *Manager = ResolveDiceManager()) {
    if (!Manager->OnDiceRollCompleted.IsAlreadyBound(
            this, &ASkaldGameMode::HandleStrategicInitiativeDiceCompleted)) {
      Manager->OnDiceRollCompleted.AddDynamic(
          this, &ASkaldGameMode::HandleStrategicInitiativeDiceCompleted);
    }
  }
}

USkaldDiceManager *ASkaldGameMode::ResolveDiceManager() {
  if (CachedDiceManager.IsValid()) {
    return CachedDiceManager.Get();
  }

  USkaldDiceManager *ResolvedManager = nullptr;
  if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
    ResolvedManager = GI->GetSubsystem<USkaldDiceManager>();
  }

  if (ResolvedManager) {
    CachedDiceManager = ResolvedManager;
  }

  return ResolvedManager;
}

void ASkaldGameMode::HandleStrategicInitiativeDiceCompleted(
    const FGuid &RollId, const TArray<int32> &Results) {
  if (!bAwaitingStrategicInitiativeInput) {
    PendingStrategicInitiativeRolls.Remove(RollId);
    return;
  }

  TWeakObjectPtr<ASkaldPlayerState> *Entry =
      PendingStrategicInitiativeRolls.Find(RollId);
  if (!Entry) {
    return;
  }

  ASkaldPlayerState *PlayerState = Entry->Get();
  PendingStrategicInitiativeRolls.Remove(RollId);

  const int32 RollValue = ResolveStrategicInitiativeResult(Results);
  if (PlayerState) {
    PlayerState->InitiativeRoll = RollValue;
    RemovePendingStrategicInitiativePlayer(PlayerState);
  } else {
    HandlePendingStrategicInitiativeUpdate();
  }
}

int32 ASkaldGameMode::ResolveStrategicInitiativeResult(
    const TArray<int32> &Results) {
  if (Results.Num() > 0) {
    return FMath::Clamp(Results[0], 1, 6);
  }

  if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
    return GI->CombatRandomStream.RandRange(1, 6);
  }

  return FMath::RandRange(1, 6);
}

void ASkaldGameMode::HandleWorldInitializationComplete() {
  USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>();
  if (!GI) {
    return;
  }

  if (GI->bIsInBattleMap) {
    GI->SetBattleMapActive(false);

    // Ensure all clients clear any stale battle-map state that would block
    // overworld interaction such as territory selection.
    if (TurnManager && HasAuthority()) {
      TurnManager->MulticastSetBattleMapActive(false);
    }
  }

  if (bPendingInitialSnapshot || GI->CachedWorldMapTerritories.Num() == 0) {
    const bool bCapturedSnapshot = GI->CacheWorldMapSnapshot(GetWorld());
    if (bCapturedSnapshot) {
      bPendingInitialSnapshot = false;
    }
  }

  const bool bHasPendingResolution =
      GI->bPendingBattleResolution || GI->PendingBattleResolution.bValid;

  if (bHasPendingResolution) {
    if (TurnManager) {
      UE_LOG(LogSkald, Verbose,
             TEXT("HandleWorldInitializationComplete: resolving deferred battle result."));
      TurnManager->ResolveGridBattleResult();
    } else {
      UE_LOG(LogSkald, Verbose,
             TEXT("HandleWorldInitializationComplete: awaiting turn manager before resolving battle result."));
      FTimerDelegate RetryDelegate = FTimerDelegate::CreateUObject(
          this, &ASkaldGameMode::HandleWorldInitializationComplete);
      GetWorldTimerManager().SetTimerForNextTick(RetryDelegate);
    }
    return;
  }

  if (GI->bTravelPending) {
    UE_LOG(LogSkald, Verbose,
           TEXT("HandleWorldInitializationComplete: clearing travel state after overworld load."));
    GI->SetTravelPending(false);
  }
}

void ASkaldGameMode::ApplyLoadedGame(USkaldSaveGame *LoadedGame) {
  if (!LoadedGame || !WorldMap) {
    return;
  }

  bWorldInitialized = true;
  bTurnsStarted = true;
  HandleWorldInitializationComplete();

  if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
    GI->CachedWorldMapTerritories = LoadedGame->Territories;
  }

  TMap<int32, const FSkaldControllerSaveData *> ControllerSaveById;
  int32 SavedAIControllers = 0;
  for (const FSkaldControllerSaveData &ControllerSave : LoadedGame->Controllers) {
    if (ControllerSave.PlayerId > 0) {
      ControllerSaveById.Add(ControllerSave.PlayerId, &ControllerSave);
    }
    if (ControllerSave.bIsAI) {
      ++SavedAIControllers;
    }
  }

  TMap<int32, ASkaldPlayerController *> HumanControllerById;
  TArray<ASkaldPlayerController *> AvailableHumanControllers;
  TSet<ASkaldPlayerController *> AssignedHumanControllers;
  if (UWorld *MutableWorld = GetWorld()) {
    for (FConstPlayerControllerIterator It =
             MutableWorld->GetPlayerControllerIterator();
         It; ++It) {
      ASkaldPlayerController *PlayerController =
          Cast<ASkaldPlayerController>(*It);
      if (!PlayerController || PlayerController->IsA<ASkaldAIController>()) {
        continue;
      }

      AvailableHumanControllers.Add(PlayerController);

      if (ASkaldPlayerState *ExistingState =
              PlayerController->GetPlayerState<ASkaldPlayerState>()) {
        HumanControllerById.Add(ExistingState->GetPlayerId(),
                                PlayerController);
      }
    }
  }

  if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
    GI->AIPlayersToSpawn = FMath::Max(SavedAIControllers, 0);
  }

  ASkaldGameState *GS = GetGameState<ASkaldGameState>();
  if (GS) {
    GS->Players.Empty();
    GS->PlayerArray.Empty();
    GS->CurrentTurnIndex = LoadedGame->CurrentPlayerIndex;
  }

  PlayerDataArray.Empty();
  TArray<FPlayerSaveStruct> PendingAISaves;
  TArray<int32> PendingAIPlayerDataIndices;
  for (const FPlayerSaveStruct &PlayerSave : LoadedGame->Players) {
    const FSkaldControllerSaveData *const *ControllerSavePtr =
        ControllerSaveById.Find(PlayerSave.PlayerID);
    const FSkaldControllerSaveData *ControllerSave =
        ControllerSavePtr ? *ControllerSavePtr : nullptr;
    const bool bIsAIPlayer = ControllerSave ? ControllerSave->bIsAI : false;

    ASkaldPlayerController *AssignedController = nullptr;
    ASkaldPlayerState *PlayerState = nullptr;
    if (!bIsAIPlayer) {
      auto FindControllerForPlayerId =
          [&](int32 PlayerId) -> ASkaldPlayerController * {
        if (PlayerId <= 0) {
          return nullptr;
        }

        if (ASkaldPlayerController **FoundController =
                HumanControllerById.Find(PlayerId)) {
          if (!AssignedHumanControllers.Contains(*FoundController)) {
            return *FoundController;
          }
        }

        return nullptr;
      };

      if (ControllerSave) {
        AssignedController = FindControllerForPlayerId(ControllerSave->PlayerId);
      }

      if (!AssignedController) {
        AssignedController = FindControllerForPlayerId(PlayerSave.PlayerID);
      }

      if (!AssignedController) {
        for (ASkaldPlayerController *Candidate : AvailableHumanControllers) {
          if (!AssignedHumanControllers.Contains(Candidate)) {
            AssignedController = Candidate;
            break;
          }
        }
      }

      if (AssignedController) {
        AssignedHumanControllers.Add(AssignedController);
        for (auto It = HumanControllerById.CreateIterator(); It; ++It) {
          if (It.Value() == AssignedController) {
            It.RemoveCurrent();
          }
        }
        PlayerState =
            AssignedController->GetPlayerState<ASkaldPlayerState>();

        if (!PlayerState) {
          PlayerState = GetWorld()->SpawnActor<ASkaldPlayerState>();
          if (PlayerState) {
            AssignedController->PlayerState = PlayerState;
            PlayerState->SetOwner(AssignedController);
          }
        }
      }
    }

    FS_PlayerData Data;
    Data.PlayerID = PlayerSave.PlayerID;
    Data.PlayerName = PlayerSave.PlayerName;
    Data.DisplayName = PlayerSave.PlayerName;
    Data.Faction = PlayerSave.Faction;
    Data.Resources = PlayerSave.Resources;
    Data.IsEliminated = PlayerSave.IsEliminated;
    Data.IsHuman = !bIsAIPlayer;
    Data.IsAI = bIsAIPlayer;
    Data.IsAlive = false;
    Data.CapitalsOwned = 0;
    Data.TroopsCount = 0;
    Data.TerritoriesOwned = 0;
    if (bIsAIPlayer) {
      const int32 PlayerDataIndex = PlayerDataArray.Add(Data);
      PendingAISaves.Add(PlayerSave);
      PendingAIPlayerDataIndices.Add(PlayerDataIndex);
      continue;
    }

    if (!PlayerState) {
      PlayerState = GetWorld()->SpawnActor<ASkaldPlayerState>();
    }

    if (!PlayerState) {
      continue;
    }

    PlayerState->SetPlayerId(PlayerSave.PlayerID);
    PlayerState->PlayerDisplayName = PlayerSave.PlayerName;
    PlayerState->SetPlayerName(PlayerSave.PlayerName);
    PlayerState->Faction = PlayerSave.Faction;
    PlayerState->Resources = PlayerSave.Resources;
    PlayerState->IsEliminated = PlayerSave.IsEliminated;
    PlayerState->bIsAI = bIsAIPlayer;

    if (AssignedController && AssignedController->PlayerState != PlayerState) {
      AssignedController->PlayerState = PlayerState;
      PlayerState->SetOwner(AssignedController);
    }

    if (AssignedController) {
      HumanControllerById.Add(PlayerState->GetPlayerId(), AssignedController);
    }

    if (GS) {
      GS->AddPlayerState(PlayerState);
    }

    if (TurnManager) {
      TurnManager->BroadcastResources(PlayerState);
    }

    PlayerDataArray.Add(Data);
  }

  const auto RestoreAISavesFromSnapshot =
      [&](const TArray<FPlayerSaveStruct> &AISaves,
          const TArray<int32> &AIDataIndices) -> bool {
    if (AISaves.Num() == 0) {
      return false;
    }

    if (!GS) {
      return false;
    }

    if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
      GI->AIPlayersToSpawn = AISaves.Num();
    }

    PopulateAIPlayers();

    TArray<ASkaldPlayerState *> SpawnedAIStates;
    for (ASkaldPlayerState *PlayerState : GS->Players) {
      if (PlayerState && PlayerState->bIsAI) {
        SpawnedAIStates.Add(PlayerState);
      }
    }

    const int32 RestoredCount =
        FMath::Min(AISaves.Num(), SpawnedAIStates.Num());
    for (int32 Index = 0; Index < RestoredCount; ++Index) {
      ASkaldPlayerState *AIState = SpawnedAIStates[Index];
      if (!AIState) {
        continue;
      }

      const FPlayerSaveStruct &PlayerSave = AISaves[Index];
      AIState->bIsAI = true;
      if (AIState->GetPlayerId() != PlayerSave.PlayerID) {
        AIState->SetPlayerId(PlayerSave.PlayerID);
      }
      AIState->PlayerDisplayName = PlayerSave.PlayerName;
      AIState->SetPlayerName(PlayerSave.PlayerName);
      AIState->Faction = PlayerSave.Faction;
      AIState->Resources = PlayerSave.Resources;
      AIState->IsEliminated = PlayerSave.IsEliminated;

      if (TurnManager) {
        TurnManager->BroadcastResources(AIState);
      }

      if (PlayerDataArray.IsValidIndex(AIDataIndices[Index])) {
        FS_PlayerData &PlayerData = PlayerDataArray[AIDataIndices[Index]];
        PlayerData.PlayerID = PlayerSave.PlayerID;
        PlayerData.PlayerName = PlayerSave.PlayerName;
        PlayerData.DisplayName = PlayerSave.PlayerName;
        PlayerData.Faction = PlayerSave.Faction;
        PlayerData.Resources = PlayerSave.Resources;
        PlayerData.IsEliminated = PlayerSave.IsEliminated;
        PlayerData.IsAI = true;
        PlayerData.IsHuman = false;
        PlayerData.IsAlive = false;
        PlayerData.CapitalsOwned = 0;
      }
    }

    if (RestoredCount < AISaves.Num()) {
      UE_LOG(LogSkald, Warning,
             TEXT("ApplyLoadedGame: Restored %d/%d AI players from save."),
             RestoredCount, AISaves.Num());
    }

    return RestoredCount > 0;
  };

  const bool bRestoredAISaves =
      RestoreAISavesFromSnapshot(PendingAISaves, PendingAIPlayerDataIndices);

  TMap<int32, FS_PlayerData *> PlayerDataById;
  for (FS_PlayerData &Data : PlayerDataArray) {
    PlayerDataById.Add(Data.PlayerID, &Data);
  }

  if (WorldMap && WorldMap->Territories.Num() == 0) {
    if (!WorldMap->GenerateTerritoriesFromTable()) {
      UE_LOG(LogSkald, Error,
             TEXT("ApplyLoadedGame: Failed to generate territories from table."));
      return;
    }
  }

  TMap<int32, ATerritory *> TerritoryById;
  TMap<int32, ASkaldPlayerState *> PlayerStateById;
  if (GS) {
    for (ASkaldPlayerState *PlayerState : GS->Players) {
      if (PlayerState) {
        PlayerStateById.Add(PlayerState->GetPlayerId(), PlayerState);
      }
    }
  }
  if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
    GI->TakenFactions.Reset();
    if (GS) {
      for (ASkaldPlayerState *PlayerState : GS->Players) {
        if (PlayerState && PlayerState->Faction != ESkaldFaction::None) {
          GI->TakenFactions.AddUnique(PlayerState->Faction);
        }
      }
    }
  }
  if (WorldMap) {
    TerritoryById.Reserve(WorldMap->Territories.Num());
    for (ATerritory *Territory : WorldMap->Territories) {
      if (!Territory) {
        continue;
      }
      TerritoryById.Add(Territory->TerritoryID, Territory);
      Territory->AdjacentTerritories.Reset();
    }
    WorldMap->SpawnedLocations.Reset();
  }

  SiegePool = LoadedGame->Sieges;
  NextSiegeID = 1;
  for (const FS_Siege &S : SiegePool) {
    NextSiegeID = FMath::Max(NextSiegeID, S.SiegeID + 1);
  }

  for (const FS_Territory &TerrData : LoadedGame->Territories) {
    ATerritory *Territory = TerritoryById.FindRef(TerrData.TerritoryID);
    if (!Territory) {
      continue;
    }

    ASkaldPlayerState *TerritoryOwner = PlayerStateById.FindRef(TerrData.OwnerPlayerID);

    Territory->OwningPlayer = TerritoryOwner;
    Territory->ArmyUnits = TerrData.ArmyUnits;
    Territory->bIsCapital = TerrData.IsCapital;
    Territory->bHasTreasure = TerrData.HasTreasure;
    WriteIntProperty(Territory, TEXT("TreasureAttachedUnitID"),
                     TerrData.TreasureAttachedUnitID);
    WriteIntProperty(Territory, TEXT("FortificationLevel"),
                     TerrData.FortificationLevel);
    WriteBoolProperty(Territory, TEXT("Moat"), TerrData.Moat);
    WriteIntProperty(Territory, TEXT("WallHealth"), TerrData.WallHealth);
    Territory->ContinentID = TerrData.ContinentID;
    Territory->BuiltSiegeID = TerrData.BuiltSiegeID;
    WriteIntProperty(Territory, TEXT("ConqueredTurn"), TerrData.ConqueredTurn);
    WriteBoolProperty(Territory, TEXT("IsNeutralSpawn"),
                      TerrData.IsNeutralSpawn);
    Territory->SetActorLocation(TerrData.Location);
    Territory->AdjacentTerritories.Reset();
    for (int32 NeighborId : TerrData.AdjacentIDs) {
      if (ATerritory *Neighbor = TerritoryById.FindRef(NeighborId)) {
        Territory->AdjacentTerritories.AddUnique(Neighbor);
      }
    }

    Territory->RefreshAppearance();

    if (FS_PlayerData **PlayerDataPtr =
            PlayerDataById.Find(TerrData.OwnerPlayerID)) {
      FS_PlayerData *PlayerData = *PlayerDataPtr;
      PlayerData->IsEliminated = false;
      PlayerData->IsAlive = true;
      PlayerData->TerritoriesOwned += 1;
      PlayerData->TroopsCount += TerrData.ArmyUnits;
      if (TerrData.IsCapital) {
        PlayerData->CapitalsOwned += 1;
        PlayerData->CapitalTerritoryIDs.AddUnique(TerrData.TerritoryID);
      }
    }

    if (WorldMap) {
      WorldMap->SpawnedLocations.Add(TerrData.TerritoryID, TerrData.Location);
    }
  }

  if (WorldMap && LoadedGame->WorldState.SelectedTerritoryId != INDEX_NONE) {
    if (ATerritory *Selected =
            WorldMap->GetTerritoryById(LoadedGame->WorldState.SelectedTerritoryId)) {
      WorldMap->SelectTerritory(Selected, false,
                                LoadedGame->WorldState.SelectedByPlayerId);
    }
  }

  if (UWorld *MutableWorld = GetWorld()) {
    if (LoadedGame->WorldState.ActiveAudio.Num() > 0) {
      TArray<UAudioComponent *> AudioComponents;
      for (TObjectIterator<UAudioComponent> It; It; ++It) {
        if (It->GetWorld() == MutableWorld) {
          AudioComponents.Add(*It);
        }
      }

      TSet<UAudioComponent *> Processed;
      for (const FSkaldAudioComponentSaveData &AudioSave :
           LoadedGame->WorldState.ActiveAudio) {
        const FSoftObjectPath SoundPath = AudioSave.Sound.ToSoftObjectPath();
        if (!SoundPath.IsValid()) {
          continue;
        }

        for (UAudioComponent *Audio : AudioComponents) {
          if (!Audio || Processed.Contains(Audio) || !Audio->Sound) {
            continue;
          }

          if (USoundBase *const Sound = Audio->Sound.Get();
              Sound && Sound->GetPathName() == SoundPath.ToString()) {
            if (AudioSave.bWasPlaying) {
              Audio->Play();
            } else {
              Audio->Stop();
            }
            Processed.Add(Audio);
            break;
          }
        }
      }
    }
  }

  for (TPair<int32, ASkaldPlayerState *> &Entry : PlayerStateById) {
    ASkaldPlayerState *PlayerState = Entry.Value;
    if (!PlayerState) {
      continue;
    }

    const FS_PlayerData *const *PlayerDataPtr = PlayerDataById.Find(Entry.Key);
    const bool bShouldBeEliminated =
        !(PlayerDataPtr && *PlayerDataPtr) || (*PlayerDataPtr)->IsEliminated;

    bool bStateChanged = false;
    if (PlayerState->IsEliminated != bShouldBeEliminated) {
      PlayerState->IsEliminated = bShouldBeEliminated;
      bStateChanged = true;
    }

    if (bShouldBeEliminated && PlayerState->bHasLockedIn) {
      PlayerState->bHasLockedIn = false;
      bStateChanged = true;
    }

    if (bStateChanged) {
      PlayerState->ForceNetUpdate();
    }
  }

  if (!bRestoredAISaves) {
    PopulateAIPlayers();
  }

  if (WorldMap && WorldMap->SelectedTerritory &&
      !TerritoryById.Contains(WorldMap->SelectedTerritory->TerritoryID)) {
    WorldMap->SelectedTerritory = nullptr;
  }

  bool bAppliedControllerCamera = false;
  if (LoadedGame->Controllers.Num() > 0) {
    if (UWorld *MutableWorld = GetWorld()) {
      for (FConstPlayerControllerIterator It =
               MutableWorld->GetPlayerControllerIterator();
           It; ++It) {
        ASkaldPlayerController *PC = Cast<ASkaldPlayerController>(*It);
        if (!PC) {
          continue;
        }

        ASkaldPlayerState *PS = PC->GetPlayerState<ASkaldPlayerState>();
        const int32 PlayerId = PS ? PS->GetPlayerId() : INDEX_NONE;
        if (PlayerId == INDEX_NONE) {
          continue;
        }

        const FSkaldControllerSaveData *const *ControllerSavePtr =
            ControllerSaveById.Find(PlayerId);
        if (!ControllerSavePtr || !*ControllerSavePtr) {
          continue;
        }

        const FSkaldControllerSaveData &ControllerSave = **ControllerSavePtr;
        if (APawn *Pawn = PC->GetPawn()) {
          Pawn->SetActorLocation(ControllerSave.Camera.Location);
          PC->SetControlRotation(ControllerSave.Camera.Rotation);

          if (USpringArmComponent *SpringArm =
                  Pawn->FindComponentByClass<USpringArmComponent>()) {
            SpringArm->TargetArmLength = ControllerSave.Camera.Zoom;
          } else if (UCameraComponent *Camera =
                         Pawn->FindComponentByClass<UCameraComponent>()) {
            Camera->SetFieldOfView(ControllerSave.Camera.Zoom);
          }

          if (ASkald_PlayerCharacter *CharacterPawn =
                  Cast<ASkald_PlayerCharacter>(Pawn)) {
            if (CharacterPawn->IsBattleCameraActive() !=
                ControllerSave.Camera.bBattleCameraActive) {
              CharacterPawn->SetBattleCameraActive(
                  ControllerSave.Camera.bBattleCameraActive);
            }
          }

          if (PC->IsLocalController()) {
            bAppliedControllerCamera = true;
          }
        }
      }
    }
  }

  if (!bAppliedControllerCamera) {
    if (APlayerController *PC = UGameplayStatics::GetPlayerController(this, 0)) {
      if (APawn *Pawn = PC->GetPawn()) {
        FVector Loc = Pawn->GetActorLocation();
        Loc.X = LoadedGame->SavedViewOffset.X;
        Loc.Y = LoadedGame->SavedViewOffset.Y;
        Pawn->SetActorLocation(Loc);

        if (UCameraComponent *Camera =
                Pawn->FindComponentByClass<UCameraComponent>()) {
          if (LoadedGame->SavedZoomAmount > 0.f) {
            Camera->SetFieldOfView(LoadedGame->SavedZoomAmount);
          }
        }
      }
    }
  }

  RefreshHUDs();

  if (GS) {
    GS->OnPlayersUpdated.Broadcast();
  }

  if (TurnManager) {
    TMap<int32, int32> MovementSnapshot;
    for (const FSkaldMovementActionSaveData &Action :
         LoadedGame->GameFlow.MovementActions) {
      if (Action.PlayerId > 0) {
        MovementSnapshot.Add(Action.PlayerId, Action.ActionsTaken);
      }
    }

    TurnManager->SetMovementActionsSnapshot(MovementSnapshot);
    TurnManager->SetPendingBattlePayload(LoadedGame->GameFlow.PendingBattle);
    TurnManager->SetPendingBattlePreparation(
        LoadedGame->GameFlow.PendingBattlePreparation);
    TurnManager->SetPendingBattleReadyState(
        LoadedGame->GameFlow.PendingReadyState);

    TurnManager->SortControllersByInitiative();

    USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>();
    bool bResumedTurns = false;
    if (GI) {
      GI->SavedTurnIndex = LoadedGame->GameFlow.ActiveTurnIndex;
      int32 SavedPlayerId = LoadedGame->SavedTurnPlayerID;
      if (SavedPlayerId <= 0 &&
          LoadedGame->GameFlow.TurnOrder.IsValidIndex(GI->SavedTurnIndex)) {
        SavedPlayerId =
            LoadedGame->GameFlow.TurnOrder[GI->SavedTurnIndex].PlayerId;
      }
      GI->SavedTurnPlayerId = SavedPlayerId;
      GI->SavedTurnPhase = LoadedGame->GameFlow.CurrentPhase;
      GI->bResumeTurns = LoadedGame->GameFlow.bTurnsStarted;

      if (GI->bResumeTurns) {
        bResumedTurns = TurnManager->AttemptResumeSavedTurnState();
        if (!bResumedTurns) {
          GI->bResumeTurns = false;
        }
      }
    }

    if (!bResumedTurns) {
      if (GI) {
        GI->bResumeTurns = false;
      }

      if (!TurnManager->HasTurnsStarted()) {
        TurnManager->StartTurns();
      }

      const TArray<ASkaldPlayerController *> Controllers =
          TurnManager->GetControllers();
      if (Controllers.Num() > 0) {
        int32 TargetIndex = LoadedGame->SavedTurnIndex;
        if (!Controllers.IsValidIndex(TargetIndex)) {
          if (LoadedGame->SavedTurnPlayerID > 0) {
            TargetIndex = Controllers.IndexOfByPredicate(
                [LoadedGame](ASkaldPlayerController *Controller) {
                  if (!Controller) {
                    return false;
                  }
                  if (ASkaldPlayerState *PS =
                          Controller->GetPlayerState<ASkaldPlayerState>()) {
                    return PS->GetPlayerId() == LoadedGame->SavedTurnPlayerID;
                  }
                  return false;
                });
          }

          if (!Controllers.IsValidIndex(TargetIndex)) {
            TargetIndex = FMath::Clamp(LoadedGame->CurrentPlayerIndex, 0,
                                       Controllers.Num() - 1);
          }
        }

        for (int32 i = 0; i < TargetIndex; ++i) {
          TurnManager->AdvanceTurn();
        }
      }
    }
  }

  if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
    GI->HideGlobalStatusMessage();
  }
}

void ASkaldGameMode::BeginArmyPlacementPhase() {
  if (!TurnManager || !WorldMap) {
    return;
  }

  GetWorldTimerManager().ClearTimer(ArmyPlacementAutoAdvanceHandle);
  GetWorldTimerManager().ClearTimer(ArmyPlacementFailsafeHandle);
  bArmyPlacementFailsafeTriggered = false;

  // Ensure controllers are sorted before placement begins and set phase.
  TurnManager->SortControllersByInitiative();
  TurnManager->StartArmyPlacementPhase();

  const TArray<ASkaldPlayerController *> Controllers =
      TurnManager->GetControllers();
  const int32 ExpectedControllerCount =
      FMath::Max(ResolveExpectedControllerCount(), Controllers.Num());

  const bool bWaitingOnControllers =
      PendingControllers.Num() > 0 ||
      (ExpectedControllerCount > 0 &&
       Controllers.Num() < ExpectedControllerCount);

  if (bWaitingOnControllers) {
    if (UWorld *World = GetWorld()) {
      FTimerManager &TimerManager = World->GetTimerManager();
      const bool bRetryScheduled =
          TimerManager.IsTimerActive(ArmyPlacementStartupRetryHandle) ||
          TimerManager.IsTimerPending(ArmyPlacementStartupRetryHandle);
      if (!bRetryScheduled) {
        UE_LOG(LogSkald, Log,
               TEXT("BeginArmyPlacementPhase: waiting for controllers "
                    "Registered=%d Expected=%d Pending=%d"),
               Controllers.Num(), ExpectedControllerCount,
               PendingControllers.Num());
        TimerManager.SetTimer(ArmyPlacementStartupRetryHandle, this,
                              &ASkaldGameMode::BeginArmyPlacementPhase,
                              RetryInitDelay, false);
      }
    }
    return;
  }

  GetWorldTimerManager().ClearTimer(ArmyPlacementStartupRetryHandle);
  ASkaldPlayerController *ActiveController =
      Controllers.Num() > 0 ? Controllers[0] : nullptr;
  const FString PhaseString =
      UEnum::GetValueAsString(TurnManager->GetCurrentPhase());
  UE_LOG(LogSkald, Log, TEXT("BeginArmyPlacementPhase: Controller=%s Phase=%s"),
         *GetNameSafe(ActiveController), *PhaseString);
  if (GEngine) {
    GEngine->AddOnScreenDebugMessage(
        -1, 5.f, FColor::Green,
        FString::Printf(TEXT("Begin Army Placement: %s - %s"),
                        *GetNameSafe(ActiveController), *PhaseString));
  }

  int32 ActivePlayerCount = 0;
  for (ASkaldPlayerController *PC : Controllers) {
    if (ASkaldPlayerState *PS =
            PC ? PC->GetPlayerState<ASkaldPlayerState>() : nullptr) {
      if (!PS->IsEliminated) {
        ++ActivePlayerCount;
      }
    }
  }

  auto ResolveInitialDeployableUnits = [](int32 PlayerCount, int32 OwnedCount) {
    switch (PlayerCount) {
    case 2:
      return 40;
    case 3:
      return 30;
    case 4:
      return 20;
    default:
      return FMath::CeilToInt(OwnedCount / 3.f);
    }
  };

  // Calculate deployable units for each player based on the total number of
  // active players in the match and update HUDs.
  for (ASkaldPlayerController *PC : Controllers) {
    if (ASkaldPlayerState *PS =
            PC ? PC->GetPlayerState<ASkaldPlayerState>() : nullptr) {
      int32 Owned = 0;
      for (ATerritory *Territory : WorldMap->Territories) {
        if (Territory && Territory->OwningPlayer == PS) {
          ++Owned;
        }
      }

      PS->DeployableUnits =
          ResolveInitialDeployableUnits(ActivePlayerCount, Owned);
      TurnManager->BroadcastDeployableUnits(PS);
    }
  }

  ArmyPlacementLeader.Reset();
  PendingArmyPlacementAIController.Reset();
  PlacementIndex = -1;
  AdvanceArmyPlacement();
}

int32 ASkaldGameMode::BuildSiegeAtTerritory(int32 TerritoryID,
                                            ESiegeWeapon Type) {
  if (!WorldMap) {
    return 0;
  }
  ATerritory *Terr = WorldMap->GetTerritoryById(TerritoryID);
  if (!Terr || Terr->BuiltSiegeID != 0) {
    return 0;
  }
  FS_Siege NewSiege;
  NewSiege.SiegeID = NextSiegeID++;
  NewSiege.Type = Type;
  NewSiege.BuiltAtTerritoryID = TerritoryID;
  SiegePool.Add(NewSiege);
  Terr->BuiltSiegeID = NewSiege.SiegeID;
  return NewSiege.SiegeID;
}

int32 ASkaldGameMode::ConsumeSiege(int32 TerritoryID) {
  if (!WorldMap) {
    return 0;
  }
  ATerritory *Terr = WorldMap->GetTerritoryById(TerritoryID);
  if (!Terr || Terr->BuiltSiegeID == 0) {
    return 0;
  }
  const int32 SiegeID = Terr->BuiltSiegeID;
  Terr->BuiltSiegeID = 0;
  if (FS_Siege *Siege = SiegePool.FindByPredicate(
          [SiegeID](const FS_Siege &S) { return S.SiegeID == SiegeID; })) {
    Siege->AssignedToUnitID = TerritoryID;
  }
  return SiegeID;
}

void ASkaldGameMode::AdvanceArmyPlacement() {
  if (!TurnManager || !WorldMap) {
    return;
  }

  GetWorldTimerManager().ClearTimer(ArmyPlacementAutoAdvanceHandle);
  GetWorldTimerManager().ClearTimer(ArmyPlacementFailsafeHandle);

  const TArray<ASkaldPlayerController *> Controllers =
      TurnManager->GetControllers();
  const int32 ExpectedControllerCount =
      FMath::Max(ResolveExpectedControllerCount(), Controllers.Num());

  const bool bWaitingOnControllers =
      PendingControllers.Num() > 0 ||
      (ExpectedControllerCount > 0 &&
       Controllers.Num() < ExpectedControllerCount);

  if (bWaitingOnControllers) {
    if (UWorld *World = GetWorld()) {
      FTimerManager &TimerManager = World->GetTimerManager();
      const bool bRetryScheduled =
          TimerManager.IsTimerActive(ArmyPlacementStartupRetryHandle) ||
          TimerManager.IsTimerPending(ArmyPlacementStartupRetryHandle);
      if (!bRetryScheduled) {
        UE_LOG(LogSkald, Log,
               TEXT("AdvanceArmyPlacement: waiting for controllers Registered=%d "
                    "Expected=%d Pending=%d"),
               Controllers.Num(), ExpectedControllerCount,
               PendingControllers.Num());
        TimerManager.SetTimer(ArmyPlacementStartupRetryHandle, this,
                              &ASkaldGameMode::AdvanceArmyPlacement,
                              RetryInitDelay, false);
      }
    }
    return;
  }

  GetWorldTimerManager().ClearTimer(ArmyPlacementStartupRetryHandle);
  const int32 NumControllers = Controllers.Num();

  ++PlacementIndex;

  while (PlacementIndex < NumControllers) {
    ASkaldPlayerController *PC = Controllers[PlacementIndex];
    ASkaldPlayerState *PS =
        PC ? PC->GetPlayerState<ASkaldPlayerState>() : nullptr;
    if (!PC || !PS) {
      ++PlacementIndex;
      continue;
    }

    if (PS->IsEliminated) {
      bool bOwnsTerritory = false;
      for (ATerritory *Territory : WorldMap->Territories) {
        if (Territory && Territory->OwningPlayer == PS) {
          bOwnsTerritory = true;
          break;
        }
      }

      if (!bOwnsTerritory) {
        ++PlacementIndex;
        continue;
      }

      UE_LOG(LogSkald, Warning,
             TEXT("AdvanceArmyPlacement: Player %s flagged eliminated despite owning territories; clearing elimination flag."),
             *PS->GetResolvedPlayerName(TEXT("AdvanceArmyPlacement_RecoverElimination")));

      PS->IsEliminated = false;
      if (FS_PlayerData *PlayerData = PlayerDataArray.FindByPredicate(
              [PS](const FS_PlayerData &Data) {
                return Data.PlayerID == PS->GetPlayerId();
              })) {
        PlayerData->IsEliminated = false;
        PlayerData->IsAlive = true;
      }
      PS->ForceNetUpdate();
    }

    if (!ArmyPlacementLeader.IsValid()) {
      ArmyPlacementLeader = PC;
    }

    const FString PhaseString =
        UEnum::GetValueAsString(TurnManager->GetCurrentPhase());
    UE_LOG(LogSkald, Log, TEXT("AdvanceArmyPlacement: Controller=%s Phase=%s"),
           *GetNameSafe(PC), *PhaseString);
    if (GEngine) {
      GEngine->AddOnScreenDebugMessage(
          -1, 5.f, FColor::Green,
          FString::Printf(TEXT("Army Placement: %s - %s"), *GetNameSafe(PC),
                          *PhaseString));
    }

    TurnManager->BroadcastDeployableUnits(PS);

    // Mark whose placement turn it is so HUDs sync on all clients.
    if (ASkaldGameState *GSLocal = GetGameState<ASkaldGameState>()) {
      int32 NewIndex = GSLocal->PlayerArray.IndexOfByKey(PS);
      if (NewIndex == INDEX_NONE) {
        NewIndex = GSLocal->PlayerArray.IndexOfByPredicate(
            [PS](const APlayerState *PlayerState) {
              const ASkaldPlayerState *SkaldPlayerState =
                  Cast<ASkaldPlayerState>(PlayerState);
              return SkaldPlayerState &&
                     SkaldPlayerState->GetPlayerId() == PS->GetPlayerId();
            });
      }
      if (NewIndex != INDEX_NONE) {
        GSLocal->CurrentTurnIndex = NewIndex; // RepNotify → OnTurnIndexChanged
        GSLocal->OnTurnIndexChanged.Broadcast(
            NewIndex); // optional immediate local broadcast
      } else {
        UE_LOG(LogSkald, Warning,
               TEXT("AdvanceArmyPlacement: Unable to resolve GameState index for %s; HUD turn sync skipped."),
               *PS->GetResolvedPlayerName(TEXT("AdvanceArmyPlacement_MissingIndex")));
      }
    }

    // Announce whose placement turn it is.
    const FString PlayerName =
        PS->GetResolvedPlayerName(TEXT("AdvanceArmyPlacement_Announcement"));
    for (ASkaldPlayerController *Controller : Controllers) {
      const bool bIsActive = Controller == PC;
      Controller->ShowTurnAnnouncement(PlayerName, bIsActive);
      Controller->ClientClearStrategicInitiativeOverlay();
      if (USkaldMainHUDWidget *HUD = Controller->GetHUDWidget()) {
        HUD->UpdateTurnBanner(PS->GetPlayerId(), 1);
        HUD->SetAwaitingStrategicInitiative(false);
      }
    }

    if (PS->bIsAI) {
      if (ASkaldAIController *AIController = Cast<ASkaldAIController>(PC)) {
        if (CanAnimateArmyPlacement() &&
            AIController->BeginArmyPlacementSetupTurn()) {
          PendingArmyPlacementAIController = AIController;
          return;
        }

        AIController->PerformArmyPlacementTurn();
      } else {
        const int32 AutoPlaced = WorldMap->AutoPlaceUnitsForAI(PS);
        if (AutoPlaced > 0) {
          TurnManager->BroadcastDeployableUnits(PS);
        }
      }

      FinalizeAIArmyPlacementTurn(PS);
      return;
    }

    // Hand control to the active player (AI or human) to deploy units.
    if (PC) {
      PC->StartTurn();
    }
    return;
  }

  // All players have finished placing armies; start the main turn loop.
  ASkaldPlayerController *StartingController = ArmyPlacementLeader.Get();
  ArmyPlacementLeader.Reset();
  bTurnsStarted = true;
  TurnManager->StartTurns(StartingController);
}

int32 ASkaldGameMode::ResolveExpectedControllerCount() const {
  int32 ExpectedCount = 0;

  if (const ASkaldGameState *GS = GetGameState<ASkaldGameState>()) {
    TSet<const ASkaldPlayerController *> UniqueControllers;
    for (const APlayerState *PlayerStateBase : GS->PlayerArray) {
      const ASkaldPlayerState *PS =
          Cast<ASkaldPlayerState>(PlayerStateBase);
      const ASkaldPlayerController *ControllerOwner =
          PS ? Cast<ASkaldPlayerController>(PS->GetOwner()) : nullptr;
      if (ControllerOwner) {
        UniqueControllers.Add(ControllerOwner);
      }
    }
    ExpectedCount = UniqueControllers.Num();
  }

  const USkaldGameInstance *GI =
      const_cast<ASkaldGameMode *>(this)->GetGameInstance<USkaldGameInstance>();
  if (GI) {
    if (GI->ExpectedLobbyPlayerCount > 0) {
      ExpectedCount = FMath::Max(ExpectedCount, GI->ExpectedLobbyPlayerCount);
    }

    const FSkaldTravelState &TravelState = GI->GetTravelState();
    if (TravelState.bValid && TravelState.ExpectedControllers > 0) {
      ExpectedCount = FMath::Max(ExpectedCount, TravelState.ExpectedControllers);
    }
  }

  if (ExpectedCount <= 0 && TurnManager) {
    ExpectedCount = TurnManager->GetControllerCount();
  }

  return ExpectedCount;
}

ATurnManager *ASkaldGameMode::ResolveTurnManager() {
  if (IsValid(TurnManager)) {
    return TurnManager;
  }

  UWorld *World = GetWorld();
  if (!World) {
    return nullptr;
  }

  UClass *DesiredClass = TurnManagerClass ? *TurnManagerClass : ATurnManager::StaticClass();
  if (!DesiredClass) {
    DesiredClass = ATurnManager::StaticClass();
  }

  auto FindManager = [&](UClass *Class) -> ATurnManager * {
    if (!Class) {
      return nullptr;
    }
    return Cast<ATurnManager>(UGameplayStatics::GetActorOfClass(World, Class));
  };

  if (ATurnManager *Existing = FindManager(DesiredClass)) {
    TurnManager = Existing;
    return TurnManager;
  }

  if (DesiredClass != ATurnManager::StaticClass()) {
    if (ATurnManager *BaseExisting = FindManager(ATurnManager::StaticClass())) {
      TurnManager = BaseExisting;
      return TurnManager;
    }
  }

  FActorSpawnParameters SpawnParams;
  SpawnParams.SpawnCollisionHandlingOverride =
      ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
  TurnManager = World->SpawnActor<ATurnManager>(DesiredClass, SpawnParams);
  return TurnManager;
}

void ASkaldGameMode::HandleArmyPlacementAutoAdvance() {
  GetWorldTimerManager().ClearTimer(ArmyPlacementAutoAdvanceHandle);

  if (!TurnManager) {
    return;
  }

  if (TurnManager->GetCurrentPhase() != ETurnPhase::ArmyPlacement) {
    return;
  }

  TurnManager->EndCurrentPhase();
}

void ASkaldGameMode::HandleArmyPlacementFailsafe() {
  GetWorldTimerManager().ClearTimer(ArmyPlacementFailsafeHandle);

  if (!TurnManager) {
    return;
  }

  if (TurnManager->GetCurrentPhase() != ETurnPhase::ArmyPlacement) {
    return;
  }

  // If a human player is currently placing armies, allow them to finish rather
  // than force-advancing into the main turn sequence. The failsafe should only
  // trip when an AI controller stalls during placement.
  if (ASkaldGameState *GS = GetGameState<ASkaldGameState>()) {
    if (GS->PlayerArray.IsValidIndex(GS->CurrentTurnIndex)) {
      if (ASkaldPlayerState *ActivePS =
              Cast<ASkaldPlayerState>(GS->PlayerArray[GS->CurrentTurnIndex])) {
        if (!ActivePS->bIsAI) {
          return;
        }
      }
    }
  }

  if (!bArmyPlacementFailsafeTriggered) {
    UE_LOG(LogSkald, Warning,
           TEXT("Army placement failsafe triggered; forcing EndCurrentPhase"));
    bArmyPlacementFailsafeTriggered = true;
  }

  TurnManager->EndCurrentPhase();
}

void ASkaldGameMode::FinalizeAIArmyPlacementTurn(
    ASkaldPlayerState *PlayerState) {
  if (!PlayerState) {
    return;
  }

  bArmyPlacementFailsafeTriggered = false;

  bool bHasRemainingUnits = PlayerState->DeployableUnits > 0;
  bool bCanPlaceMore = false;
  if (bHasRemainingUnits && WorldMap) {
    for (ATerritory *Terr : WorldMap->Territories) {
      if (!Terr || Terr->OwningPlayer != PlayerState) {
        continue;
      }

      const int32 TerritoryId = Terr->GetTerritoryId();
      const int32 AlreadyPlaced =
          PlayerState->GetArmyPlacementDeploymentForTerritory(TerritoryId);
      if (AlreadyPlaced < Skald::ArmyPlacement::DeployPerTerritoryLimit) {
        bCanPlaceMore = true;
        break;
      }
    }
  }

  if (bHasRemainingUnits && bCanPlaceMore) {
    UE_LOG(LogSkald, Verbose,
           TEXT("AdvanceArmyPlacement: %s retains %d units after placement; retrying."),
           *PlayerState->GetResolvedPlayerName(TEXT("AdvanceArmyPlacement_AIRetry")),
           PlayerState->DeployableUnits);
    GetWorldTimerManager().SetTimer(ArmyPlacementAutoAdvanceHandle, this,
                                    &ASkaldGameMode::AdvanceArmyPlacement,
                                    RetryInitDelay, false);
    return;
  }

  bool bScheduledAutoAdvance = false;
  if (ArmyPlacementAutoAdvanceDelay > KINDA_SMALL_NUMBER) {
    if (UWorld *World = GetWorld()) {
      World->GetTimerManager().SetTimer(
          ArmyPlacementAutoAdvanceHandle, this,
          &ASkaldGameMode::HandleArmyPlacementAutoAdvance,
          ArmyPlacementAutoAdvanceDelay, false);
      bScheduledAutoAdvance = true;
    }
  }

  if (!bScheduledAutoAdvance && TurnManager) {
    TurnManager->EndCurrentPhase();
  }

  GetWorldTimerManager().SetTimer(ArmyPlacementFailsafeHandle, this,
                                  &ASkaldGameMode::HandleArmyPlacementFailsafe,
                                  2.0f, false);
}

void ASkaldGameMode::HandleAIArmyPlacementSetupComplete(
    ASkaldAIController *AIController) {
  if (!AIController) {
    return;
  }

  if (!PendingArmyPlacementAIController.IsValid() ||
      PendingArmyPlacementAIController.Get() != AIController) {
    return;
  }

  PendingArmyPlacementAIController.Reset();

  ASkaldPlayerState *PS = AIController->GetPlayerState<ASkaldPlayerState>();
  if (!PS) {
    AdvanceArmyPlacement();
    return;
  }

  FinalizeAIArmyPlacementTurn(PS);
}

bool ASkaldGameMode::CanAnimateArmyPlacement() const {
#if defined(WITH_AUTOMATION_TESTS) && WITH_AUTOMATION_TESTS
  if (GIsAutomationTesting) {
    return false;
  }
#endif
  return true;
}

bool ASkaldGameMode::InitializeWorld() {
  if (!WorldMap) {
    // Look for an existing world map actor placed in the level.
    WorldMap = Cast<AWorldMap>(UGameplayStatics::GetActorOfClass(
        GetWorld(), AWorldMap::StaticClass()));
  }
  if (!WorldMap) {
    UE_LOG(LogSkald, Error,
           TEXT("InitializeWorld failed: WorldMap missing in %s. Place a "
                "WorldMap actor in the level."),
           *GetName());
    if (GEngine) {
      GEngine->AddOnScreenDebugMessage(
          -1, 5.f, FColor::Red,
          FString::Printf(TEXT("InitializeWorld: WorldMap missing in %s. Place "
                               "a WorldMap actor in the level."),
                          *GetName()));
    }
    return false;
  }

  ASkaldGameState *GS = GetGameState<ASkaldGameState>();
  if (!GS) {
    UE_LOG(LogSkald, Error, TEXT("InitializeWorld failed: GameState missing"));
    if (GEngine) {
      GEngine->AddOnScreenDebugMessage(
          -1, 5.f, FColor::Red, TEXT("InitializeWorld: GameState missing"));
    }
    return false;
  }
  if (GS->PlayerArray.Num() == 0) {
    UE_LOG(LogSkald, Error, TEXT("InitializeWorld failed: no players"));
    if (GEngine) {
      GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red,
                                       TEXT("InitializeWorld: no players"));
    }
    return false;
  }

  int32 HumanPlayerCount = 0;
  for (APlayerState *PlayerStateBase : GS->PlayerArray) {
    if (ASkaldPlayerState *PlayerState =
            Cast<ASkaldPlayerState>(PlayerStateBase)) {
      if (!PlayerState->bIsAI) {
        ++HumanPlayerCount;
      }
    }
  }

  const int32 TotalPlayerCount = GS->PlayerArray.Num();
  if (HumanPlayerCount < MinPlayerCount) {
    UE_LOG(
        LogSkald, Warning,
        TEXT("InitializeWorld aborted: need at least %d human players but found %d (total players=%d)"),
        MinPlayerCount, HumanPlayerCount, TotalPlayerCount);
    if (GEngine) {
      GEngine->AddOnScreenDebugMessage(
          -1, 5.f, FColor::Yellow,
          FString::Printf(TEXT("InitializeWorld: need %d human players but found %d (total=%d)"),
                          MinPlayerCount, HumanPlayerCount, TotalPlayerCount));
    }
    return false;
  }

  if (WorldMap->Territories.Num() == 0) {
    if (!WorldMap->TerritoryClass) {
      UE_LOG(LogSkald, Error,
             TEXT("InitializeWorld failed: WorldMap %s missing TerritoryClass"),
             *WorldMap->GetName());
      if (GEngine) {
        GEngine->AddOnScreenDebugMessage(
            -1, 5.f, FColor::Red,
            FString::Printf(TEXT("InitializeWorld: %s missing TerritoryClass"),
                            *WorldMap->GetName()));
      }
      return false;
    }
    if (!WorldMap->TerritoryTable) {
      UE_LOG(LogSkald, Error,
             TEXT("InitializeWorld failed: WorldMap %s missing TerritoryTable"),
             *WorldMap->GetName());
      if (GEngine) {
        GEngine->AddOnScreenDebugMessage(
            -1, 5.f, FColor::Red,
            FString::Printf(TEXT("InitializeWorld: %s missing TerritoryTable"),
                            *WorldMap->GetName()));
      }
      return false;
    }
    if (!WorldMap->GenerateTerritoriesFromTable()) {
      UE_LOG(LogSkald, Error,
             TEXT("InitializeWorld failed: WorldMap %s could not generate "
                  "territories"),
             *WorldMap->GetName());
      if (GEngine) {
        GEngine->AddOnScreenDebugMessage(
            -1, 5.f, FColor::Red,
            FString::Printf(
                TEXT("InitializeWorld: %s could not generate territories"),
                *WorldMap->GetName()));
      }
      return false;
    }
  }
  if (WorldMap->Territories.Num() == 0) {
    UE_LOG(LogSkald, Error,
           TEXT("InitializeWorld failed: WorldMap %s has no territories"),
           *WorldMap->GetName());
    if (GEngine) {
      GEngine->AddOnScreenDebugMessage(
          -1, 5.f, FColor::Red,
          FString::Printf(TEXT("InitializeWorld: %s has no territories"),
                          *WorldMap->GetName()));
    }
    return false;
  }

  // The world map now exists with generated territories; ensure all player
  // controllers bind to its selection event.
  for (FConstPlayerControllerIterator It =
           GetWorld()->GetPlayerControllerIterator();
       It; ++It) {
    if (ASkaldPlayerController *PC = Cast<ASkaldPlayerController>(*It)) {
      PC->TryBindWorldMap();
    }
  }

  // Shuffle territories before assignment
  Algo::RandomShuffle(WorldMap->Territories);

  // Roll initiative and sort players accordingly. Preserve existing rolls so
  // the initiative winner from the initial match start remains the leader even
  // if InitializeWorld is triggered again (for example due to late HUD
  // initialisation callbacks).
  for (APlayerState *PSBase : GS->PlayerArray) {
    if (ASkaldPlayerState *PS = Cast<ASkaldPlayerState>(PSBase)) {
      if (PS->InitiativeRoll > 0) {
        continue;
      }

      if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
        PS->InitiativeRoll = GI->CombatRandomStream.RandRange(1, 6);
      } else {
        static FRandomStream FallbackStream;
        FallbackStream.Initialize(FMath::Rand());
        PS->InitiativeRoll = FallbackStream.RandRange(1, 6);
      }
    }
  }

  GS->PlayerArray.Sort([](const APlayerState &A, const APlayerState &B) {
    const ASkaldPlayerState *PSA = Cast<const ASkaldPlayerState>(&A);
    const ASkaldPlayerState *PSB = Cast<const ASkaldPlayerState>(&B);
    const int32 RollA = PSA ? PSA->InitiativeRoll : 0;
    const int32 RollB = PSB ? PSB->InitiativeRoll : 0;
    return RollA > RollB;
  });

  // Assign starting resources to each player
  for (APlayerState *PSBase : GS->PlayerArray) {
    if (ASkaldPlayerState *PS = Cast<ASkaldPlayerState>(PSBase)) {
      PS->Resources = StartingResources;
      FS_PlayerData *PlayerData =
          PlayerDataArray.FindByPredicate([PS](const FS_PlayerData &Data) {
            return Data.PlayerID == PS->GetPlayerId();
          });
      if (PlayerData) {
        PlayerData->Resources = StartingResources;
      }
      if (TurnManager) {
        TurnManager->BroadcastResources(PS);
      }
    }
  }
  TArray<ASkaldPlayerState *> OrderedPlayerStates;
  OrderedPlayerStates.Reserve(GS->PlayerArray.Num());
  for (APlayerState *PSBase : GS->PlayerArray) {
    if (ASkaldPlayerState *PS = Cast<ASkaldPlayerState>(PSBase)) {
      OrderedPlayerStates.Add(PS);
    }
  }
  const int32 PlayerCount = OrderedPlayerStates.Num();

  // Assign territories in two phases: first guarantee each player receives
  // capital-eligible territories from the data table, then distribute the
  // remaining territories round-robin.
  TArray<ATerritory *> CapitalTerritories;
  TArray<ATerritory *> NonCapitalTerritories;
  for (ATerritory *Territory : WorldMap->Territories) {
    if (!Territory) {
      continue;
    }
    if (WorldMap->IsCapitalCandidate(Territory->TerritoryID)) {
      CapitalTerritories.Add(Territory);
    } else {
      NonCapitalTerritories.Add(Territory);
    }
  }

  Algo::RandomShuffle(CapitalTerritories);
  Algo::RandomShuffle(NonCapitalTerritories);

  const int32 CapitalsPerPlayer = 2;
  const int32 RequiredCapitalSlots = PlayerCount * CapitalsPerPlayer;
  if (PlayerCount > 0 && CapitalTerritories.Num() < RequiredCapitalSlots) {
    UE_LOG(LogSkald, Warning,
           TEXT("InitializeWorld: Only %d capital candidate territories for %d "
                "players; some capitals will fall back to non-candidate "
                "territories."),
           CapitalTerritories.Num(), PlayerCount);
  }

  auto AssignTerritory =
      [](ATerritory *Territory, ASkaldPlayerState *InOwner) {
    if (!Territory || !InOwner) {
      return;
    }
    Territory->OwningPlayer = InOwner;
    Territory->bIsCapital = false;
    Territory->ArmyUnits = 1;
    Territory->RefreshAppearance();
  };

  int32 NextPlayerIndex = 0;
  TArray<int32> CandidateAssignments;
  CandidateAssignments.Init(0, PlayerCount);

  int32 CandidateIndex = 0;
  for (int32 CapitalRound = 0; CapitalRound < CapitalsPerPlayer; ++CapitalRound) {
    for (int32 PlayerIndex = 0; PlayerIndex < PlayerCount; ++PlayerIndex) {
      if (CandidateIndex >= CapitalTerritories.Num()) {
        break;
      }
      ASkaldPlayerState *PlayerOwner = OrderedPlayerStates[PlayerIndex];
      AssignTerritory(CapitalTerritories[CandidateIndex++], PlayerOwner);
      CandidateAssignments[PlayerIndex]++;
      if (PlayerCount > 0) {
        NextPlayerIndex = (PlayerIndex + 1) % PlayerCount;
      }
    }
    if (CandidateIndex >= CapitalTerritories.Num()) {
      break;
    }
  }

  for (int32 PlayerIndex = 0; PlayerIndex < PlayerCount; ++PlayerIndex) {
    if (CandidateAssignments[PlayerIndex] >= CapitalsPerPlayer) {
      continue;
    }
    ASkaldPlayerState *PlayerOwner = OrderedPlayerStates[PlayerIndex];
    const int32 AssignedCount = CandidateAssignments[PlayerIndex];
    UE_LOG(LogSkald, Warning,
           TEXT("InitializeWorld: Player %s received only %d capital candidate "
                "territory/territories during initial assignment; falling "
                "back to other owned territories to reach %d total capitals."),
           *PlayerOwner->GetPlayerName(), AssignedCount, CapitalsPerPlayer);
  }

  TArray<ATerritory *> RemainingTerritories;
  for (; CandidateIndex < CapitalTerritories.Num(); ++CandidateIndex) {
    RemainingTerritories.Add(CapitalTerritories[CandidateIndex]);
  }
  RemainingTerritories.Append(NonCapitalTerritories);
  Algo::RandomShuffle(RemainingTerritories);

  int32 PlayerIndex = PlayerCount > 0 ? NextPlayerIndex : 0;
  for (ATerritory *Territory : RemainingTerritories) {
    if (PlayerCount == 0) {
      break;
    }
    AssignTerritory(Territory, OrderedPlayerStates[PlayerIndex]);
    PlayerIndex = (PlayerIndex + 1) % PlayerCount;
  }

  // Choose capitals for each player
  for (APlayerState *PSBase : GS->PlayerArray) {
    ASkaldPlayerState *PS = Cast<ASkaldPlayerState>(PSBase);
    if (!PS) {
      continue;
    }

    TArray<ATerritory *> OwnedTerritories;
    for (ATerritory *Territory : WorldMap->Territories) {
      if (Territory && Territory->OwningPlayer == PS) {
        OwnedTerritories.Add(Territory);
      }
    }
    Algo::RandomShuffle(OwnedTerritories);

    TArray<ATerritory *> CapitalCandidates;
    for (ATerritory *Territory : OwnedTerritories) {
      if (Territory && WorldMap->IsCapitalCandidate(Territory->TerritoryID)) {
        CapitalCandidates.Add(Territory);
      }
    }

    if (CapitalCandidates.Num() > 0) {
      Algo::RandomShuffle(CapitalCandidates);
      if (CapitalCandidates.Num() < 2) {
        UE_LOG(
            LogSkald, Warning,
            TEXT("InitializeWorld: Player %s has only %d capital candidate "
                 "territory/territories; filling remaining capital slots "
                 "with other owned territories."),
            *PS->GetPlayerName(), CapitalCandidates.Num());
      }
    } else {
      UE_LOG(LogSkald, Warning,
             TEXT("InitializeWorld: Player %s owns no territories marked as "
                  "capital candidates; falling back to random owned "
                  "territories."),
             *PS->GetPlayerName());
      CapitalCandidates = OwnedTerritories;
    }

    FS_PlayerData *PlayerData =
        PlayerDataArray.FindByPredicate([PS](const FS_PlayerData &Data) {
          return Data.PlayerID == PS->GetPlayerId();
        });
    if (PlayerData) {
      PlayerData->CapitalTerritoryIDs.Reset();
    }

    int32 CapitalsAssigned = 0;
    for (ATerritory *Territory : CapitalCandidates) {
      if (CapitalsAssigned >= 2) {
        break;
      }
      Territory->bIsCapital = true;
      Territory->RefreshAppearance();
      if (PlayerData) {
        PlayerData->CapitalTerritoryIDs.Add(Territory->TerritoryID);
      }
      ++CapitalsAssigned;
    }

    if (CapitalsAssigned < 2) {
      for (ATerritory *Territory : OwnedTerritories) {
        if (CapitalsAssigned >= 2) {
          break;
        }
        if (!Territory || Territory->bIsCapital) {
          continue;
        }

        Territory->bIsCapital = true;
        Territory->RefreshAppearance();
        if (PlayerData) {
          PlayerData->CapitalTerritoryIDs.Add(Territory->TerritoryID);
        }
        ++CapitalsAssigned;
      }
    }
  }

  // Determine highest initiative roll
  ASkaldPlayerState *HighestPS = nullptr;
  int32 HighestRoll = 0;
  for (APlayerState *PSBase : GS->PlayerArray) {
    ASkaldPlayerState *PS = Cast<ASkaldPlayerState>(PSBase);
    if (!PS) {
      continue;
    }

    if (PS->InitiativeRoll > HighestRoll) {
      HighestRoll = PS->InitiativeRoll;
      HighestPS = PS;
    }
  }

  if (HighestPS) {
    for (FConstPlayerControllerIterator It =
             GetWorld()->GetPlayerControllerIterator();
         It; ++It) {
      if (ASkaldPlayerController *PC = Cast<ASkaldPlayerController>(*It)) {
        ASkaldPlayerState *PS = PC->GetPlayerState<ASkaldPlayerState>();
        const bool bIsAI = PS && PS->bIsAI;
        if (!bIsAI && PS) {
          const int32 RollValue = PS->InitiativeRoll;
          const bool bWon = (PS == HighestPS);
          const int32 *RoundPtr = StrategicInitiativeRoundByPlayer.Find(PS);
          const int32 PlayerRound = RoundPtr && *RoundPtr > 0 ? *RoundPtr : 1;
          NotifyStrategicInitiativeRoll(PC, PlayerRound, RollValue, bWon);
        }
      }
    }

    const FString Message = FString::Printf(
        TEXT("%s wins initiative with a roll of %d"),
        *HighestPS->GetResolvedPlayerName(TEXT("InitializeWorld_Initiative")),
        HighestRoll);
    for (FConstPlayerControllerIterator It =
             GetWorld()->GetPlayerControllerIterator();
         It; ++It) {
      if (ASkaldPlayerController *PC = Cast<ASkaldPlayerController>(*It)) {
        ASkaldPlayerState *PS = PC->GetPlayerState<ASkaldPlayerState>();
        const bool bIsAI = PS && PS->bIsAI;
        if (USkaldMainHUDWidget *HUD = PC->GetHUDWidget()) {
          HUD->UpdateInitiativeText(Message);
        } else if (!bIsAI && PC->IsLocalController()) {
          UE_LOG(LogSkald, Warning,
                 TEXT("InitializeWorld: Controller %s missing HUD widget"),
                 *PC->GetName());
          if (GEngine) {
            GEngine->AddOnScreenDebugMessage(
                -1, 5.f, FColor::Yellow,
                FString::Printf(TEXT("No HUD for %s"), *PC->GetName()));
          }
        }
      }
    }
  }
  return true;
}

void ASkaldGameMode::FillSaveGame(USkaldSaveGame *SaveGameObject) const {
  if (!SaveGameObject) {
    return;
  }

  SaveGameObject->Controllers.Reset();
  SaveGameObject->Territories.Reset();
  SaveGameObject->Players.Reset();
  SaveGameObject->WorldState = FSkaldWorldStateSaveData();
  SaveGameObject->GameFlow = FSkaldGameFlowSaveData();

  const UWorld *World = GetWorld();
  if (World) {
    FString MapPath;
    if (const ULevel *PersistentLevel = World->PersistentLevel) {
      if (const UPackage *Package = PersistentLevel->GetOutermost()) {
        MapPath = Package->GetName();
      }
    }

    if (MapPath.IsEmpty()) {
      if (const UPackage *WorldPackage = World->GetOutermost()) {
        MapPath = WorldPackage->GetName();
      }
    }

    if (MapPath.IsEmpty()) {
      MapPath = World->GetMapName();
    }

    if (!MapPath.IsEmpty()) {
      auto StripPIEPrefix = [](const FString &InPath) {
        FString Result = InPath;
        const FString PIEPrefix = TEXT("UEDPIE_");
        int32 PrefixIndex = Result.Find(PIEPrefix);
        while (PrefixIndex != INDEX_NONE) {
          int32 RemovalStart = PrefixIndex;
          int32 RemovalEnd = PrefixIndex + PIEPrefix.Len();
          while (RemovalEnd < Result.Len() && FChar::IsDigit(Result[RemovalEnd])) {
            ++RemovalEnd;
          }
          if (RemovalEnd < Result.Len() && Result[RemovalEnd] == TEXT('_')) {
            ++RemovalEnd;
          }
          Result.RemoveAt(RemovalStart, RemovalEnd - RemovalStart);
          PrefixIndex = Result.Find(PIEPrefix);
        }
        return Result;
      };

      SaveGameObject->MapAssetPath = StripPIEPrefix(MapPath);
    } else {
      SaveGameObject->MapAssetPath.Empty();
    }
  }

  const USkaldGameInstance *GameInstance = GetGameInstance<USkaldGameInstance>();
  if (GameInstance) {
    SaveGameObject->RandomSeed = GameInstance->CombatRandomStream.GetCurrentSeed();
  }

  // Copy persistent player data from the runtime snapshots.
  for (const FS_PlayerData &Data : PlayerDataArray) {
    FPlayerSaveStruct PlayerSave;
    PlayerSave.PlayerID = Data.PlayerID;
    PlayerSave.PlayerName = Data.PlayerName;
    PlayerSave.Faction = Data.Faction;
    PlayerSave.Resources = Data.Resources;
    PlayerSave.CapitalTerritoryIDs = Data.CapitalTerritoryIDs;
    PlayerSave.IsEliminated = Data.IsEliminated;
    SaveGameObject->Players.Add(PlayerSave);
  }

  TMap<int32, TArray<int32>> OwnedTerritoriesByPlayer;
  if (WorldMap) {
    if (WorldMap->SelectedTerritory) {
      SaveGameObject->WorldState.SelectedTerritoryId =
          WorldMap->SelectedTerritory->TerritoryID;
      SaveGameObject->WorldState.SelectedByPlayerId = WorldMap->SelectedByPlayerId;
    }

    SaveGameObject->Territories.Reserve(WorldMap->Territories.Num());
    for (ATerritory *Territory : WorldMap->Territories) {
      if (!IsValid(Territory)) {
        continue;
      }

      FS_Territory &TerrData = SaveGameObject->Territories.Emplace_GetRef();
      TerrData.TerritoryID = Territory->TerritoryID;
      TerrData.TerritoryName = Territory->TerritoryName;
      TerrData.OwnerPlayerID =
          Territory->OwningPlayer ? Territory->OwningPlayer->GetPlayerId() : 0;
      TerrData.IsCapital = Territory->bIsCapital;
      TerrData.CapitalOwner = TerrData.OwnerPlayerID;
      TerrData.ArmyUnits = Territory->ArmyUnits;
      TerrData.ContinentID = Territory->ContinentID;
      TerrData.Location = Territory->GetActorLocation();
      TerrData.HasTreasure = Territory->bHasTreasure;
      TerrData.TreasureAttachedUnitID =
          ReadIntProperty(Territory, TEXT("TreasureAttachedUnitID"));
      TerrData.FortificationLevel =
          ReadIntProperty(Territory, TEXT("FortificationLevel"));
      TerrData.Moat = ReadBoolProperty(Territory, TEXT("Moat"));
      TerrData.WallHealth = ReadIntProperty(Territory, TEXT("WallHealth"));
      TerrData.BuiltSiegeID = Territory->BuiltSiegeID;
      TerrData.ConqueredTurn =
          ReadIntProperty(Territory, TEXT("ConqueredTurn"));
      TerrData.IsNeutralSpawn =
          ReadBoolProperty(Territory, TEXT("IsNeutralSpawn"));

      TerrData.AdjacentIDs.Reset();
      for (ATerritory *Adj : Territory->AdjacentTerritories) {
        if (IsValid(Adj)) {
          TerrData.AdjacentIDs.AddUnique(Adj->TerritoryID);
        }
      }

      if (TerrData.OwnerPlayerID > 0) {
        OwnedTerritoriesByPlayer.FindOrAdd(TerrData.OwnerPlayerID)
            .Add(TerrData.TerritoryID);
      }
    }
  }

  // Capture world audio state so ambience resumes after loading.
  if (World) {
    SaveGameObject->WorldState.ActiveAudio.Reset();
    for (TObjectIterator<UAudioComponent> It; It; ++It) {
      UAudioComponent *Audio = *It;
      if (!Audio || Audio->GetWorld() != World) {
        continue;
      }
      if (!Audio->Sound) {
        continue;
      }

      FSkaldAudioComponentSaveData &AudioData =
          SaveGameObject->WorldState.ActiveAudio.Emplace_GetRef();
      AudioData.Sound = Audio->Sound;
      AudioData.bWasPlaying = Audio->IsPlaying();
      AudioData.PlaybackTime = 0.f;
    }
  }

  const ATurnManager *Manager = GetTurnManager();
  const ASkaldGameState *SkaldGameState = GetGameState<ASkaldGameState>();
  const TArray<ASkaldPlayerController *> Controllers =
      Manager ? Manager->GetControllers() : TArray<ASkaldPlayerController *>();

  SaveGameObject->GameFlow.CurrentPhase =
      Manager ? Manager->GetCurrentPhase() : ETurnPhase::Reinforcement;
  SaveGameObject->GameFlow.ActiveTurnIndex =
      Manager ? Manager->GetCurrentControllerIndex() : 0;
  SaveGameObject->GameFlow.bTurnsStarted =
      Manager ? Manager->HasTurnsStarted() : false;
  if (Manager) {
    SaveGameObject->GameFlow.PendingBattle = Manager->GetPendingBattlePayload();
    SaveGameObject->GameFlow.PendingBattlePreparation =
        Manager->GetPendingBattlePreparation();
    SaveGameObject->GameFlow.PendingReadyState =
        Manager->GetPendingBattleReadyState();

    const TMap<int32, int32> MovementSnapshot =
        Manager->GetMovementActionsSnapshot();
    for (const TPair<int32, int32> &Entry : MovementSnapshot) {
      if (Entry.Key <= 0) {
        continue;
      }

      FSkaldMovementActionSaveData &ActionData =
          SaveGameObject->GameFlow.MovementActions.Emplace_GetRef();
      ActionData.PlayerId = Entry.Key;
      ActionData.ActionsTaken = Entry.Value;
    }
  }

  int32 ControllerCount = Controllers.Num();
  if (ControllerCount == 0 && SkaldGameState) {
    ControllerCount = SkaldGameState->PlayerArray.Num();
  }

  int32 ActivePlayerId = INDEX_NONE;

  for (int32 Index = 0; Index < Controllers.Num(); ++Index) {
    ASkaldPlayerController *Controller = Controllers[Index];
    if (!Controller) {
      continue;
    }

    FSkaldControllerSaveData ControllerSave;
    ControllerSave.bIsAI = Controller->IsA<ASkaldAIController>();
    ControllerSave.TurnOrderIndex = Index;

    if (ASkaldPlayerState *PS =
            Controller->GetPlayerState<ASkaldPlayerState>()) {
      ControllerSave.PlayerId = PS->GetPlayerId();
      ControllerSave.PlayerName = PS->PlayerDisplayName;
      ControllerSave.Faction = PS->Faction;
      if (GameInstance) {
        ControllerSave.FactionEmblem =
            GameInstance->GetFactionEmblem(PS->Faction);
      }
      const FLinearColor PlayerColor =
          GameInstance ? GameInstance->GetFactionColor(PS->Faction)
                       : USkaldGameInstance::GetDefaultFactionColor(PS->Faction);
      ControllerSave.PlayerColor = PlayerColor;

      if (ControllerSave.PlayerId > 0) {
        if (const TArray<int32> *Owned =
                OwnedTerritoriesByPlayer.Find(ControllerSave.PlayerId)) {
          ControllerSave.OwnedTerritoryIds = *Owned;
        }
      }

      if (Index == SaveGameObject->GameFlow.ActiveTurnIndex) {
        ActivePlayerId = PS->GetPlayerId();
      }
    }

    FSkaldCameraSaveData CameraSave;
    if (APawn *Pawn = Controller->GetPawn()) {
      CameraSave.Location = Pawn->GetActorLocation();
      CameraSave.Rotation = Controller->GetControlRotation();

      if (USpringArmComponent *SpringArm =
              Pawn->FindComponentByClass<USpringArmComponent>()) {
        CameraSave.Zoom = SpringArm->TargetArmLength;
      } else if (UCameraComponent *Camera =
                     Pawn->FindComponentByClass<UCameraComponent>()) {
        CameraSave.Zoom = Camera->FieldOfView;
      }

      if (const ASkald_PlayerCharacter *CharacterPawn =
              Cast<ASkald_PlayerCharacter>(Pawn)) {
        CameraSave.bBattleCameraActive =
            CharacterPawn->IsBattleCameraActive();
      }
    }

    if (WorldMap && ControllerSave.PlayerId > 0 &&
        WorldMap->SelectedTerritory &&
        WorldMap->SelectedByPlayerId == ControllerSave.PlayerId) {
      CameraSave.bHasLockedTerritory = true;
      CameraSave.LockedTerritoryId =
          WorldMap->SelectedTerritory->TerritoryID;
    }

    ControllerSave.Camera = CameraSave;
    SaveGameObject->Controllers.Add(ControllerSave);

    FSkaldTurnParticipantSaveData &TurnEntry =
        SaveGameObject->GameFlow.TurnOrder.Emplace_GetRef();
    TurnEntry.PlayerId = ControllerSave.PlayerId;
    TurnEntry.bIsAI = ControllerSave.bIsAI;
    if (ASkaldPlayerState *PS =
            Controller->GetPlayerState<ASkaldPlayerState>()) {
      TurnEntry.Initiative = PS->InitiativeRoll;
    }
  }

  SaveGameObject->Sieges = SiegePool;

  if (SkaldGameState) {
    SaveGameObject->CurrentPlayerIndex = SkaldGameState->CurrentTurnIndex;
    int32 EffectiveControllerCount = FMath::Max(ControllerCount, 1);
    int32 DerivedTurnNumber =
        (SkaldGameState->CurrentTurnIndex / EffectiveControllerCount) + 1;
    SaveGameObject->TurnNumber = DerivedTurnNumber;
    SaveGameObject->GameFlow.TurnNumber = DerivedTurnNumber;
  }

  SaveGameObject->SavedTurnPhase = SaveGameObject->GameFlow.CurrentPhase;
  SaveGameObject->SavedTurnIndex = SaveGameObject->GameFlow.ActiveTurnIndex;
  SaveGameObject->bTurnsStarted = SaveGameObject->GameFlow.bTurnsStarted;

  if (SaveGameObject->SavedTurnPlayerID <= 0) {
    SaveGameObject->SavedTurnPlayerID = ActivePlayerId;
  }

  // Legacy overview camera information for backwards compatibility.
  if (APlayerController *PC = UGameplayStatics::GetPlayerController(this, 0)) {
    if (APawn *Pawn = PC->GetPawn()) {
      const FVector Loc = Pawn->GetActorLocation();
      SaveGameObject->SavedViewOffset = FVector2D(Loc.X, Loc.Y);

      if (UCameraComponent *Camera =
              Pawn->FindComponentByClass<UCameraComponent>()) {
        SaveGameObject->SavedZoomAmount = Camera->FieldOfView;
      }
    }
  }
}

void ASkaldGameMode::CheckVictoryConditions() {
  if (!WorldMap) {
    return;
  }

  ASkaldGameState *GS = GetGameState<ASkaldGameState>();
  if (!GS) {
    return;
  }

  int32 RemainingPlayers = 0;
  ASkaldPlayerState *WinningPlayer = nullptr;

  for (APlayerState *PSBase : GS->PlayerArray) {
    ASkaldPlayerState *PS = Cast<ASkaldPlayerState>(PSBase);
    if (!PS) {
      continue;
    }

    bool bHasTerritory = false;
    for (ATerritory *Territory : WorldMap->Territories) {
      if (Territory && Territory->OwningPlayer == PS) {
        bHasTerritory = true;
        break;
      }
    }

    FS_PlayerData *Data =
        PlayerDataArray.FindByPredicate([PS](const FS_PlayerData &D) {
          return D.PlayerID == PS->GetPlayerId();
        });
    if (Data) {
      Data->IsEliminated = !bHasTerritory;
    }

    if (bHasTerritory) {
      ++RemainingPlayers;
      WinningPlayer = PS;
    }
  }

  if (RemainingPlayers == 1 && WinningPlayer) {
    OnGameOver.Broadcast(WinningPlayer);
    if (UWorld *WorldToTravel = GetWorld()) {
      if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
        GI->SetTravelPending(true);
      }
      WorldToTravel->ServerTravel(TEXT("EndScreen"));
    }
  }
}
