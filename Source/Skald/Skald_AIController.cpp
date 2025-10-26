#include "Skald_AIController.h"
#include "AIController.h"
#include "Algo/Sort.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FighterPawn.h"
#include "GameFramework/Controller.h"
#include "GridBattleManager.h"
#include "GridOverlayComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Skald.h"
#include "SkaldLogging.h"
#include "Skald_BattleGameMode.h"
#include "Skald_GameInstance.h"
#include "Skald_GameUserSettings.h"
#include "Skald_GameMode.h"
#include "Skald_PlayerState.h"
#include "Skald_TurnManager.h"
#include "SkaldTypes.h"
#include "Territory.h"
#include "TimerManager.h"
#include "UI/SkaldMainHUDWidget.h"
#include "WorldMap.h"
#include <limits>

namespace {
constexpr int32 MaxAIIterations = 100;
constexpr TCHAR EnemyPlanningMessage[] =
    TEXT("Enemy is planning their next move...");
constexpr TCHAR EnemyBattleTransitionMessage[] =
    TEXT("Enemy is preparing for battle...");
}

ASkald_BattleGameMode *ASkaldAIController::ResolveBattleGameMode() const {
  if (UWorld *World = GetWorld()) {
    if (ASkald_BattleGameMode *BattleGameMode =
            World->GetAuthGameMode<ASkald_BattleGameMode>()) {
      return BattleGameMode;
    }
  }

  if (const USkaldGameInstance *GameInstance =
          GetGameInstance<USkaldGameInstance>()) {
    return GameInstance->GetActiveBattleGameMode();
  }

  return nullptr;
}

void ASkaldAIController::BeginPlay() {
  Super::BeginPlay();

  if (const USkaldGameUserSettings *Settings =
          USkaldGameUserSettings::GetSkaldGameUserSettings()) {
    EnemyTurnStepDelay = Settings->GetEnemyTurnStepDelay();
    BattleActionDelay = Settings->GetBattleActionDelay();
    BattleActionDelay = FMath::Max(1.0f, BattleActionDelay);
  }

  USkaldGameInstance *GameInstance = GetGameInstance<USkaldGameInstance>();
  if (GameInstance) {
    GameInstance->OnBattleMapStateChanged.RemoveDynamic(
        this, &ASkaldAIController::HandleBattleMapStateChanged);
    GameInstance->OnBattleMapStateChanged.AddDynamic(
        this, &ASkaldAIController::HandleBattleMapStateChanged);
  }

  SetupBattleAutomation();

  if (GameInstance && GameInstance->bIsInBattleMap) {
    HandleBattleMapStateChanged(true);
  }

  if (HasAuthority()) {
    if (ASkald_BattleGameMode *BattleGameMode = ResolveBattleGameMode()) {
      BattleGameMode->OnAIControllerReady(this);
    }
  }
}

void ASkaldAIController::StartTurn() {
  DecisionIterationCount = 0;
  bAwaitingBattleTransition = false;
  bPendingPhaseAdvance = false;
  ClearDecisionTimers();
  MakeAIDecision();
}

void ASkaldAIController::EndTurn() {
  ClearDecisionTimers();
  bAwaitingBattleTransition = false;
  bPendingPhaseAdvance = false;
  ClearEnemyTurnStatus();
  Super::EndTurn();
}

void ASkaldAIController::MakeAIDecision() { ProcessCurrentPhase(); }

