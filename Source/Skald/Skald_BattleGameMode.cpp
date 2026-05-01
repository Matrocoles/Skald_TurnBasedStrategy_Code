#include "Skald_BattleGameMode.h"
#include "SkaldLogging.h"

#include "Skald_AIController.h"

#include "Algo/RandomShuffle.h"
#include "Algo/Sort.h"
#include "AIController.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FighterPawn.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GridBattleManager.h"
#include "GridObstacleActor.h"
#include "GridObstacleComponent.h"
#include "GridOverlayComponent.h"
#include "Skald.h"
#include "Skald_GameInstance.h"
#include "Skald_GameState.h"
#include "Skald_PlayerController.h"
#include "Skald_PlayerState.h"
#include "Territory.h"
#include "TimerManager.h"
#include "WorldMap.h"
#include "Math/RotationMatrix.h"

namespace {

struct FPendingControllerSlot {
  TWeakObjectPtr<AController> Controller;
  bool bIsAI = false;
  int32 PlayerId = INDEX_NONE;
  FString DisplayName;
};

static TArray<FPendingControllerSlot> GPendingControllers;
static bool GBattleSetupTriggered = false;
constexpr float BattleSpawnHeightPadding = 100.f;
constexpr int32 BattleSpawnEdgeColumns = 3;

static bool IsAIController(const AController *C) {
  return C && C->IsA(ASkaldAIController::StaticClass());
}

static bool IsAIControllerIdentity(const ASkaldPlayerController *Controller) {
  if (!Controller) {
    return false;
  }
  if (Controller->IsA<ASkaldAIController>()) {
    return true;
  }
  if (const ASkaldPlayerState *PS =
          Controller->GetPlayerState<ASkaldPlayerState>()) {
    if (PS->bIsAI) {
      return true;
    }
  }
  const FString ClassName =
      Controller->GetClass() ? Controller->GetClass()->GetName() : FString();
  const FString InstanceName = Controller->GetName();
  return ClassName.Contains(TEXT("AiController"), ESearchCase::IgnoreCase) ||
         ClassName.Contains(TEXT("AIController"), ESearchCase::IgnoreCase) ||
         InstanceName.Contains(TEXT("AiController"), ESearchCase::IgnoreCase) ||
         InstanceName.Contains(TEXT("AIController"), ESearchCase::IgnoreCase);
}

static void NormalizeSinglePlayerControllerRoles(UWorld *World,
                                                 const USkaldGameInstance *GI) {
  if (!World || !GI || GI->bIsMultiplayer) {
    return;
  }

  for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator();
       It; ++It) {
    ASkaldPlayerController *PC = Cast<ASkaldPlayerController>(*It);
    ASkaldPlayerState *PS =
        PC ? PC->GetPlayerState<ASkaldPlayerState>() : nullptr;
    if (!PC || !PS) {
      continue;
    }

    const bool bIsHumanLocal =
        PC->IsLocalController() && PC->IsLocalPlayerController() &&
        PC->GetLocalPlayer() != nullptr && PC->Player != nullptr;
    PS->bIsAI = !bIsHumanLocal;
  }
}

static int32 ResolveBattlePlayerId(const ASkaldPlayerState *PlayerState) {
  if (!PlayerState) {
    return INDEX_NONE;
  }

  const int32 StableId = PlayerState->GetStablePlayerId();
  if (StableId > 0) {
    return StableId;
  }

  const int32 AuthoritativeId = PlayerState->GetAuthoritativePlayerId();
  if (AuthoritativeId > 0) {
    return AuthoritativeId;
  }

  return INDEX_NONE;
}

static uint32 BuildBattleSetupSignature(const FS_BattlePayload& Battle) {
  uint32 Hash = 0;
  Hash = HashCombine(Hash, ::GetTypeHash(Battle.FromTerritoryID));
  Hash = HashCombine(Hash, ::GetTypeHash(Battle.TargetTerritoryID));
  Hash = HashCombine(Hash, ::GetTypeHash(Battle.AttackerPlayerID));
  Hash = HashCombine(Hash, ::GetTypeHash(Battle.DefenderPlayerID));
  Hash = HashCombine(Hash, ::GetTypeHash(Battle.ArmyCountSent));
  Hash = HashCombine(Hash, ::GetTypeHash(Battle.DefenderArmyCount));
  return Hash;
}

static int32 GetPlayerIdFrom(AController *C) {
  if (!C) {
    return INDEX_NONE;
  }
  if (!C->PlayerState) {
    C->InitPlayerState();
  }
  if (const ASkaldPlayerState *SPS = C->GetPlayerState<ASkaldPlayerState>()) {
    const int32 ResolvedId = ResolveBattlePlayerId(SPS);
    if (ResolvedId > 0) {
      return ResolvedId;
    }
  }
  return INDEX_NONE;
}

static FString GetDisplayName(AController *C) {
  if (!C) {
    return TEXT("");
  }
  if (APlayerState *PS = C->PlayerState) {
    return PS->GetPlayerName();
  }
  return C->GetName();
}

static void CompactSlots() {
  GPendingControllers.RemoveAll([](const FPendingControllerSlot &S) {
    return !S.Controller.IsValid();
  });
}

static void AssignControllerSlot(AController *C) {
  if (!C) {
    return;
  }
  if (!C->PlayerState) {
    C->InitPlayerState();
  }

  const bool bAIFlag = IsAIController(C) ||
                       (C->GetPlayerState<ASkaldPlayerState>() &&
                        C->GetPlayerState<ASkaldPlayerState>()->bIsAI);
  const int32 PlayerId = GetPlayerIdFrom(C);
  const FString Name = GetDisplayName(C);

  CompactSlots();

  for (int32 i = 0; i < GPendingControllers.Num(); ++i) {
    auto &S = GPendingControllers[i];
    if (S.Controller.Get() == C || (PlayerId > 0 && S.PlayerId == PlayerId)) {
      S.Controller = C;
      S.bIsAI = bAIFlag;
      S.PlayerId = PlayerId;
      S.DisplayName = Name;
      UE_LOG(LogSkaldBattle, Log,
             TEXT("Assigned %s to slot %d (update)"), *GetNameSafe(C), i);
      return;
    }
  }

  for (int32 i = 0; i < GPendingControllers.Num(); ++i) {
    auto &S = GPendingControllers[i];
    if (!S.Controller.IsValid()) {
      S.Controller = C;
      S.bIsAI = bAIFlag;
      S.PlayerId = PlayerId;
      S.DisplayName = Name;
      UE_LOG(LogSkaldBattle, Log,
             TEXT("Assigned %s to slot %d (empty)"), *GetNameSafe(C), i);
      return;
    }
  }

  FPendingControllerSlot NewSlot;
  NewSlot.Controller = C;
  NewSlot.bIsAI = bAIFlag;
  NewSlot.PlayerId = PlayerId;
  NewSlot.DisplayName = Name;
  const int32 NewIdx = GPendingControllers.Add(MoveTemp(NewSlot));
  UE_LOG(LogSkaldBattle, Log,
         TEXT("Assigned %s to slot %d (append)"), *GetNameSafe(C), NewIdx);
}

static void RefreshControllerSlotForState(ASkaldPlayerState *State) {
  if (!State) {
    return;
  }

  if (AController *Owner = State->GetOwner<AController>()) {
    AssignControllerSlot(Owner);
  }
}

void BuildTerritoryMap(const TArray<FS_Territory> &Source,
                       TMap<int32, FS_Territory> &Destination) {
  Destination.Reset();
  Destination.Reserve(Source.Num());
  for (const FS_Territory &Territory : Source) {
    if (Territory.TerritoryID <= 0) {
      continue;
    }

    Destination.Add(Territory.TerritoryID, Territory);
  }
}

void MergeHumanTerritories(const FSkaldTravelState &TravelState,
                           ASkaldGameState *GameState,
                           const TMap<int32, FS_Territory> &TerritoryMap,
                           TSet<int32> &OutHumanTerritories) {
  OutHumanTerritories.Reset();

  for (int32 TerritoryID : TravelState.HumanOwnedTerritories) {
    if (TerritoryID > 0) {
      OutHumanTerritories.Add(TerritoryID);
    }
  }

  for (const TPair<int32, FS_Territory> &Pair : TerritoryMap) {
    const FS_Territory &Territory = Pair.Value;
    if (Territory.TerritoryID <= 0 ||
        OutHumanTerritories.Contains(Territory.TerritoryID)) {
      continue;
    }

    bool bIsHuman = TravelState.HumanOwnedTerritories.Contains(Territory.TerritoryID);
    if (!bIsHuman && GameState && Territory.OwnerPlayerID > 0) {
      if (ASkaldPlayerState *Owner = GameState->GetPlayerById(Territory.OwnerPlayerID)) {
        bIsHuman = !Owner->bIsAI;
      }
    }

    if (bIsHuman) {
      OutHumanTerritories.Add(Territory.TerritoryID);
    }
  }
}

ASkaldPlayerState *EnsureBattleParticipant(ASkaldGameState *GameState, UWorld *World,
                                           int32 PlayerID, const FString &DisplayName,
                                           ESkaldFaction Faction, bool bIsAI) {
  if (!GameState || !World || PlayerID <= 0) {
    return nullptr;
  }

  auto ApplyDisplayName = [&](ASkaldPlayerState *Target) {
    if (!Target || DisplayName.IsEmpty()) {
      return;
    }

    const FString ExistingName = Target->PlayerDisplayName;
    const FString CurrentPlayerName = Target->GetPlayerName();
    if (!ExistingName.IsEmpty() &&
        !ExistingName.Equals(DisplayName, ESearchCase::IgnoreCase)) {
      UE_LOG(LogSkaldBattle, Warning,
             TEXT("EnsureBattleParticipant: refusing to override display name for PlayerId %d (existing='%s' requested='%s')"),
            ResolveBattlePlayerId(Target), *ExistingName, *DisplayName);
      return;
    }

    Target->PlayerDisplayName = DisplayName;
    if (!CurrentPlayerName.Equals(DisplayName, ESearchCase::CaseSensitive)) {
      Target->SetPlayerName(DisplayName);
    }
  };

  if (ASkaldPlayerState *Existing = GameState->GetPlayerById(PlayerID)) {
    if (!DisplayName.IsEmpty()) {
      ApplyDisplayName(Existing);
    } else if (Existing->GetPlayerName().IsEmpty()) {
      UE_LOG(LogSkald, Warning,
             TEXT("EnsureBattleParticipant: Missing name for PlayerId %d"),
             PlayerID);
    }
    if (Faction != ESkaldFaction::None) {
      Existing->Faction = Faction;
    }
    Existing->bIsAI = bIsAI;
    RefreshControllerSlotForState(Existing);
    return Existing;
  }

  for (APlayerState *BasePS : GameState->PlayerArray) {
    ASkaldPlayerState *Candidate = Cast<ASkaldPlayerState>(BasePS);
    if (!Candidate || ResolveBattlePlayerId(Candidate) != PlayerID) {
      continue;
    }

    if (!DisplayName.IsEmpty()) {
      ApplyDisplayName(Candidate);
    } else if (Candidate->GetPlayerName().IsEmpty()) {
      UE_LOG(LogSkald, Warning,
             TEXT("EnsureBattleParticipant: Missing name for PlayerId %d"),
             PlayerID);
    }
    if (Faction != ESkaldFaction::None) {
      Candidate->Faction = Faction;
    }
    Candidate->bIsAI = bIsAI;

    bool bAddedToList = false;
    if (!GameState->Players.Contains(Candidate)) {
      GameState->Players.Add(Candidate);
      GameState->Players.RemoveAll([](const TObjectPtr<ASkaldPlayerState>& Player) {
        return Player.Get() == nullptr;
      });
      GameState->Players.Sort([](const TObjectPtr<ASkaldPlayerState>& A,
                                 const TObjectPtr<ASkaldPlayerState>& B) {
        ASkaldPlayerState* const PlayerA = A.Get();
        ASkaldPlayerState* const PlayerB = B.Get();
        if (PlayerA == PlayerB) {
          return false;
        }
        if (!PlayerA) {
          return false;
        }
        if (!PlayerB) {
          return true;
        }
      return ResolveBattlePlayerId(PlayerA) < ResolveBattlePlayerId(PlayerB);
      });
      bAddedToList = true;
    }
    if (bAddedToList) {
      GameState->OnPlayersUpdated.Broadcast();
      GameState->ForceNetUpdate();
    }

    RefreshControllerSlotForState(Candidate);
    return Candidate;
  }

  FActorSpawnParameters Params;
  Params.SpawnCollisionHandlingOverride =
      ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
  ASkaldPlayerState *NewState = World->SpawnActor<ASkaldPlayerState>(
      ASkaldPlayerState::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator,
      Params);
  if (!NewState) {
    return nullptr;
  }

  NewState->SetPlayerId(PlayerID);
  if (!DisplayName.IsEmpty()) {
    ApplyDisplayName(NewState);
  } else {
    UE_LOG(LogSkald, Warning,
           TEXT("EnsureBattleParticipant: Spawned PlayerState without name (Id=%d)"),
           PlayerID);
  }
  if (Faction != ESkaldFaction::None) {
    NewState->Faction = Faction;
  }
  NewState->bIsAI = bIsAI;

  GameState->AddPlayerState(NewState);
  GameState->ForceNetUpdate();
  RefreshControllerSlotForState(NewState);
  return NewState;
}
} // namespace

