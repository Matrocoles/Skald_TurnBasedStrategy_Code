#include "Skald_GameMode.h"
#include "Algo/RandomShuffle.h"
#include "Camera/CameraComponent.h"
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
#include "Skald_AIController.h"
#include "Skald_BattleGameMode.h"
#include "Skald_GameInstance.h"
#include "Skald_GameState.h"
#include "Skald_PropertyAccess.h"
#include "Skald_PlayerCharacter.h"
#include "Skald_PlayerController.h"
#include "Skald_PlayerState.h"
#include "Skald_TurnManager.h"
#include "Territory.h"
#include "TimerManager.h"
#include "UI/SkaldMainHUDWidget.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "WorldMap.h"

namespace {
constexpr float StartGameTimeout = 10.f;
constexpr int32 StartingResources = 100;
constexpr float RetryInitDelay = 0.01f;
constexpr float ArmyPlacementAutoAdvanceDelay = 0.15f;
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

void ASkaldGameMode::InitGame(const FString &Map, const FString &Options,
                              FString &Error) {
  Super::InitGame(Map, Options, Error);

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

  if (UWorld *World = GetWorld()) {
    FTimerManager &TimerManager = World->GetTimerManager();
    TimerManager.ClearTimer(RetryInitTimerHandle);
    TimerManager.ClearTimer(WorldMapRetryHandle);
    TimerManager.ClearTimer(StartGameTimerHandle);
    TimerManager.ClearTimer(ArmyPlacementAutoAdvanceHandle);
    TimerManager.ClearTimer(ArmyPlacementFailsafeHandle);
  } else {
    RetryInitTimerHandle.Invalidate();
    WorldMapRetryHandle.Invalidate();
    StartGameTimerHandle.Invalidate();
    ArmyPlacementAutoAdvanceHandle.Invalidate();
    ArmyPlacementFailsafeHandle.Invalidate();
  }

  if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
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

  if (GS) {
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
  }

  if (!PS) {
    // Player state may not yet be replicated in multiplayer; queue a retry
    // next tick while the controller remains valid. In singleplayer, assume
    // the state will be ready without queuing retries.
    if (bIsMultiplayer) {
      PendingControllers.AddUnique(PC);
      FTimerDelegate RetryDelegate = FTimerDelegate::CreateUObject(
          this, &ASkaldGameMode::RegisterPlayer, PC);
      GetWorldTimerManager().SetTimerForNextTick(RetryDelegate);
    }
    return;
  }

  // Player state is valid; perform normal registration.
  PendingControllers.Remove(PC);

  PS->bIsAI = PC->IsA<ASkaldAIController>();

  if (GS) {
    if (!GS->PlayerArray.Contains(PS)) {
      GS->AddPlayerState(PS);
    }

    if (!TurnManager && !bIsMultiplayer) {
      TurnManager = ResolveTurnManager();
    }

    if (TurnManager) {
      if (!TurnManager->GetControllers().Contains(PC)) {
        TurnManager->RegisterController(PC);
        UE_LOG(LogSkald, Log, TEXT("RegisterPlayer: ControllerCount=%d"),
               TurnManager->GetControllerCount());
      }
    } else {
      // Defer final registration until the turn manager is available. Only
      // queue retries when running in multiplayer.
      if (bIsMultiplayer) {
        PendingControllers.AddUnique(PC);
      }
      return;
    }

    if (PlayerDataArray.Num() < GS->PlayerArray.Num()) {
      PlayerDataArray.SetNum(GS->PlayerArray.Num());
    }

    if (GI) {
      if (PS->PlayerDisplayName.IsEmpty()) {
        PS->PlayerDisplayName = GI->DisplayName;
        PS->SetPlayerName(PS->PlayerDisplayName);
      }
      if (PS->Faction == ESkaldFaction::None) {
        PS->Faction = GI->Faction;
      }
    }

    const int32 Index = GS->PlayerArray.IndexOfByKey(PS);
    if (PlayerDataArray.IsValidIndex(Index)) {
      PlayerDataArray[Index].PlayerID = PS->GetPlayerId();
      PlayerDataArray[Index].PlayerName =
          PS->GetResolvedPlayerName(TEXT("RegisterPlayer"));
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
  if (!GS || !GI || GI->bIsMultiplayer || !AIControllerClass) {
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
    AIState->PlayerDisplayName =
        FString::Printf(TEXT("AI_%d"), GS->PlayerArray.Num());
    AIState->SetPlayerName(AIState->PlayerDisplayName);

    ESkaldFaction AssignedFaction = ESkaldFaction::None;
    if (ReservedFactions.Num() > 0) {
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
  const FSkaldTravelState *TravelState =
      GI ? &GI->GetTravelState() : nullptr;

  TMap<FString, int32> DesiredIdByName;
  TArray<int32> DesiredHumanIds;
  TArray<int32> DesiredAIIds;

  if (TravelState && TravelState->bValid &&
      TravelState->PlayerSnapshots.Num() > 0) {
    for (const FS_PlayerData &Snapshot : TravelState->PlayerSnapshots) {
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
  bool bWantsResume = GI && GI->bResumeTurns;
  if (GI && !bWantsResume && !bHasPendingTravelSnapshot &&
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
        GetWorldTimerManager().SetTimer(RetryInitTimerHandle, RetryInit, 0.f,
                                        false);
        return;
      }
    }
  }

  const bool bNeedsSnapshotRestore =
      !bWorldInitialized && (bWantsResume || bHasPendingTravelSnapshot);

  if (bNeedsSnapshotRestore) {
    const bool bRestored = GI && GI->RestoreWorldFromSnapshot(GetWorld());
    if (!bRestored) {
      if (GI && (GI->bResumeTurns || bHasPendingTravelSnapshot)) {
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
  TSet<ASkaldPlayerController *> UniqueControllers;
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
    GetWorldTimerManager().SetTimer(RetryInitTimerHandle, RetryInit, 0.f,
                                    false);
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
    if (!bAllLockedIn || !bAllHaveControllers) {
      break;
    }
  }

  const int32 CurrentPlayerCount = UniqueControllers.Num();
  const bool bReadyToStart =
      bAllLockedIn && bAllHaveControllers &&
      CurrentPlayerCount >= MinPlayerCount && TurnManager &&
      TurnManager->GetControllerCount() >= CurrentPlayerCount;

  UE_LOG(
      LogSkald, Log,
      TEXT("TryInitializeWorldAndStart: bAllLockedIn=%s bAllHaveControllers=%s "
           "CurrentPlayerCount=%d ControllerCount=%d bReadyToStart=%s"),
      bAllLockedIn ? TEXT("true") : TEXT("false"),
      bAllHaveControllers ? TEXT("true") : TEXT("false"), CurrentPlayerCount,
      TurnManager ? TurnManager->GetControllerCount() : 0,
      bReadyToStart ? TEXT("true") : TEXT("false"));

  if (GEngine && CurrentPlayerCount < MinPlayerCount) {
    GEngine->AddOnScreenDebugMessage(
        -1, 4.f, FColor::Yellow,
        FString::Printf(TEXT("Waiting for players: %d/%d ready"),
                        CurrentPlayerCount, MinPlayerCount));
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
  bAwaitingStrategicInitiativeInput = true;
  bStrategicInitiativePromptIssued = true;

  for (APlayerState *PSBase : GS->PlayerArray) {
    if (ASkaldPlayerState *PS = Cast<ASkaldPlayerState>(PSBase)) {
      if (PS->InitiativeRoll > 0) {
        PS->InitiativeRoll = 0;
      }

      if (!PS->bIsAI) {
        PendingStrategicInitiativePlayers.Add(PS);
      }
    }
  }

  for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator();
       It; ++It) {
    if (ASkaldPlayerController *PC = Cast<ASkaldPlayerController>(*It)) {
      ASkaldPlayerState *PS = PC->GetPlayerState<ASkaldPlayerState>();
      const bool bIsAI = PS && PS->bIsAI;
      if (!bIsAI) {
        PC->ClientPromptStrategicInitiative(/*RoundNumber*/ 1, /*RollValue*/ 0,
                                            /*bWonInitiative*/ false);
      }
    }
  }

  if (PendingStrategicInitiativePlayers.Num() == 0) {
    ResolveStrategicInitiativePhase();
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

void ASkaldGameMode::HandlePendingStrategicInitiativeUpdate() {
  PrunePendingStrategicInitiativePlayers();

  if (bAwaitingStrategicInitiativeInput &&
      PendingStrategicInitiativePlayers.Num() == 0) {
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

  RemovePendingStrategicInitiativePlayer(PS);
}

void ASkaldGameMode::ResolveStrategicInitiativePhase() {
  if (!bAwaitingStrategicInitiativeInput) {
    return;
  }

  UE_LOG(LogSkald, Log,
         TEXT("TryInitializeWorldAndStart: Initializing world after initiative "
              "confirmations"));

  bAwaitingStrategicInitiativeInput = false;

  const bool bInitialized = InitializeWorld();
  PendingStrategicInitiativePlayers.Empty();
  bStrategicInitiativePromptIssued = false;

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

  if (bStrategicInitiativePromptIssued) {
    Controller->ClientDisplayStrategicInitiativeResult(RoundNumber, RollValue,
                                                      bWonInitiative);
  } else {
    Controller->ClientPromptStrategicInitiative(RoundNumber, RollValue,
                                               bWonInitiative);
  }
}

void ASkaldGameMode::HandleWorldInitializationComplete() {
  USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>();
  if (!GI || GI->bIsInBattleMap) {
    return;
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

  ASkaldGameState *GS = GetGameState<ASkaldGameState>();
  if (GS) {
    GS->Players.Empty();
    GS->PlayerArray.Empty();
    GS->CurrentTurnIndex = LoadedGame->CurrentPlayerIndex;
  }

  PlayerDataArray.Empty();
  for (const FPlayerSaveStruct &PlayerSave : LoadedGame->Players) {
    ASkaldPlayerState *PS = GetWorld()->SpawnActor<ASkaldPlayerState>();
    if (!PS) {
      continue;
    }
    PS->SetPlayerId(PlayerSave.PlayerID);
    PS->PlayerDisplayName = PlayerSave.PlayerName;
    PS->SetPlayerName(PlayerSave.PlayerName);
    PS->Faction = PlayerSave.Faction;
    PS->Resources = PlayerSave.Resources;
    if (GS) {
      GS->AddPlayerState(PS);
    }

    FS_PlayerData Data;
    Data.PlayerID = PlayerSave.PlayerID;
    Data.PlayerName = PlayerSave.PlayerName;
    Data.Faction = PlayerSave.Faction;
    Data.Resources = PlayerSave.Resources;
    Data.IsEliminated = true;
    Data.IsHuman = !PS->bIsAI;
    Data.IsAI = PS->bIsAI;
    Data.IsAlive = false;
    Data.CapitalsOwned = 0;
    Data.TroopsCount = 0;
    PlayerDataArray.Add(Data);

    if (TurnManager) {
      TurnManager->BroadcastResources(PS);
    }
  }

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

  if (WorldMap && WorldMap->SelectedTerritory &&
      !TerritoryById.Contains(WorldMap->SelectedTerritory->TerritoryID)) {
    WorldMap->SelectedTerritory = nullptr;
  }

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

  RefreshHUDs();

  if (GS) {
    GS->OnPlayersUpdated.Broadcast();
  }

  if (TurnManager) {
    TurnManager->SortControllersByInitiative();
    for (int32 i = 0; i < LoadedGame->CurrentPlayerIndex; ++i) {
      TurnManager->AdvanceTurn();
    }
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
      ++PlacementIndex;
      continue;
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
      const int32 NewIndex = GSLocal->PlayerArray.IndexOfByKey(PS);
      if (NewIndex != INDEX_NONE) {
        GSLocal->CurrentTurnIndex = NewIndex; // RepNotify → OnTurnIndexChanged
        GSLocal->OnTurnIndexChanged.Broadcast(
            NewIndex); // optional immediate local broadcast
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
      WorldMap->AutoPlaceUnitsForAI(PS);
      TurnManager->BroadcastDeployableUnits(PS);
      bArmyPlacementFailsafeTriggered = false;

      bool bScheduledAutoAdvance = false;
      if (ArmyPlacementAutoAdvanceDelay > KINDA_SMALL_NUMBER) {
        if (UWorld *World = GetWorld()) {
          // Allow a short pause so replicated HUDs can reveal their phase buttons
          // before the turn advances to the next player.
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

  const int32 TotalPlayerCount = GS->PlayerArray.Num();
  if (TotalPlayerCount < MinPlayerCount) {
    UE_LOG(
        LogSkald, Warning,
        TEXT("InitializeWorld aborted: need at least %d players but found %d"),
        MinPlayerCount, TotalPlayerCount);
    if (GEngine) {
      GEngine->AddOnScreenDebugMessage(
          -1, 5.f, FColor::Yellow,
          FString::Printf(
              TEXT("InitializeWorld: need at least %d players but found %d"),
              MinPlayerCount, TotalPlayerCount));
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
          NotifyStrategicInitiativeRoll(PC, /*RoundNumber*/ 1, RollValue, bWon);
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

  if (const UWorld *World = GetWorld()) {
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

  // Store basic turn information.
  SaveGameObject->TurnNumber = 0; // Turn tracking not yet implemented
  if (const ASkaldGameState *GS = GetGameState<ASkaldGameState>()) {
    SaveGameObject->CurrentPlayerIndex = GS->CurrentTurnIndex;
  }

  // Preserve current camera position and zoom so the view can be restored
  // when the game is loaded again.
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

  // Copy player data.
  SaveGameObject->Players.Empty();
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

  // Capture current territory state.
  SaveGameObject->Territories.Empty();
  if (WorldMap) {
    for (ATerritory *Territory : WorldMap->Territories) {
      if (!Territory) {
        continue;
      }

      FS_Territory TerrData;
      TerrData.TerritoryID = Territory->TerritoryID;
      TerrData.TerritoryName = Territory->TerritoryName;
      TerrData.OwnerPlayerID =
          Territory->OwningPlayer ? Territory->OwningPlayer->GetPlayerId() : 0;
      TerrData.IsCapital = Territory->bIsCapital;
      TerrData.CapitalOwner = TerrData.OwnerPlayerID;
      TerrData.ArmyUnits = Territory->ArmyUnits;
      TerrData.ContinentID = Territory->ContinentID;
      for (ATerritory *Adj : Territory->AdjacentTerritories) {
        if (Adj) {
          TerrData.AdjacentIDs.Add(Adj->TerritoryID);
        }
      }
      TerrData.Location = Territory->GetActorLocation();
      TerrData.HasTreasure = Territory->bHasTreasure;
      TerrData.TreasureAttachedUnitID =
          ReadIntProperty(Territory, TEXT("TreasureAttachedUnitID"));
      TerrData.FortificationLevel =
          ReadIntProperty(Territory, TEXT("FortificationLevel"));
      TerrData.Moat = ReadBoolProperty(Territory, TEXT("Moat"));
      TerrData.WallHealth =
          ReadIntProperty(Territory, TEXT("WallHealth"));
      TerrData.BuiltSiegeID = Territory->BuiltSiegeID;
      TerrData.ConqueredTurn =
          ReadIntProperty(Territory, TEXT("ConqueredTurn"));
      TerrData.IsNeutralSpawn =
          ReadBoolProperty(Territory, TEXT("IsNeutralSpawn"));
      SaveGameObject->Territories.Add(TerrData);
    }
  }

  // Store current siege equipment state.
  SaveGameObject->Sieges = SiegePool;
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