void ASkaldAIController::ProcessCurrentPhase() {
  if (!TurnManager) {
    UE_LOG(LogSkald, Warning,
           TEXT("ProcessCurrentPhase called without a valid TurnManager"));
    EndTurn();
    return;
  }

  if (bPendingPhaseAdvance) {
    bPendingPhaseAdvance = false;
    TurnManager->AdvancePhase();
  }

  if (bAwaitingBattleTransition) {
    if (ShouldPauseForBattleTransition()) {
      BroadcastEnemyTurnStatus(FString(EnemyBattleTransitionMessage));
      ScheduleNextDecisionStep(EnemyBattleTransitionPollDelay);
      return;
    }

    bAwaitingBattleTransition = false;
  }

  if (DecisionIterationCount >= MaxAIIterations) {
    UE_LOG(LogSkald, Warning,
           TEXT("AI decision hit iteration limit (%d)"), MaxAIIterations);
    EndTurn();
    return;
  }

  ++DecisionIterationCount;

  ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>();
  if (!PS) {
    UE_LOG(LogSkald, Warning,
           TEXT("ProcessCurrentPhase missing PlayerState; ending turn"));
    EndTurn();
    return;
  }

  AWorldMap *WorldMap = Cast<AWorldMap>(
      UGameplayStatics::GetActorOfClass(GetWorld(), AWorldMap::StaticClass()));
  if (!WorldMap) {
    UE_LOG(LogSkald, Warning,
           TEXT("ProcessCurrentPhase missing world map; ending turn"));
    EndTurn();
    return;
  }

  const ETurnPhase Phase = TurnManager->GetCurrentPhase();
  if (Phase == ETurnPhase::Revolt) {
    UE_LOG(LogSkald, Log, TEXT("AI decision completed in %d steps"),
           DecisionIterationCount);
    EndTurn();
    return;
  }

  if (Phase == ETurnPhase::ArmyPlacement) {
    ClearEnemyTurnStatus();
    return;
  } else if (Phase == ETurnPhase::Reinforcement) {
    TArray<ATerritory *> OwnedTerritories;
    for (ATerritory *Territory : WorldMap->Territories) {
      if (Territory && Territory->OwningPlayer == PS) {
        OwnedTerritories.Add(Territory);
      }
    }

    int32 SpreadIndex = 0;
    while (PS->DeployableUnits > 0 && OwnedTerritories.Num() > 0) {
      ATerritory *TargetTerritory =
          OwnedTerritories[SpreadIndex % OwnedTerritories.Num()];
      ++TargetTerritory->ArmyUnits;
      TargetTerritory->RefreshAppearance();
      --PS->DeployableUnits;
      --PS->Resources;
      ++SpreadIndex;
    }
    TurnManager->BroadcastDeployableUnits(PS);
    TurnManager->BroadcastResources(PS);

    if (TurnManager->HasTurnsStarted()) {
      const float ReinforcementDelay =
          FMath::Max(EnemyTurnStepDelay, ReinforcementPostDeployDelay);
      SchedulePhaseAdvance(ReinforcementDelay);
    } else {
      EndTurn();
    }
    return;
  } else if (Phase == ETurnPhase::Attack) {
    if (TurnManager->HasPendingBattlePreparation()) {
      bAwaitingBattleTransition = true;
      BroadcastEnemyTurnStatus(FString(EnemyBattleTransitionMessage));
      ScheduleNextDecisionStep(EnemyBattleTransitionPollDelay);
      return;
    }

    ATerritory *BestSource = nullptr;
    ATerritory *BestTarget = nullptr;
    int32 WeakestStrength = std::numeric_limits<int32>::max();

    for (ATerritory *Source : WorldMap->Territories) {
      if (!Source || Source->OwningPlayer != PS || Source->ArmyUnits <= 1) {
        continue;
      }

      for (ATerritory *Neighbor : Source->AdjacentTerritories) {
        if (!Neighbor || Neighbor->OwningPlayer == PS) {
          continue;
        }

        if (Neighbor->ArmyUnits < WeakestStrength) {
          BestSource = Source;
          BestTarget = Neighbor;
          WeakestStrength = Neighbor->ArmyUnits;
        }
      }
    }

    if (BestSource && BestTarget && BestSource->ArmyUnits > 1) {
      const int32 ArmySent = BestSource->ArmyUnits - 1;
      HandleAttackRequested(BestSource->TerritoryID, BestTarget->TerritoryID,
                            ArmySent, false);

      if (TurnManager && TurnManager->HasPendingBattlePreparation()) {
        bAwaitingBattleTransition = true;
      }

      if (const USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
        if (GI->bTravelPending) {
          bAwaitingBattleTransition = true;
        }
      }
    }

    if (bAwaitingBattleTransition) {
      BroadcastEnemyTurnStatus(FString(EnemyBattleTransitionMessage));
      ScheduleNextDecisionStep(EnemyBattleTransitionPollDelay);
      return;
    }

    SchedulePhaseAdvance(EnemyTurnStepDelay);
    return;
  } else if (Phase == ETurnPhase::Engineering ||
             Phase == ETurnPhase::Treasure) {
    SchedulePhaseAdvance(EnemyTurnStepDelay);
    return;
  } else if (Phase == ETurnPhase::Movement) {
    auto CountEnemyNeighbors = [PS](ATerritory *Territory) {
      int32 Count = 0;
      if (!Territory) {
        return Count;
      }
      for (ATerritory *Neighbor : Territory->AdjacentTerritories) {
        if (Neighbor && Neighbor->OwningPlayer != PS) {
          ++Count;
        }
      }
      return Count;
    };

    ATerritory *BestSource = nullptr;
    ATerritory *BestTarget = nullptr;
    int32 BestSourcePressure = 0;
    int32 BestTargetPressure = 0;
    float BestScore = std::numeric_limits<float>::lowest();

    for (ATerritory *Source : WorldMap->Territories) {
      if (!Source || Source->OwningPlayer != PS || Source->ArmyUnits <= 1) {
        continue;
      }

      const int32 SourcePressure = CountEnemyNeighbors(Source);

      for (ATerritory *Neighbor : Source->AdjacentTerritories) {
        if (!Neighbor || Neighbor->OwningPlayer != PS) {
          continue;
        }

        const int32 TargetPressure = CountEnemyNeighbors(Neighbor);
        const int32 PressureDiff = TargetPressure - SourcePressure;
        const int32 SourceUnits = Source->ArmyUnits;
        const int32 TargetUnits = Neighbor->ArmyUnits;

        if (PressureDiff <= 0 && SourceUnits <= TargetUnits + 1) {
          continue;
        }

        const int32 StrengthDiff = SourceUnits - TargetUnits;
        const float Score = PressureDiff * 10.f + StrengthDiff;

        if (Score > BestScore && Score > 0.f) {
          BestScore = Score;
          BestSource = Source;
          BestTarget = Neighbor;
          BestSourcePressure = SourcePressure;
          BestTargetPressure = TargetPressure;
        }
      }
    }

    if (BestSource && BestTarget) {
      const int32 SourceUnits = BestSource->ArmyUnits;
      const int32 TargetUnits = BestTarget->ArmyUnits;
      const int32 MaxMovable = SourceUnits - 1;
      if (MaxMovable > 0) {
        int32 Surplus = SourceUnits - TargetUnits;
        Surplus = FMath::Max(Surplus, 0);
        int32 TroopsToMove = FMath::Clamp(Surplus / 2, 1, MaxMovable);
        if (BestTargetPressure > BestSourcePressure) {
          const int32 PressureGap = BestTargetPressure - BestSourcePressure;
          TroopsToMove = FMath::Clamp(FMath::Max(TroopsToMove, PressureGap), 1,
                                      MaxMovable);
        }
        HandleMoveRequested(BestSource->TerritoryID, BestTarget->TerritoryID,
                            TroopsToMove);
      }
    }

    SchedulePhaseAdvance(EnemyTurnStepDelay);
    return;
  } else if (Phase == ETurnPhase::EndTurn) {
    SchedulePhaseAdvance(EnemyTurnStepDelay);
    return;
  } else {
    UE_LOG(LogSkald, Warning,
           TEXT("ProcessCurrentPhase encountered unexpected phase %s"),
           *UEnum::GetValueAsString(Phase));
    EndTurn();
    return;
  }

}

