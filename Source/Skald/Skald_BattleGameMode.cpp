#include "Skald_BattleGameMode.h"

#include "Algo/RandomShuffle.h"
#include "Algo/Sort.h"
#include "AIController.h"
#include "GridBattleManager.h"
#include "Skald.h"
#include "Skald_GameInstance.h"
#include "Skald_GameState.h"
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

  ReadyControllers.Empty();
  CachedHumanTerritoryIDs.Reset();
  CachedTerritoryMap.Reset();
  bPendingBattleSetupComplete = false;
  bBattleLaunched = false;
  bLoggedTravelCache = false;
  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(TravelBootstrapHandle);
  }

  USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>();
  if (GI) {
    GI->SetTravelPending(false);
  }

  ASkaldGameState *GS = GetGameState<ASkaldGameState>();

  if (GI) {
    const FSkaldTravelState &TravelState = GI->GetTravelState();
    ExpectedControllers = TravelState.bValid ? TravelState.ExpectedControllers : 0;

    const TArray<FS_Territory> *Source = &TravelState.CachedTerritories;
    if (Source->Num() == 0) {
      Source = &GI->CachedWorldMapTerritories;
    }

    BuildTerritoryMap(*Source, CachedTerritoryMap);
    MergeHumanTerritories(TravelState, GS, CachedTerritoryMap,
                          CachedHumanTerritoryIDs);

    UE_LOG(LogSkald, Log,
           TEXT("BattleGM InitGame: Restored %d human territories from travel cache; ExpectedControllers=%d"),
           CachedHumanTerritoryIDs.Num(), ExpectedControllers);
    bLoggedTravelCache = true;
  } else {
    ExpectedControllers = 0;
    UE_LOG(LogSkald, Warning,
           TEXT("BattleGM InitGame: GameInstance unavailable; ExpectedControllers reset"));
  }

  UE_LOG(LogSkald, Log, TEXT("[HUD] Skipping MainHUD in BattleGameMode"));

  BootstrapFromTravelState();
}

void ASkald_BattleGameMode::BeginPlay() {
  Super::BeginPlay();

  ReadyControllers.Empty();

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

  UE_LOG(LogSkald, Log,
         TEXT("BattleGM BeginPlay: BattleManager ready; ExpectedControllers=%d"),
         ExpectedControllers);

  BootstrapFromTravelState();
}

