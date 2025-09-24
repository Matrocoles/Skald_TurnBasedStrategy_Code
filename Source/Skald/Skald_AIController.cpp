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
#include "Skald_GameMode.h"
#include "Skald_PlayerState.h"
#include "Skald_TurnManager.h"
#include "SkaldTypes.h"
#include "Territory.h"
#include "TimerManager.h"
#include "WorldMap.h"
#include <limits>

namespace {
constexpr int32 MaxAIIterations = 100;
}

void ASkaldAIController::BeginPlay() {
  Super::BeginPlay();

  if (UWorld *World = GetWorld()) {
    if (ASkald_BattleGameMode *BattleGM =
            World->GetAuthGameMode<ASkald_BattleGameMode>()) {
      BattleGM->OnControllerReady(this);
    }
  }

  SetupBattleAutomation();
}

void ASkaldAIController::StartTurn() { MakeAIDecision(); }

void ASkaldAIController::MakeAIDecision() {
  if (!TurnManager) {
    EndTurn();
    return;
  }

  ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>();
  if (!PS) {
    EndTurn();
    return;
  }

  AWorldMap *WorldMap = Cast<AWorldMap>(
      UGameplayStatics::GetActorOfClass(GetWorld(), AWorldMap::StaticClass()));
  if (!WorldMap) {
    EndTurn();
    return;
  }

  ETurnPhase Phase = TurnManager->GetCurrentPhase();
  int32 IterationCount = 0;
  while (Phase != ETurnPhase::Revolt && IterationCount++ < MaxAIIterations) {
    const ETurnPhase PrevPhase = Phase;

    if (Phase == ETurnPhase::ArmyPlacement) {
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

      TurnManager->AdvancePhase();
    } else if (Phase == ETurnPhase::Attack) {
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
      }

      TurnManager->AdvancePhase();
    } else if (Phase == ETurnPhase::Engineering ||
               Phase == ETurnPhase::Treasure) {
      TurnManager->AdvancePhase();
    } else if (Phase == ETurnPhase::Movement) {
      ATerritory *BestSource = nullptr;
      ATerritory *BestTarget = nullptr;
      int32 WeakestStrength = std::numeric_limits<int32>::max();

      for (ATerritory *Source : WorldMap->Territories) {
        if (!Source || Source->OwningPlayer != PS || Source->ArmyUnits <= 1) {
          continue;
        }

        for (ATerritory *Neighbor : Source->AdjacentTerritories) {
          if (!Neighbor || Neighbor->OwningPlayer != PS) {
            continue;
          }

          if (Neighbor->ArmyUnits < WeakestStrength) {
            BestSource = Source;
            BestTarget = Neighbor;
            WeakestStrength = Neighbor->ArmyUnits;
          }
        }
      }

      if (BestSource && BestTarget) {
        int32 TroopsToMove = BestSource->ArmyUnits / 2;
        TroopsToMove = FMath::Clamp(TroopsToMove, 1, BestSource->ArmyUnits - 1);
        HandleMoveRequested(BestSource->TerritoryID, BestTarget->TerritoryID,
                            TroopsToMove);
      }

      TurnManager->AdvancePhase();
    } else if (Phase == ETurnPhase::EndTurn) {
      TurnManager->AdvancePhase();
    } else {
      UE_LOG(LogSkald, Warning,
             TEXT("MakeAIDecision encountered unexpected phase %s"),
             *UEnum::GetValueAsString(Phase));
      break;
    }

    Phase = TurnManager->GetCurrentPhase();
    if (Phase == PrevPhase) {
      UE_LOG(LogSkald, Warning,
             TEXT("MakeAIDecision phase %s did not advance; breaking"),
             *UEnum::GetValueAsString(Phase));
      break;
    }
  }

  if (IterationCount >= MaxAIIterations) {
    UE_LOG(LogSkald, Warning, TEXT("MakeAIDecision hit iteration limit (%d)"),
           MaxAIIterations);
  } else {
    UE_LOG(LogSkald, Log, TEXT("MakeAIDecision completed in %d iterations"),
           IterationCount);
  }

  EndTurn();
}