void ASkald_BattleGameMode::InitializeBattleGameMode(const FString &MapName,
                                                     const FString &Options,
                                                     FString &ErrorMessage) {
  InitGame(MapName, Options, ErrorMessage);
}

void ASkald_BattleGameMode::InitGame(const FString &Map, const FString &Options,
                                     FString &Error) {
  Super::InitGame(Map, Options, Error);

  GPendingControllers.Reset();
  GBattleSetupTriggered = false;

  CachedHumanTerritoryIDs.Reset();
  CachedTerritoryMap.Reset();
  bSetupStarted = false;
  bSetupCompleted = false;
  bBattleLaunched = false;
  bLoggedTravelCache = false;
  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(WaitForPlayersHandle);
  }

  USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>();
  if (GI) {
    GI->SetTravelPending(false);
  }

  ASkaldGameState *GS = GetGameState<ASkaldGameState>();

  if (GI) {
    const FSkaldTravelState &TravelState = GI->GetTravelState();
    if (TravelState.bValid && TravelState.ExpectedControllers > 0) {
      ExpectedControllers = TravelState.ExpectedControllers;
    } else if (ExpectedControllers <= 0) {
      ExpectedControllers = 1;
    }

    const TArray<FS_Territory> *Source = &TravelState.CachedTerritories;
    if (Source->Num() == 0) {
      Source = &GI->CachedWorldMapTerritories;
    }

    BuildTerritoryMap(*Source, CachedTerritoryMap);
    MergeHumanTerritories(TravelState, GS, CachedTerritoryMap,
                          CachedHumanTerritoryIDs);

    UE_LOG(LogSkaldBattle, Log,
           TEXT("BattleGM InitGame: Restored %d human territories from travel cache; ExpectedControllers=%d"),
           CachedHumanTerritoryIDs.Num(), ExpectedControllers);
    bLoggedTravelCache = true;
  } else {
    if (ExpectedControllers <= 0) {
      ExpectedControllers = 1;
    }
    UE_LOG(LogSkaldBattle, Warning,
           TEXT("BattleGM InitGame: GameInstance unavailable; ExpectedControllers reset"));
  }

  UE_LOG(LogSkald, Log, TEXT("[HUD] Skipping MainHUD in BattleGameMode"));
}

void ASkald_BattleGameMode::BeginPlay() {
  Super::BeginPlay();

  if (!BattleManager) {
    UClass *ClassToUse = BattleManagerClass ? *BattleManagerClass
                                            : UGridBattleManager::StaticClass();
    BattleManager = NewObject<UGridBattleManager>(this, ClassToUse);
    const int32 Seed =
        static_cast<int32>(FDateTime::Now().GetTicks() & 0x7FFFFFFF);
    BattleManager->SetRandomSeed(Seed);
    BattleManager->OnBattleEnded.AddDynamic(this,
                                           &ASkald_BattleGameMode::HandleBattleEnded);
  }

  if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
    GI->GridBattleManager = BattleManager;
    const FSkaldTravelState &TravelState = GI->GetTravelState();
    if (ExpectedControllers <= 0 && TravelState.bValid &&
        TravelState.ExpectedControllers > 0) {
      ExpectedControllers = TravelState.ExpectedControllers;
    }
  }

  const UClass *BattleClass = GetClass();
  if (BattleClass == StaticClass()) {
    UE_LOG(LogSkaldBattle, Warning,
           TEXT("BattleGM BeginPlay: Running native Skald_BattleGameMode. ")
           TEXT("Expected Blueprint child (Skald_BattleGameMode_SC) when streaming a battle sublevel."));
  } else {
    UE_LOG(LogSkaldBattle, Log,
           TEXT("BattleGM BeginPlay: Using class %s (Path=%s)"),
           *BattleClass->GetName(), *BattleClass->GetPathName());
  }

  UE_LOG(LogSkaldBattle, Log,
         TEXT("BattleGM BeginPlay: BattleManager ready; ExpectedControllers=%d"),
         ExpectedControllers);
  UE_LOG(LogSkaldBattle, Log, TEXT("BGM BeginPlay. ExpectedControllers=%d"),
         ExpectedControllers);

  // Battle bootstrap is deferred until player states have replicated via the
  // travel callbacks (HandleStartingNewPlayer/PostLogin/HandleSeamlessTravelPlayer).
  // If the streaming-level notification never fires (e.g. when the battle map
  // is loaded directly), ensure we still run the bootstrap logic on BeginPlay
  // so participants register and the fighter selection UI can appear.
  if (!bPendingStreamingActivation)
  {
    UE_LOG(LogSkaldBattle, Verbose,
           TEXT("BattleGM BeginPlay: streaming activation missing; triggering bootstrap manually."));
    bPendingStreamingActivation = true;
  }
  ProcessStreamingActivation();
}

void ASkald_BattleGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason) {
  const ENetMode NetMode = GetNetMode();
  UE_LOG(LogSkaldBattle, Warning,
         TEXT("BattleGameMode EndPlay (Reason=%d, NetMode=%d). Battle maps should remain connected until the host explicitly ends the session."),
         static_cast<int32>(EndPlayReason), static_cast<int32>(NetMode));

  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(WaitForPlayersHandle);
  }

  WaitForPlayersHandle.Invalidate();

  Super::EndPlay(EndPlayReason);
}

void ASkald_BattleGameMode::NotifyBattleLevelActivated() {
  UE_LOG(LogSkaldBattle, Log, TEXT("BattleGM: Battle level activated; deferring bootstrap until BeginPlay."));
  bPendingStreamingActivation = true;
  ProcessStreamingActivation();
}

void ASkald_BattleGameMode::ProcessStreamingActivation() {
  if (!bPendingStreamingActivation) {
    return;
  }

  if (!HasAuthority()) {
    bPendingStreamingActivation = false;
    return;
  }

  if (!HasActorBegunPlay()) {
    return;
  }

  UWorld *World = GetWorld();
  if (!World) {
    return;
  }

  bPendingStreamingActivation = false;

  TArray<AController *> ControllersToNotify;
  for (FConstControllerIterator It = World->GetControllerIterator(); It; ++It) {
    if (AController *Controller = It->Get()) {
      ControllersToNotify.Add(Controller);
    }
  }

  if (ControllersToNotify.Num() == 0) {
    UE_LOG(LogSkaldBattle, Verbose,
           TEXT("BattleGM streaming activation: no controllers discovered"));
  }

  for (AController *Controller : ControllersToNotify) {
    if (Controller) {
      OnControllerReady(Controller);
    }
  }

  if (!GetWorldTimerManager().IsTimerActive(WaitForPlayersHandle)) {
    GetWorldTimerManager().SetTimer(
        WaitForPlayersHandle, this, &ASkald_BattleGameMode::PollBattleBootstrap, 0.25f,
        true);
  }

  PollBattleBootstrap();
}