void ASkaldAIController::ScheduleNextDecisionStep(float DelaySeconds) {
  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(EnemyTurnStepTimerHandle);
    World->GetTimerManager().SetTimer(EnemyTurnStepTimerHandle, this,
                                      &ASkaldAIController::ProcessCurrentPhase,
                                      FMath::Max(DelaySeconds, 0.f), false);
  }
}

void ASkaldAIController::SchedulePhaseAdvance(float DelaySeconds) {
  bPendingPhaseAdvance = true;
  BroadcastEnemyTurnStatus(FString(EnemyPlanningMessage));
  ScheduleNextDecisionStep(DelaySeconds);
}

void ASkaldAIController::ClearDecisionTimers() {
  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(EnemyTurnStepTimerHandle);
  }
}

void ASkaldAIController::BroadcastEnemyTurnStatus(const FString &Message) {
  if (!TurnManager) {
    return;
  }

  for (ASkaldPlayerController *Controller : TurnManager->GetControllers()) {
    if (!Controller) {
      continue;
    }

    if (USkaldMainHUDWidget *HUD = Controller->GetHUDWidget()) {
      HUD->ShowEnemyTurnInProgress(Message);
    }
  }
}

void ASkaldAIController::ClearEnemyTurnStatus() {
  if (!TurnManager) {
    return;
  }

  for (ASkaldPlayerController *Controller : TurnManager->GetControllers()) {
    if (!Controller) {
      continue;
    }

    if (USkaldMainHUDWidget *HUD = Controller->GetHUDWidget()) {
      HUD->HideEnemyTurnInProgress();
    }
  }
}

bool ASkaldAIController::ShouldPauseForBattleTransition() const {
  if (!bAwaitingBattleTransition) {
    return false;
  }

  if (TurnManager && TurnManager->HasPendingBattlePreparation()) {
    return true;
  }

  if (const USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
    return GI->bTravelPending;
  }

  return false;
}

void ASkaldAIController::EndPlay(const EEndPlayReason::Type EndPlayReason) {
  ClearDecisionTimers();
  ClearEnemyTurnStatus();
  bAwaitingBattleTransition = false;
  if (USkaldGameInstance *GameInstance =
          GetGameInstance<USkaldGameInstance>()) {
    GameInstance->OnBattleMapStateChanged.RemoveDynamic(
        this, &ASkaldAIController::HandleBattleMapStateChanged);
  }
  TeardownBattleAutomation();
  Super::EndPlay(EndPlayReason);
}