void ASkaldAIController::EndPlay(const EEndPlayReason::Type EndPlayReason) {
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
  TryActivateNextFighter();
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

int32 ASkaldAIController::ComputeManhattanDistance(UGridOverlayComponent *Grid,
                                                   const AFighterPawn *A,
                                                   const AFighterPawn *B) const {
  if (!Grid || !A || !B) {
    return TNumericLimits<int32>::Max();
  }

  const FIntPoint CellA = Grid->WorldToGrid(A->GetActorLocation());
  const FIntPoint CellB = Grid->WorldToGrid(B->GetActorLocation());
  if (!Grid->IsCellInBounds(CellA) || !Grid->IsCellInBounds(CellB)) {
    return TNumericLimits<int32>::Max();
  }

  return FMath::Abs(CellA.X - CellB.X) + FMath::Abs(CellA.Y - CellB.Y);
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

  const FIntPoint StartCell = Grid->WorldToGrid(Fighter->GetActorLocation());
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

    const int32 Distance = ComputeManhattanDistance(Grid, Fighter, Candidate);
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
    const int32 Distance = ComputeManhattanDistance(
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

  const FIntPoint SelfCell = Grid->WorldToGrid(Fighter->GetActorLocation());
  const FIntPoint TargetCell = Grid->WorldToGrid(Target->GetActorLocation());
  if (!Grid->IsCellInBounds(SelfCell) || !Grid->IsCellInBounds(TargetCell)) {
    return false;
  }

  const int32 Distance = FMath::Abs(SelfCell.X - TargetCell.X) +
                         FMath::Abs(SelfCell.Y - TargetCell.Y);
  if (Distance > Fighter->Stats.AttackRange) {
    return false;
  }

  if (!Grid->HasLineOfSight(SelfCell, TargetCell)) {
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

  const FIntPoint StartCell = Grid->WorldToGrid(Fighter->GetActorLocation());
  const FIntPoint EnemyCell = Grid->WorldToGrid(Enemy->GetActorLocation());
  if (!Grid->IsCellInBounds(StartCell) || !Grid->IsCellInBounds(EnemyCell)) {
    return false;
  }

  FIntPoint Current = StartCell;
  int32 CurrentDistance = FMath::Abs(EnemyCell.X - Current.X) +
                          FMath::Abs(EnemyCell.Y - Current.Y);

  const int32 MaxSteps = Fighter->Stats.Movement;
  for (int32 Step = 0; Step < MaxSteps; ++Step) {
    TArray<FIntPoint> Directions = {FIntPoint(1, 0), FIntPoint(-1, 0),
                                    FIntPoint(0, 1), FIntPoint(0, -1)};
    Directions.Sort([&](const FIntPoint &A, const FIntPoint &B) {
      const FIntPoint PosA = Current + A;
      const FIntPoint PosB = Current + B;
      const int32 DistA =
          FMath::Abs(EnemyCell.X - PosA.X) + FMath::Abs(EnemyCell.Y - PosA.Y);
      const int32 DistB =
          FMath::Abs(EnemyCell.X - PosB.X) + FMath::Abs(EnemyCell.Y - PosB.Y);
      return DistA < DistB;
    });

    bool bMovedThisStep = false;
    for (const FIntPoint &Dir : Directions) {
      const FIntPoint Candidate = Current + Dir;
      if (!Grid->IsCellInBounds(Candidate) || Grid->IsOccupied(Candidate) ||
          Grid->IsObscured(Candidate)) {
        continue;
      }

      const int32 CandidateDistance = FMath::Abs(EnemyCell.X - Candidate.X) +
                                      FMath::Abs(EnemyCell.Y - Candidate.Y);
      if (CandidateDistance >= CurrentDistance) {
        continue;
      }

      Current = Candidate;
      CurrentDistance = CandidateDistance;
      bMovedThisStep = true;
      break;
    }

    if (!bMovedThisStep) {
      break;
    }
  }

  if (Current == StartCell) {
    return false;
  }

  const int32 ActionsBefore = Fighter->ActionsRemaining;
  Fighter->MoveToCell(Current);
  return Fighter->ActionsRemaining < ActionsBefore;
}

void ASkaldAIController::ExecuteActivationForFighter(AFighterPawn *Fighter) {
  if (!Fighter || !CachedBattleManager.IsValid()) {
    return;
  }

  int32 SafetyCounter = 0;
  while (Fighter->IsAlive() && Fighter->bIsCurrentlyActive &&
         Fighter->ActionsRemaining > 0 && SafetyCounter < 8) {
    bool bActionTaken = false;
    if (TryAttackNearestEnemy(Fighter)) {
      bActionTaken = true;
    } else if (TryMoveTowardsNearestEnemy(Fighter)) {
      bActionTaken = true;
    }

    if (!bActionTaken) {
      break;
    }

    ++SafetyCounter;
  }

  if (CachedBattleManager.IsValid()) {
    CachedBattleManager->FinishActivation(Fighter);
  }
}

void ASkaldAIController::TryActivateNextFighter() {
  if (!CachedBattleManager.IsValid()) {
    SetupBattleAutomation();
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
    return;
  }

  if (!CachedBattleManager->CanActivateFighter(NextFighter)) {
    return;
  }

  CachedBattleManager->ActivateFighter(NextFighter);
}

void ASkaldAIController::HandleActiveFighterChanged(AFighterPawn *NewFighter) {
  if (bProcessingActivation) {
    return;
  }

  if (NewFighter) {
    if (!ControlsFighter(NewFighter)) {
      return;
    }

    bProcessingActivation = true;
    ExecuteActivationForFighter(NewFighter);
    bProcessingActivation = false;
    TryActivateNextFighter();
    return;
  }

  TryActivateNextFighter();
}

void ASkaldAIController::HandleRoundStarted(int32 /*RoundNumber*/,
                                            ESkaldFaction /*InitiativeWinner*/) {
  DetermineControlledBattleSide();
  TryActivateNextFighter();
}

void ASkaldAIController::HandleBattleEnded(ESkaldFaction /*WinningFaction*/,
                                           int32 /*AttackerCasualties*/,
                                           int32 /*DefenderCasualties*/) {
  TeardownBattleAutomation();
}