void ASkald_BattleGameMode::EnsureBattleControllers() {
  if (!HasAuthority()) {
    return;
  }

  UWorld *World = GetWorld();
  if (!World || World->GetNetMode() != NM_Standalone) {
    return;
  }

  USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>();
  if (!GI || GI->bIsMultiplayer) {
    return;
  }

  const FS_BattlePayload &Battle = GI->PendingBattle;

  struct FBattleParticipant {
    int32 PlayerId;
    bool bIsAI;
    FString DisplayName;
    ESkaldFaction Faction;
  };

  TArray<FBattleParticipant> Participants;
  auto AddParticipant = [&](int32 PlayerId, bool bIsAI, const FString &Name,
                            ESkaldFaction Faction) {
    if (PlayerId > 0) {
      Participants.Add({PlayerId, bIsAI, Name, Faction});
    }
  };

  AddParticipant(Battle.AttackerPlayerID, Battle.bAttackerIsAI,
                 Battle.AttackerDisplayName, Battle.AttackerFaction);
  AddParticipant(Battle.DefenderPlayerID, Battle.bDefenderIsAI,
                 Battle.DefenderDisplayName, Battle.DefenderFaction);

  if (Participants.Num() == 0) {
    return;
  }

  ExpectedControllers = FMath::Max(ExpectedControllers, Participants.Num());

  bEnsuringBattleControllers = true;
  DeferredReadyControllers.Reset();

  TArray<ASkaldPlayerController *> HumanControllers;
  TArray<ASkaldPlayerController *> AIControllers;
  TMap<int32, ASkaldPlayerController *> ControllersById;
  TSet<ASkaldPlayerController *> UsedControllers;

  for (FConstControllerIterator It = World->GetControllerIterator(); It; ++It) {
    ASkaldPlayerController *PC = Cast<ASkaldPlayerController>(*It);
    if (!PC) {
      continue;
    }

    if (AIControllerClass && PC->IsA(AIControllerClass)) {
      AIControllers.Add(PC);
    } else {
      HumanControllers.Add(PC);
    }

    if (ASkaldPlayerState *PS = PC->GetPlayerState<ASkaldPlayerState>()) {
      const int32 ResolvedId = ResolveBattlePlayerId(PS);
      if (ResolvedId > 0) {
        ControllersById.Add(ResolvedId, PC);
      }
    }
  }

  auto EnsurePlayerState = [&](ASkaldPlayerController *Controller,
                               bool bIsAI) -> ASkaldPlayerState * {
    if (!Controller) {
      return nullptr;
    }

    ASkaldPlayerState *PS = Controller->GetPlayerState<ASkaldPlayerState>();
    if (!PS) {
      Controller->InitPlayerState();
      PS = Controller->GetPlayerState<ASkaldPlayerState>();
    }
    if (!PS) {
      return nullptr;
    }

    PS->bIsAI = bIsAI;

    if (ASkaldGameState *GS = GetGameState<ASkaldGameState>()) {
      if (!GS->PlayerArray.Contains(PS)) {
        GS->AddPlayerState(PS);
      }
    }

    return PS;
  };

  auto AcquireController = [&](const FBattleParticipant &Participant)
      -> ASkaldPlayerController * {
    if (Participant.PlayerId > 0) {
      if (ASkaldPlayerController **Existing =
              ControllersById.Find(Participant.PlayerId)) {
        ASkaldPlayerController *Controller = *Existing;
        if (Controller && !UsedControllers.Contains(Controller)) {
          UsedControllers.Add(Controller);
          return Controller;
        }
      }
    }

    TArray<ASkaldPlayerController *> &Pool =
        Participant.bIsAI ? AIControllers : HumanControllers;
    for (ASkaldPlayerController *Candidate : Pool) {
      if (Candidate && !UsedControllers.Contains(Candidate)) {
        UsedControllers.Add(Candidate);
        return Candidate;
      }
    }

    if (!Participant.bIsAI || !AIControllerClass) {
      return nullptr;
    }

    FTransform SpawnTransform = FTransform::Identity;
    ASkaldPlayerController *NewController = Cast<ASkaldPlayerController>(
        World->SpawnActorDeferred<APlayerController>(
            AIControllerClass, SpawnTransform, nullptr, nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn));
    if (!NewController) {
      return nullptr;
    }

    NewController->FinishSpawning(SpawnTransform);
    ASkaldPlayerState *NewPS = EnsurePlayerState(NewController, true);
    if (NewPS) {
      const int32 ResolvedId = ResolveBattlePlayerId(NewPS);
      if (ResolvedId > 0) {
        ControllersById.Add(ResolvedId, NewController);
      }
    }

    AIControllers.Add(NewController);
    UsedControllers.Add(NewController);
    return NewController;
  };

  bool bUpdatedPlayers = false;

  for (const FBattleParticipant &Participant : Participants) {
    ASkaldPlayerController *Controller = AcquireController(Participant);
    if (!Controller) {
      UE_LOG(LogSkaldBattle, Warning,
             TEXT("EnsureBattleControllers: Unable to allocate controller for PlayerId=%d"),
             Participant.PlayerId);
      continue;
    }

    ASkaldPlayerState *PS = EnsurePlayerState(Controller, Participant.bIsAI);
    if (!PS) {
      continue;
    }

    const int32 PreviousId = ResolveBattlePlayerId(PS);
    if (Participant.PlayerId > 0 && PreviousId != Participant.PlayerId &&
        PreviousId > 0) {
      ControllersById.Remove(PreviousId);
    }

    if (Participant.PlayerId > 0) {
      PS->SetPlayerId(Participant.PlayerId);
      ControllersById.Add(Participant.PlayerId, Controller);
    }
    if (!Participant.DisplayName.IsEmpty()) {
      PS->PlayerDisplayName = Participant.DisplayName;
      PS->SetPlayerName(Participant.DisplayName);
    }
    if (Participant.Faction != ESkaldFaction::None) {
      PS->Faction = Participant.Faction;
    }
    PS->bIsAI = Participant.bIsAI;
    RefreshControllerSlotForState(PS);
    bUpdatedPlayers = true;
  }

  if (bUpdatedPlayers) {
    if (ASkaldGameState *GS = GetGameState<ASkaldGameState>()) {
      GS->OnPlayersUpdated.Broadcast();
      GS->ForceNetUpdate();
    }
  }

  bEnsuringBattleControllers = false;
  ProcessDeferredControllers();
}

void ASkald_BattleGameMode::ProcessDeferredControllers() {
  if (DeferredReadyControllers.Num() == 0) {
    return;
  }

  TArray<TWeakObjectPtr<AController>> LocalPendingControllers =
      DeferredReadyControllers;
  DeferredReadyControllers.Reset();

  for (const TWeakObjectPtr<AController> &WeakController : LocalPendingControllers) {
    if (AController *Controller = WeakController.Get()) {
      OnControllerReady(Controller);
    }
  }
}

void ASkald_BattleGameMode::QueueDeferredController(AController *Controller) {
  if (!Controller) {
    return;
  }

  DeferredReadyControllers.AddUnique(Controller);

  if (UWorld *World = GetWorld()) {
    FTimerDelegate RetryDelegate = FTimerDelegate::CreateUObject(
        this, &ASkald_BattleGameMode::ProcessDeferredControllers);
    World->GetTimerManager().SetTimerForNextTick(RetryDelegate);
  }
}

bool ASkald_BattleGameMode::ControllerHasStablePlayerId(
    AController *Controller) const {
  if (!Controller) {
    return false;
  }

  if (!Controller->PlayerState) {
    return false;
  }

  if (const ASkaldPlayerState *PlayerState =
          Controller->GetPlayerState<ASkaldPlayerState>()) {
    return ResolveBattlePlayerId(PlayerState) > 0;
  }

  return false;
}

bool ASkald_BattleGameMode::IsRegisteredBattleParticipant(
    const ASkaldPlayerState *PlayerState) const {
  if (!PlayerState) {
    return false;
  }

  const int32 PlayerId = ResolveBattlePlayerId(PlayerState);
  if (PlayerId <= 0) {
    return false;
  }

  const ASkaldGameState *SkaldGameState = GetGameState<ASkaldGameState>();
  if (!SkaldGameState) {
    return false;
  }

  FBattlePlayerEntry Entry;
  const bool bFound = SkaldGameState->GetBattleEntryForPlayer(PlayerId, Entry);
  UE_LOG(LogSkaldBattle, Log,
         TEXT("IsRegisteredBattleParticipant PlayerId=%d Name=%s Found=%d ActiveFlag=%d"),
         PlayerId, *PlayerState->GetResolvedPlayerName(TEXT("RegisteredParticipant")),
         bFound ? 1 : 0, PlayerState->bIsActiveBattlePlayer ? 1 : 0);
  return bFound || PlayerState->bIsActiveBattlePlayer;
}

void ASkald_BattleGameMode::RegisterParticipantController(AController *Controller) {
  if (!HasAuthority() || !Controller) {
    return;
  }

  if (!Controller->PlayerState) {
    Controller->InitPlayerState();
  }

  if (ASkaldPlayerState *PS = Controller->GetPlayerState<ASkaldPlayerState>()) {
    if (IsRegisteredBattleParticipant(PS)) {
      PS->bIsActiveBattlePlayer = true;
      SyncBattlePlayerEntry(PS);
      UE_LOG(LogSkaldBattle, Log,
             TEXT("RegisterParticipantController: accepted %s (Id=%d AI=%d)"),
            *GetNameSafe(Controller), ResolveBattlePlayerId(PS), PS->bIsAI ? 1 : 0);
    } else {
      UE_LOG(LogSkaldBattle, Warning,
             TEXT("RegisterParticipantController: controller %s (Id=%d) is not in BattleParticipants"),
            *GetNameSafe(Controller), ResolveBattlePlayerId(PS));
    }
  }
}

void ASkald_BattleGameMode::SyncBattlePlayerEntry(ASkaldPlayerState *PlayerState) const
{
  if (!HasAuthority() || !PlayerState)
  {
    return;
  }

  ASkaldGameState *GS = GetGameState<ASkaldGameState>();
  if (!GS)
  {
    return;
  }

  FBattlePlayerEntry Entry;
  Entry.PlayerId = ResolveBattlePlayerId(PlayerState);
  Entry.DisplayName = PlayerState->GetResolvedPlayerName(TEXT("BattleGM.SyncBattlePlayerEntry"));
  Entry.Faction = PlayerState->Faction;
  Entry.bIsAI = PlayerState->bIsAI;
  Entry.PendingArmyBudget = PlayerState->PendingArmyBudget;

  UE_LOG(LogSkaldBattle, Log,
         TEXT("SyncBattlePlayerEntry: PlayerId=%d Name=%s Faction=%d Budget=%d AI=%s"),
         Entry.PlayerId, *Entry.DisplayName, static_cast<int32>(Entry.Faction),
         Entry.PendingArmyBudget, Entry.bIsAI ? TEXT("true") : TEXT("false"));

  GS->UpsertBattleEntry(Entry);
}

void ASkald_BattleGameMode::BeginPreBattleSelection(ASkaldPlayerState *AttackerPS,
                                                    ASkaldPlayerState *DefenderPS,
                                                    int32 AttackerBudget,
                                                    int32 DefenderBudget)
{
  if (!HasAuthority()) {
    return;
  }
  NormalizeSinglePlayerControllerRoles(GetWorld(),
                                       GetGameInstance<USkaldGameInstance>());

  if (AttackerPS) {
    AttackerPS->PendingArmyBudget = AttackerBudget;
    AttackerPS->PendingArmy.Reset();
    AttackerPS->bArmyLockedIn = false;

    if (!AttackerPS->bIsAI) {
      if (ASkaldPlayerController *APC =
              Cast<ASkaldPlayerController>(AttackerPS->GetOwner())) {
        if (!IsAIControllerIdentity(APC)) {
          APC->Client_ShowFighterSelection(AttackerBudget, AttackerPS->Faction);
        }
      }
    }
  }

  if (DefenderPS) {
    DefenderPS->PendingArmyBudget = DefenderBudget;
    DefenderPS->PendingArmy.Reset();
    DefenderPS->bArmyLockedIn = false;

    if (!DefenderPS->bIsAI) {
      if (ASkaldPlayerController *DPC =
              Cast<ASkaldPlayerController>(DefenderPS->GetOwner())) {
        if (!IsAIControllerIdentity(DPC)) {
          DPC->Client_ShowFighterSelection(DefenderBudget, DefenderPS->Faction);
        }
      }
    }
  }

  if (ASkaldGameState *GS = GetGameState<ASkaldGameState>()) {
    GS->SetBattlePhase(EBattlePhase::FighterSelection);
  }

  LogParticipantLockState(TEXT("BeginPreBattleSelection"));
}