void ASkaldAIController::SetupBattleAutomation() {
  if (CachedBattleManager.IsValid()) {
    return;
  }

  USkaldGameInstance *GameInstance = GetGameInstance<USkaldGameInstance>();
  if (!GameInstance || !GameInstance->GridBattleManager) {
    if (UWorld *World = GetWorld()) {
      if (!World->GetTimerManager().IsTimerActive(BattleAutomationPollHandle)) {
        World->GetTimerManager().SetTimer(
            BattleAutomationPollHandle, this,
            &ASkaldAIController::SetupBattleAutomation, 0.5f, false);
      }
    }
    return;
  }

  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(BattleAutomationPollHandle);
  }

  CachedBattleManager = GameInstance->GridBattleManager;
  if (!CachedBattleManager.IsValid()) {
    return;
  }

  CachedBattleManager->OnActiveFighterChanged.AddDynamic(
      this, &ASkaldAIController::HandleActiveFighterChanged);
  CachedBattleManager->OnRoundStarted.AddDynamic(
      this, &ASkaldAIController::HandleRoundStarted);
  CachedBattleManager->OnBattleEnded.AddDynamic(
      this, &ASkaldAIController::HandleBattleEnded);

  DetermineControlledBattleSide();
  ScheduleTryActivateNextFighter();
}

void ASkaldAIController::TeardownBattleAutomation() {
  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(BattleAutomationPollHandle);
  }

  if (CachedBattleManager.IsValid()) {
    CachedBattleManager->OnActiveFighterChanged.RemoveDynamic(
        this, &ASkaldAIController::HandleActiveFighterChanged);
    CachedBattleManager->OnRoundStarted.RemoveDynamic(
        this, &ASkaldAIController::HandleRoundStarted);
    CachedBattleManager->OnBattleEnded.RemoveDynamic(
        this, &ASkaldAIController::HandleBattleEnded);
  }

  CachedBattleManager.Reset();
  bAIControlsAttackerSide = false;
  bAIControlsDefenderSide = false;
}

void ASkaldAIController::DetermineControlledBattleSide() {
  bAIControlsAttackerSide = false;
  bAIControlsDefenderSide = false;

  const ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>();
  if (!PS) {
    return;
  }

  const USkaldGameInstance *GameInstance = GetGameInstance<USkaldGameInstance>();
  if (!GameInstance) {
    return;
  }

  const FS_BattlePayload &Battle = GameInstance->PendingBattle;
  const int32 PlayerId = PS->GetPlayerId();

  if (PlayerId == Battle.AttackerPlayerID && PlayerId > 0) {
    bAIControlsAttackerSide = true;
  }
  if (PlayerId == Battle.DefenderPlayerID && PlayerId > 0) {
    bAIControlsDefenderSide = true;
  }

  if (!bAIControlsAttackerSide || !bAIControlsDefenderSide) {
    FString PlayerName = PS->GetPlayerName();
    if (PlayerName.IsEmpty()) {
      PlayerName = PS->PlayerDisplayName;
    }

    auto MatchesParticipantName = [&PlayerName](const FString &Candidate) {
      if (PlayerName.IsEmpty() || Candidate.IsEmpty()) {
        return false;
      }
      return PlayerName.Equals(Candidate, ESearchCase::IgnoreCase);
    };

    if (!bAIControlsAttackerSide &&
        MatchesParticipantName(Battle.AttackerDisplayName)) {
      bAIControlsAttackerSide = true;
    }
    if (!bAIControlsDefenderSide &&
        MatchesParticipantName(Battle.DefenderDisplayName)) {
      bAIControlsDefenderSide = true;
    }
  }
}

bool ASkaldAIController::ControlsFighter(const AFighterPawn *Fighter) const {
  if (!Fighter) {
    return false;
  }
  return Fighter->bIsAttacker ? bAIControlsAttackerSide
                              : bAIControlsDefenderSide;
}

bool ASkaldAIController::IsMyTurn() const {
  if (!CachedBattleManager.IsValid()) {
    return false;
  }
  const bool bAttackerTurn = CachedBattleManager->IsAttackerTurn();
  return (bAttackerTurn && bAIControlsAttackerSide) ||
         (!bAttackerTurn && bAIControlsDefenderSide);
}