void ASkald_BattleGameMode::SetupPendingBattle() {
  if (!HasAuthority()) {
    return;
  }

  if (bPendingBattleSetupComplete) {
    return;
  }

  bBattleLaunched = false;

  USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>();
  if (!GI || !GI->GridBattleManager) {
    UE_LOG(LogSkald, Warning,
           TEXT("BattleGM SetupPendingBattle: GameInstance or GridBattleManager missing"));
    return;
  }

  ASkaldGameState *GS = GetGameState<ASkaldGameState>();
  if (!GS) {
    UE_LOG(LogSkald, Warning,
           TEXT("BattleGM SetupPendingBattle: GameState unavailable"));
    return;
  }

  UWorld *World = GetWorld();
  if (!World) {
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

  auto ResolveParticipant = [&](int32 TerritoryId, int32 &InOutPlayerId,
                                FString &InOutDisplayName,
                                ESkaldFaction &InOutFaction,
                                bool &bInOutIsAI) {
    if (TerritoryId > 0) {
      if (const FS_Territory *Territory = CachedTerritoryMap.Find(TerritoryId)) {
        if (Territory->OwnerPlayerID > 0) {
          InOutPlayerId = Territory->OwnerPlayerID;
        }
      }
    }

    ASkaldPlayerState *PlayerState =
        (InOutPlayerId > 0) ? GS->GetPlayerById(InOutPlayerId) : nullptr;
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
                     Battle.bAttackerIsAI);
  ResolveParticipant(Battle.TargetTerritoryID, Battle.DefenderPlayerID,
                     Battle.DefenderDisplayName, Battle.DefenderFaction,
                     Battle.bDefenderIsAI);

  if (Battle.AttackerPlayerID <= 0 || Battle.DefenderPlayerID <= 0) {
    UE_LOG(LogSkald, Error,
           TEXT("BattleGM SetupPendingBattle: Unable to resolve participants (AttackerId=%d DefenderId=%d Territories=%d/%d)"),
           Battle.AttackerPlayerID, Battle.DefenderPlayerID,
           Battle.FromTerritoryID, Battle.TargetTerritoryID);
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

  UE_LOG(LogSkald, Log,
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
    UE_LOG(LogSkald, Error,
           TEXT("BattleGM SetupPendingBattle: Failed to ensure participants (AttackerValid=%s DefenderValid=%s)"),
           AttackerPS ? TEXT("true") : TEXT("false"),
           DefenderPS ? TEXT("true") : TEXT("false"));
    return;
  }

  BeginPreBattleSelection(AttackerPS, DefenderPS, AttackerBudget, DefenderBudget);
  UE_LOG(LogSkald, Log,
         TEXT("BattleGM: BeginPreBattleSelection started (AttackerID=%d DefenderID=%d Budgets=%d/%d)"),
         Battle.AttackerPlayerID, Battle.DefenderPlayerID, AttackerBudget,
         DefenderBudget);

  AutoCommitAIArmy(AttackerPS, AttackerPS->bIsAI ? AttackerBudget : 0);
  AutoCommitAIArmy(DefenderPS, DefenderPS->bIsAI ? DefenderBudget : 0);

  bPendingBattleSetupComplete = true;
  GI->PendingBattle = Battle;

  UE_LOG(LogSkald, Log, TEXT("BattleGM SetupPendingBattle complete"));

  TryStartBattle();
}

void ASkald_BattleGameMode::BootstrapFromTravelState() {
  if (!HasAuthority()) {
    return;
  }

  UWorld *World = GetWorld();
  if (!World) {
    return;
  }

  USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>();
  ASkaldGameState *GS = GetGameState<ASkaldGameState>();
  if (!GI || !GS) {
    World->GetTimerManager().SetTimer(
        TravelBootstrapHandle, this,
        &ASkald_BattleGameMode::BootstrapFromTravelState, 0.25f, false);
    return;
  }

  const FSkaldTravelState &TravelState = GI->GetTravelState();
  if (ExpectedControllers <= 0) {
    if (TravelState.bValid && TravelState.ExpectedControllers > 0) {
      ExpectedControllers = TravelState.ExpectedControllers;
    } else {
      ExpectedControllers = GS->PlayerArray.Num();
    }
  }

  int32 ControllerCount = 0;
  for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It;
       ++It) {
    ++ControllerCount;
  }

  const int32 PlayerStateCount = GS->PlayerArray.Num();
  if (ControllerCount > 0) {
    ExpectedControllers = FMath::Max(ExpectedControllers, ControllerCount);
  }
  const int32 NeededControllers = FMath::Max(2, ExpectedControllers);

  UE_LOG(LogSkald, Verbose,
         TEXT("BattleGM BootstrapFromTravelState: Controllers=%d PlayerStates=%d Expected=%d ReadyControllers=%d SetupComplete=%s"),
         ControllerCount, PlayerStateCount, NeededControllers,
         ReadyControllers.Num(),
         bPendingBattleSetupComplete ? TEXT("true") : TEXT("false"));

  if (ControllerCount < NeededControllers || PlayerStateCount < NeededControllers) {
    World->GetTimerManager().SetTimer(
        TravelBootstrapHandle, this,
        &ASkald_BattleGameMode::BootstrapFromTravelState, 0.25f, false);
    return;
  }

  World->GetTimerManager().ClearTimer(TravelBootstrapHandle);

  if (!bPendingBattleSetupComplete) {
    SetupPendingBattle();
  } else {
    TryStartBattle();
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

void ASkald_BattleGameMode::PruneInvalidReadyControllers() {
  for (auto It = ReadyControllers.CreateIterator(); It; ++It) {
    if (!It->IsValid()) {
      It.RemoveCurrent();
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

  if (NewPlayer) {
    OnControllerReady(NewPlayer);
  }
}

void ASkald_BattleGameMode::OnAIControllerReady(AAIController *Controller) {
  UE_LOG(LogSkald, Log, TEXT("BattleGM OnAIControllerReady: %s"),
         *GetNameSafe(Controller));
  OnControllerReady(Controller);
}

void ASkald_BattleGameMode::OnControllerReady(AController *Controller) {
  if (!IsValid(Controller)) {
    return;
  }

  PruneInvalidReadyControllers();
  ReadyControllers.Add(Controller);

  UE_LOG(LogSkald, Log,
         TEXT("BattleGM OnControllerReady: %s ReadyControllers=%d Expected=%d"),
         *GetNameSafe(Controller), ReadyControllers.Num(), ExpectedControllers);

  BootstrapFromTravelState();
  TryStartBattle();
}

void ASkald_BattleGameMode::TryStartBattle() {
  if (bBattleLaunched) {
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

  PruneInvalidReadyControllers();

  if (!bPendingBattleSetupComplete) {
    UE_LOG(LogSkald, Verbose,
           TEXT("BattleGM TryStartBattle: Pending setup incomplete; ReadyControllers=%d Expected=%d"),
           ReadyControllers.Num(), ExpectedControllers);
    return;
  }

  const int32 PlayerStateCount = GS->PlayerArray.Num();
  if (ExpectedControllers <= 0 && PlayerStateCount > 0) {
    ExpectedControllers = PlayerStateCount;
  }

  const int32 NeededControllers = FMath::Max(2, ExpectedControllers);
  const int32 ReadyCount = ReadyControllers.Num();

  UE_LOG(LogSkald, Verbose,
         TEXT("BattleGM TryStartBattle: ReadyControllers=%d PlayerStates=%d Expected=%d"),
         ReadyCount, PlayerStateCount, NeededControllers);

  if (ReadyCount < NeededControllers || PlayerStateCount < NeededControllers) {
    return;
  }

  TryLaunchBattle();
}