void ASkald_BattleGameMode::SetupPendingBattle() {
  if (!HasAuthority()) {
    return;
  }

  if (bSetupCompleted) {
    return;
  }

  bBattleLaunched = false;

  auto ResetSetupAttempt = [this]() {
    bSetupStarted = false;
    GBattleSetupTriggered = false;
    ActiveSetupSignature = 0;
  };

  USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>();
  if (!GI || !GI->GridBattleManager) {
    UE_LOG(LogSkaldBattle, Warning,
           TEXT("BattleGM SetupPendingBattle: GameInstance or GridBattleManager missing"));
    ResetSetupAttempt();
    return;
  }

  UE_LOG(LogSkaldBattle, Log,
         TEXT("BattleGM SetupPendingBattle: building battle payload (TravelValid=%d BattleValid=%d)"),
         GI->GetTravelState().bValid ? 1 : 0,
         GI->PendingBattle.FromTerritoryID > 0 ? 1 : 0);

  ASkaldGameState *GS = GetGameState<ASkaldGameState>();
  if (!GS) {
    UE_LOG(LogSkaldBattle, Warning,
           TEXT("BattleGM SetupPendingBattle: GameState unavailable"));
    ResetSetupAttempt();
    return;
  }

  UWorld *World = GetWorld();
  if (!World) {
    ResetSetupAttempt();
    return;
  }

  const FSkaldTravelState &TS = GI->GetTravelState();
  MergeHumanTerritories(TS, GS, CachedTerritoryMap,
                        CachedHumanTerritoryIDs);

  FS_BattlePayload Battle = GI->PendingBattle;
  Battle.FromTerritoryID = Battle.FromTerritoryID > 0
                               ? Battle.FromTerritoryID
                               : TS.AttackerTerritory;
  Battle.TargetTerritoryID = Battle.TargetTerritoryID > 0
                                 ? Battle.TargetTerritoryID
                                 : TS.DefenderTerritory;
  if (Battle.AttackerPlayerID <= 0 && TS.AttackerPlayerId > 0) {
    Battle.AttackerPlayerID = TS.AttackerPlayerId;
  }
  if (Battle.DefenderPlayerID <= 0 && TS.DefenderPlayerId > 0) {
    Battle.DefenderPlayerID = TS.DefenderPlayerId;
  }
  if (Battle.ArmyCountSent <= 0 && TS.AttackerArmyBudget > 0) {
    Battle.ArmyCountSent = TS.AttackerArmyBudget;
  }
  if (Battle.DefenderArmyCount <= 0 && TS.DefenderArmyBudget > 0) {
    Battle.DefenderArmyCount = TS.DefenderArmyBudget;
  }

  const uint32 SetupSignature = BuildBattleSetupSignature(Battle);
  if (LastCompletedSetupSignature != 0 &&
      LastCompletedSetupSignature == SetupSignature) {
    UE_LOG(LogSkaldBattle, Verbose,
           TEXT("SetupPendingBattle: skipping duplicate completed setup (Signature=%u)"),
           SetupSignature);
    bSetupCompleted = true;
    bSetupStarted = false;
    GBattleSetupTriggered = false;
    return;
  }

  if (ActiveSetupSignature != 0 && ActiveSetupSignature == SetupSignature &&
      bSetupStarted) {
    UE_LOG(LogSkaldBattle, Verbose,
           TEXT("SetupPendingBattle: duplicate in-flight setup ignored (Signature=%u)"),
           SetupSignature);
    return;
  }
  ActiveSetupSignature = SetupSignature;

  auto BackfillFromSlots = [&](int32& PlayerId, FString& DisplayName,
                               ESkaldFaction& Faction, bool& bIsAI)
  {
    if (PlayerId > 0)
    {
      return;
    }

    for (const FPendingControllerSlot& Slot : GPendingControllers)
    {
      if (!Slot.Controller.IsValid())
      {
        continue;
      }

      if (Slot.PlayerId > 0)
      {
        PlayerId = Slot.PlayerId;
      }
      else if (AController* Controller = Slot.Controller.Get())
      {
        PlayerId = GetPlayerIdFrom(Controller);
      }

      if (PlayerId <= 0)
      {
        continue;
      }

      if (DisplayName.IsEmpty())
      {
        DisplayName = Slot.DisplayName;
      }

      if (Faction == ESkaldFaction::None)
      {
        if (const ASkaldPlayerState* SlotPS = Slot.Controller.IsValid()
                                                 ? Slot.Controller->GetPlayerState<ASkaldPlayerState>()
                                                 : nullptr)
        {
          Faction = SlotPS->Faction;
          bIsAI = SlotPS->bIsAI;
        }
      }

      UE_LOG(LogSkaldBattle, Log,
             TEXT("SetupPendingBattle: Backfilled participant from slots (PlayerId=%d Name=%s Faction=%d AI=%s)"),
             PlayerId, *DisplayName, static_cast<int32>(Faction), bIsAI ? TEXT("true") : TEXT("false"));
      return;
    }
  };

  BackfillFromSlots(Battle.AttackerPlayerID, Battle.AttackerDisplayName,
                    Battle.AttackerFaction, Battle.bAttackerIsAI);
  BackfillFromSlots(Battle.DefenderPlayerID, Battle.DefenderDisplayName,
                    Battle.DefenderFaction, Battle.bDefenderIsAI);

  // When travelling into the battle map we depend on OnControllerReady to
  // populate the pending controller slots. In practice the callbacks can fire
  // before the battle mode finishes bootstrapping, which means SetupPendingBattle
  // might execute before any slots have been recorded. Fallback by scanning the
  // active controllers so the participant lookup below never runs against an
  // empty slot array.
  for (FConstControllerIterator It = World->GetControllerIterator(); It; ++It) {
    if (AController *Controller = It->Get()) {
      if (!Controller->PlayerState) {
        Controller->InitPlayerState();
      }
      AssignControllerSlot(Controller);
    }
  }

  CompactSlots();

  TSet<int32> Used;

  auto Acquire = [&](int32 PrefPid, bool bPrefAI) -> AController * {
    if (PrefPid > 0) {
      for (int32 SlotIdx = 0; SlotIdx < GPendingControllers.Num(); ++SlotIdx) {
        const auto &S = GPendingControllers[SlotIdx];
        if (Used.Contains(SlotIdx) || !S.Controller.IsValid()) {
          continue;
        }
        if (S.PlayerId == PrefPid) {
          Used.Add(SlotIdx);
          return S.Controller.Get();
        }
      }
    }
    for (int32 SlotIdx = 0; SlotIdx < GPendingControllers.Num(); ++SlotIdx) {
      const auto &S = GPendingControllers[SlotIdx];
      if (Used.Contains(SlotIdx) || !S.Controller.IsValid()) {
        continue;
      }
      if (S.bIsAI == bPrefAI) {
        Used.Add(SlotIdx);
        return S.Controller.Get();
      }
    }
    for (int32 SlotIdx = 0; SlotIdx < GPendingControllers.Num(); ++SlotIdx) {
      const auto &S = GPendingControllers[SlotIdx];
      if (Used.Contains(SlotIdx) || !S.Controller.IsValid()) {
        continue;
      }
      Used.Add(SlotIdx);
      return S.Controller.Get();
    }
    return nullptr;
  };

  AController *AttackerC =
      Acquire(Battle.AttackerPlayerID, Battle.bAttackerIsAI);
  AController *DefenderC =
      Acquire(Battle.DefenderPlayerID, Battle.bDefenderIsAI);

  auto SpawnAIController = [&]() -> AController * {
    if (!AIControllerClass || !World) {
      return nullptr;
    }

    FTransform SpawnTransform = FTransform::Identity;
    ASkaldAIController *NewAI = Cast<ASkaldAIController>(
        World->SpawnActorDeferred<APlayerController>(
            AIControllerClass, SpawnTransform, nullptr, nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn));
    if (!NewAI) {
      return nullptr;
    }

    NewAI->FinishSpawning(SpawnTransform);
    if (NewAI->PlayerState == nullptr) {
      NewAI->InitPlayerState();
    }
    if (ASkaldPlayerState *AIState =
            NewAI->GetPlayerState<ASkaldPlayerState>()) {
      AIState->bIsAI = true;
    }

    AssignControllerSlot(NewAI);
    return NewAI;
  };

  if (!AttackerC && Battle.bAttackerIsAI) {
    AttackerC = SpawnAIController();
  }
  if (!DefenderC && Battle.bDefenderIsAI) {
    DefenderC = SpawnAIController();
  }

  if (!AttackerC || !DefenderC) {
    UE_LOG(LogSkaldBattle, Error,
           TEXT("BattleGM SetupPendingBattle: Unable to resolve participants (From=%d To=%d PendingControllers=%d)"),
           Battle.FromTerritoryID, Battle.TargetTerritoryID,
           GPendingControllers.Num());
    ResetSetupAttempt();
    return;
  }

  {
    TArray<AController *> ControllersToRelocate;
    ControllersToRelocate.Reserve(2);
    if (AttackerC) {
      ControllersToRelocate.AddUnique(AttackerC);
    }
    if (DefenderC) {
      ControllersToRelocate.AddUnique(DefenderC);
    }

    if (ControllersToRelocate.Num() > 0) {
      TMap<AController *, bool> ControllerSides;
      if (AttackerC) {
        ControllerSides.Add(AttackerC, true);
      }
      if (DefenderC) {
        ControllerSides.Add(DefenderC, false);
      }

      RelocateControllersNearBattleGrid(
          ControllersToRelocate,
          ControllerSides.Num() > 0 ? &ControllerSides : nullptr);
    }
  }

  ASkaldPlayerState *AttackerSlotPS =
      AttackerC->GetPlayerState<ASkaldPlayerState>();
  ASkaldPlayerState *DefenderSlotPS =
      DefenderC->GetPlayerState<ASkaldPlayerState>();

  auto ResolveParticipant = [&](int32 TerritoryId, int32 &InOutPlayerId,
                                FString &InOutDisplayName,
                                ESkaldFaction &InOutFaction,
                                bool &bInOutIsAI,
                                ASkaldPlayerState *SlotPlayerState) {
    if (TerritoryId > 0) {
      if (const FS_Territory *Territory = CachedTerritoryMap.Find(TerritoryId)) {
        if (Territory->OwnerPlayerID > 0) {
          InOutPlayerId = Territory->OwnerPlayerID;
        }
      }
    }

    ASkaldPlayerState *PlayerState =
        (InOutPlayerId > 0) ? GS->GetPlayerById(InOutPlayerId) : nullptr;
    if (!PlayerState && SlotPlayerState) {
      InOutPlayerId = ResolveBattlePlayerId(SlotPlayerState);
      PlayerState = SlotPlayerState;
    }
    if (!PlayerState) {
      return;
    }

    if (InOutDisplayName.IsEmpty()) {
      InOutDisplayName = PlayerState->GetResolvedPlayerName(
          TEXT("BattleGM.SetupPendingBattle"));
    }
    if (InOutFaction == ESkaldFaction::None) {
      InOutFaction = PlayerState->Faction;
    }
    bInOutIsAI = PlayerState->bIsAI;
  };

  ResolveParticipant(Battle.FromTerritoryID, Battle.AttackerPlayerID,
                     Battle.AttackerDisplayName, Battle.AttackerFaction,
                     Battle.bAttackerIsAI, AttackerSlotPS);
  ResolveParticipant(Battle.TargetTerritoryID, Battle.DefenderPlayerID,
                     Battle.DefenderDisplayName, Battle.DefenderFaction,
                     Battle.bDefenderIsAI, DefenderSlotPS);

  if (Battle.AttackerPlayerID <= 0 || Battle.DefenderPlayerID <= 0) {
    UE_LOG(LogSkaldBattle, Error,
           TEXT("BattleGM SetupPendingBattle: Unable to resolve participants (AttackerId=%d DefenderId=%d Territories=%d/%d)"),
           Battle.AttackerPlayerID, Battle.DefenderPlayerID,
           Battle.FromTerritoryID, Battle.TargetTerritoryID);
    ResetSetupAttempt();
    return;
  }

  const int32 AttackerBudget = FMath::Max(0, Battle.ArmyCountSent);
  int32 DefenderBudget = Battle.DefenderArmyCount;
  const FS_Territory *DefSnapshot =
      CachedTerritoryMap.Find(Battle.TargetTerritoryID);
  if (DefenderBudget <= 0 && DefSnapshot) {
    DefenderBudget = DefSnapshot->ArmyUnits;
  }
  if (Battle.DefenderTerritoryName.IsEmpty() && DefSnapshot) {
    Battle.DefenderTerritoryName = DefSnapshot->TerritoryName;
  }
  if (DefenderBudget <= 0) {
    DefenderBudget = AttackerBudget;
  }
  Battle.DefenderArmyCount = DefenderBudget;

  UE_LOG(LogSkaldBattle, Log,
         TEXT("BattleGM SetupPendingBattle: AttackerID=%d Name=%s Faction=%d Budget=%d DefenderID=%d Name=%s Faction=%d Budget=%d"),
         Battle.AttackerPlayerID, *Battle.AttackerDisplayName,
         static_cast<int32>(Battle.AttackerFaction), AttackerBudget,
         Battle.DefenderPlayerID, *Battle.DefenderDisplayName,
         static_cast<int32>(Battle.DefenderFaction), DefenderBudget);

  ASkaldPlayerState *AttackerPS = EnsureBattleParticipant(
      GS, World, Battle.AttackerPlayerID, Battle.AttackerDisplayName,
      Battle.AttackerFaction, Battle.bAttackerIsAI);
  ASkaldPlayerState *DefenderPS = EnsureBattleParticipant(
      GS, World, Battle.DefenderPlayerID, Battle.DefenderDisplayName,
      Battle.DefenderFaction, Battle.bDefenderIsAI);

  const auto ResolveParticipantAIFlag =
      [this](ASkaldPlayerState *PlayerState, AController *Controller,
             bool &bInOutPayloadFlag) {
        if (!PlayerState && !Controller) {
          return;
        }

        bool bResolvedIsAI = bInOutPayloadFlag;
        if (Controller) {
          const bool bControllerIsAI =
              AIControllerClass && Controller->IsA(AIControllerClass);
          bResolvedIsAI = bControllerIsAI;
        } else if (PlayerState) {
          bResolvedIsAI = PlayerState->bIsAI;
        }

        if (PlayerState) {
          PlayerState->bIsAI = bResolvedIsAI;
        }

        bInOutPayloadFlag = bResolvedIsAI;
      };

  ResolveParticipantAIFlag(AttackerPS, AttackerC, Battle.bAttackerIsAI);
  ResolveParticipantAIFlag(DefenderPS, DefenderC, Battle.bDefenderIsAI);

  if (!AttackerPS || !DefenderPS) {
    UE_LOG(LogSkaldBattle, Error,
           TEXT("BattleGM SetupPendingBattle: Failed to ensure participants (AttackerValid=%s DefenderValid=%s)"),
           AttackerPS ? TEXT("true") : TEXT("false"),
           DefenderPS ? TEXT("true") : TEXT("false"));
    ResetSetupAttempt();
    return;
  }

  LockedInPlayers.Reset();

  BeginPreBattleSelection(AttackerPS, DefenderPS, AttackerBudget, DefenderBudget);
  UE_LOG(LogSkaldBattle, Log,
         TEXT("BattleGM: BeginPreBattleSelection started (AttackerID=%d DefenderID=%d Budgets=%d/%d)"),
         Battle.AttackerPlayerID, Battle.DefenderPlayerID, AttackerBudget,
         DefenderBudget);

  UE_LOG(LogSkaldBattle, Log,
         TEXT("BattleGM: Battle phase advancing to FighterSelection (NetMode=%d)"),
         static_cast<int32>(GetNetMode()));

  if (ASkaldGameState *SyncGS = GetGameState<ASkaldGameState>()) {
    SyncGS->SetActiveBattlePayload(Battle);
  }

  SyncBattlePlayerEntry(AttackerPS);
  SyncBattlePlayerEntry(DefenderPS);

  AutoCommitAIArmy(AttackerPS, AttackerPS->bIsAI ? AttackerBudget : 0);
  AutoCommitAIArmy(DefenderPS, DefenderPS->bIsAI ? DefenderBudget : 0);

  if (AttackerPS && AttackerPS->bIsAI && AttackerPS->bArmyLockedIn) {
    RegisterPlayerLockIn(ResolveBattlePlayerId(AttackerPS));
  }
  if (DefenderPS && DefenderPS->bIsAI && DefenderPS->bArmyLockedIn) {
    RegisterPlayerLockIn(ResolveBattlePlayerId(DefenderPS));
  }

  bSetupCompleted = true;
  LastCompletedSetupSignature = SetupSignature;
  ActiveSetupSignature = 0;
  GI->PendingBattle = Battle;

  TryAdvanceAfterLockIn();

  UE_LOG(LogSkaldBattle, Log,
         TEXT("SetupPendingBattle completed. Awaiting lock-in (AttackerAI=%d DefenderAI=%d)"),
         AttackerPS->bIsAI ? 1 : 0, DefenderPS->bIsAI ? 1 : 0);

  TryStartBattle();
}