int32 ASkaldAIController::ComputeChebyshevDistance(UGridOverlayComponent * /*Grid*/,
                                                   const AFighterPawn *A,
                                                   const AFighterPawn *B) const {
  if (!A || !B) {
    return TNumericLimits<int32>::Max();
  }

  return A->GetFootprintDistanceToFighter(B);
}

AFighterPawn *ASkaldAIController::FindNearestEnemy(AFighterPawn *Fighter) const {
  if (!Fighter) {
    return nullptr;
  }

  UGridOverlayComponent *Grid = Fighter->GetGrid();
  if (!Grid) {
    return nullptr;
  }

  UWorld *World = GetWorld();
  if (!World) {
    return nullptr;
  }

  AFighterPawn *BestEnemy = nullptr;
  int32 BestDistance = TNumericLimits<int32>::Max();

  const FIntPoint StartCell = Fighter->GetCurrentCell();
  if (!Grid->IsCellInBounds(StartCell)) {
    return nullptr;
  }

  for (TActorIterator<AFighterPawn> It(World); It; ++It) {
    AFighterPawn *Candidate = *It;
    if (!Candidate || Candidate == Fighter || !Candidate->IsAlive()) {
      continue;
    }
    if (ControlsFighter(Candidate)) {
      continue;
    }

    const int32 Distance = ComputeChebyshevDistance(Grid, Fighter, Candidate);
    if (Distance < BestDistance) {
      BestDistance = Distance;
      BestEnemy = Candidate;
    }
  }

  return BestEnemy;
}

AFighterPawn *ASkaldAIController::FindNextFriendlyFighter(bool bExpectAttacker) const {
  UWorld *World = GetWorld();
  if (!World) {
    return nullptr;
  }

  AFighterPawn *BestFighter = nullptr;
  int32 BestDistance = TNumericLimits<int32>::Max();

  for (TActorIterator<AFighterPawn> It(World); It; ++It) {
    AFighterPawn *Candidate = *It;
    if (!Candidate || !Candidate->IsAlive()) {
      continue;
    }
    if (Candidate->bIsAttacker != bExpectAttacker) {
      continue;
    }
    if (!ControlsFighter(Candidate) || Candidate->HasActivatedThisRound()) {
      continue;
    }

    UGridOverlayComponent *Grid = Candidate->GetGrid();
    const int32 Distance = ComputeChebyshevDistance(
        Grid, Candidate, FindNearestEnemy(Candidate));
    if (Distance < BestDistance || !BestFighter) {
      BestDistance = Distance;
      BestFighter = Candidate;
    }
  }

  return BestFighter;
}

bool ASkaldAIController::TryAttackNearestEnemy(AFighterPawn *Fighter) {
  if (!Fighter || Fighter->ActionsRemaining <= 0) {
    return false;
  }

  UGridOverlayComponent *Grid = Fighter->GetGrid();
  if (!Grid) {
    return false;
  }

  AFighterPawn *Target = FindNearestEnemy(Fighter);
  if (!Target) {
    return false;
  }

  const int32 Distance = Fighter->GetFootprintDistanceToFighter(Target);
  if (Distance > Fighter->Stats.AttackRange) {
    return false;
  }

  if (Grid &&
      !Fighter->HasLineOfSightToFighter(Target, Fighter->Stats.AttackRange, Grid)) {
    return false;
  }

  const int32 ActionsBefore = Fighter->ActionsRemaining;
  Fighter->PerformAttack(Target);
  return Fighter->ActionsRemaining < ActionsBefore;
}

