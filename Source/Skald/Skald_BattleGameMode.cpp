#include "Skald_BattleGameMode.h"
#include "SkaldLogging.h"

#include "Algo/RandomShuffle.h"
#include "Algo/Sort.h"
#include "AIController.h"
#include "GridBattleManager.h"
#include "Skald.h"
#include "Skald_GameInstance.h"
#include "Skald_GameState.h"
#include "Skald_PlayerController.h"
#include "Skald_PlayerState.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FighterPawn.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GridOverlayComponent.h"
#include "Territory.h"
#include "TimerManager.h"
#include "WorldMap.h"

namespace {
TArray<TWeakObjectPtr<AController>> PendingControllers;
bool bPendingBattleSetupTriggered = false;

void AssignControllerSlot(AController *Controller,
                          const TSubclassOf<APlayerController> &AIControllerClass) {
  PendingControllers.SetNum(2);

  ASkaldPlayerController *PlayerController =
      Controller ? Cast<ASkaldPlayerController>(Controller) : nullptr;
  if (!PlayerController) {
    return;
  }

  bool bIsAI = AIControllerClass && Controller->IsA(AIControllerClass);
  if (!bIsAI) {
    if (ASkaldPlayerState *SlotPS =
            PlayerController->GetPlayerState<ASkaldPlayerState>()) {
      bIsAI = SlotPS->bIsAI;
    }
  }
  const int32 SlotIndex = bIsAI ? 1 : 0;
  PendingControllers[SlotIndex] = Controller;

  UE_LOG(LogSkaldBattle, Log,
         TEXT("BattleGM Normalized controller slot %d: %s"), SlotIndex,
         *GetNameSafe(Controller));
}

bool TrySetupBattleWhenReady() {
  PendingControllers.SetNum(2);

  if (!PendingControllers[0].IsValid() || !PendingControllers[1].IsValid()) {
    return false;
  }

  if (bPendingBattleSetupTriggered) {
    return false;
  }

  bPendingBattleSetupTriggered = true;

  UE_LOG(LogSkaldBattle, Log,
         TEXT("BattleGM TrySetupBattleWhenReady: controllers ready (Human=%s AI=%s)"),
         *GetNameSafe(PendingControllers[0].Get()),
         *GetNameSafe(PendingControllers[1].Get()));
  return true;
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

  if (ASkaldPlayerState *Existing = GameState->GetPlayerById(PlayerID)) {
    if (!DisplayName.IsEmpty()) {
      Existing->PlayerDisplayName = DisplayName;
      Existing->SetPlayerName(DisplayName);
    } else if (Existing->GetPlayerName().IsEmpty()) {
      UE_LOG(LogSkald, Warning,
             TEXT("EnsureBattleParticipant: Missing name for PlayerId %d"),
             PlayerID);
    }
    if (Faction != ESkaldFaction::None) {
      Existing->Faction = Faction;
    }
    Existing->bIsAI = bIsAI;
    return Existing;
  }

  for (APlayerState *BasePS : GameState->PlayerArray) {
    ASkaldPlayerState *Candidate = Cast<ASkaldPlayerState>(BasePS);
    if (!Candidate || Candidate->GetPlayerId() != PlayerID) {
      continue;
    }

    if (!DisplayName.IsEmpty()) {
      Candidate->PlayerDisplayName = DisplayName;
      Candidate->SetPlayerName(DisplayName);
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
      GameState->Players.RemoveAll([](const ASkaldPlayerState* Player) {
        return Player == nullptr;
      });
      GameState->Players.Sort([](const ASkaldPlayerState& A,
                                 const ASkaldPlayerState& B) {
        return A.GetPlayerId() < B.GetPlayerId();
      });
      bAddedToList = true;
    }
    if (bAddedToList) {
      GameState->OnPlayersUpdated.Broadcast();
      GameState->ForceNetUpdate();
    }

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
    NewState->PlayerDisplayName = DisplayName;
    NewState->SetPlayerName(DisplayName);
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
  return NewState;
}
} // namespace

void ASkald_BattleGameMode::InitGame(const FString &Map, const FString &Options,
                                     FString &Error) {
  Super::InitGame(Map, Options, Error);

  PendingControllers.Reset();
  PendingControllers.SetNum(2);
  bPendingBattleSetupTriggered = false;

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

  UE_LOG(LogSkaldBattle, Log,
         TEXT("BattleGM BeginPlay: BattleManager ready; ExpectedControllers=%d"),
         ExpectedControllers);
  UE_LOG(LogSkaldBattle, Log, TEXT("BGM BeginPlay. ExpectedControllers=%d"),
         ExpectedControllers);

  EnsureBattleControllers();

  GetWorldTimerManager().SetTimer(
      WaitForPlayersHandle, this, &ASkald_BattleGameMode::PollBattleBootstrap, 0.25f,
      true);
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
      ControllersById.Add(PS->GetPlayerId(), PC);
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
      ControllersById.Add(NewPS->GetPlayerId(), NewController);
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

    const int32 PreviousId = PS->GetPlayerId();
    if (Participant.PlayerId > 0 && PreviousId != Participant.PlayerId) {
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

void ASkald_BattleGameMode::SetupPendingBattle() {
  if (!HasAuthority()) {
    return;
  }

  if (bSetupCompleted) {
    return;
  }

  bBattleLaunched = false;

  auto ResetSetupStarted = [this]() { bSetupStarted = false; };

  USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>();
  if (!GI || !GI->GridBattleManager) {
    UE_LOG(LogSkaldBattle, Warning,
           TEXT("BattleGM SetupPendingBattle: GameInstance or GridBattleManager missing"));
    ResetSetupStarted();
    return;
  }

  ASkaldGameState *GS = GetGameState<ASkaldGameState>();
  if (!GS) {
    UE_LOG(LogSkaldBattle, Warning,
           TEXT("BattleGM SetupPendingBattle: GameState unavailable"));
    ResetSetupStarted();
    return;
  }

  UWorld *World = GetWorld();
  if (!World) {
    ResetSetupStarted();
    return;
  }

  const FSkaldTravelState &TravelState = GI->GetTravelState();
  MergeHumanTerritories(TravelState, GS, CachedTerritoryMap,
                        CachedHumanTerritoryIDs);

  FS_BattlePayload Battle = GI->PendingBattle;
  Battle.FromTerritoryID = Battle.FromTerritoryID > 0
                               ? Battle.FromTerritoryID
                               : TravelState.AttackerTerritory;
  Battle.TargetTerritoryID = Battle.TargetTerritoryID > 0
                                 ? Battle.TargetTerritoryID
                                 : TravelState.DefenderTerritory;

  const int32 AttackerId = Battle.bAttackerIsAI ? 1 : 0;
  const int32 DefenderId = Battle.bDefenderIsAI ? 1 : 0;

  PendingControllers.SetNum(2);

  AController *AttackerC = PendingControllers.IsValidIndex(AttackerId)
                               ? PendingControllers[AttackerId].Get()
                               : nullptr;
  AController *DefenderC = PendingControllers.IsValidIndex(DefenderId)
                               ? PendingControllers[DefenderId].Get()
                               : nullptr;

  if (!AttackerC || !DefenderC) {
    UE_LOG(LogSkaldBattle, Error,
           TEXT("BattleGM SetupPendingBattle: Unable to resolve participants (AttackerId=%d DefenderId=%d Territories=%d/%d)"),
           Battle.AttackerPlayerID, Battle.DefenderPlayerID,
           Battle.FromTerritoryID, Battle.TargetTerritoryID);
    ResetSetupStarted();
    return;
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
      InOutPlayerId = SlotPlayerState->GetPlayerId();
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
    ResetSetupStarted();
    return;
  }

  const int32 AttackerBudget = FMath::Max(0, Battle.ArmyCountSent);
  int32 DefenderBudget = Battle.DefenderArmyCount;
  if (DefenderBudget <= 0) {
    if (const FS_Territory *DefSnapshot =
            CachedTerritoryMap.Find(Battle.TargetTerritoryID)) {
      DefenderBudget = DefSnapshot->ArmyUnits;
    }
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

  if (!AttackerPS || !DefenderPS) {
    UE_LOG(LogSkaldBattle, Error,
           TEXT("BattleGM SetupPendingBattle: Failed to ensure participants (AttackerValid=%s DefenderValid=%s)"),
           AttackerPS ? TEXT("true") : TEXT("false"),
           DefenderPS ? TEXT("true") : TEXT("false"));
    ResetSetupStarted();
    return;
  }

  BeginPreBattleSelection(AttackerPS, DefenderPS, AttackerBudget, DefenderBudget);
  UE_LOG(LogSkaldBattle, Log,
         TEXT("BattleGM: BeginPreBattleSelection started (AttackerID=%d DefenderID=%d Budgets=%d/%d)"),
         Battle.AttackerPlayerID, Battle.DefenderPlayerID, AttackerBudget,
         DefenderBudget);

  AutoCommitAIArmy(AttackerPS, AttackerPS->bIsAI ? AttackerBudget : 0);
  AutoCommitAIArmy(DefenderPS, DefenderPS->bIsAI ? DefenderBudget : 0);

  bSetupCompleted = true;
  GI->PendingBattle = Battle;

  GS->SetBattlePhase(EBattlePhase::Deploy);

  UE_LOG(LogSkaldBattle, Log, TEXT("SetupPendingBattle completed. Phase=Deploy"));

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

void ASkald_BattleGameMode::AutoCommitAIArmy(ASkaldPlayerState *PlayerState,
                                             int32 Budget) const {
  if (!PlayerState || !PlayerState->bIsAI || !BattleManager) {
    return;
  }

  UE_LOG(LogSkald, Log,
         TEXT("BattleGM AutoCommitAIArmy: PlayerId=%d Budget=%d"),
         PlayerState->GetPlayerId(), Budget);

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
         PlayerState->PendingArmy.Num(), PlayerState->GetPlayerId());
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

  UGridOverlayComponent *Grid = nullptr;
  for (TActorIterator<AActor> It(GetWorld()); It; ++It) {
    if (UGridOverlayComponent *Found =
            It->FindComponentByClass<UGridOverlayComponent>()) {
      Grid = Found;
      break;
    }
  }

  const int32 Edge = 3;
  const int32 MaxX = UGridBattleManager::GridSize - 1;
  const int32 MaxY = UGridBattleManager::GridSize - 1;

  for (const FFighterDefinition &Def : Roster) {
    FIntPoint Cell;
    Cell.Y = GI->CombatRandomStream.RandRange(0, MaxY);
    Cell.X = bAsAttacker ? GI->CombatRandomStream.RandRange(0, Edge - 1)
                         : GI->CombatRandomStream.RandRange(MaxX - (Edge - 1), MaxX);

    const FVector SpawnLoc = Grid ? Grid->GridToWorld(Cell) : FVector::ZeroVector;
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride =
        ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
    AFighterPawn *Pawn = GetWorld()->SpawnActor<AFighterPawn>(
        AFighterPawn::StaticClass(), SpawnLoc, FRotator::ZeroRotator, Params);
    if (Pawn) {
      Pawn->Stats = Def.Stats;
      Pawn->bIsAttacker = bAsAttacker;
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
    Attackers.Add(Fighter);
  }

  TArray<FFighter> Defenders;
  Defenders.Reserve(DefenderDefs.Num());
  for (const FFighterDefinition &Def : DefenderDefs) {
    FFighter Fighter;
    Fighter.Stats = Def.Stats;
    Fighter.Faction = DefenderPS ? DefenderPS->Faction : Def.Faction;
    Defenders.Add(Fighter);
  }

  UE_LOG(LogSkald, Log,
         TEXT("BattleGM TryLaunchBattle: Launching battle (Attackers=%d Defenders=%d)"),
         AttackerDefs.Num(), DefenderDefs.Num());

  GI->GridBattleManager->InitBattle(Attackers, Defenders);

  SpawnFighterSide(AttackerDefs, /*bAsAttacker=*/true);
  SpawnFighterSide(DefenderDefs, /*bAsAttacker=*/false);

  GI->GridBattleManager->RollInitiative();
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
}

void ASkald_BattleGameMode::TryInitializeWorldAndStart() {
  bWorldInitialized = true;
  bTurnsStarted = true;
  GetWorldTimerManager().ClearTimer(RetryInitTimerHandle);
}

void ASkald_BattleGameMode::PostLogin(APlayerController *NewPlayer) {
  Super::PostLogin(NewPlayer);

  UE_LOG(LogSkaldBattle, Log, TEXT("PostLogin: %s"), *GetNameSafe(NewPlayer));
  OnControllerReady(NewPlayer);
}

void ASkald_BattleGameMode::OnAIControllerReady(AAIController *Controller) {
  OnControllerReady(Controller);
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

  if (Controller && !Controller->PlayerState) {
    Controller->InitPlayerState();
  }

  AssignControllerSlot(Controller, AIControllerClass);

  if (TrySetupBattleWhenReady()) {
    PollBattleBootstrap();
  }

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