bool ASkald_BattleGameMode::IsSoloMatch() const {
  // Solo if only the human travels; AI is created inside SetupPendingBattle
  return ExpectedControllers <= 1;
}

void ASkald_BattleGameMode::PollBattleBootstrap() {
  if (!HasAuthority()) {
    return;
  }

  UWorld *World = GetWorld();
  if (!World) {
    return;
  }

  // In standalone play we need to make sure the expected attacker/defender
  // controllers exist on the battle map before we count them. Otherwise the
  // bootstrap waits forever (no fighter selection widget, no battle start)
  // because only the human controller travelled from the overworld.
  EnsureBattleControllers();

  int32 ControllerCount = 0;
  for (FConstControllerIterator It = World->GetControllerIterator(); It; ++It) {
    if (It->Get()) {
      ++ControllerCount;
    }
  }

  const int32 PlayerStateCount = (GameState && GameState->PlayerArray.IsValidIndex(0))
                                     ? GameState->PlayerArray.Num()
                                     : 0;

  UE_LOG(LogSkaldBattle, Verbose,
         TEXT("PollBootstrap: Controllers=%d PlayerStates=%d SetupStarted=%d"),
         ControllerCount, PlayerStateCount, bSetupStarted ? 1 : 0);

  const bool bSolo = IsSoloMatch();

  const bool CanSetup = bSolo
                            ? (ControllerCount >= 1 && PlayerStateCount >= 1)
                            : (ControllerCount >= ExpectedControllers &&
                               PlayerStateCount >= ExpectedControllers);

  if (!bSetupStarted && CanSetup) {
    UE_LOG(LogSkaldBattle, Log, TEXT("Starting SetupPendingBattle (bSolo=%d)"),
           bSolo ? 1 : 0);
    bSetupStarted = true;

    SetupPendingBattle();
  }

  if (bSetupCompleted) {
    UE_LOG(LogSkaldBattle, Log,
           TEXT("Setup completed; clearing bootstrap timer."));
    GetWorldTimerManager().ClearTimer(WaitForPlayersHandle);
  }
}

bool ASkald_BattleGameMode::RelocateControllersNearBattleGrid(
    const TArray<AController *> &Controllers,
    const TMap<AController *, bool> *ControllerSides) const {
  if (Controllers.Num() == 0) {
    return false;
  }

  UWorld *World = GetWorld();
  if (!World) {
    return false;
  }

  UGridOverlayComponent *Grid = Skald::GridOverlay::FindActiveGridOverlay(World);

  FVector BaseLocation = FVector::ZeroVector;
  bool bHasLocation = false;

  float HighestSurfaceZ = BaseLocation.Z;

  if (Grid) {
    const int32 Width = Grid->GetWidth();
    const int32 Length = Grid->GetLength();
    const float CellSize = Grid->GetCellSize();

    BaseLocation = Grid->GetOrigin();
    HighestSurfaceZ = FMath::Max(HighestSurfaceZ, BaseLocation.Z);

    if (Width > 0 && Length > 0 && CellSize > KINDA_SMALL_NUMBER) {
      BaseLocation.X += (static_cast<float>(Width) * CellSize) * 0.5f;
      BaseLocation.Y += (static_cast<float>(Length) * CellSize) * 0.5f;
      bHasLocation = true;

      for (int32 Y = 0; Y < Length; ++Y) {
        for (int32 X = 0; X < Width; ++X) {
          const float CellHeight = Grid->GetCellHeight(FIntPoint(X, Y));
          HighestSurfaceZ = FMath::Max(HighestSurfaceZ, CellHeight);
        }
      }
    } else if (AActor *OwnerActor = Grid->GetOwner()) {
      BaseLocation = OwnerActor->GetActorLocation();
      HighestSurfaceZ = FMath::Max(HighestSurfaceZ, BaseLocation.Z);
      bHasLocation = true;
    }
  }

  if (!bHasLocation && Grid && Grid->GetOwner()) {
    BaseLocation = Grid->GetOwner()->GetActorLocation();
    HighestSurfaceZ = FMath::Max(HighestSurfaceZ, BaseLocation.Z);
    bHasLocation = true;
  }

  if (!bHasLocation) {
    for (AController *Controller : Controllers) {
      if (Controller) {
        if (APawn *Pawn = Controller->GetPawn()) {
          BaseLocation = Pawn->GetActorLocation();
          HighestSurfaceZ = FMath::Max(HighestSurfaceZ, BaseLocation.Z);
          bHasLocation = true;
          break;
        }
      }
    }
  }

  if (!bHasLocation) {
    UE_LOG(LogSkaldBattle, Verbose,
           TEXT("RelocateControllersNearBattleGrid: Unable to determine base location"));
    return false;
  }

  if (World) {
    for (TActorIterator<AGridObstacleActor> It(World); It; ++It) {
      const FBox Bounds = Skald::GridObstacle::CalculateRelevantBounds(*It);
      HighestSurfaceZ = FMath::Max(HighestSurfaceZ, Bounds.Max.Z);
    }
  }

  BaseLocation.Z = HighestSurfaceZ + BattleSpawnHeightPadding;

  FVector AttackerAnchor = BaseLocation;
  FVector DefenderAnchor = BaseLocation;

  if (Grid) {
    const int32 Width = Grid->GetWidth();
    const int32 Length = Grid->GetLength();

    if (Width > 0 && Length > 0) {
      const int32 MaxX = FMath::Max(Width - 1, 0);
      const int32 MaxY = FMath::Max(Length - 1, 0);
      const int32 EffectiveEdge = FMath::Clamp(BattleSpawnEdgeColumns, 1, Width);

      const int32 AttackerMinX = 0;
      const int32 AttackerMaxX = FMath::Clamp(EffectiveEdge - 1, 0, MaxX);
      const int32 DefenderMinX = FMath::Clamp(Width - EffectiveEdge, 0, MaxX);
      const int32 DefenderMaxX = MaxX;
      const int32 CenterY = FMath::Clamp(Length / 2, 0, MaxY);

      const FIntPoint AttackerCell((AttackerMinX + AttackerMaxX) / 2, CenterY);
      const FIntPoint DefenderCell((DefenderMinX + DefenderMaxX) / 2, CenterY);

      AttackerAnchor = Grid->GridToWorld(AttackerCell);
      DefenderAnchor = Grid->GridToWorld(DefenderCell);

      AttackerAnchor.Z = BaseLocation.Z;
      DefenderAnchor.Z = BaseLocation.Z;
    }
  }

  FVector AttackDirection = DefenderAnchor - AttackerAnchor;
  AttackDirection.Z = 0.f;
  if (AttackDirection.IsNearlyZero()) {
    AttackDirection = FVector::ForwardVector;
  }
  AttackDirection.Normalize();

  FVector DefenderDirection = -AttackDirection;
  if (DefenderDirection.IsNearlyZero()) {
    DefenderDirection = FVector::BackwardVector;
  }

  const FRotator AttackerRotation = AttackDirection.Rotation();
  const FRotator DefenderRotation = DefenderDirection.Rotation();
  const float YSpacing = 150.f;

  TArray<AController *> AttackerControllers;
  TArray<AController *> DefenderControllers;
  TArray<AController *> NeutralControllers;

  for (AController *Controller : Controllers) {
    if (!Controller) {
      continue;
    }

    if (ControllerSides) {
      if (const bool *bIsAttacker = ControllerSides->Find(Controller)) {
        (bIsAttacker && *bIsAttacker ? AttackerControllers : DefenderControllers)
            .Add(Controller);
        continue;
      }
    }

    NeutralControllers.Add(Controller);
  }

  const auto PlaceControllers = [&](const TArray<AController *> &Group,
                                    const FVector &Anchor,
                                    const FRotator &Facing) {
    if (Group.Num() == 0) {
      return;
    }

    const FRotator FlatFacing(0.f, Facing.Yaw, 0.f);
    FVector RightVector = FRotationMatrix(FlatFacing).GetScaledAxis(EAxis::Y);
    if (RightVector.IsNearlyZero()) {
      RightVector = FVector::YAxisVector;
    }
    RightVector.Normalize();

    for (int32 Index = 0; Index < Group.Num(); ++Index) {
      AController *Controller = Group[Index];
      if (!Controller) {
        continue;
      }

      APawn *Pawn = Controller->GetPawn();
      if (!Pawn) {
        continue;
      }

      FVector TargetLocation = Anchor;
      if (Group.Num() > 1) {
        const float OffsetIndex = static_cast<float>(Index) -
                                  0.5f * static_cast<float>(Group.Num() - 1);
        TargetLocation += RightVector * (OffsetIndex * YSpacing);
      }

      FRotator TargetRotation = FlatFacing;

      Pawn->SetActorLocationAndRotation(TargetLocation, TargetRotation, false,
                                        nullptr, ETeleportType::TeleportPhysics);
    }
  };

  PlaceControllers(AttackerControllers, AttackerAnchor, AttackerRotation);
  PlaceControllers(DefenderControllers, DefenderAnchor, DefenderRotation);
  PlaceControllers(NeutralControllers, BaseLocation, AttackerRotation);

  return true;
}