bool ASkaldAIController::TryMoveTowardsNearestEnemy(AFighterPawn *Fighter) {
  if (!Fighter || Fighter->ActionsRemaining <= 0 || Fighter->Stats.Movement <= 0) {
    return false;
  }

  UGridOverlayComponent *Grid = Fighter->GetGrid();
  if (!Grid) {
    return false;
  }

  AFighterPawn *Enemy = FindNearestEnemy(Fighter);
  if (!Enemy) {
    return false;
  }

  const FIntPoint StartCell = Fighter->GetCurrentCell();
  const FIntPoint EnemyCell = Enemy->GetCurrentCell();
  if (!Grid->IsCellInBounds(StartCell) || !Grid->IsCellInBounds(EnemyCell)) {
    return false;
  }

  const TArray<FIntPoint> EnemyFootprint = Enemy->GetOccupiedCells();

  auto ComputeDistanceFromAnchor = [&](const FIntPoint &Anchor) {
    const TArray<FIntPoint> CandidateCells = Fighter->GetOccupiedCells(Anchor);
    int32 BestDistance = TNumericLimits<int32>::Max();
    for (const FIntPoint &SelfCell : CandidateCells) {
      for (const FIntPoint &EnemyCellCoord : EnemyFootprint) {
        const int32 Distance = FMath::Max(
            FMath::Abs(SelfCell.X - EnemyCellCoord.X),
            FMath::Abs(SelfCell.Y - EnemyCellCoord.Y));
        if (Distance < BestDistance) {
          BestDistance = Distance;
          if (BestDistance == 0) {
            break;
          }
        }
      }
      if (BestDistance == 0) {
        break;
      }
    }
    return BestDistance;
  };

  const int32 CurrentDistance = ComputeDistanceFromAnchor(StartCell);
  const int32 MaxSteps = Fighter->Stats.Movement;

  TSet<FIntPoint> IgnoredCells;
  const TArray<FIntPoint> CurrentFootprint = Fighter->GetOccupiedCells();
  for (const FIntPoint &Cell : CurrentFootprint) {
    IgnoredCells.Add(Cell);
  }

  auto CanOccupyAnchor = [&](const FIntPoint &Anchor) {
    const TArray<FIntPoint> CandidateCells = Fighter->GetOccupiedCells(Anchor);
    for (const FIntPoint &Cell : CandidateCells) {
      if (!Grid->IsCellInBounds(Cell) || Grid->IsObscured(Cell)) {
        return false;
      }
      if (Grid->IsOccupied(Cell) && !IgnoredCells.Contains(Cell)) {
        return false;
      }
    }
    return true;
  };

  auto IsDiagonalStepClear = [&](const FIntPoint &From, const FIntPoint &To) {
    if (From.X == To.X || From.Y == To.Y) {
      return true;
    }

    const TArray<FIntPoint> FromCells = Fighter->GetOccupiedCells(From);
    const TArray<FIntPoint> NextCells = Fighter->GetOccupiedCells(To);

    TSet<FIntPoint> NextCellSet;
    NextCellSet.Reserve(NextCells.Num());
    for (const FIntPoint &NextCell : NextCells) {
      NextCellSet.Add(NextCell);
    }

    auto IsBlocked = [&](const FIntPoint &CheckCell) {
      if (!Grid->IsCellInBounds(CheckCell) || Grid->IsObscured(CheckCell)) {
        return true;
      }
      if (Grid->IsOccupied(CheckCell) && !IgnoredCells.Contains(CheckCell) &&
          !NextCellSet.Contains(CheckCell)) {
        return true;
      }
      return false;
    };

    const int32 StepX = FMath::Clamp(To.X - From.X, -1, 1);
    const int32 StepY = FMath::Clamp(To.Y - From.Y, -1, 1);
    for (const FIntPoint &FromCell : FromCells) {
      if (IsBlocked(FromCell + FIntPoint(StepX, 0)) ||
          IsBlocked(FromCell + FIntPoint(0, StepY))) {
        return false;
      }
    }

    return true;
  };

  static const FIntPoint Directions[] = {
      FIntPoint(1, 0),  FIntPoint(-1, 0), FIntPoint(0, 1),  FIntPoint(0, -1),
      FIntPoint(1, 1),  FIntPoint(1, -1), FIntPoint(-1, 1), FIntPoint(-1, -1)};

  TSet<FIntPoint> Visited;
  TQueue<TPair<FIntPoint, int32>> Frontier;
  Visited.Add(StartCell);
  Frontier.Enqueue(TPair<FIntPoint, int32>(StartCell, 0));

  FIntPoint BestAnchor = StartCell;
  int32 BestDistance = CurrentDistance;
  int32 BestPathCost = TNumericLimits<int32>::Max();

  while (!Frontier.IsEmpty()) {
    TPair<FIntPoint, int32> Node;
    Frontier.Dequeue(Node);

    const FIntPoint Cell = Node.Key;
    const int32 DistanceFromStart = Node.Value;

    for (const FIntPoint &Dir : Directions) {
      const FIntPoint Next = Cell + Dir;

      if (Visited.Contains(Next)) {
        continue;
      }

      const int32 StepCost = DistanceFromStart + 1;
      if (StepCost > MaxSteps) {
        continue;
      }

      if (!CanOccupyAnchor(Next)) {
        continue;
      }

      if (!IsDiagonalStepClear(Cell, Next)) {
        continue;
      }

      Visited.Add(Next);
      Frontier.Enqueue(TPair<FIntPoint, int32>(Next, StepCost));

      const int32 CandidateDistance = ComputeDistanceFromAnchor(Next);
      if (CandidateDistance > BestDistance) {
        continue;
      }

      if (CandidateDistance < BestDistance || StepCost < BestPathCost) {
        BestDistance = CandidateDistance;
        BestPathCost = StepCost;
        BestAnchor = Next;
      }
    }
  }

  if (BestAnchor == StartCell) {
    return false;
  }

  const int32 ActionsBefore = Fighter->ActionsRemaining;
  Fighter->MoveToCell(BestAnchor);
  return Fighter->ActionsRemaining < ActionsBefore;
}

