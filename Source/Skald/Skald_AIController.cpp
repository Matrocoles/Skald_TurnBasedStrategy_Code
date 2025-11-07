#include "Skald_AIController.h"
#include "AIController.h"
#include "Abilities/SkaldAbilityComponent.h"
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
#include "Containers/Map.h"
#include <limits>

namespace {
constexpr int32 MaxAIIterations = 100;
constexpr TCHAR EnemyPlanningMessage[] =
    TEXT("Enemy is planning their next move...");
constexpr TCHAR EnemyBattleTransitionMessage[] =
    TEXT("Enemy is preparing for battle...");
constexpr TCHAR EnemyStrategyPlanningMessage[] =
    TEXT("Enemy is evaluating the battlefield...");
constexpr float StrategyPlanningDelayFraction = 0.5f;
constexpr float MinimumStrategyPlanningDelay = 0.75f;
constexpr float AttackDamageWeight = 1.5f;
constexpr float AttackDiceWeight = 1.0f;
constexpr float AttackThreatWeight = 0.35f;
constexpr float AttackDistancePenalty = 0.5f;
constexpr float AttackKillPriorityBonus = 12.0f;
constexpr float AttackLowHealthBonus = 4.0f;
constexpr float PassiveHumanAdjacencyBonus = 2.5f;
constexpr float PassiveElfMovementIncentive = 3.0f;
constexpr float AbilityBaselineScore = 5.0f;
constexpr float TrapPlacementAdjacencyScore = 6.0f;
constexpr int32 MaxAbilityAttemptsPerActivation = 3;

float EvaluateAbilityIntrinsicBonus(const FName &AbilityId) {
  static const TMap<FName, float> AbilityBonuses = {
      {TEXT("Ability_Elf_Line"), 8.0f},
      {TEXT("Ability_Elf_Elite"), 6.0f},
      {TEXT("Ability_Elf_Skirmish"), 3.5f},
      {TEXT("Ability_Dwarf_Skirmish"), 2.5f},
      {TEXT("Ability_Dwarf_Line"), 3.5f},
      {TEXT("Ability_Dwarf_Elite"), 5.0f},
      {TEXT("Ability_Undead_Skirmish"), 3.5f},
      {TEXT("Ability_Undead_Line"), 4.0f},
      {TEXT("Ability_Undead_Elite"), 6.0f},
      {TEXT("Ability_Human_Skirmish"), 3.0f},
      {TEXT("Ability_Human_Line"), 2.5f},
      {TEXT("Ability_Human_Elite"), 6.0f},
      {TEXT("Ability_Ravpack_Skirmish"), 2.5f},
      {TEXT("Ability_Ravpack_Line"), 4.5f},
      {TEXT("Ability_Ravpack_Elite"), 4.0f},
      {TEXT("Ability_Orc_Skirmish"), 2.5f},
      {TEXT("Ability_Orc_Line"), 3.0f},
      {TEXT("Ability_Orc_Elite"), 5.5f},
      {TEXT("Ability_Empire_Skirmish"), 3.0f},
      {TEXT("Ability_Empire_Line"), 3.5f},
      {TEXT("Ability_Empire_Elite"), 5.5f},
      {TEXT("Ability_Gnoll_Skirmish"), 2.0f},
      {TEXT("Ability_Gnoll_Line"), 3.0f},
      {TEXT("Ability_Gnoll_Elite"), 3.0f},
      {TEXT("Ability_Goblin_Skirmish"), 3.5f},
      {TEXT("Ability_Goblin_Line"), 4.0f},
      {TEXT("Ability_Goblin_Elite"), 4.5f},
      {TEXT("Ability_Inflicted_Skirmish"), 3.5f},
      {TEXT("Ability_Inflicted_Line"), 4.0f},
      {TEXT("Ability_Inflicted_Elite"), 6.5f},
      {TEXT("Ability_Lizard_Skirmish"), 3.5f},
      {TEXT("Ability_Lizard_Line"), 3.0f},
      {TEXT("Ability_Lizard_Elite"), 4.5f},
      {TEXT("Ability_Lizardfolk_Skirmish"), 3.5f},
      {TEXT("Ability_Lizardfolk_Line"), 3.0f},
      {TEXT("Ability_Lizardfolk_Elite"), 4.5f},
      {TEXT("Ability_Frog_Skirmish"), 3.5f},
      {TEXT("Ability_Frog_Line"), 3.0f},
      {TEXT("Ability_Frog_Elite"), 5.5f},
      {TEXT("Ability_Frogfolk_Skirmish"), 3.5f},
      {TEXT("Ability_Frogfolk_Line"), 3.0f},
      {TEXT("Ability_Frogfolk_Elite"), 5.5f},
  };

  if (const float *Score = AbilityBonuses.Find(AbilityId)) {
    return *Score;
  }
  return 0.0f;
}
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
    EnemyTurnStepDelay =
        FMath::Max(Settings->GetEnemyTurnStepDelay(),
                   USkaldGameUserSettings::MinimumEnemyTurnStepDelay);
    BattleActionDelay =
        FMath::Max(Settings->GetBattleActionDelay(),
                   USkaldGameUserSettings::MinimumBattleActionDelay);
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

void ASkaldAIController::InitializeHUDWidget() {
  UE_LOG(LogSkald, Verbose,
         TEXT("ASkaldAIController %s does not create a HUD widget."),
         *GetName());
}

void ASkaldAIController::StartTurn() {
  DecisionIterationCount = 0;
  bAwaitingBattleTransition = false;
  bPendingPhaseAdvance = false;
  bStrategyEvaluatedThisTurn = false;
  CachedStrategicContext = FStrategicContext();
  CurrentStrategy = EAIStrategy::Hybrid;
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

int32 ASkaldAIController::PerformArmyPlacementTurn() {
  if (!TurnManager) {
    return 0;
  }

  ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>();
  if (!PS || PS->DeployableUnits <= 0) {
    return 0;
  }

  AWorldMap *WorldMap = Cast<AWorldMap>(
      UGameplayStatics::GetActorOfClass(GetWorld(), AWorldMap::StaticClass()));
  if (!WorldMap) {
    UE_LOG(LogSkald, Warning,
           TEXT("PerformArmyPlacementTurn: missing world map; cannot deploy armies."));
    return 0;
  }

  const int32 UnitsBefore = PS->DeployableUnits;

  const FStrategicContext SavedContext = CachedStrategicContext;
  const EAIStrategy SavedStrategy = CurrentStrategy;
  const bool bSavedStrategyEvaluated = bStrategyEvaluatedThisTurn;

  RefreshStrategicContext(WorldMap, PS);
  if (CachedStrategicContext.OwnedTerritories.Num() > 0) {
    CurrentStrategy = SelectStrategyFromContext(CachedStrategicContext);
    bStrategyEvaluatedThisTurn = true;
    ExecuteStrategicArmyPlacement(WorldMap, PS);
  }

  if (PS->DeployableUnits > 0) {
    const int32 FallbackPlaced = WorldMap->AutoPlaceUnitsForAI(PS);
    if (FallbackPlaced > 0) {
      UE_LOG(LogSkald, Verbose,
             TEXT("PerformArmyPlacementTurn: fallback auto-placement deployed %d units for %s."),
             FallbackPlaced,
             *PS->GetResolvedPlayerName(TEXT("PerformArmyPlacementTurn_Fallback")));
    }
  }

  TurnManager->BroadcastDeployableUnits(PS);

  const int32 UnitsPlaced = UnitsBefore - PS->DeployableUnits;

  CachedStrategicContext = SavedContext;
  CurrentStrategy = SavedStrategy;
  bStrategyEvaluatedThisTurn = bSavedStrategyEvaluated;

  return UnitsPlaced;
}

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

  if (EnsureStrategySelected(WorldMap, PS)) {
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
    ExecuteStrategicArmyPlacement(WorldMap, PS);
    TurnManager->BroadcastDeployableUnits(PS);

    if (TurnManager->HasTurnsStarted()) {
      const float PlacementDelay =
          FMath::Max(EnemyTurnStepDelay * StrategyPlanningDelayFraction,
                     MinimumStrategyPlanningDelay);
      SchedulePhaseAdvance(PlacementDelay);
    } else {
      EndTurn();
    }
    return;
  } else if (Phase == ETurnPhase::Reinforcement) {
    ExecuteStrategicReinforcements(WorldMap, PS);
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

    const bool bInitiatedAttack = ExecuteStrategicAttack(WorldMap, PS);

    if (TurnManager && TurnManager->HasPendingBattlePreparation()) {
      bAwaitingBattleTransition = true;
    }

    if (const USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
      if (GI->bTravelPending) {
        bAwaitingBattleTransition = true;
      }
    }

    if (bAwaitingBattleTransition) {
      BroadcastEnemyTurnStatus(FString(EnemyBattleTransitionMessage));
      ScheduleNextDecisionStep(EnemyBattleTransitionPollDelay);
      return;
    }

    if (bInitiatedAttack) {
      const float AttackDelay =
          FMath::Max(EnemyTurnStepDelay * StrategyPlanningDelayFraction,
                     MinimumStrategyPlanningDelay);
      ScheduleNextDecisionStep(AttackDelay);
    } else {
      SchedulePhaseAdvance(EnemyTurnStepDelay);
    }
    return;
  } else if (Phase == ETurnPhase::Engineering ||
             Phase == ETurnPhase::Treasure) {
    SchedulePhaseAdvance(EnemyTurnStepDelay);
    return;
  } else if (Phase == ETurnPhase::Movement) {
    const bool bMoved = ExecuteStrategicMovement(WorldMap, PS);
    if (bMoved) {
      const float MoveDelay =
          FMath::Max(EnemyTurnStepDelay * StrategyPlanningDelayFraction,
                     MinimumStrategyPlanningDelay);
      ScheduleNextDecisionStep(MoveDelay);
    } else {
      SchedulePhaseAdvance(EnemyTurnStepDelay);
    }
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

bool ASkaldAIController::EnsureStrategySelected(AWorldMap *WorldMap,
                                                ASkaldPlayerState *InPlayerState) {
  RefreshStrategicContext(WorldMap, InPlayerState);

  if (bStrategyEvaluatedThisTurn) {
    return false;
  }

  CurrentStrategy = SelectStrategyFromContext(CachedStrategicContext);
  bStrategyEvaluatedThisTurn = true;

  const FString StrategyDescriptor = DescribeStrategy(CurrentStrategy);
  const FString PlanningMessage = FString::Printf(
      TEXT("%s They adopt a %s stance."), EnemyStrategyPlanningMessage,
      *StrategyDescriptor);
  BroadcastEnemyTurnStatus(PlanningMessage);

  const float PlanningDelay =
      FMath::Max(EnemyTurnStepDelay * StrategyPlanningDelayFraction,
                 MinimumStrategyPlanningDelay);
  ScheduleNextDecisionStep(PlanningDelay);
  return true;
}

void ASkaldAIController::RefreshStrategicContext(AWorldMap *WorldMap,
                                                 ASkaldPlayerState *InPlayerState) {
  CachedStrategicContext = FStrategicContext();
  if (!WorldMap || !InPlayerState) {
    return;
  }

  FStrategicContext Context;
  Context.OwnedTerritories.Reserve(WorldMap->Territories.Num());
  Context.EnemyTerritories.Reserve(WorldMap->Territories.Num());

  for (ATerritory *Territory : WorldMap->Territories) {
    if (!Territory) {
      continue;
    }

    if (Territory->OwningPlayer == InPlayerState) {
      Context.OwnedTerritories.Add(Territory);
      Context.TotalFriendlyUnits += Territory->ArmyUnits;
      if (Territory->bIsCapital) {
        Context.Capital = Territory;
      }
    } else {
      Context.EnemyTerritories.Add(Territory);
      Context.TotalEnemyUnits += Territory->ArmyUnits;
      if (Territory->bIsCapital) {
        Context.EnemyCapitals.Add(Territory);
      }
    }
  }

  for (ATerritory *Territory : Context.OwnedTerritories) {
    if (!Territory) {
      continue;
    }

    int32 EnemyPressure = 0;
    bool bAdjacentEnemyCapital = false;
    bool bAdjacentFriendlyCapital = false;

    for (ATerritory *Neighbor : Territory->AdjacentTerritories) {
      if (!Neighbor) {
        continue;
      }

      if (Neighbor->OwningPlayer == InPlayerState) {
        if (Context.Capital && Neighbor == Context.Capital) {
          bAdjacentFriendlyCapital = true;
        }
        continue;
      }

      EnemyPressure += Neighbor->ArmyUnits;
      Context.BorderTerritories.Add(Territory);
      Context.TotalEnemyBorderUnits += Neighbor->ArmyUnits;

      if (Neighbor->bIsCapital) {
        bAdjacentEnemyCapital = true;
      }

      if (Context.Capital && Territory == Context.Capital) {
        Context.bCapitalThreatened = true;
      }
    }

    if (EnemyPressure > 0) {
      Context.FriendlyBorderUnits += Territory->ArmyUnits;
    }

    if (bAdjacentEnemyCapital) {
      Context.EnemyCapitalApproach.Add(Territory);
      Context.bCanThreatenEnemyCapital = true;
    }

    if (bAdjacentFriendlyCapital) {
      Context.CapitalDefenseRing.Add(Territory);
    }

    Context.EnemyPressure.Add(Territory, EnemyPressure);

    if (Territory == Context.Capital && EnemyPressure > 0) {
      Context.bCapitalThreatened = true;
    }
  }

  CachedStrategicContext = MoveTemp(Context);
}

ASkaldAIController::EAIStrategy
ASkaldAIController::SelectStrategyFromContext(
    const FStrategicContext &Context) const {
  if (Context.OwnedTerritories.Num() == 0) {
    return EAIStrategy::Defensive;
  }

  const float FriendlyStrength = FMath::Max(1, Context.TotalFriendlyUnits);
  const float EnemyStrength = FMath::Max(1, Context.TotalEnemyUnits);
  const float BorderPressureRatio =
      Context.TotalEnemyBorderUnits / FriendlyStrength;
  const bool bOverwhelmedBorders =
      Context.TotalEnemyBorderUnits >
      (Context.FriendlyBorderUnits + Context.OwnedTerritories.Num());
  const bool bSeverelyThreatened = Context.bCapitalThreatened ||
                                   BorderPressureRatio > 1.1f ||
                                   FriendlyStrength < EnemyStrength * 0.8f ||
                                   bOverwhelmedBorders;

  if (bSeverelyThreatened) {
    return EAIStrategy::Defensive;
  }

  if ((Context.bCanThreatenEnemyCapital &&
       FriendlyStrength >= EnemyStrength * 0.9f) ||
      FriendlyStrength > EnemyStrength * 1.2f) {
    return EAIStrategy::Offensive;
  }

  return EAIStrategy::Hybrid;
}

FString ASkaldAIController::DescribeStrategy(EAIStrategy Strategy) const {
  switch (Strategy) {
  case EAIStrategy::Offensive:
    return TEXT("offensive");
  case EAIStrategy::Defensive:
    return TEXT("defensive");
  default:
    return TEXT("balanced");
  }
}

float ASkaldAIController::EvaluateOffensivePriority(
    const FStrategicContext &Context, ATerritory *Territory) const {
  if (!Territory) {
    return 0.f;
  }

  const bool bIsBorder = Context.BorderTerritories.Contains(Territory);
  const bool bApproachesEnemyCapital =
      Context.EnemyCapitalApproach.Contains(Territory);
  const bool bIsCapital = Territory == Context.Capital;
  const int32 EnemyPressure = Context.EnemyPressure.FindRef(Territory);

  float Score = 0.f;
  Score += bIsBorder ? 25.f : 5.f;
  Score += bApproachesEnemyCapital ? 35.f : 0.f;
  Score += bIsCapital ? 10.f : 0.f;
  Score += static_cast<float>(Territory->Resources) * 0.5f;
  Score += static_cast<float>(Territory->ArmyUnits) * 0.3f;
  Score -= static_cast<float>(EnemyPressure) * 0.5f;
  return FMath::Max(Score, 0.f);
}

float ASkaldAIController::EvaluateDefensivePriority(
    const FStrategicContext &Context, ATerritory *Territory) const {
  if (!Territory) {
    return 0.f;
  }

  const bool bIsCapital = Territory == Context.Capital;
  const bool bInCapitalRing =
      Context.CapitalDefenseRing.Contains(Territory);
  const bool bIsBorder = Context.BorderTerritories.Contains(Territory);
  const int32 EnemyPressure = Context.EnemyPressure.FindRef(Territory);

  float Score = 0.f;
  Score += bIsCapital ? 60.f : 0.f;
  Score += bInCapitalRing ? 35.f : 0.f;
  Score += bIsBorder ? 20.f : 0.f;
  Score += static_cast<float>(EnemyPressure) * 2.0f;
  Score += FMath::Max(0.f, static_cast<float>(Territory->ArmyUnits) * 0.2f);
  return FMath::Max(Score, 0.f);
}

void ASkaldAIController::ExecuteStrategicArmyPlacement(
    AWorldMap *WorldMap, ASkaldPlayerState *InPlayerState) {
  if (!WorldMap || !InPlayerState || InPlayerState->DeployableUnits <= 0) {
    return;
  }

  struct FPlacementTarget {
    ATerritory *Territory = nullptr;
    float Score = 0.f;
    int32 RemainingCapacity = 0;
  };

  const int32 MaxPerTerritory =
      Skald::ArmyPlacement::DeployPerTerritoryLimit;

  TArray<FPlacementTarget> Targets;
  Targets.Reserve(CachedStrategicContext.OwnedTerritories.Num());

  for (ATerritory *Territory : CachedStrategicContext.OwnedTerritories) {
    if (!Territory) {
      continue;
    }

    const int32 TerritoryId = Territory->GetTerritoryId();
    const int32 AlreadyPlaced =
        InPlayerState->GetArmyPlacementDeploymentForTerritory(TerritoryId);
    const int32 RemainingCapacity = MaxPerTerritory - AlreadyPlaced;
    if (RemainingCapacity <= 0) {
      continue;
    }

    const float OffensiveScore =
        EvaluateOffensivePriority(CachedStrategicContext, Territory);
    const float DefensiveScore =
        EvaluateDefensivePriority(CachedStrategicContext, Territory);

    float Score = 0.f;
    switch (CurrentStrategy) {
    case EAIStrategy::Offensive:
      Score = OffensiveScore + DefensiveScore * 0.3f;
      break;
    case EAIStrategy::Defensive:
      Score = DefensiveScore + OffensiveScore * 0.2f;
      break;
    case EAIStrategy::Hybrid:
    default:
      Score = (OffensiveScore * 0.5f) + (DefensiveScore * 0.5f);
      break;
    }

    if (Score <= 0.f) {
      Score = FMath::Max(OffensiveScore, DefensiveScore);
    }

    if (Score <= 0.f) {
      continue;
    }

    FPlacementTarget Target;
    Target.Territory = Territory;
    Target.Score = Score;
    Target.RemainingCapacity = RemainingCapacity;
    Targets.Add(Target);
  }

  Targets.Sort([](const FPlacementTarget &A, const FPlacementTarget &B) {
    return A.Score > B.Score;
  });

  int32 Index = 0;
  while (InPlayerState->DeployableUnits > 0 && Targets.Num() > 0) {
    FPlacementTarget &Target = Targets[Index];
    if (Target.RemainingCapacity <= 0) {
      Targets.RemoveAt(Index);
      if (Targets.Num() == 0) {
        break;
      }
      Index %= Targets.Num();
      continue;
    }

    ++Target.Territory->ArmyUnits;
    Target.Territory->RefreshAppearance();
    --InPlayerState->DeployableUnits;
    InPlayerState->AddArmyPlacementDeployment(
        Target.Territory->GetTerritoryId(), 1);
    --Target.RemainingCapacity;

    if (Targets.Num() > 0) {
      Index = (Index + 1) % Targets.Num();
    }
  }
}

void ASkaldAIController::ExecuteStrategicReinforcements(
    AWorldMap *WorldMap, ASkaldPlayerState *InPlayerState) {
  if (!WorldMap || !InPlayerState || InPlayerState->DeployableUnits <= 0 ||
      InPlayerState->Resources <= 0) {
    return;
  }

  struct FReinforcementTarget {
    ATerritory *Territory = nullptr;
    float Score = 0.f;
  };

  TArray<FReinforcementTarget> Targets;
  Targets.Reserve(CachedStrategicContext.OwnedTerritories.Num());

  for (ATerritory *Territory : CachedStrategicContext.OwnedTerritories) {
    if (!Territory) {
      continue;
    }

    const float OffensiveScore =
        EvaluateOffensivePriority(CachedStrategicContext, Territory);
    const float DefensiveScore =
        EvaluateDefensivePriority(CachedStrategicContext, Territory);

    float Score = 0.f;
    switch (CurrentStrategy) {
    case EAIStrategy::Offensive:
      Score = OffensiveScore + DefensiveScore * 0.25f;
      break;
    case EAIStrategy::Defensive:
      Score = DefensiveScore + OffensiveScore * 0.15f;
      break;
    case EAIStrategy::Hybrid:
    default:
      Score = OffensiveScore * 0.5f + DefensiveScore * 0.6f;
      break;
    }

    Score += static_cast<float>(
        CachedStrategicContext.EnemyPressure.FindRef(Territory));

    if (Score <= 0.f) {
      continue;
    }

    Targets.Add({Territory, Score});
  }

  Targets.Sort([](const FReinforcementTarget &A,
                  const FReinforcementTarget &B) { return A.Score > B.Score; });

  int32 Index = 0;
  while (InPlayerState->DeployableUnits > 0 && InPlayerState->Resources > 0 &&
         Targets.Num() > 0) {
    FReinforcementTarget &Target = Targets[Index];
    if (!Target.Territory) {
      Targets.RemoveAt(Index);
      if (Targets.Num() == 0) {
        break;
      }
      Index %= Targets.Num();
      continue;
    }

    ++Target.Territory->ArmyUnits;
    Target.Territory->RefreshAppearance();
    --InPlayerState->DeployableUnits;
    --InPlayerState->Resources;

    if (Targets.Num() > 0) {
      Index = (Index + 1) % Targets.Num();
    }
  }
}

bool ASkaldAIController::ExecuteStrategicAttack(AWorldMap *WorldMap,
                                                ASkaldPlayerState *InPlayerState) {
  if (!WorldMap || !InPlayerState) {
    return false;
  }

  struct FAttackOption {
    ATerritory *Source = nullptr;
    ATerritory *Target = nullptr;
    float Score = 0.f;
    int32 UnitsToSend = 0;
  };

  FAttackOption BestOption;
  BestOption.Score = std::numeric_limits<float>::lowest();
  bool bFoundOption = false;

  for (ATerritory *Source : CachedStrategicContext.OwnedTerritories) {
    if (!Source || Source->OwningPlayer != InPlayerState ||
        Source->ArmyUnits <= 1) {
      continue;
    }

    const int32 SourceUnits = Source->ArmyUnits;
    const int32 SourceEnemyPressure =
        CachedStrategicContext.EnemyPressure.FindRef(Source);
    const bool bSourceBorder =
        CachedStrategicContext.BorderTerritories.Contains(Source);
    const bool bProtectsCapital =
        CachedStrategicContext.CapitalDefenseRing.Contains(Source) ||
        Source == CachedStrategicContext.Capital;

    for (ATerritory *Neighbor : Source->AdjacentTerritories) {
      if (!Neighbor || Neighbor->OwningPlayer == InPlayerState) {
        continue;
      }

      const int32 TargetUnits = Neighbor->ArmyUnits;
      const int32 StrengthDelta = SourceUnits - TargetUnits;
      if (StrengthDelta <= 0) {
        continue;
      }

      const bool bTargetIsCapital = Neighbor->bIsCapital;
      const bool bTargetThreatensCapital = CachedStrategicContext.Capital &&
                                          Neighbor->IsAdjacentTo(
                                              CachedStrategicContext.Capital);

      float Score = static_cast<float>(StrengthDelta) * 5.f;
      if (bTargetIsCapital) {
        Score += 150.f;
      }

      switch (CurrentStrategy) {
      case EAIStrategy::Offensive:
        Score += CachedStrategicContext.EnemyCapitalApproach.Contains(Source)
                     ? 40.f
                     : 0.f;
        Score += FMath::Max(0, StrengthDelta - SourceEnemyPressure) * 2.f;
        break;
      case EAIStrategy::Defensive:
        if (!bTargetThreatensCapital && !bSourceBorder && !bProtectsCapital) {
          continue;
        }
        Score += bTargetThreatensCapital ? 80.f : 0.f;
        Score += FMath::Max(SourceEnemyPressure - TargetUnits, 0) * 2.f;
        break;
      case EAIStrategy::Hybrid:
      default:
        Score += bTargetThreatensCapital ? 40.f : 0.f;
        Score += bTargetIsCapital ? 80.f : 0.f;
        Score += StrengthDelta > 1 ? 15.f : 0.f;
        break;
      }

      if (Score <= 0.f) {
        continue;
      }

      int32 UnitsToSend =
          DetermineArmyToSend(CurrentStrategy, SourceUnits, TargetUnits);
      if (UnitsToSend <= 0) {
        continue;
      }

      if (bTargetIsCapital) {
        const int32 MaxMovable = SourceUnits - 1;
        if (MaxMovable < SkaldConstants::CapitalAttackArmyRequirement) {
          // Not enough available forces to meet the capital attack requirement.
          continue;
        }

        UnitsToSend = FMath::Min(UnitsToSend, MaxMovable);
        if (UnitsToSend < SkaldConstants::CapitalAttackArmyRequirement) {
          // The AI cannot commit the required force during the attack phase.
          continue;
        }

        if (!SkaldHelpers::MeetsCapitalAttackRequirement(true, UnitsToSend)) {
          continue;
        }
      }

      if (!SkaldHelpers::MeetsCapitalAttackRequirement(bTargetIsCapital,
                                                       UnitsToSend)) {
        continue;
      }

      if (!bFoundOption || Score > BestOption.Score) {
        BestOption.Source = Source;
        BestOption.Target = Neighbor;
        BestOption.Score = Score;
        BestOption.UnitsToSend = UnitsToSend;
        bFoundOption = true;
      }
    }
  }

  if (!bFoundOption || !BestOption.Source || !BestOption.Target) {
    return false;
  }

  HandleAttackRequested(BestOption.Source->TerritoryID,
                        BestOption.Target->TerritoryID,
                        BestOption.UnitsToSend, false);
  return true;
}

bool ASkaldAIController::ExecuteStrategicMovement(AWorldMap *WorldMap,
                                                  ASkaldPlayerState *InPlayerState) {
  if (!WorldMap || !InPlayerState || !TurnManager) {
    return false;
  }

  const int32 PlayerID = InPlayerState->GetPlayerId();
  if (PlayerID <= 0 ||
      TurnManager->GetMovementActionsRemaining(PlayerID) <= 0) {
    return false;
  }

  struct FMovementOption {
    ATerritory *Source = nullptr;
    ATerritory *Target = nullptr;
    float Score = 0.f;
    int32 UnitsToMove = 0;
  };

  FMovementOption BestOption;
  BestOption.Score = std::numeric_limits<float>::lowest();
  bool bFoundOption = false;

  for (ATerritory *Source : CachedStrategicContext.OwnedTerritories) {
    if (!Source || Source->OwningPlayer != InPlayerState ||
        Source->ArmyUnits <= 1) {
      continue;
    }

    const int32 SourceEnemyPressure =
        CachedStrategicContext.EnemyPressure.FindRef(Source);
    const bool bSourceBorder =
        CachedStrategicContext.BorderTerritories.Contains(Source);

    for (ATerritory *Neighbor : Source->AdjacentTerritories) {
      if (!Neighbor || Neighbor->OwningPlayer != InPlayerState) {
        continue;
      }

      const int32 TargetEnemyPressure =
          CachedStrategicContext.EnemyPressure.FindRef(Neighbor);
      const bool bTargetBorder =
          CachedStrategicContext.BorderTerritories.Contains(Neighbor);

      float Score = 0.f;
      switch (CurrentStrategy) {
      case EAIStrategy::Offensive:
        Score = (TargetEnemyPressure - SourceEnemyPressure) * 2.f;
        Score += bTargetBorder ? 20.f : 0.f;
        Score +=
            CachedStrategicContext.EnemyCapitalApproach.Contains(Neighbor)
                ? 25.f
                : 0.f;
        Score += !bSourceBorder ? 10.f : 0.f;
        break;
      case EAIStrategy::Defensive:
        if (Source == CachedStrategicContext.Capital) {
          continue;
        }
        Score = (SourceEnemyPressure - TargetEnemyPressure) * 2.f;
        Score += Neighbor == CachedStrategicContext.Capital ? 60.f : 0.f;
        Score += CachedStrategicContext.CapitalDefenseRing.Contains(Neighbor)
                     ? 25.f
                     : 0.f;
        Score += static_cast<float>(TargetEnemyPressure) * 1.5f;
        break;
      case EAIStrategy::Hybrid:
      default:
        Score = (TargetEnemyPressure - SourceEnemyPressure) * 1.5f;
        Score += bTargetBorder ? 15.f : 0.f;
        Score += Neighbor == CachedStrategicContext.Capital ? 25.f : 0.f;
        break;
      }

      if (Score <= 0.f) {
        continue;
      }

      const int32 MaxMovable = Source->ArmyUnits - 1;
      if (MaxMovable <= 0) {
        continue;
      }

      int32 UnitsToMove = 0;
      switch (CurrentStrategy) {
      case EAIStrategy::Offensive:
        UnitsToMove = FMath::Clamp(TargetEnemyPressure + 2, 1, MaxMovable);
        break;
      case EAIStrategy::Defensive:
        UnitsToMove = Neighbor == CachedStrategicContext.Capital
                           ? MaxMovable
                           : FMath::Clamp(TargetEnemyPressure + 1, 1, MaxMovable);
        UnitsToMove = FMath::Min(UnitsToMove, MaxMovable);
        break;
      case EAIStrategy::Hybrid:
      default:
        UnitsToMove = FMath::Clamp((TargetEnemyPressure + MaxMovable) / 2, 1,
                                   MaxMovable);
        break;
      }

      if (UnitsToMove <= 0) {
        continue;
      }

      if (!bFoundOption || Score > BestOption.Score) {
        BestOption.Source = Source;
        BestOption.Target = Neighbor;
        BestOption.Score = Score;
        BestOption.UnitsToMove = UnitsToMove;
        bFoundOption = true;
      }
    }
  }

  if (!bFoundOption || !BestOption.Source || !BestOption.Target) {
    return false;
  }

  HandleMoveRequested(BestOption.Source->TerritoryID,
                      BestOption.Target->TerritoryID,
                      BestOption.UnitsToMove);
  return true;
}

int32 ASkaldAIController::DetermineArmyToSend(EAIStrategy Strategy,
                                              int32 SourceUnits,
                                              int32 TargetUnits) const {
  const int32 MaxMovable = SourceUnits - 1;
  if (MaxMovable <= 0) {
    return 0;
  }

  const int32 StrengthDelta = SourceUnits - TargetUnits;
  switch (Strategy) {
  case EAIStrategy::Offensive:
    return FMath::Clamp(SourceUnits - FMath::Max(1, SourceUnits / 4), 1,
                        MaxMovable);
  case EAIStrategy::Defensive:
    return FMath::Clamp(TargetUnits + FMath::Max(1, StrengthDelta / 2), 1,
                        MaxMovable);
  case EAIStrategy::Hybrid:
  default:
    return FMath::Clamp(TargetUnits + FMath::Max(1, StrengthDelta / 3), 1,
                        MaxMovable);
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

  USkaldGameInstance *GameInstance = GetGameInstance<USkaldGameInstance>();
  if (!GameInstance) {
    return;
  }

  FS_BattlePayload &Battle = GameInstance->PendingBattle;
  const int32 PlayerId = PS->GetPlayerId();
  FString PlayerName = PS->PlayerDisplayName;
  if (PlayerName.IsEmpty()) {
    PlayerName = PS->GetResolvedPlayerName(TEXT("SkaldAIController"));
  }

  const bool bIsAIPlayer = PS->bIsAI;

  auto MatchesFaction = [&](ESkaldFaction ParticipantFaction) {
    return ParticipantFaction != ESkaldFaction::None &&
           ParticipantFaction == PS->Faction;
  };

  bool bMatchedAttacker = false;
  bool bMatchedDefender = false;

  if (PlayerId > 0) {
    if (Battle.AttackerPlayerID == PlayerId) {
      bAIControlsAttackerSide = true;
      bMatchedAttacker = true;
    }

    if (Battle.DefenderPlayerID == PlayerId) {
      bAIControlsDefenderSide = true;
      bMatchedDefender = true;
    }
  }

  if (!bMatchedAttacker || !bMatchedDefender) {
    if (!PlayerName.IsEmpty()) {
      if (!bMatchedAttacker && !Battle.AttackerDisplayName.IsEmpty() &&
          PlayerName.Equals(Battle.AttackerDisplayName,
                            ESearchCase::IgnoreCase)) {
        bAIControlsAttackerSide = true;
        bMatchedAttacker = true;
      }

      if (!bMatchedDefender && !Battle.DefenderDisplayName.IsEmpty() &&
          PlayerName.Equals(Battle.DefenderDisplayName,
                            ESearchCase::IgnoreCase)) {
        bAIControlsDefenderSide = true;
        bMatchedDefender = true;
      }
    }
  }

  if (bIsAIPlayer) {
    if (!bMatchedAttacker && MatchesFaction(Battle.AttackerFaction)) {
      bAIControlsAttackerSide = true;
      bMatchedAttacker = true;
    }

    if (!bMatchedDefender && MatchesFaction(Battle.DefenderFaction)) {
      bAIControlsDefenderSide = true;
      bMatchedDefender = true;
    }

    auto SideHasPlayerIdentity = [](const FS_BattlePayload &Payload,
                                    bool bForAttackers) {
      const int32 ParticipantId =
          bForAttackers ? Payload.AttackerPlayerID : Payload.DefenderPlayerID;
      const FString &ParticipantName = bForAttackers
                                           ? Payload.AttackerDisplayName
                                           : Payload.DefenderDisplayName;
      return ParticipantId > 0 || !ParticipantName.IsEmpty();
    };

    if (!bMatchedAttacker && !bMatchedDefender) {
      const bool bAttackerHasIdentity = SideHasPlayerIdentity(Battle, true);
      const bool bDefenderHasIdentity = SideHasPlayerIdentity(Battle, false);

      if (!bAttackerHasIdentity && bDefenderHasIdentity) {
        bAIControlsAttackerSide = true;
        bMatchedAttacker = true;
      } else if (!bDefenderHasIdentity && bAttackerHasIdentity) {
        bAIControlsDefenderSide = true;
        bMatchedDefender = true;
      }
    }

    if (!bMatchedAttacker && !bMatchedDefender) {
      if (Battle.bAttackerIsAI && !Battle.bDefenderIsAI) {
        bAIControlsAttackerSide = true;
        bMatchedAttacker = true;
      } else if (Battle.bDefenderIsAI && !Battle.bAttackerIsAI) {
        bAIControlsDefenderSide = true;
        bMatchedDefender = true;
      }
    }
  }

  if (bAIControlsAttackerSide && bIsAIPlayer) {
    Battle.bAttackerIsAI = true;
  }

  if (bAIControlsDefenderSide && bIsAIPlayer) {
    Battle.bDefenderIsAI = true;
  }

  if (!bAIControlsAttackerSide && !bAIControlsDefenderSide && bIsAIPlayer) {
    UE_LOG(LogSkald, Warning,
           TEXT("ASkaldAIController %s could not resolve a battle side."),
           *GetName());
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

float ASkaldAIController::EvaluatePassiveAttackBonus(
    const AFighterPawn *Fighter, const AFighterPawn *Target) const {
  if (!Fighter || !Target) {
    return 0.0f;
  }

  const USkaldAbilityComponent *Ability = Fighter->GetAbilityComponent();
  if (!Ability) {
    return 0.0f;
  }

  const FSkaldAbilityDefinition Passive = Ability->GetPassiveAbility();
  if (Passive.AbilityId == TEXT("Ability_Human_Passive")) {
    UWorld *World = GetWorld();
    if (!World) {
      return 0.0f;
    }

    int32 AdjacentAllies = 0;
    for (TActorIterator<AFighterPawn> It(World); It; ++It) {
      AFighterPawn *Ally = *It;
      if (!Ally || Ally == Fighter || !Ally->IsAlive()) {
        continue;
      }

      if (Ally->Faction != Fighter->Faction) {
        continue;
      }

      if (Fighter->GetFootprintDistanceToFighter(Ally) <= 1) {
        ++AdjacentAllies;
      }
    }

    if (AdjacentAllies > 0) {
      return PassiveHumanAdjacencyBonus * AdjacentAllies;
    }
  }

  if (Passive.AbilityId == TEXT("Ability_Ravpack_Passive")) {
    if (Target->Stats.Health <= Fighter->Stats.AttackDamage) {
      return AttackLowHealthBonus * 0.25f;
    }
  }

  return 0.0f;
}

float ASkaldAIController::EvaluateBestAttackScoreFromAnchor(
    const AFighterPawn *Fighter, const FIntPoint &Anchor,
    AFighterPawn **OutTarget, int32 RangeOverride) const {
  if (OutTarget) {
    *OutTarget = nullptr;
  }

  if (!Fighter) {
    return -TNumericLimits<float>::Max();
  }

  UGridOverlayComponent *Grid = Fighter->GetGrid();
  if (!Grid) {
    return -TNumericLimits<float>::Max();
  }

  UWorld *World = GetWorld();
  if (!World) {
    return -TNumericLimits<float>::Max();
  }

  const int32 Range = RangeOverride == INDEX_NONE ? Fighter->Stats.AttackRange
                                                  : RangeOverride;
  const TArray<FIntPoint> SourceCells = Fighter->GetOccupiedCells(Anchor);

  float BestScore = -TNumericLimits<float>::Max();
  AFighterPawn *BestTarget = nullptr;

  for (TActorIterator<AFighterPawn> It(World); It; ++It) {
    AFighterPawn *Candidate = *It;
    if (!Candidate || Candidate == Fighter || !Candidate->IsAlive()) {
      continue;
    }

    if (ControlsFighter(Candidate)) {
      continue;
    }

    const TArray<FIntPoint> EnemyCells = Candidate->GetOccupiedCells();
    bool bHasLineOfSight = false;
    int32 BestDistance = TNumericLimits<int32>::Max();

    for (const FIntPoint &SelfCell : SourceCells) {
      for (const FIntPoint &EnemyCell : EnemyCells) {
        const int32 Distance = FMath::Max(
            FMath::Abs(SelfCell.X - EnemyCell.X),
            FMath::Abs(SelfCell.Y - EnemyCell.Y));

        if (Range >= 0 && Distance > Range) {
          continue;
        }

        if (!Grid->HasLineOfSight(SelfCell, EnemyCell)) {
          continue;
        }

        bHasLineOfSight = true;
        if (Distance < BestDistance) {
          BestDistance = Distance;
        }
      }
    }

    if (!bHasLineOfSight) {
      continue;
    }

    const FFighterStats &SelfStats = Fighter->Stats;
    const FFighterStats &EnemyStats = Candidate->Stats;

    float Score = SelfStats.AttackDamage * AttackDamageWeight;
    Score += SelfStats.AttackDice * AttackDiceWeight;
    Score -= EnemyStats.Defence * 0.5f;
    Score -= BestDistance * AttackDistancePenalty;
    Score += (EnemyStats.AttackDamage + EnemyStats.AttackDice) *
             AttackThreatWeight;

    if (SelfStats.AttackDamage >= EnemyStats.Health) {
      Score += AttackKillPriorityBonus;
    } else if (SelfStats.AttackDamage + SelfStats.CriticalBonusDamage >=
               EnemyStats.Health) {
      Score += AttackLowHealthBonus;
    }

    Score += EvaluatePassiveAttackBonus(Fighter, Candidate);

    if (Score > BestScore) {
      BestScore = Score;
      BestTarget = Candidate;
    }
  }

  if (OutTarget) {
    *OutTarget = BestTarget;
  }

  return BestScore;
}

float ASkaldAIController::EvaluateBestAttackScore(
    const AFighterPawn *Fighter) const {
  AFighterPawn *DummyTarget = nullptr;
  return EvaluateBestAttackScoreFromAnchor(Fighter, Fighter->GetCurrentCell(),
                                           &DummyTarget);
}

bool ASkaldAIController::ShouldStrafeBeforeAttacking(
    const AFighterPawn *Fighter) const {
  if (!Fighter || bPendingFighterMovedThisActivation) {
    return false;
  }

  const USkaldAbilityComponent *Ability = Fighter->GetAbilityComponent();
  if (!Ability) {
    return false;
  }

  const FSkaldAbilityDefinition Passive = Ability->GetPassiveAbility();
  if (Passive.AbilityId != TEXT("Ability_Elf_Passive")) {
    return false;
  }

  return Fighter->Stats.Movement > 0 && Fighter->GetActionsRemaining() > 1;
}

bool ASkaldAIController::TryExecuteBestAbility(AFighterPawn *Fighter,
                                               bool &bOutTriggeredAttack) {
  bOutTriggeredAttack = false;

  if (!Fighter) {
    return false;
  }

  USkaldAbilityComponent *AbilityComponent = Fighter->GetAbilityComponent();
  if (!AbilityComponent) {
    return false;
  }

  if (AbilitiesTriedThisActivation.Num() >= MaxAbilityAttemptsPerActivation) {
    return false;
  }

  struct FAiAbilityOption {
    ESkaldAbilitySlot Slot = ESkaldAbilitySlot::Ability1;
    const FSkaldAbilityState *State = nullptr;
    float Score = 0.0f;
    AFighterPawn *Target = nullptr;
    FIntPoint Cell = FIntPoint(INDEX_NONE, INDEX_NONE);
    bool bTriggersAttack = false;
    bool bPlacesTrap = false;
    bool bSelfCast = false;
  };

  TArray<FAiAbilityOption> Options;
  Options.Reserve(3);

  const ESkaldAbilitySlot Slots[] = {ESkaldAbilitySlot::Ability1,
                                     ESkaldAbilitySlot::Ability2,
                                     ESkaldAbilitySlot::Ability3};

  for (ESkaldAbilitySlot Slot : Slots) {
    const FSkaldAbilityState *State = AbilityComponent->FindAbilityState(Slot);
    if (!State || !State->Definition.IsValid() || State->Definition.bIsPassive) {
      continue;
    }

    if (AbilitiesTriedThisActivation.Contains(State->Definition.AbilityId)) {
      continue;
    }

    FText FailureReason;
    if (!AbilityComponent->CanActivateAbility(Slot, &FailureReason)) {
      continue;
    }

    const FName AbilityId = State->Definition.AbilityId;
    const FSkaldAbilityTargetingInfo Targeting =
        GetAbilityTargetingInfo(AbilityId);
    const int32 Range = Targeting.RangeOverride == INDEX_NONE
                            ? Fighter->Stats.AttackRange
                            : Targeting.RangeOverride;
    const float Intrinsic = AbilityBaselineScore +
                            EvaluateAbilityIntrinsicBonus(AbilityId);

    if (Targeting.CommandMode == EBattleCommandMode::None) {
      float Score = Intrinsic;
      if (State->Definition.CostType != ESkaldAbilityCostType::Action) {
        Score += 1.0f;
      }
      Options.Add({Slot, State, Score, nullptr,
                   FIntPoint(INDEX_NONE, INDEX_NONE), false, false, true});
      continue;
    }

    if (Targeting.CommandMode == EBattleCommandMode::AbilityTargetEnemy) {
      AFighterPawn *BestTarget = nullptr;
      const float AttackScore = EvaluateBestAttackScoreFromAnchor(
          Fighter, Fighter->GetCurrentCell(), &BestTarget, Range);
      if (BestTarget) {
        const float Score = AttackScore + Intrinsic;
        Options.Add({Slot, State, Score, BestTarget, FIntPoint(), true, false,
                     false});
      }
      continue;
    }

    if (Targeting.CommandMode == EBattleCommandMode::AbilityTargetCell) {
      FIntPoint TrapCell;
      float TrapScore = 0.0f;
      if (FindBestTrapPlacement(Fighter, AbilityId, Targeting, Range, TrapCell,
                                TrapScore)) {
        const float Score = TrapScore + Intrinsic;
        Options.Add({Slot, State, Score, nullptr, TrapCell, false, true,
                     false});
      }
      continue;
    }
  }

  if (Options.Num() == 0) {
    return false;
  }

  Options.Sort([](const FAiAbilityOption &A, const FAiAbilityOption &B) {
    return A.Score > B.Score;
  });

  FText Error;
  for (const FAiAbilityOption &Option : Options) {
    AbilitiesTriedThisActivation.Add(Option.State->Definition.AbilityId);

    if (Option.bSelfCast) {
      if (USkaldAbilityComponent *AbilityComp = Fighter->GetAbilityComponent()) {
        if (AbilityComp->TryBeginAbility(Option.Slot, Error)) {
          return true;
        }
      }
      continue;
    }

    if (Option.bPlacesTrap) {
      if (TryExecuteAbilityAtCell(Fighter, Option.Slot, Option.Cell, Error)) {
        return true;
      }
      continue;
    }

    if (Option.Target) {
      if (TryExecuteAbilityOnFighter(Fighter, Option.Slot, Option.Target, Error)) {
        bOutTriggeredAttack = true;
        return true;
      }
    }
  }

  return false;
}

bool ASkaldAIController::TryExecuteBestAttack(AFighterPawn *Fighter) {
  if (!Fighter || Fighter->ActionsRemaining <= 0) {
    return false;
  }

  UGridOverlayComponent *Grid = Fighter->GetGrid();
  if (!Grid) {
    return false;
  }

  AFighterPawn *Target = nullptr;
  EvaluateBestAttackScoreFromAnchor(Fighter, Fighter->GetCurrentCell(),
                                    &Target);
  if (!Target) {
    return false;
  }

  const int32 ActionsBefore = Fighter->ActionsRemaining;
  Fighter->PerformAttack(Target);
  return Fighter->ActionsRemaining < ActionsBefore;
}

bool ASkaldAIController::FindBestTrapPlacement(
    AFighterPawn *Fighter, const FName AbilityId,
    const FSkaldAbilityTargetingInfo &Targeting, int32 RangeOverride,
    FIntPoint &OutCell, float &OutScore) const {
  if (!Fighter) {
    return false;
  }

  UGridOverlayComponent *Grid = Fighter->GetGrid();
  if (!Grid) {
    return false;
  }

  UWorld *World = GetWorld();
  if (!World) {
    return false;
  }

  const int32 Range = RangeOverride == INDEX_NONE ? Fighter->Stats.AttackRange
                                                  : RangeOverride;
  const TArray<FIntPoint> SourceCells = Fighter->GetOccupiedCells();

  float BestScore = -TNumericLimits<float>::Max();
  FIntPoint BestCell(INDEX_NONE, INDEX_NONE);

  const int32 GridWidth = Grid->GetWidth();
  const int32 GridHeight = Grid->GetLength();

  for (int32 Y = 0; Y < GridHeight; ++Y) {
    for (int32 X = 0; X < GridWidth; ++X) {
      const FIntPoint Cell(X, Y);
      if (!Grid->IsCellInBounds(Cell)) {
        continue;
      }

      if (Range >= 0 && Fighter->GetFootprintDistanceToCell(Cell) > Range) {
        continue;
      }

      if (Grid->IsOccupied(Cell) || Grid->IsObscured(Cell) ||
          Grid->HasTrapMarker(Cell)) {
        continue;
      }

      if (!Targeting.bAllowEmptyCell && !Grid->IsOccupied(Cell)) {
        continue;
      }

      if (Targeting.bRequireLineOfSight) {
        bool bHasLineOfSight = false;
        for (const FIntPoint &SelfCell : SourceCells) {
          if (Grid->HasLineOfSight(SelfCell, Cell)) {
            bHasLineOfSight = true;
            break;
          }
        }

        if (!bHasLineOfSight) {
          continue;
        }
      }

      int32 AdjacentEnemies = 0;
      float ThreatScore = 0.0f;

      for (TActorIterator<AFighterPawn> It(World); It; ++It) {
        AFighterPawn *Enemy = *It;
        if (!Enemy || !Enemy->IsAlive() || ControlsFighter(Enemy)) {
          continue;
        }

        const int32 Distance = Enemy->GetFootprintDistanceToCell(Cell);
        if (Distance <= 1) {
          ++AdjacentEnemies;
          ThreatScore += Enemy->Stats.AttackDamage + Enemy->Stats.AttackDice;
        } else if (Distance <= 3) {
          ThreatScore += FMath::Max(0, 3 - Distance);
        }
      }

      if (AdjacentEnemies <= 0 && ThreatScore <= 0.0f) {
        continue;
      }

      const float Score = AdjacentEnemies * TrapPlacementAdjacencyScore +
                          ThreatScore + EvaluateAbilityIntrinsicBonus(AbilityId);
      if (Score > BestScore) {
        BestScore = Score;
        BestCell = Cell;
      }
    }
  }

  if (BestCell.X != INDEX_NONE) {
    OutCell = BestCell;
    OutScore = BestScore;
    return true;
  }

  return false;
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
  if (!Grid->IsCellInBounds(StartCell)) {
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

  struct FAIMovementFrontierNode {
    FIntPoint Cell;
    int32 PathCost;
  };

  auto FrontierComparator = [](const FAIMovementFrontierNode &A,
                               const FAIMovementFrontierNode &B) {
    return A.PathCost < B.PathCost;
  };

  TArray<FAIMovementFrontierNode> Frontier;
  Frontier.Reserve(32);
  Frontier.Add({StartCell, 0});

  TMap<FIntPoint, int32> BestPathCosts;
  BestPathCosts.Reserve(32);
  BestPathCosts.Add(StartCell, 0);

  float BestScore = -TNumericLimits<float>::Max();
  FIntPoint BestAnchor = StartCell;
  FIntPoint PassiveAnchor = StartCell;
  int32 PassiveStepCost = TNumericLimits<int32>::Max();

  while (Frontier.Num() > 0) {
    Frontier.Sort(FrontierComparator);

    const FAIMovementFrontierNode Node = Frontier[0];
    Frontier.RemoveAt(0, 1, EAllowShrinking::No);

    const FIntPoint Cell = Node.Cell;
    const int32 DistanceFromStart = Node.PathCost;

    const int32 *RecordedCost = BestPathCosts.Find(Cell);
    if (!RecordedCost || DistanceFromStart > *RecordedCost) {
      continue;
    }

    for (const FIntPoint &Dir : Directions) {
      const FIntPoint Next = Cell + Dir;

      const int32 MovementCost =
          FMath::Max(1, Fighter->GetMovementStepCost(Cell, Next, Grid));
      const int32 StepCost = DistanceFromStart + MovementCost;
      if (StepCost > MaxSteps) {
        continue;
      }

      if (!CanOccupyAnchor(Next)) {
        continue;
      }

      if (!IsDiagonalStepClear(Cell, Next)) {
        continue;
      }

      const int32 *ExistingCost = BestPathCosts.Find(Next);
      if (ExistingCost && StepCost >= *ExistingCost) {
        continue;
      }

      BestPathCosts.Add(Next, StepCost);
      Frontier.Add({Next, StepCost});

      AFighterPawn *ProjectedTarget = nullptr;
      const float AttackScore = EvaluateBestAttackScoreFromAnchor(
          Fighter, Next, &ProjectedTarget);
      const int32 CandidateDistance = ComputeDistanceFromAnchor(Next);
      const float DistanceImprovement = static_cast<float>(CurrentDistance -
                                                           CandidateDistance);
      float Score = AttackScore;

      if (!ProjectedTarget) {
        Score = DistanceImprovement * AttackDamageWeight - StepCost;
      }

      if (ShouldStrafeBeforeAttacking(Fighter)) {
        Score += PassiveElfMovementIncentive;
      }

      if (Score > BestScore) {
        BestScore = Score;
        BestAnchor = Next;
      }

      if (StepCost > 0 && StepCost < PassiveStepCost) {
        PassiveStepCost = StepCost;
        PassiveAnchor = Next;
      }
    }
  }

  if (BestAnchor == StartCell) {
    if (ShouldStrafeBeforeAttacking(Fighter) &&
        PassiveStepCost != TNumericLimits<int32>::Max()) {
      BestAnchor = PassiveAnchor;
    } else {
      return false;
    }
  }

  const int32 ActionsBefore = Fighter->ActionsRemaining;
  Fighter->MoveToCell(BestAnchor);

  const bool bActionConsumed =
      Fighter->ActionsRemaining < ActionsBefore || Fighter->IsMoving();
  if (bActionConsumed) {
    bPendingFighterMovedThisActivation = true;
  }
  return bActionConsumed;
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

void ASkaldAIController::ScheduleMovementCompletionPoll() {
  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(FighterActionTimerHandle);
    World->GetTimerManager().SetTimer(
        FighterActionTimerHandle, this,
        &ASkaldAIController::ProcessQueuedActivationIntent,
        MovementCompletionPollInterval, false);
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

  if (bAwaitingMovementCompletion) {
    if (!Fighter || !Fighter->IsMoving()) {
      bAwaitingMovementCompletion = false;

      if (!ShouldContinueActivation(Fighter)) {
        CompleteFighterActivation();
        return;
      }

      QueueActivationIntent(Fighter, EAIBattleActivationIntent::UseAbility);
      QueueActivationIntent(Fighter, EAIBattleActivationIntent::Attack);
      ScheduleNextActivationAttempt();
      return;
    }

    ScheduleMovementCompletionPoll();
    return;
  }

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
  bool bQueuedFollowUp = false;

  switch (Intent) {
  case EAIBattleActivationIntent::UseAbility: {
    bool bTriggeredAttack = false;
    bActionTaken = TryExecuteBestAbility(Fighter, bTriggeredAttack);
    bRequiresAttackResolution = bTriggeredAttack;

    if (!bActionTaken) {
      QueueActivationIntent(Fighter, EAIBattleActivationIntent::Attack);
      ScheduleNextActivationAttempt();
      return;
    }
    break;
  }
  case EAIBattleActivationIntent::Attack:
    if (ShouldStrafeBeforeAttacking(Fighter)) {
      QueueActivationIntent(Fighter, EAIBattleActivationIntent::Move);
      ScheduleNextActivationAttempt();
      return;
    }

    bActionTaken = TryExecuteBestAttack(Fighter);
    bRequiresAttackResolution = bActionTaken;
    if (!bActionTaken && ShouldContinueActivation(Fighter)) {
      QueueActivationIntent(Fighter, EAIBattleActivationIntent::Move);
      ScheduleNextActivationAttempt();
      return;
    }
    break;
  case EAIBattleActivationIntent::Move:
    bActionTaken = TryMoveTowardsNearestEnemy(Fighter);
    if (bActionTaken) {
      if (Fighter && Fighter->IsMoving()) {
        bAwaitingMovementCompletion = true;
        ScheduleMovementCompletionPoll();
        return;
      }

      if (ShouldContinueActivation(Fighter)) {
        QueueActivationIntent(Fighter, EAIBattleActivationIntent::UseAbility);
        QueueActivationIntent(Fighter, EAIBattleActivationIntent::Attack);
        bQueuedFollowUp = true;
      }
    }
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

  if (!bQueuedFollowUp) {
    QueueActivationIntent(Fighter, EAIBattleActivationIntent::Attack);
  }
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

  QueueActivationIntent(Fighter, EAIBattleActivationIntent::UseAbility);
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
  bAwaitingMovementCompletion = false;
  AbilitiesTriedThisActivation.Reset();
  bPendingFighterMovedThisActivation = false;
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
  bPendingFighterMovedThisActivation = false;
  AbilitiesTriedThisActivation.Reset();

  Fighter->OnQueuedAttackFinalized.RemoveAll(this);
  Fighter->OnQueuedAttackFinalized.AddUObject(
      this, &ASkaldAIController::HandleQueuedAttackFinalized);

  QueueActivationIntent(Fighter, EAIBattleActivationIntent::UseAbility);
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