void ASkald_BattleGameMode::AutoCommitAIArmy(ASkaldPlayerState *PlayerState,
                                             int32 Budget) const {
  if (!PlayerState || !PlayerState->bIsAI || !BattleManager) {
    return;
  }

  UE_LOG(LogSkald, Log,
         TEXT("BattleGM AutoCommitAIArmy: PlayerId=%d Budget=%d"),
         ResolveBattlePlayerId(PlayerState), Budget);

  TArray<FFighterDefinition> Definitions =
      BattleManager->GetFightersForFaction(PlayerState->Faction);
  if (Definitions.Num() == 0) {
    PlayerState->PendingArmy.Reset();
    PlayerState->bArmyLockedIn = true;
    PlayerState->PendingArmyBudget = Budget;
    return;
  }

  Algo::RandomShuffle(Definitions);

  TArray<FFighterDefinition> Selection;
  int32 CurrentCost = 0;
  for (const FFighterDefinition &Def : Definitions) {
    const int32 Cost = FMath::Max(Def.Stats.ArmyCost, 0);
    if (Cost > 0 && Budget >= 0 && CurrentCost + Cost > Budget) {
      continue;
    }

    Selection.Add(Def);
    if (Cost > 0) {
      CurrentCost += Cost;
    }

    if (Budget > 0 && CurrentCost >= Budget) {
      break;
    }
  }

  PlayerState->PendingArmy = Selection;
  PlayerState->PendingArmyBudget = Budget;
  PlayerState->bArmyLockedIn = true;

  UE_LOG(LogSkald, Log,
         TEXT("BattleGM AutoCommitAIArmy: Locked %d fighters for PlayerId=%d"),
         PlayerState->PendingArmy.Num(), ResolveBattlePlayerId(PlayerState));
}

void ASkald_BattleGameMode::SpawnFighterSide(const TArray<FFighterDefinition> &Roster,
                                             bool bAsAttacker) {
  if (!BattleManager || !GetWorld()) {
    return;
  }

  USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>();
  if (!GI) {
    return;
  }

  UGridOverlayComponent *Grid =
      Skald::GridOverlay::FindActiveGridOverlay(GetWorld());

  const int32 Edge = BattleSpawnEdgeColumns;
  int32 GridWidth = Grid ? Grid->GetWidth() : UGridBattleManager::GridSize;
  int32 GridLength = Grid ? Grid->GetLength() : UGridBattleManager::GridSize;

  if (GridWidth <= 0) {
    GridWidth = UGridBattleManager::GridSize;
  }

  if (GridLength <= 0) {
    GridLength = UGridBattleManager::GridSize;
  }

  const int32 EffectiveEdge = FMath::Clamp(Edge, 1, GridWidth);

  for (const FFighterDefinition &Def : Roster) {
    FIntPoint Cell = FIntPoint::ZeroValue;

    UClass *DesiredClass = AFighterPawn::StaticClass();
    if (UClass *SelectedClass = Def.MeshClass.Get()) {
      if (SelectedClass->IsChildOf(AFighterPawn::StaticClass())) {
        DesiredClass = SelectedClass;
      } else {
        UE_LOG(LogSkaldBattle, Warning,
               TEXT("SpawnFighterSide: MeshClass %s for fighter %s is not an "
                    "AFighterPawn; falling back to default."),
               *GetNameSafe(SelectedClass), *Def.Id.ToString());
      }
    }

    const AFighterPawn *DefaultPawn =
        DesiredClass->GetDefaultObject<AFighterPawn>();
    const int32 FootprintSideLength =
        DefaultPawn ? FMath::Max(DefaultPawn->GetFootprintSideLength(), 1) : 1;
    const int32 MaxAnchorX = FMath::Max(0, GridWidth - FootprintSideLength);
    const int32 MaxAnchorY = FMath::Max(0, GridLength - FootprintSideLength);

    const int32 MinSpawnX = bAsAttacker ? 0
                                        : FMath::Clamp(GridWidth - EffectiveEdge, 0, MaxAnchorX);
    const int32 MaxSpawnX = bAsAttacker
                                ? FMath::Clamp(EffectiveEdge - 1, 0, MaxAnchorX)
                                : MaxAnchorX;
    const int32 ClampedMaxSpawnX = FMath::Max(MinSpawnX, MaxSpawnX);

    Cell.X = GI->CombatRandomStream.RandRange(MinSpawnX, ClampedMaxSpawnX);
    Cell.Y = GI->CombatRandomStream.RandRange(0, MaxAnchorY);

    FVector TerrainLocation =
        Grid ? Grid->GridToWorld(Cell) : FVector::ZeroVector;
    float RequestedHalfHeight = 0.f;
    if (DefaultPawn) {
      RequestedHalfHeight = DefaultPawn->GetSimpleCollisionHalfHeight();

      if (Grid) {
        const TArray<FIntPoint> FootprintCells =
            DefaultPawn->GetOccupiedCells(Cell);
        FVector AccumulatedLocation = FVector::ZeroVector;
        int32 CellCount = 0;
        for (const FIntPoint &Occupied : FootprintCells) {
          AccumulatedLocation += Grid->GridToWorld(Occupied);
          ++CellCount;
        }

        if (CellCount > 0) {
          TerrainLocation = AccumulatedLocation /
                            static_cast<float>(CellCount);
        }
      }
    }

    FVector SpawnLoc = TerrainLocation;
    SpawnLoc.Z += RequestedHalfHeight;
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride =
        ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

    AFighterPawn *Pawn = GetWorld()->SpawnActor<AFighterPawn>(
        DesiredClass, SpawnLoc, FRotator::ZeroRotator, Params);
    if (Pawn) {
      const float ActualHalfHeight = Pawn->GetSimpleCollisionHalfHeight();

      if (!FMath::IsNearlyEqual(ActualHalfHeight, RequestedHalfHeight,
                                KINDA_SMALL_NUMBER)) {
        FVector AdjustedLocation = TerrainLocation;
        AdjustedLocation.Z += ActualHalfHeight;

        FHitResult HitResult;
        Pawn->SetActorLocation(AdjustedLocation, /*bSweep*/ true, &HitResult);
      }
      Pawn->Stats = Def.Stats;
      Pawn->InitializeMaxHealth(Def.Stats.Health);
      Pawn->FighterId = Def.Id;
      Pawn->FighterPortrait = Def.Portrait;
      Pawn->Faction = Def.Faction;
      Pawn->SetAttackType(Def.AttackType);
      Pawn->AttackFX = Def.AttackFX;
      Pawn->bIsAttacker = bAsAttacker;

      Pawn->UpdateAbilityLoadout();
      BattleManager->RegisterFighter(Pawn, bAsAttacker);
    }
  }
}

void ASkald_BattleGameMode::TryLaunchBattle() {
  if (bBattleLaunched || !HasAuthority()) {
    return;
  }

  USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>();
  if (!GI || !GI->GridBattleManager) {
    return;
  }

  ASkaldGameState *GS = GetGameState<ASkaldGameState>();
  if (!GS) {
    return;
  }

  UWorld *World = GetWorld();
  if (!World) {
    return;
  }

  UE_LOG(LogSkald, Verbose, TEXT("BattleGM TryLaunchBattle: evaluating lock-in state"));

  const FS_BattlePayload &Battle = GI->PendingBattle;
  ASkaldPlayerState *AttackerPS = EnsureBattleParticipant(
      GS, World, Battle.AttackerPlayerID, Battle.AttackerDisplayName,
      Battle.AttackerFaction, Battle.bAttackerIsAI);
  ASkaldPlayerState *DefenderPS = EnsureBattleParticipant(
      GS, World, Battle.DefenderPlayerID, Battle.DefenderDisplayName,
      Battle.DefenderFaction, Battle.bDefenderIsAI);

  if (!AttackerPS || !DefenderPS) {
    UE_LOG(LogSkald, Error,
           TEXT("BattleGM TryLaunchBattle: Missing participant PlayerStates (AttackerValid=%s DefenderValid=%s)"),
           AttackerPS ? TEXT("true") : TEXT("false"),
           DefenderPS ? TEXT("true") : TEXT("false"));
    return;
  }

  if ((AttackerPS && !AttackerPS->bArmyLockedIn) ||
      (DefenderPS && !DefenderPS->bArmyLockedIn)) {
    UE_LOG(LogSkald, Verbose,
           TEXT("BattleGM TryLaunchBattle: waiting for lock-in (AttackerLocked=%s DefenderLocked=%s)"),
           AttackerPS && AttackerPS->bArmyLockedIn ? TEXT("true") : TEXT("false"),
           DefenderPS && DefenderPS->bArmyLockedIn ? TEXT("true") : TEXT("false"));
    return;
  }

  if (!AreBothParticipantsLocked()) {
    UE_LOG(LogSkaldBattle, Verbose,
           TEXT("BattleGM TryLaunchBattle: waiting for participants to confirm lock-in"));
    return;
  }

  TArray<AController *> ControllersToRelocate;
  TMap<AController *, bool> ControllerSides;
  if (AttackerPS) {
    if (AController *OwnerController =
            Cast<AController>(AttackerPS->GetOwner())) {
      ControllersToRelocate.AddUnique(OwnerController);
      ControllerSides.Add(OwnerController, true);
    }
  }
  if (DefenderPS) {
    if (AController *OwnerController =
            Cast<AController>(DefenderPS->GetOwner())) {
      ControllersToRelocate.AddUnique(OwnerController);
      ControllerSides.Add(OwnerController, false);
    }
  }

  if (ControllersToRelocate.Num() > 0) {
    RelocateControllersNearBattleGrid(
        ControllersToRelocate,
        ControllerSides.Num() > 0 ? &ControllerSides : nullptr);
  }

  TArray<FFighterDefinition> AttackerDefs =
      AttackerPS ? AttackerPS->PendingArmy : TArray<FFighterDefinition>();
  TArray<FFighterDefinition> DefenderDefs =
      DefenderPS ? DefenderPS->PendingArmy : TArray<FFighterDefinition>();

  TArray<FFighter> Attackers;
  Attackers.Reserve(AttackerDefs.Num());
  for (const FFighterDefinition &Def : AttackerDefs) {
    FFighter Fighter;
    Fighter.Stats = Def.Stats;
    Fighter.Faction = AttackerPS ? AttackerPS->Faction : Def.Faction;
    Fighter.AttackType = Def.AttackType;
    Attackers.Add(Fighter);
  }

  TArray<FFighter> Defenders;
  Defenders.Reserve(DefenderDefs.Num());
  for (const FFighterDefinition &Def : DefenderDefs) {
    FFighter Fighter;
    Fighter.Stats = Def.Stats;
    Fighter.Faction = DefenderPS ? DefenderPS->Faction : Def.Faction;
    Fighter.AttackType = Def.AttackType;
    Defenders.Add(Fighter);
  }

  UE_LOG(LogSkald, Log,
         TEXT("BattleGM TryLaunchBattle: Launching battle (Attackers=%d Defenders=%d)"),
         AttackerDefs.Num(), DefenderDefs.Num());

  GI->GridBattleManager->InitBattle(Attackers, Defenders);

  SpawnFighterSide(AttackerDefs, /*bAsAttacker=*/true);
  SpawnFighterSide(DefenderDefs, /*bAsAttacker=*/false);

  GI->GridBattleManager->StartRound();

  UE_LOG(LogSkald, Log, TEXT("BattleGM TryLaunchBattle: Battle initialised and round started"));

  if (AttackerPS) {
    AttackerPS->bArmyLockedIn = false;
    AttackerPS->PendingArmy.Reset();
    AttackerPS->PendingArmyBudget = 0;
  }
  if (DefenderPS) {
    DefenderPS->bArmyLockedIn = false;
    DefenderPS->PendingArmy.Reset();
    DefenderPS->PendingArmyBudget = 0;
  }

  bBattleLaunched = true;
  LockedInPlayers.Reset();
}