void ASkaldAIController::QueueActivationIntent(
    AFighterPawn *Fighter, EAIBattleActivationIntent Intent) {
  if (!Fighter) {
    return;
  }

  if (PendingActivationFighter != Fighter) {
    PendingActivationFighter = Fighter;
  }

  PendingActivationIntents.Enqueue(Intent);
}

bool ASkaldAIController::ShouldContinueActivation(
    const AFighterPawn *Fighter) const {
  return Fighter && Fighter->IsAlive() && Fighter->bIsCurrentlyActive &&
         Fighter->ActionsRemaining > 0 && ActivationIntentIterationCount < 8;
}

void ASkaldAIController::ScheduleNextActivationAttempt() {
  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(FighterActionTimerHandle);
    const float EffectiveDelay = FMath::Max(1.0f, BattleActionDelay);
    World->GetTimerManager().SetTimer(
        FighterActionTimerHandle, this,
        &ASkaldAIController::ProcessQueuedActivationIntent, EffectiveDelay,
        false);
  }
}

void ASkaldAIController::ProcessQueuedActivationIntent() {
  if (bAwaitingQueuedAttackResolution) {
    return;
  }

  if (CachedBattleManager.IsValid() &&
      CachedBattleManager->IsAwaitingAttackPresentation()) {
    ScheduleNextActivationAttempt();
    return;
  }

  AFighterPawn *Fighter = PendingActivationFighter.Get();
  if (!ShouldContinueActivation(Fighter)) {
    CompleteFighterActivation();
    return;
  }

  EAIBattleActivationIntent Intent;
  if (!PendingActivationIntents.Dequeue(Intent)) {
    QueueActivationIntent(Fighter, EAIBattleActivationIntent::Attack);
    if (!PendingActivationIntents.Dequeue(Intent)) {
      return;
    }
  }

  bool bActionTaken = false;
  bool bRequiresAttackResolution = false;

  switch (Intent) {
  case EAIBattleActivationIntent::Attack:
    bActionTaken = TryAttackNearestEnemy(Fighter);
    bRequiresAttackResolution = bActionTaken;
    if (!bActionTaken && ShouldContinueActivation(Fighter)) {
      QueueActivationIntent(Fighter, EAIBattleActivationIntent::Move);
      ScheduleNextActivationAttempt();
      return;
    }
    break;
  case EAIBattleActivationIntent::Move:
    bActionTaken = TryMoveTowardsNearestEnemy(Fighter);
    break;
  }

  if (!bActionTaken) {
    CompleteFighterActivation();
    return;
  }

  ++ActivationIntentIterationCount;

  if (!ShouldContinueActivation(Fighter)) {
    if (bRequiresAttackResolution) {
      bAwaitingQueuedAttackResolution = true;
    } else {
      CompleteFighterActivation();
    }
    return;
  }

  if (bRequiresAttackResolution) {
    bAwaitingQueuedAttackResolution = true;
    return;
  }

  QueueActivationIntent(Fighter, EAIBattleActivationIntent::Attack);
  ScheduleNextActivationAttempt();
}

void ASkaldAIController::HandleQueuedAttackFinalized() {
  if (!bAwaitingQueuedAttackResolution) {
    return;
  }

  bAwaitingQueuedAttackResolution = false;

  AFighterPawn *Fighter = PendingActivationFighter.Get();
  if (!ShouldContinueActivation(Fighter)) {
    CompleteFighterActivation();
    return;
  }

  QueueActivationIntent(Fighter, EAIBattleActivationIntent::Attack);
  ScheduleNextActivationAttempt();
}

void ASkaldAIController::ClearActivationTimers() {
  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(FighterActionTimerHandle);
    World->GetTimerManager().ClearTimer(ActivationGapTimerHandle);
  }

  PendingActivationIntents.Empty();
  bAwaitingQueuedAttackResolution = false;
}

void ASkaldAIController::CompleteFighterActivation() {
  ClearActivationTimers();

  AFighterPawn *Fighter = PendingActivationFighter.Get();
  if (Fighter) {
    Fighter->OnQueuedAttackFinalized.RemoveAll(this);
  }

  bProcessingActivation = false;
  ActivationIntentIterationCount = 0;

  if (CachedBattleManager.IsValid() && Fighter) {
    CachedBattleManager->FinishActivation(Fighter, EGridActivationFinishReason::Auto);
  }

  PendingActivationFighter = nullptr;
}

void ASkaldAIController::HandleBattleMapStateChanged(bool bInBattleMap) {
  if (bInBattleMap) {
    SetupBattleAutomation();
  } else {
    TeardownBattleAutomation();
  }
}

void ASkaldAIController::ScheduleTryActivateNextFighter() {
  if (CachedBattleManager.IsValid() &&
      CachedBattleManager->IsAwaitingAttackPresentation()) {
    if (UWorld *World = GetWorld()) {
      World->GetTimerManager().ClearTimer(ActivationGapTimerHandle);
    }
    return;
  }

  if (CachedBattleManager.IsValid() &&
      CachedBattleManager->IsAwaitingInitiativeRoll()) {
    if (UWorld *World = GetWorld()) {
      World->GetTimerManager().ClearTimer(ActivationGapTimerHandle);
    }
    return;
  }

  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(ActivationGapTimerHandle);
    World->GetTimerManager().SetTimer(
        ActivationGapTimerHandle, this, &ASkaldAIController::TryActivateNextFighter,
        ActivationGapDelay, false);
  }
}

void ASkaldAIController::ExecuteActivationForFighter(AFighterPawn *Fighter) {
  if (!Fighter || !CachedBattleManager.IsValid()) {
    bProcessingActivation = false;
    return;
  }

  ClearActivationTimers();
  ActivationIntentIterationCount = 0;
  PendingActivationFighter = Fighter;
  bAwaitingQueuedAttackResolution = false;

  Fighter->OnQueuedAttackFinalized.RemoveAll(this);
  Fighter->OnQueuedAttackFinalized.AddUObject(
      this, &ASkaldAIController::HandleQueuedAttackFinalized);

  QueueActivationIntent(Fighter, EAIBattleActivationIntent::Attack);
  ScheduleNextActivationAttempt();
}

void ASkaldAIController::TryActivateNextFighter() {
  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(ActivationGapTimerHandle);
  }

  if (!CachedBattleManager.IsValid()) {
    SetupBattleAutomation();
    return;
  }

  if (CachedBattleManager->IsAwaitingAttackPresentation()) {
    return;
  }

  if (CachedBattleManager->IsAwaitingInitiativeRoll()) {
    return;
  }

  if (CachedBattleManager->GetActiveFighter()) {
    return;
  }

  if (!IsMyTurn()) {
    return;
  }

  const bool bAttackerTurn = CachedBattleManager->IsAttackerTurn();
  AFighterPawn *NextFighter = FindNextFriendlyFighter(bAttackerTurn);
  if (!NextFighter) {
    CachedBattleManager->AdvanceTurn();
    return;
  }

  if (!CachedBattleManager->CanActivateFighter(NextFighter)) {
    const FString FighterName = NextFighter
                                    ? NextFighter->GetHumanReadableName()
                                    : FString(TEXT("<None>"));
    UE_LOG(LogSkaldBattle, Verbose,
           TEXT("[AI] Skipping activation for %s; advancing turn."),
           FighterName.IsEmpty() ? TEXT("<Unnamed Fighter>")
                                 : *FighterName);
    CachedBattleManager->AdvanceTurn();
    return;
  }

  CachedBattleManager->ActivateFighter(NextFighter);
}

void ASkaldAIController::HandleActiveFighterChanged(AFighterPawn *NewFighter) {
  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(ActivationGapTimerHandle);
  }

  if (NewFighter) {
    if (!ControlsFighter(NewFighter)) {
      return;
    }

    bProcessingActivation = true;
    ExecuteActivationForFighter(NewFighter);
    return;
  }

  const bool bPendingFighterInvalid = !PendingActivationFighter.IsValid();
  if (bProcessingActivation &&
      (bAwaitingQueuedAttackResolution || bPendingFighterInvalid)) {
    CompleteFighterActivation();
  }

  if (bProcessingActivation) {
    return;
  }

  ScheduleTryActivateNextFighter();
}

void ASkaldAIController::HandleRoundStarted(int32 /*RoundNumber*/,
                                            ESkaldFaction /*InitiativeWinner*/) {
  DetermineControlledBattleSide();
  ScheduleTryActivateNextFighter();
}

void ASkaldAIController::HandleBattleEnded(ESkaldFaction /*WinningFaction*/,
                                           int32 /*AttackerCasualties*/,
                                           int32 /*DefenderCasualties*/) {
  ClearActivationTimers();
  if (AFighterPawn *Fighter = PendingActivationFighter.Get()) {
    Fighter->OnQueuedAttackFinalized.RemoveAll(this);
  }
  PendingActivationFighter = nullptr;
  bProcessingActivation = false;
  bAwaitingQueuedAttackResolution = false;
  ActivationIntentIterationCount = 0;
  TeardownBattleAutomation();
}