void ASkald_BattleGameMode::TryInitializeWorldAndStart() {
  bWorldInitialized = true;
  bTurnsStarted = true;
  GetWorldTimerManager().ClearTimer(RetryInitTimerHandle);
}

void ASkald_BattleGameMode::StartBootstrapTimer()
{
  if (!HasAuthority())
  {
    return;
  }

  if (!GetWorldTimerManager().IsTimerActive(WaitForPlayersHandle))
  {
    GetWorldTimerManager().SetTimer(
        WaitForPlayersHandle, this, &ASkald_BattleGameMode::PollBattleBootstrap, 0.25f, true);
  }

  if (HasActorBegunPlay())
  {
    PollBattleBootstrap();
  }
}

void ASkald_BattleGameMode::HandleStartingNewPlayer_Implementation(
    APlayerController *NewPlayer) {
  UE_LOG(LogSkaldBattle, Log,
         TEXT("HandleStartingNewPlayer: %s (HasAuthority=%d)"),
         *GetNameSafe(NewPlayer), HasAuthority() ? 1 : 0);

  Super::HandleStartingNewPlayer_Implementation(NewPlayer);

  if (!HasAuthority()) {
    return;
  }

  if (!IsValid(NewPlayer)) {
    return;
  }

  if (ASkaldPlayerState *PS = NewPlayer->GetPlayerState<ASkaldPlayerState>()) {
    SyncBattlePlayerEntry(PS);
    RegisterParticipantController(NewPlayer);
    StartBootstrapTimer();
    return;
  }

  if (UWorld *World = GetWorld()) {
    FTimerDelegate RetryDelegate = FTimerDelegate::CreateWeakLambda(
        this, [this, WeakPC = TWeakObjectPtr<APlayerController>(NewPlayer)]() {
          if (!WeakPC.IsValid()) {
            return;
          }

          if (ASkaldPlayerState *RetryPS =
                  WeakPC->GetPlayerState<ASkaldPlayerState>()) {
            SyncBattlePlayerEntry(RetryPS);
            StartBootstrapTimer();
          }
        });

    World->GetTimerManager().SetTimerForNextTick(RetryDelegate);
  }
}

void ASkald_BattleGameMode::HandleSeamlessTravelPlayer(AController *&C) {
  UE_LOG(LogSkaldBattle, Log,
         TEXT("HandleSeamlessTravelPlayer: %s (HasAuthority=%d)"),
         *GetNameSafe(C), HasAuthority() ? 1 : 0);

  Super::HandleSeamlessTravelPlayer(C);

  if (!HasAuthority()) {
    return;
  }

  if (AController *Controller = C) {
    if (ASkaldPlayerState *PS = Controller->GetPlayerState<ASkaldPlayerState>()) {
      SyncBattlePlayerEntry(PS);
      RegisterParticipantController(Controller);
    }
    OnControllerReady(Controller);
  }
}

void ASkald_BattleGameMode::PostLogin(APlayerController *NewPlayer) {
  Super::PostLogin(NewPlayer);

  UE_LOG(LogSkaldBattle, Log, TEXT("PostLogin: %s"), *GetNameSafe(NewPlayer));
  if (ASkaldPlayerState *PS = NewPlayer->GetPlayerState<ASkaldPlayerState>()) {
    SyncBattlePlayerEntry(PS);
    RegisterParticipantController(NewPlayer);
  }
  StartBootstrapTimer();
  OnControllerReady(NewPlayer);
}

void ASkald_BattleGameMode::OnAIControllerReady(ASkaldAIController *Controller) {
  OnControllerReady(Controller);
}

bool ASkald_BattleGameMode::TrySetupBattleWhenReady() {
  if (GBattleSetupTriggered) {
    return false;
  }

  CompactSlots();

  TArray<AController *> Valid;
  Valid.Reserve(GPendingControllers.Num());
  for (const auto &S : GPendingControllers) {
    if (AController *C = S.Controller.Get()) {
      Valid.Add(C);
    }
  }

  int32 RequiredControllers = 2;
  if (ExpectedControllers <= 1) {
    // Solo overworld attacks can be resolved through the streaming flow while
    // only one controller is present. The defender AI is spawned during setup,
    // so allow the bootstrap to proceed once the travelling player has been
    // registered.
    RequiredControllers = 1;
  }

  if (Valid.Num() < RequiredControllers) {
    return false;
  }

  GBattleSetupTriggered = true;

  FString Summary;
  for (int32 i = 0; i < GPendingControllers.Num(); ++i) {
    const auto &S = GPendingControllers[i];
    if (AController *C = S.Controller.Get()) {
      Summary += FString::Printf(TEXT(" [%d:%s AI=%d Pid=%d]"), i,
                                 *GetNameSafe(C), S.bIsAI ? 1 : 0, S.PlayerId);
    }
  }
  UE_LOG(LogSkaldBattle, Log,
         TEXT("BattleGM: controllers ready (%d)%s"), Valid.Num(), *Summary);

  SetupPendingBattle();
  return true;
}

void ASkald_BattleGameMode::OnControllerReady(AController *Controller) {
  UE_LOG(LogSkaldBattle, Log, TEXT("OnControllerReady: %s  HasAuthority=%d"),
         *GetNameSafe(Controller), HasAuthority() ? 1 : 0);

  if (!IsValid(Controller)) {
    return;
  }

  if (bEnsuringBattleControllers) {
    DeferredReadyControllers.AddUnique(Controller);
    return;
  }

  if (!HasAuthority()) {
    return;
  }

  // Ensure controllers that survived seamless travel have a valid pawn they
  // actually own. If a controller is missing its pawn (or is holding onto a
  // pawn possessed by someone else) the resulting camera replication appears
  // to jump between players. Restarting the player guarantees a fresh pawn
  // that is owned and driven locally on each connection.
  APawn *ExistingPawn = Controller->GetPawn();
  const bool bPawnOwnedByController =
      ExistingPawn && ExistingPawn->GetController() == Controller;

  if (!ExistingPawn || !bPawnOwnedByController) {
    UE_LOG(LogSkaldBattle, Log,
           TEXT("OnControllerReady: Respawning pawn for %s (Pawn=%s OwnedBySelf=%d)"),
           *GetNameSafe(Controller), *GetNameSafe(ExistingPawn),
           bPawnOwnedByController ? 1 : 0);
    RestartPlayer(Controller);
    ExistingPawn = Controller->GetPawn();
  }

  if (APlayerController *PC = Cast<APlayerController>(Controller)) {
    FString CameraSummary(TEXT("<none>"));
    if (ExistingPawn) {
      if (const AActor *CamOwner = PC->GetViewTarget()) {
        CameraSummary = CamOwner->GetName();
      } else {
        CameraSummary = ExistingPawn->GetName();
      }
    }
    UE_LOG(LogSkaldBattle, Log,
           TEXT("OnControllerReady: %s owns pawn %s view=%s"),
           *GetNameSafe(PC), *GetNameSafe(ExistingPawn), *CameraSummary);
  }

  if (Controller && !Controller->PlayerState) {
    Controller->InitPlayerState();
  }

  if (!ControllerHasStablePlayerId(Controller)) {
    QueueDeferredController(Controller);
    return;
  }

  if (ASkaldPlayerState *PS = Controller->GetPlayerState<ASkaldPlayerState>()) {
    if (IsRegisteredBattleParticipant(PS)) {
      PS->bIsActiveBattlePlayer = true;
      UE_LOG(LogSkaldBattle, Log,
             TEXT("OnControllerReady: participant confirmed for %s (Id=%d)"),
             *GetNameSafe(Controller), ResolveBattlePlayerId(PS));
    }
    if (ResolveBattlePlayerId(PS) > 0) {
      SyncBattlePlayerEntry(PS);
    }
  }

  AssignControllerSlot(Controller);

  StartBootstrapTimer();

  if (Controller) {
    TArray<AController *> SingleController;
    SingleController.Add(Controller);
    RelocateControllersNearBattleGrid(SingleController);
  }

  // Push the pending battle state to newly-arrived clients so their UI binds
  // to the correct PlayerState and payload before any selection widgets
  // initialise.
  if (ASkaldPlayerController *SkaldPC = Cast<ASkaldPlayerController>(Controller)) {
    if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
      SkaldPC->ClientApplyPendingBattleState(GI->PendingBattle,
                                             GI->GetTravelState(), GI->bIsInBattleMap);
      UE_LOG(LogSkaldBattle, Log,
             TEXT("OnControllerReady: Sent pending battle payload to %s (BattleValid=%d TravelValid=%d)"),
             *GetNameSafe(SkaldPC), GI->PendingBattle.FromTerritoryID > 0 ? 1 : 0,
             GI->GetTravelState().bValid ? 1 : 0);
    }
  }

  TrySetupBattleWhenReady();

  TryStartBattle();
}

void ASkald_BattleGameMode::TryStartBattle() {
  if (bBattleLaunched) {
    return;
  }

  if (!bSetupCompleted) {
    if (HasAuthority()) {
      PollBattleBootstrap();
    }
    if (!bSetupCompleted) {
      UE_LOG(LogSkaldBattle, Verbose,
             TEXT("BattleGM TryStartBattle: pending setup incomplete (SetupStarted=%d ExpectedControllers=%d)"),
             bSetupStarted ? 1 : 0, ExpectedControllers);
      return;
    }
  }

  USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>();
  if (!GI || !GI->GridBattleManager) {
    return;
  }

  ASkaldGameState *GS = GetGameState<ASkaldGameState>();
  if (!GS) {
    return;
  }

  const FS_BattlePayload &Battle = GI->PendingBattle;

  ASkaldPlayerState *AttackerPS =
      GS->GetPlayerById(Battle.AttackerPlayerID);
  ASkaldPlayerState *DefenderPS =
      GS->GetPlayerById(Battle.DefenderPlayerID);

  if (!AttackerPS || !DefenderPS) {
    UE_LOG(LogSkaldBattle, Verbose,
           TEXT("BattleGM TryStartBattle: waiting for participant PlayerStates (AttackerId=%d Status=%s DefenderId=%d Status=%s)"),
           Battle.AttackerPlayerID, AttackerPS ? TEXT("ready") : TEXT("missing"),
           Battle.DefenderPlayerID, DefenderPS ? TEXT("ready") : TEXT("missing"));
    return;
  }

  if (!AttackerPS->bArmyLockedIn || !DefenderPS->bArmyLockedIn) {
    UE_LOG(LogSkaldBattle, Verbose,
           TEXT("BattleGM TryStartBattle: waiting for armies lock-in (Attacker=%s Defender=%s)"),
           AttackerPS->bArmyLockedIn ? TEXT("locked") : TEXT("pending"),
           DefenderPS->bArmyLockedIn ? TEXT("locked") : TEXT("pending"));
    return;
  }

  TryLaunchBattle();
}

void ASkald_BattleGameMode::HandleHumanLockIn(
    ASkaldPlayerController *PC,
    const TArray<FFighterDefinition> &SelectedFighters)
{
  if (!PC) {
    return;
  }

  ASkaldPlayerState *PS = PC->GetPlayerState<ASkaldPlayerState>();
  if (!PS) {
    PC->Client_OnLockInResult(false, TEXT("No PlayerState"));
    return;
  }

  if (PS->bIsAI) {
    PC->Client_OnLockInResult(false, TEXT("AI controllers cannot lock in"));
    return;
  }

  USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>();
  if (!GI) {
    PC->Client_OnLockInResult(false, TEXT("GameInstance unavailable"));
    return;
  }

  const FS_BattlePayload &Battle = GI->PendingBattle;
  const int32 PlayerId = ResolveBattlePlayerId(PS);
  if (PlayerId != Battle.AttackerPlayerID &&
      PlayerId != Battle.DefenderPlayerID) {
    PC->Client_OnLockInResult(false, TEXT("Not part of pending battle"));
    return;
  }

  if (LockedInPlayers.Contains(PlayerId)) {
    PC->Client_OnLockInResult(true, TEXT("Already locked"));
    return;
  }

  FString FailureReason;
  if (!ValidateAndRecordSelection(PS, SelectedFighters, FailureReason)) {
    UE_LOG(LogSkaldBattle, Warning,
           TEXT("HandleHumanLockIn: validation failed for PlayerId=%d (%s)"),
           PlayerId, *FailureReason);
    LogParticipantLockState(TEXT("HandleHumanLockIn (validation failed)"));
    PC->Client_OnLockInResult(false, FailureReason);
    return;
  }

  RegisterPlayerLockIn(PlayerId);
  UE_LOG(LogSkaldBattle, Log,
         TEXT("HandleHumanLockIn: PlayerId=%d locked selection"), PlayerId);
  LogParticipantLockState(TEXT("HandleHumanLockIn (post-commit)"));
  PC->Client_OnLockInResult(true, TEXT("Committed"));

  TryAdvanceAfterLockIn();
}

void ASkald_BattleGameMode::RegisterPlayerLockIn(int32 PlayerId)
{
  if (PlayerId <= 0)
  {
    return;
  }

  if (LockedInPlayers.Contains(PlayerId))
  {
    UE_LOG(LogSkaldBattle, Verbose,
           TEXT("RegisterPlayerLockIn: PlayerId=%d already locked; duplicate notice for ControlChannel tracing."),
           PlayerId);
    return;
  }

  LockedInPlayers.Add(PlayerId);
  UE_LOG(LogSkaldBattle, Log,
         TEXT("RegisterPlayerLockIn: PlayerId=%d locked (Total=%d)"), PlayerId,
         LockedInPlayers.Num());
}

bool ASkald_BattleGameMode::AreBothParticipantsLocked() const
{
  const USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>();
  if (!GI) {
    return false;
  }

  const FS_BattlePayload &Battle = GI->PendingBattle;
  const ASkaldGameState *GS = GetGameState<ASkaldGameState>();

  auto IsPlayerLocked = [&](int32 PlayerId) {
    if (PlayerId <= 0) {
      return false;
    }

    if (GS) {
      if (const ASkaldPlayerState *PS = GS->GetPlayerById(PlayerId)) {
        return PS->bArmyLockedIn;
      }
    }

    return LockedInPlayers.Contains(PlayerId);
  };

  const bool bAttackerLocked = IsPlayerLocked(Battle.AttackerPlayerID);
  const bool bDefenderLocked = IsPlayerLocked(Battle.DefenderPlayerID);

  return bAttackerLocked && bDefenderLocked;
}

void ASkald_BattleGameMode::TryAdvanceAfterLockIn()
{
  auto AutoLockAIIfNeeded = [this](ASkaldPlayerState* PlayerPS)
  {
    if (!PlayerPS || !PlayerPS->bIsAI || PlayerPS->bArmyLockedIn)
    {
      return;
    }

    AutoCommitAIArmy(PlayerPS, PlayerPS->PendingArmyBudget);

    if (PlayerPS->bArmyLockedIn)
    {
      RegisterPlayerLockIn(ResolveBattlePlayerId(PlayerPS));
    }
  };

  if (const ASkaldGameState* GS = GetGameState<ASkaldGameState>())
  {
    const FS_BattlePayload& Battle = GS->GetActiveBattlePayload();

    AutoLockAIIfNeeded(GS->GetPlayerById(Battle.AttackerPlayerID));
    AutoLockAIIfNeeded(GS->GetPlayerById(Battle.DefenderPlayerID));
  }

  LogParticipantLockState(TEXT("TryAdvanceAfterLockIn"));

  if (!AreBothParticipantsLocked()) {
    return;
  }

  if (ASkaldGameState *GS = GetGameState<ASkaldGameState>()) {
    if (GS->BattlePhase != EBattlePhase::Deploy) {
      UE_LOG(LogSkaldBattle, Log,
             TEXT("Both participants locked in -> advancing to Deploy"));
    }
    GS->SetBattlePhase(EBattlePhase::Deploy);
  } else {
    UE_LOG(LogSkaldBattle, Warning,
           TEXT("TryAdvanceAfterLockIn: GameState unavailable"));
  }

  TryStartBattle();
}

bool ASkald_BattleGameMode::ValidateAndRecordSelection(
    ASkaldPlayerState *PlayerState,
    const TArray<FFighterDefinition> &SelectedFighters,
    FString &OutReason)
{
  OutReason.Reset();

  if (!PlayerState) {
    OutReason = TEXT("Invalid player");
    return false;
  }

  if (!BattleManager) {
    OutReason = TEXT("Battle data unavailable");
    return false;
  }

  const int32 Budget = PlayerState->PendingArmyBudget;
  if (SelectedFighters.Num() == 0 && Budget > 0) {
    OutReason = TEXT("No fighters selected");
    return false;
  }

  TArray<FFighterDefinition> Available =
      BattleManager->GetFightersForFaction(PlayerState->Faction);
  TMap<FName, FFighterDefinition> AvailableById;
  AvailableById.Reserve(Available.Num());
  for (const FFighterDefinition &Def : Available) {
    AvailableById.Add(Def.Id, Def);
  }

  TArray<FFighterDefinition> ValidSelection;
  ValidSelection.Reserve(SelectedFighters.Num());
  int32 TotalCost = 0;

  for (const FFighterDefinition &Requested : SelectedFighters) {
    const FName FighterId = Requested.Id;
    if (FighterId.IsNone()) {
      OutReason = TEXT("Invalid fighter in selection");
      break;
    }

    const FFighterDefinition *Canonical = AvailableById.Find(FighterId);
    if (!Canonical) {
      OutReason = TEXT("Invalid fighter in selection");
      break;
    }

    const int32 Cost = FMath::Max(Canonical->Stats.ArmyCost, 0);
    if (Budget >= 0 && TotalCost + Cost > Budget) {
      OutReason = TEXT("Selection exceeds budget");
      break;
    }

    ValidSelection.Add(*Canonical);
    TotalCost += Cost;
  }

  if (!OutReason.IsEmpty()) {
    return false;
  }

  if (Budget >= 0 && TotalCost > Budget) {
    OutReason = TEXT("Selection exceeds budget");
    return false;
  }

  PlayerState->PendingArmy = MoveTemp(ValidSelection);
  PlayerState->bArmyLockedIn = true;

  UE_LOG(LogSkaldBattle, Log,
         TEXT("ValidateAndRecordSelection: Player %d locked in %d fighters (Cost=%d / Budget=%d)"),
         ResolveBattlePlayerId(PlayerState), PlayerState->PendingArmy.Num(), TotalCost,
         Budget);

  return true;
}

void ASkald_BattleGameMode::LogParticipantLockState(const TCHAR *Context)
{
  USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>();
  ASkaldGameState *GS = GetGameState<ASkaldGameState>();

  if (!GI || !GS)
  {
    UE_LOG(LogSkaldBattle, Warning,
           TEXT("LockState[%s]: Missing GameInstance (%s) or GameState (%s)"),
           Context ? Context : TEXT("Unknown"), GI ? TEXT("ok") : TEXT("null"),
           GS ? TEXT("ok") : TEXT("null"));
    return;
  }

  const FS_BattlePayload &Battle = GI->PendingBattle;

  auto DescribeParticipant = [&](int32 PlayerId) -> FString {
    if (PlayerId <= 0)
    {
      return FString::Printf(TEXT("Id=%d (none)"), PlayerId);
    }

    ASkaldPlayerState *PlayerPS = GS->GetPlayerById(PlayerId);
    const bool bLocked = LockedInPlayers.Contains(PlayerId);

    if (!PlayerPS)
    {
      return FString::Printf(TEXT("Id=%d (PlayerState missing) Locked=%s"),
                             PlayerId, bLocked ? TEXT("true") : TEXT("false"));
    }

    return FString::Printf(
        TEXT("Id=%d Name=%s AI=%s LockedSet=%s ArmyLocked=%s ArmySize=%d Budget=%d"),
        PlayerId, *PlayerPS->PlayerDisplayName,
        PlayerPS->bIsAI ? TEXT("true") : TEXT("false"),
        bLocked ? TEXT("true") : TEXT("false"),
        PlayerPS->bArmyLockedIn ? TEXT("true") : TEXT("false"),
        PlayerPS->PendingArmy.Num(), PlayerPS->PendingArmyBudget);
  };

  FString LockedIds;
  for (int32 LockedId : LockedInPlayers)
  {
    LockedIds += LockedIds.IsEmpty() ? TEXT("") : TEXT(",");
    LockedIds += FString::FromInt(LockedId);
  }

  UE_LOG(LogSkaldBattle, Log,
         TEXT("LockState[%s]: Attacker[%s] Defender[%s] LockedInPlayers={%s} Phase=%s"),
         Context ? Context : TEXT("Unknown"),
         *DescribeParticipant(Battle.AttackerPlayerID),
         *DescribeParticipant(Battle.DefenderPlayerID), *LockedIds,
         *UEnum::GetValueAsString(GS->BattlePhase));
}
