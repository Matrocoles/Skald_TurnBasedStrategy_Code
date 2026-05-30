#include "Skald_AIController.h"
#include "AIController.h"
#include "Algo/Sort.h"
#include "Abilities/SkaldAbilityComponent.h"
#include "Abilities/SkaldAbilityTypes.h"
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
#include "Skald_BattleLevelManager.h"
#include "Skald_GameInstance.h"
#include "Skald_GameState.h"
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
constexpr TCHAR EnemyAwaitingResultsMessage[] =
    TEXT("Enemy is reviewing battle results...");
constexpr TCHAR EnemyStrategyPlanningMessage[] =
    TEXT("Enemy is evaluating the battlefield...");
constexpr TCHAR EnemyPostBattleReevaluationMessage[] =
    TEXT("Enemy is reassessing their offensive...");
constexpr TCHAR EnemyPostBattleResumeMessage[] =
    TEXT("Enemy resumes their assault.");
constexpr TCHAR EnemyPostBattleStandDownMessage[] =
    TEXT("Enemy stands down from their assault.");
constexpr float StrategyPlanningDelayFraction = 0.5f;
constexpr float MinimumStrategyPlanningDelay = 0.75f;
constexpr float PostBattleEvaluationPauseSeconds = 8.0f;
constexpr float PostBattleAttackScoreThreshold = 75.0f;
constexpr int32 MovementMaxDetourDistance = 2;
constexpr float MovementHuntDistanceWeight = 10.0f;
constexpr float MovementHuntStepWeight = 1.0f;

enum class EAIFactionAbilityCategory : uint8 {
  None,
  AttackDamageBuff,
  AttackDebuffEnemy,
  AoEAttack,
  MovementBuff,
  AllySupport
};

TMap<FName, EAIFactionAbilityCategory> BuildDefaultAbilityCategoryMap() {
  return {
      {TEXT("Ability_Inflicted_Line"), EAIFactionAbilityCategory::AttackDamageBuff},
      {TEXT("Ability_Inflicted_Elite"), EAIFactionAbilityCategory::AoEAttack},
      {TEXT("Ability_Empire_Elite"), EAIFactionAbilityCategory::AoEAttack},
      {TEXT("Ability_Gnoll_Elite"), EAIFactionAbilityCategory::AttackDamageBuff},
      {TEXT("Ability_Ravpack_Elite"), EAIFactionAbilityCategory::AttackDamageBuff},
      {TEXT("Ability_Lizardfolk_Line"), EAIFactionAbilityCategory::AttackDamageBuff},
      {TEXT("Ability_Lizard_Line"), EAIFactionAbilityCategory::AttackDamageBuff},
      {TEXT("Ability_Lizardfolk_Elite"), EAIFactionAbilityCategory::AoEAttack},
      {TEXT("Ability_Lizard_Elite"), EAIFactionAbilityCategory::AoEAttack},
      {TEXT("Ability_Goblin_Elite"), EAIFactionAbilityCategory::AttackDamageBuff},
      {TEXT("Ability_Elf_Elite"), EAIFactionAbilityCategory::AttackDamageBuff},
      {TEXT("Ability_Elf_Line"), EAIFactionAbilityCategory::AttackDamageBuff},
      {TEXT("Ability_Inflicted_Skirmish"), EAIFactionAbilityCategory::AttackDebuffEnemy},
      {TEXT("Ability_Ravpack_Skirmish"), EAIFactionAbilityCategory::AttackDebuffEnemy},
      {TEXT("Ability_Gnoll_Skirmish"), EAIFactionAbilityCategory::AttackDebuffEnemy},
      {TEXT("Ability_Goblin_Skirmish"), EAIFactionAbilityCategory::AttackDebuffEnemy},
      {TEXT("Ability_Goblin_Line"), EAIFactionAbilityCategory::AttackDebuffEnemy},
      {TEXT("Ability_Empire_Skirmish"), EAIFactionAbilityCategory::AttackDebuffEnemy},
      {TEXT("Ability_Undead_Line"), EAIFactionAbilityCategory::AttackDamageBuff},
      {TEXT("Ability_Undead_Skirmish"), EAIFactionAbilityCategory::AttackDebuffEnemy},
      {TEXT("Ability_Empire_Line"), EAIFactionAbilityCategory::AllySupport},
      {TEXT("Ability_Gnoll_Line"), EAIFactionAbilityCategory::AllySupport},
      {TEXT("Ability_Human_Skirmish"), EAIFactionAbilityCategory::AllySupport},
      {TEXT("Ability_Human_Line"), EAIFactionAbilityCategory::AllySupport},
      {TEXT("Ability_Human_Elite"), EAIFactionAbilityCategory::AllySupport},
      {TEXT("Ability_Frogfolk_Line"), EAIFactionAbilityCategory::AllySupport},
      {TEXT("Ability_Frog_Line"), EAIFactionAbilityCategory::AllySupport},
      {TEXT("Ability_Frogfolk_Elite"), EAIFactionAbilityCategory::AllySupport},
      {TEXT("Ability_Frog_Elite"), EAIFactionAbilityCategory::AllySupport},
      {TEXT("Ability_Orc_Skirmish"), EAIFactionAbilityCategory::MovementBuff},
      {TEXT("Ability_Orc_Elite"), EAIFactionAbilityCategory::AllySupport},
      {TEXT("Ability_Lizardfolk_Skirmish"), EAIFactionAbilityCategory::AttackDebuffEnemy},
      {TEXT("Ability_Lizard_Skirmish"), EAIFactionAbilityCategory::AttackDebuffEnemy},
      {TEXT("Ability_Ravpack_Line"), EAIFactionAbilityCategory::AttackDebuffEnemy},
      {TEXT("Ability_Dwarf_Line"), EAIFactionAbilityCategory::AttackDebuffEnemy},
      {TEXT("Ability_Dwarf_Skirmish"), EAIFactionAbilityCategory::AttackDamageBuff},
      {TEXT("Ability_Dwarf_Elite"), EAIFactionAbilityCategory::AoEAttack}
  };
}

EAIFactionAbilityCategory ResolveFactionAbilityCategory(
    const FName &AbilityId, const UDataTable *AbilityCategoryTable) {
  static const TMap<FName, EAIFactionAbilityCategory> DefaultAbilityCategoryMap =
      BuildDefaultAbilityCategoryMap();

  if (AbilityCategoryTable) {
    TArray<FSkaldAIAbilityCategoryRow *> Rows;
    AbilityCategoryTable->GetAllRows(TEXT("AIAbilityCategory"), Rows);
    for (const FSkaldAIAbilityCategoryRow *Row : Rows) {
      if (!Row || Row->AbilityId.IsNone()) {
        continue;
      }
      if (Row->AbilityId == AbilityId) {
        return static_cast<EAIFactionAbilityCategory>(Row->Category);
      }
    }
  }

  if (const EAIFactionAbilityCategory *Found = DefaultAbilityCategoryMap.Find(AbilityId)) {
    return *Found;
  }

  return EAIFactionAbilityCategory::None;
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
    GameInstance->OnBattleMapStateChanged.AddUniqueDynamic(
        this, &ASkaldAIController::HandleBattleMapStateChanged);
  }

  SetupBattleAutomation();

  if (AbilityCategoryTable) {
    TArray<FSkaldAIAbilityCategoryRow *> Rows;
    AbilityCategoryTable->GetAllRows(TEXT("AIAbilityCategoryValidation"), Rows);
    UE_LOG(LogSkald, Log, TEXT("AI ability category table loaded with %d rows."),
           Rows.Num());
  } else {
    UE_LOG(LogSkald, Warning,
           TEXT("AI ability category table not configured; falling back to native defaults."));
  }

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

void ASkaldAIController::ShowMainHUD() {
  // AI controllers do not own local HUD/UI.
}

void ASkaldAIController::HideMainHUD() {
  // AI controllers do not own local HUD/UI.
}

void ASkaldAIController::ShowBattleResultWidget(
    const FBattleResultDisplayData & /*DisplayData*/) {
  // AI controllers do not display local battle-result widgets.
}

void ASkaldAIController::InitializeBattleHUD() {
  // AI controllers do not create local battle HUD widgets.
}

void ASkaldAIController::ShowFighterSelectionUI(
    int32 /*MaxBudget*/, ESkaldFaction /*Faction*/) {
  // AI controllers do not create local fighter-selection widgets.
}

void ASkaldAIController::InitializeFighterSelectionIfNeeded() {
  // AI controllers do not initialize local fighter-selection widgets.
}

void ASkaldAIController::ShowPrepareForBattlePromptLocal(
    const FPrepareForBattlePromptData &PromptData) {
  // AI controllers never display local widgets. Avoid the base implementation
  // because it can queue UI prompt retries/timers intended for human players.
  HidePrepareForBattlePromptLocal();
  ProcessPrepareForBattlePrompt(PromptData);
}

void ASkaldAIController::HandlePrepareForBattlePromptDirect(
    const FPrepareForBattlePromptData &PromptData) {
  ProcessPrepareForBattlePrompt(PromptData);
}

void ASkaldAIController::ProcessPrepareForBattlePrompt(
    const FPrepareForBattlePromptData &PromptData) {
  
  if (bAutoRetreatPending) {
    return;
  }

  const EAIPrepareForBattleDecision Decision =
      DeterminePrepareForBattleDecision(PromptData);
  switch (Decision) {
  case EAIPrepareForBattleDecision::AttemptRetreat:
    UE_LOG(LogSkaldReady, Log,
           TEXT("AI controller %s initiating retreat from prepare-for-battle prompt."),
           *GetName());
    bAutoRetreatPending = true;
    ServerRequestRetreat();
    break;
  case EAIPrepareForBattleDecision::Ready:
    UE_LOG(LogSkaldReady, Verbose,
           TEXT("AI controller %s auto-readying for battle."), *GetName());
    ServerSetReadyForBattle(true);
    break;
  default:
    break;
  }
}

void ASkaldAIController::StartTurn() {
  DecisionIterationCount = 0;
  bAwaitingBattleTransition = false;
  bPendingPhaseAdvance = false;
  bStrategyEvaluatedThisTurn = false;
  CachedStrategicContext = FStrategicContext();
  CurrentStrategy = EAIStrategy::Hybrid;
  AttacksInitiatedThisPhase = 0;
  bPostBattleEvaluationPending = false;
  bPostBattlePauseActive = false;
  ClearDecisionTimers();
  MakeAIDecision();
}

void ASkaldAIController::EndTurn() {
  ClearDecisionTimers();
  bAwaitingBattleTransition = false;
  bPendingPhaseAdvance = false;
  bPostBattleEvaluationPending = false;
  bPostBattlePauseActive = false;
  AttacksInitiatedThisPhase = 0;
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

bool ASkaldAIController::BeginArmyPlacementSetupTurn() {
  if (!TurnManager) {
    return false;
  }

  ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>();
  if (!PS || PS->DeployableUnits <= 0) {
    return false;
  }

  AWorldMap *WorldMap = Cast<AWorldMap>(UGameplayStatics::GetActorOfClass(
      GetWorld(), AWorldMap::StaticClass()));
  if (!WorldMap) {
    return false;
  }

  RefreshStrategicContext(WorldMap, PS);
  if (CachedStrategicContext.OwnedTerritories.Num() == 0) {
    return false;
  }

  bArmyPlacementSetupInProgress = true;
  ExecuteStrategicArmyPlacement(WorldMap, PS, true);

  if (!bAnimatingArmyPlacement) {
    bArmyPlacementSetupInProgress = false;
    return false;
  }

  return true;
}

void ASkaldAIController::ProcessCurrentPhase() {
  if (!TurnManager) {
    UE_LOG(LogSkald, Warning,
           TEXT("ProcessCurrentPhase called without a valid TurnManager"));
    EndTurn();
    return;
  }

  if (TurnManager->IsAwaitingBattleResultAcknowledgement()) {
    BroadcastEnemyTurnStatus(FString(EnemyAwaitingResultsMessage));
    ScheduleNextDecisionStep(EnemyBattleTransitionPollDelay);
    return;
  }

  if (const USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
    const USkaldBattleLevelManager *BattleLevelManager =
        GI->GetBattleLevelManager();
    const bool bBattleLevelActive =
        BattleLevelManager &&
        (BattleLevelManager->IsBattleLevelActive() ||
         BattleLevelManager->IsBattleLevelFullyReady());
    if (GI->bTravelPending || GI->bIsInBattleMap || bBattleLevelActive) {
      BroadcastEnemyTurnStatus(FString(EnemyBattleTransitionMessage));
      ScheduleNextDecisionStep(EnemyBattleTransitionPollDelay);
      return;
    }
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

  if (bAnimatingArmyPlacement) {
    return;
  }

  if (EnsureStrategySelected(WorldMap, PS)) {
    return;
  }

  const ETurnPhase Phase = TurnManager->GetCurrentPhase();
  if (Phase != ETurnPhase::Attack) {
    bPostBattleEvaluationPending = false;
    bPostBattlePauseActive = false;
    AttacksInitiatedThisPhase = 0;
  }
  if (Phase == ETurnPhase::Revolt) {
    UE_LOG(LogSkald, Log, TEXT("AI decision completed in %d steps"),
           DecisionIterationCount);
    EndTurn();
    return;
  }

  if (Phase == ETurnPhase::ArmyPlacement) {
    ClearEnemyTurnStatus();
    ExecuteStrategicArmyPlacement(WorldMap, PS, true);
    if (bAnimatingArmyPlacement) {
      return;
    }

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

    if (HandlePostBattleReevaluation(WorldMap, PS)) {
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
    AWorldMap *WorldMap, ASkaldPlayerState *InPlayerState,
    bool bAnimatePlacement) {
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

  if (!bAnimatePlacement) {
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
    return;
  }

  int32 UnitsRemaining = InPlayerState->DeployableUnits;
  TArray<FAnimatedArmyPlacementStep> PlacementSequence;
  PlacementSequence.Reserve(Targets.Num());
  TMap<ATerritory *, int32> TerritoryToSequenceIndex;

  int32 Index = 0;
  while (UnitsRemaining > 0 && Targets.Num() > 0) {
    FPlacementTarget &Target = Targets[Index];
    if (Target.RemainingCapacity <= 0) {
      Targets.RemoveAt(Index);
      if (Targets.Num() == 0) {
        break;
      }
      Index %= Targets.Num();
      continue;
    }

    int32 SequenceIndex = 0;
    if (int32 *ExistingIndex =
            TerritoryToSequenceIndex.Find(Target.Territory)) {
      SequenceIndex = *ExistingIndex;
    } else {
      SequenceIndex = PlacementSequence.Add({Target.Territory, 0});
      TerritoryToSequenceIndex.Add(Target.Territory, SequenceIndex);
    }

    FAnimatedArmyPlacementStep &PlacementStep =
        PlacementSequence[SequenceIndex];
    ++PlacementStep.Units;
    --Target.RemainingCapacity;
    --UnitsRemaining;

    if (Targets.Num() > 0) {
      Index = (Index + 1) % Targets.Num();
    }
  }

  if (PlacementSequence.Num() > 0) {
    StartArmyPlacementAnimation(PlacementSequence, InPlayerState);
  }
}

void ASkaldAIController::StartArmyPlacementAnimation(
    const TArray<FAnimatedArmyPlacementStep> &PlacementOrder,
    ASkaldPlayerState *InPlayerState) {
  if (PlacementOrder.Num() == 0 || !InPlayerState) {
    return;
  }

  if (bAnimatingArmyPlacement) {
    CompleteArmyPlacementAnimation(false);
  }

  PendingArmyPlacementTargets = PlacementOrder;
  AnimatedPlacementPlayerState = InPlayerState;
  bAnimatingArmyPlacement = true;

  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(ArmyPlacementAnimationHandle);
  }

  HandleArmyPlacementAnimationStep();
}

void ASkaldAIController::HandleArmyPlacementAnimationStep() {
  if (!bAnimatingArmyPlacement) {
    return;
  }

  ASkaldPlayerState *AnimatedPlayerState = AnimatedPlacementPlayerState.Get();
  if (!AnimatedPlayerState) {
    CompleteArmyPlacementAnimation(true);
    return;
  }

  if (PendingArmyPlacementTargets.Num() == 0 ||
      AnimatedPlayerState->DeployableUnits <= 0) {
    CompleteArmyPlacementAnimation(true);
    return;
  }

  const FAnimatedArmyPlacementStep NextPlacementStep =
      PendingArmyPlacementTargets[0];
  PendingArmyPlacementTargets.RemoveAt(0);

  if (ATerritory *Target = NextPlacementStep.Territory.Get()) {
    const int32 UnitsToDeploy = FMath::Clamp(NextPlacementStep.Units, 0,
                                            AnimatedPlayerState->DeployableUnits);
    if (UnitsToDeploy > 0) {
      Target->ArmyUnits += UnitsToDeploy;
      Target->RefreshAppearance();
      AnimatedPlayerState->DeployableUnits -= UnitsToDeploy;
      AnimatedPlayerState->AddArmyPlacementDeployment(
          Target->GetTerritoryId(), UnitsToDeploy);
    }
  }

  if (TurnManager) {
    TurnManager->BroadcastDeployableUnits(AnimatedPlayerState);
  }

  if (PendingArmyPlacementTargets.Num() == 0 ||
      AnimatedPlayerState->DeployableUnits <= 0) {
    CompleteArmyPlacementAnimation(true);
    return;
  }

  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().SetTimer(
        ArmyPlacementAnimationHandle, this,
        &ASkaldAIController::HandleArmyPlacementAnimationStep,
        FMath::Max(AnimatedArmyPlacementDelay, 0.f), false);
  } else {
    CompleteArmyPlacementAnimation(true);
  }
}

void ASkaldAIController::CompleteArmyPlacementAnimation(bool bAdvancePhase) {
  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(ArmyPlacementAnimationHandle);
  }

  const bool bWasAnimating = bAnimatingArmyPlacement;
  bAnimatingArmyPlacement = false;

  ASkaldPlayerState *AnimatedPlayerState = AnimatedPlacementPlayerState.Get();
  AnimatedPlacementPlayerState.Reset();
  PendingArmyPlacementTargets.Reset();

  if (!bWasAnimating) {
    return;
  }

  if (bArmyPlacementSetupInProgress && (!TurnManager ||
                                        !TurnManager->HasTurnsStarted())) {
    FinalizeArmyPlacementSetupTurn();
    return;
  }

  if (!bAdvancePhase || !TurnManager || !AnimatedPlayerState) {
    return;
  }

  TurnManager->BroadcastDeployableUnits(AnimatedPlayerState);

  if (TurnManager->HasTurnsStarted()) {
    const float PlacementDelay =
        FMath::Max(EnemyTurnStepDelay * StrategyPlanningDelayFraction,
                   MinimumStrategyPlanningDelay);
    SchedulePhaseAdvance(PlacementDelay);
  } else {
    EndTurn();
  }
}

void ASkaldAIController::FinalizeArmyPlacementSetupTurn() {
  if (!bArmyPlacementSetupInProgress) {
    return;
  }

  bArmyPlacementSetupInProgress = false;

  ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>();
  if (!PS) {
    if (CachedGameMode) {
      CachedGameMode->HandleAIArmyPlacementSetupComplete(this);
    }
    return;
  }

  AWorldMap *WorldMap = Cast<AWorldMap>(UGameplayStatics::GetActorOfClass(
      GetWorld(), AWorldMap::StaticClass()));

  if (PS->DeployableUnits > 0 && WorldMap) {
    const int32 FallbackPlaced = WorldMap->AutoPlaceUnitsForAI(PS);
    if (FallbackPlaced > 0) {
      UE_LOG(LogSkald, Verbose,
             TEXT("FinalizeArmyPlacementSetupTurn: fallback auto-placement deployed %d units for %s."),
             FallbackPlaced,
             *PS->GetResolvedPlayerName(TEXT("FinalizeArmyPlacementSetupTurn")));
    }
  }

  if (TurnManager) {
    TurnManager->BroadcastDeployableUnits(PS);
  }

  if (CachedGameMode) {
    CachedGameMode->HandleAIArmyPlacementSetupComplete(this);
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

bool ASkaldAIController::EvaluateBestStrategicAttack(
    AWorldMap *WorldMap, ASkaldPlayerState *InPlayerState,
    FAIStrategicAttackOption &OutOption) const {
  if (!WorldMap || !InPlayerState) {
    return false;
  }

  FAIStrategicAttackOption BestOption;
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
          continue;
        }

        UnitsToSend = FMath::Min(UnitsToSend, MaxMovable);
        if (UnitsToSend < SkaldConstants::CapitalAttackArmyRequirement) {
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

  OutOption = BestOption;
  return true;
}

bool ASkaldAIController::ExecuteStrategicAttack(AWorldMap *WorldMap,
                                                ASkaldPlayerState *InPlayerState) {
  if (!WorldMap || !InPlayerState) {
    return false;
  }

  if (AttacksInitiatedThisPhase >= MaxStrategicAttacksPerPhase) {
    return false;
  }

  FAIStrategicAttackOption BestOption;
  if (!EvaluateBestStrategicAttack(WorldMap, InPlayerState, BestOption)) {
    return false;
  }

  HandleAttackRequested(BestOption.Source->TerritoryID,
                        BestOption.Target->TerritoryID,
                        BestOption.UnitsToSend, false);
  ++AttacksInitiatedThisPhase;
  return true;
}

bool ASkaldAIController::ShouldContinueAttackingAfterBattle(
    const FAIStrategicAttackOption &Option) const {
  if (!Option.Source || !Option.Target) {
    return false;
  }

  if (AttacksInitiatedThisPhase >= MaxStrategicAttacksPerPhase) {
    return false;
  }

  if (Option.Score < PostBattleAttackScoreThreshold) {
    return false;
  }

  const int32 FriendlyUnits = CachedStrategicContext.TotalFriendlyUnits;
  const int32 EnemyUnits = FMath::Max(1, CachedStrategicContext.TotalEnemyUnits);
  const float ForceRatio = FriendlyUnits > 0
                               ? static_cast<float>(FriendlyUnits) / EnemyUnits
                               : 0.f;

  if (ForceRatio < 0.5f && AttacksInitiatedThisPhase > 0) {
    return false;
  }

  const int32 StrengthDelta = Option.Source->ArmyUnits - Option.Target->ArmyUnits;
  if (StrengthDelta <= 1 && Option.UnitsToSend <= 2) {
    return false;
  }

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

bool ASkaldAIController::HandlePostBattleReevaluation(
    AWorldMap *WorldMap, ASkaldPlayerState *InPlayerState) {
  if (!bPostBattleEvaluationPending || !WorldMap || !InPlayerState) {
    return false;
  }

  if (!bPostBattlePauseActive) {
    bPostBattlePauseActive = true;
    BroadcastEnemyTurnStatus(FString(EnemyPostBattleReevaluationMessage));
    ScheduleNextDecisionStep(PostBattleEvaluationPauseSeconds);
    return true;
  }

  bPostBattlePauseActive = false;
  bPostBattleEvaluationPending = false;

  RefreshStrategicContext(WorldMap, InPlayerState);

  FAIStrategicAttackOption BestOption;
  if (!EvaluateBestStrategicAttack(WorldMap, InPlayerState, BestOption) ||
      !ShouldContinueAttackingAfterBattle(BestOption)) {
    BroadcastEnemyTurnStatus(FString(EnemyPostBattleStandDownMessage));
    SchedulePhaseAdvance(EnemyTurnStepDelay);
    return true;
  }

  BroadcastEnemyTurnStatus(FString(EnemyPostBattleResumeMessage));
  return false;
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
  CompleteArmyPlacementAnimation(false);
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

  // Defensive rebind: remove first to avoid duplicate multicast bindings
  // across PIE travel / BeginPlay re-entry edge cases.
  CachedBattleManager->OnActiveFighterChanged.RemoveDynamic(
      this, &ASkaldAIController::HandleActiveFighterChanged);
  CachedBattleManager->OnActiveFighterChanged.AddUniqueDynamic(
      this, &ASkaldAIController::HandleActiveFighterChanged);

  CachedBattleManager->OnRoundStarted.RemoveDynamic(
      this, &ASkaldAIController::HandleRoundStarted);
  CachedBattleManager->OnRoundStarted.AddUniqueDynamic(
      this, &ASkaldAIController::HandleRoundStarted);

  CachedBattleManager->OnBattleEnded.RemoveDynamic(
      this, &ASkaldAIController::HandleBattleEnded);
  CachedBattleManager->OnBattleEnded.AddUniqueDynamic(
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

  FS_BattlePayload Battle = GameInstance->PendingBattle;
  if (const UWorld *World = GetWorld()) {
    if (const ASkaldGameState *GameState = World->GetGameState<ASkaldGameState>()) {
      const FS_BattlePayload &ReplicatedBattle = GameState->GetActiveBattlePayload();
      if (ReplicatedBattle.AttackerPlayerID > 0 ||
          ReplicatedBattle.DefenderPlayerID > 0 ||
          ReplicatedBattle.AttackerFaction != ESkaldFaction::None ||
          ReplicatedBattle.DefenderFaction != ESkaldFaction::None) {
        Battle = ReplicatedBattle;
      }
    }
  }
  const int32 StablePlayerId = PS->GetStablePlayerId();
  const int32 PlayerId = StablePlayerId > 0 ? StablePlayerId : PS->GetPlayerId();
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
    GameInstance->PendingBattle.bAttackerIsAI = true;
  }

  if (bAIControlsDefenderSide && bIsAIPlayer) {
    GameInstance->PendingBattle.bDefenderIsAI = true;
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

bool ASkaldAIController::IsActiveTurnController() const {
  if (!TurnManager) {
    return false;
  }

  const int32 CurrentIndex = TurnManager->GetCurrentControllerIndex();
  const TArray<ASkaldPlayerController *> Controllers =
      TurnManager->GetControllers();

  if (!Controllers.IsValidIndex(CurrentIndex)) {
    return false;
  }

  return Controllers[CurrentIndex] == this;
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
  float BestThreatScore = TNumericLimits<float>::Lowest();

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
    const float ThreatScore = static_cast<float>(Candidate->Stats.AttackDamage) +
                              Candidate->Stats.Strength * 1.5f +
                              Candidate->Stats.Health * 0.5f;
    if (Distance < BestDistance ||
        (Distance == BestDistance && ThreatScore > BestThreatScore)) {
      BestDistance = Distance;
      BestThreatScore = ThreatScore;
      BestEnemy = Candidate;
    }
  }

  return BestEnemy;
}


AFighterPawn *ASkaldAIController::FindBestTacticalEnemy(
    AFighterPawn *Fighter) const {
  if (!Fighter) {
    return nullptr;
  }

  UWorld *World = GetWorld();
  if (!World) {
    return nullptr;
  }

  AFighterPawn *BestEnemy = nullptr;
  float BestScore = TNumericLimits<float>::Lowest();
  for (TActorIterator<AFighterPawn> It(World); It; ++It) {
    AFighterPawn *Candidate = *It;
    if (!Candidate || Candidate == Fighter || !Candidate->IsAlive() ||
        ControlsFighter(Candidate)) {
      continue;
    }

    const int32 Distance = Fighter->GetFootprintDistanceToFighter(Candidate);
    const int32 AlliesThreatening = CountAlliesThreateningTarget(Candidate);
    const int32 ThreatenedAllies =
        CountEnemiesThreateningAllyNearTarget(Candidate);
    const float ThreatScore =
        static_cast<float>(Candidate->Stats.AttackDamage) * 5.f +
        Candidate->Stats.Strength * 3.f + Candidate->Stats.Health;
    float Score = ThreatScore - Distance * 8.f;
    Score += AlliesThreatening * 45.f;
    Score += ThreatenedAllies * 35.f;

    if (Candidate->Stats.Health <= Fighter->Stats.AttackDamage) {
      Score += 60.f;
    }

    if (!BestEnemy || Score > BestScore) {
      BestScore = Score;
      BestEnemy = Candidate;
    }
  }

  return BestEnemy ? BestEnemy : FindNearestEnemy(Fighter);
}

bool ASkaldAIController::GatherEnemiesInRange(
    AFighterPawn *Fighter, TArray<AFighterPawn *> &OutTargets) const {
  OutTargets.Reset();
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

  const int32 AttackRange = FMath::Max(1, Fighter->Stats.AttackRange);
  bool bFoundTarget = false;
  for (TActorIterator<AFighterPawn> It(World); It; ++It) {
    AFighterPawn *Candidate = *It;
    if (!Candidate || Candidate == Fighter || !Candidate->IsAlive()) {
      continue;
    }

    if (ControlsFighter(Candidate)) {
      continue;
    }

    const int32 Distance = Fighter->GetFootprintDistanceToFighter(Candidate);
    if (Distance > AttackRange) {
      continue;
    }

    if (!Fighter->HasLineOfSightToFighter(Candidate, AttackRange, Grid)) {
      continue;
    }

    OutTargets.Add(Candidate);
    bFoundTarget = true;
  }

  return bFoundTarget;
}


int32 ASkaldAIController::CountAlliesThreateningTarget(
    const AFighterPawn *Target) const {
  if (!Target) {
    return 0;
  }

  UWorld *World = GetWorld();
  if (!World) {
    return 0;
  }

  int32 Count = 0;
  for (TActorIterator<AFighterPawn> It(World); It; ++It) {
    AFighterPawn *Candidate = *It;
    if (!Candidate || Candidate == Target || !Candidate->IsAlive() ||
        !ControlsFighter(Candidate)) {
      continue;
    }

    const int32 Range = FMath::Max(1, Candidate->Stats.AttackRange);
    if (Candidate->GetFootprintDistanceToFighter(Target) > Range) {
      continue;
    }

    UGridOverlayComponent *Grid = Candidate->GetGrid();
    if (Grid && !Candidate->HasLineOfSightToFighter(Target, Range, Grid)) {
      continue;
    }

    ++Count;
  }

  return Count;
}

int32 ASkaldAIController::CountEnemiesThreateningAllyNearTarget(
    const AFighterPawn *Target) const {
  if (!Target) {
    return 0;
  }

  UWorld *World = GetWorld();
  if (!World) {
    return 0;
  }

  int32 Count = 0;
  for (TActorIterator<AFighterPawn> It(World); It; ++It) {
    AFighterPawn *Ally = *It;
    if (!Ally || !Ally->IsAlive() || !ControlsFighter(Ally)) {
      continue;
    }

    const int32 AllyThreatRange = FMath::Max(1, Target->Stats.AttackRange);
    if (Target->GetFootprintDistanceToFighter(Ally) <= AllyThreatRange) {
      ++Count;
    }
  }

  return Count;
}

float ASkaldAIController::EvaluateAttackTargetScore(
    const AFighterPawn *Attacker, const AFighterPawn *Target) const {
  return EvaluateAttackTargetScoreInternal(Attacker, Target, true);
}

float ASkaldAIController::EvaluateAttackTargetScoreInternal(
    const AFighterPawn *Attacker, const AFighterPawn *Target,
    bool bIncludeRandom) const {
  if (!Attacker || !Target) {
    return TNumericLimits<float>::Lowest();
  }

  AFighterPawn *MutableAttacker = const_cast<AFighterPawn *>(Attacker);
  AFighterPawn *MutableTarget = const_cast<AFighterPawn *>(Target);

  FFighterStats EffectiveAttackerStats = Attacker->Stats;
  if (USkaldAbilityComponent *AttackerAbility =
          MutableAttacker->FindComponentByClass<USkaldAbilityComponent>()) {
    AttackerAbility->ModifyOutgoingAttackStats(MutableTarget,
                                               EffectiveAttackerStats);
  }

  if (USkaldAbilityComponent *TargetAbility =
          MutableTarget->FindComponentByClass<USkaldAbilityComponent>()) {
    TargetAbility->ModifyIncomingAttackStats(MutableAttacker,
                                             EffectiveAttackerStats);
  }

  const int32 TargetHealth = FMath::Max(1, Target->Stats.Health);
  const int32 AttackDamage = EffectiveAttackerStats.AttackDamage;
  const int32 CritDamage =
      AttackDamage + EffectiveAttackerStats.CriticalBonusDamage;
  const int32 Distance = Attacker->GetFootprintDistanceToFighter(Target);
  const int32 AttackRange = FMath::Max(1, EffectiveAttackerStats.AttackRange);

  float Score = 0.f;
  if (AttackDamage >= TargetHealth) {
    Score += 150.f;
  } else {
    const float DamageRatio = static_cast<float>(AttackDamage) /
                              static_cast<float>(TargetHealth);
    Score += DamageRatio * 100.f;
  }

  if (CritDamage >= TargetHealth) {
    Score += 50.f;
  }

  const float ThreatValue = static_cast<float>(Target->Stats.AttackDamage) +
                            Target->Stats.Strength * 1.5f +
                            Target->Stats.Health * 0.25f;
  Score += ThreatValue;

  Score += FMath::Max(0, AttackRange - Distance) * 10.f;
  Score += EffectiveAttackerStats.AttackDice * 5.f;
  Score += EffectiveAttackerStats.Strength * 2.f;
  Score += FMath::Min(Attacker->ActionsRemaining, 3) * 6.f;

  const int32 AlliesThreatening = CountAlliesThreateningTarget(Target);
  if (AlliesThreatening > 0) {
    Score += AlliesThreatening * 30.f;
  }

  const int32 ThreatenedAllies = CountEnemiesThreateningAllyNearTarget(Target);
  if (ThreatenedAllies > 0) {
    Score += ThreatenedAllies * 20.f;
  }
  Score += FMath::Min(Target->ActionsRemaining, 3) * 4.f;

  if (bIncludeRandom) {
    Score += FMath::FRandRange(0.f, 1.f);
  }

  return Score;
}

float ASkaldAIController::ComputeAbilityActivationBonus(
    AFighterPawn *Fighter, const FSkaldAbilityState &AbilityState) const {
  if (!Fighter || !AbilityState.Definition.IsValid()) {
    return 0.f;
  }

  switch (ResolveFactionAbilityCategory(AbilityState.Definition.AbilityId, AbilityCategoryTable)) {
  case EAIFactionAbilityCategory::AttackDamageBuff:
    return 60.f + Fighter->Stats.AttackDamage * 2.f +
           Fighter->Stats.AttackDice * 2.f;
  case EAIFactionAbilityCategory::AttackDebuffEnemy:
    return 40.f + Fighter->Stats.AttackDamage * 1.5f;
  case EAIFactionAbilityCategory::AoEAttack:
    return 55.f + Fighter->Stats.AttackDamage * 1.5f;
  case EAIFactionAbilityCategory::MovementBuff:
    return 35.f + Fighter->Stats.Movement * 3.f;
  default:
    break;
  }

  return 0.f;
}

float ASkaldAIController::ComputeAbilityAttackScoreBonus(
    AFighterPawn *Fighter, AFighterPawn *Target,
    const FSkaldAbilityState &AbilityState) const {
  if (!Fighter || !Target || !AbilityState.Definition.IsValid()) {
    return 0.f;
  }

  const FName AbilityId = AbilityState.Definition.AbilityId;
  const EAIFactionAbilityCategory Category =
      ResolveFactionAbilityCategory(AbilityId, AbilityCategoryTable);

  float Bonus = 0.f;
  switch (Category) {
  case EAIFactionAbilityCategory::AttackDamageBuff: {
    if (AbilityId == TEXT("Ability_Ravpack_Elite") && Fighter->Stats.Health <= 1) {
      return 0.f;
    }

    Bonus = 40.f + Fighter->Stats.AttackDamage * 5.f +
            Fighter->Stats.AttackDice * 4.f;

    if (AbilityId == TEXT("Ability_Inflicted_Line")) {
      Bonus += 20.f;
    } else if (AbilityId == TEXT("Ability_Empire_Elite")) {
      const int32 Nearby = CountEnemiesNearTarget(Target, 2);
      if (Nearby > 0) {
        Bonus += Nearby * 25.f;
      }
    } else if (AbilityId == TEXT("Ability_Lizardfolk_Line") ||
               AbilityId == TEXT("Ability_Lizard_Line")) {
      Bonus += 15.f;
    }
    break;
  }
  case EAIFactionAbilityCategory::AttackDebuffEnemy: {
    const float ThreatScore = static_cast<float>(Target->Stats.AttackDamage) *
                                  4.f +
                              Target->Stats.Movement * 3.f +
                              Target->Stats.Strength * 2.f;
    Bonus = 20.f + ThreatScore;

    if (AbilityId == TEXT("Ability_Goblin_Skirmish") &&
        Target->Stats.Defence <= 1) {
      Bonus *= 0.5f;
    }
    if (AbilityId == TEXT("Ability_Gnoll_Skirmish")) {
      if (const USkaldAbilityComponent *TargetAbility =
              Target->GetAbilityComponent()) {
        if (TargetAbility->HasHarrierDashDefencePenalty()) {
          Bonus *= 0.5f;
        }
      }
      if (Target->Stats.Defence <= 0) {
        Bonus *= 0.5f;
      }
    }
    break;
  }
  case EAIFactionAbilityCategory::AoEAttack: {
    const int32 Nearby = CountEnemiesNearTarget(Target, 2);
    if (Nearby <= 0) {
      return 0.f;
    }
    Bonus = 35.f * Nearby + Fighter->Stats.AttackDamage * 3.f;
    break;
  }
  default:
    break;
  }

  return Bonus;
}

bool ASkaldAIController::ShouldTriggerAbilityForAttack(
    AFighterPawn *Fighter, AFighterPawn *Target,
    const FSkaldAbilityState &AbilityState,
    const FSkaldAbilityTargetingInfo &Targeting) const {
  if (!Fighter || !Target || !AbilityState.Definition.IsValid()) {
    return false;
  }

  if (Targeting.CommandMode != EBattleCommandMode::None &&
      Targeting.CommandMode != EBattleCommandMode::AbilityTargetEnemy) {
    return false;
  }

  const EAIFactionAbilityCategory Category =
      ResolveFactionAbilityCategory(AbilityState.Definition.AbilityId, AbilityCategoryTable);
  if (Category != EAIFactionAbilityCategory::AttackDamageBuff &&
      Category != EAIFactionAbilityCategory::AttackDebuffEnemy &&
      Category != EAIFactionAbilityCategory::AoEAttack) {
    return false;
  }

  if (AbilityState.Definition.CostType == ESkaldAbilityCostType::Reaction) {
    return false;
  }

  const int32 AttackRange = FMath::Max(1, Fighter->Stats.AttackRange);
  bool bHasDirectAttack =
      Fighter->GetFootprintDistanceToFighter(Target) <= AttackRange;
  if (bHasDirectAttack) {
    if (UGridOverlayComponent *Grid = Fighter->GetGrid()) {
      if (!Fighter->HasLineOfSightToFighter(Target, AttackRange, Grid)) {
        bHasDirectAttack = false;
      }
    }
  }

  if (bHasDirectAttack &&
      Category != EAIFactionAbilityCategory::AoEAttack) {
    const int32 TargetHealth = FMath::Max(1, Target->Stats.Health);
    if (Fighter->Stats.AttackDamage >= TargetHealth) {
      return false;
    }
  }

  const float Bonus =
      ComputeAbilityAttackScoreBonus(Fighter, Target, AbilityState);
  if (Bonus <= 0.f) {
    return false;
  }

  const float BaseScore =
      EvaluateAttackTargetScoreInternal(Fighter, Target, false);
  if (BaseScore <= TNumericLimits<float>::Lowest() / 2) {
    return false;
  }

  const float Threshold =
      Category == EAIFactionAbilityCategory::AttackDebuffEnemy ? 25.f : 35.f;
  return Bonus >= Threshold || (BaseScore + Bonus) >= BaseScore * 1.2f;
}

ASkaldAIController::EAIAttackAbilityResult
ASkaldAIController::TryUseFactionAbilityBeforeAttack(AFighterPawn *Fighter,
                                                     AFighterPawn *Target) {
  if (!Fighter || !Target) {
    return EAIAttackAbilityResult::None;
  }

  USkaldAbilityComponent *AbilityComponent = Fighter->GetAbilityComponent();
  if (!AbilityComponent) {
    return EAIAttackAbilityResult::None;
  }

  const FSkaldAbilityState *AbilityState =
      AbilityComponent->FindAbilityState(ESkaldAbilitySlot::Ability1);
  if (!AbilityState || !AbilityState->Definition.IsValid()) {
    return EAIAttackAbilityResult::None;
  }

  const FName AbilityId = AbilityState->Definition.AbilityId;
  const ESkaldAbilityCostType AbilityCost = AbilityState->Definition.CostType;
  const FSkaldAbilityTargetingInfo Targeting =
      ResolveAIAbilityTargeting(AbilityId);

  FText FailureReason;
  if (Targeting.CommandMode == EBattleCommandMode::AbilityTargetEnemy) {
    FSkaldAbilityContext AbilityContext;
    AbilityContext.AbilityId = AbilityId;
    AbilityContext.TargetFighter = Target;
    AbilityComponent->SetPendingAbilityContext(AbilityContext);
  }

  if (!AbilityComponent->CanActivateAbility(ESkaldAbilitySlot::Ability1,
                                            &FailureReason)) {
    AbilityComponent->ClearPendingAbilityContext();
    return EAIAttackAbilityResult::None;
  }

  if (!ShouldTriggerAbilityForAttack(Fighter, Target, *AbilityState,
                                     Targeting)) {
    AbilityComponent->ClearPendingAbilityContext();
    return EAIAttackAbilityResult::None;
  }

  if (!AbilityComponent->TryBeginAbility(ESkaldAbilitySlot::Ability1,
                                         FailureReason)) {
    UE_LOG(LogSkaldBattle, Verbose,
           TEXT("[AI] Failed to trigger ability %s for %s: %s"),
           *AbilityId.ToString(), *Fighter->GetHumanReadableName(),
           *FailureReason.ToString());
    AbilityComponent->ClearPendingAbilityContext();
    return EAIAttackAbilityResult::None;
  }

  if (AbilityCost == ESkaldAbilityCostType::Action) {
    Fighter->TryRestoreAction();
  }

  UE_LOG(LogSkaldBattle, Verbose,
         TEXT("[AI] Triggered ability %s before attack for %s"),
         *AbilityId.ToString(), *Fighter->GetHumanReadableName());

  if (Targeting.CommandMode == EBattleCommandMode::AbilityTargetEnemy) {
    Fighter->PerformAttack(Target);
    if (AbilityId == TEXT("Ability_Elf_Line")) {
      Fighter->TryRestoreAction();
    }
    return EAIAttackAbilityResult::AbilityTriggeredAttackExecuted;
  }

  return EAIAttackAbilityResult::AbilityTriggeredNoAttack;
}

bool ASkaldAIController::TryUseMovementAbility(AFighterPawn *Fighter,
                                               AFighterPawn *Target) {
  if (!Fighter || !Target) {
    return false;
  }

  USkaldAbilityComponent *AbilityComponent = Fighter->GetAbilityComponent();
  if (!AbilityComponent) {
    return false;
  }

  const FSkaldAbilityState *AbilityState =
      AbilityComponent->FindAbilityState(ESkaldAbilitySlot::Ability1);
  if (!AbilityState || !AbilityState->Definition.IsValid()) {
    return false;
  }

  const FName AbilityId = AbilityState->Definition.AbilityId;
  const ESkaldAbilityCostType AbilityCost = AbilityState->Definition.CostType;
  if (ResolveFactionAbilityCategory(AbilityId, AbilityCategoryTable) !=
      EAIFactionAbilityCategory::MovementBuff) {
    return false;
  }

  const FSkaldAbilityTargetingInfo Targeting =
      ResolveAIAbilityTargeting(AbilityId);
  if (Targeting.CommandMode != EBattleCommandMode::None &&
      Targeting.CommandMode != EBattleCommandMode::AbilityTargetEnemy) {
    return false;
  }

  const int32 Distance = Fighter->GetFootprintDistanceToFighter(Target);
  const int32 AttackRange = FMath::Max(1, Fighter->Stats.AttackRange);
  if (Distance <= AttackRange) {
    return false;
  }

  if (Distance <= Fighter->Stats.Movement) {
    return false;
  }

  if (Distance <= Fighter->Stats.Movement + AttackRange) {
    return false;
  }

  int32 ExpectedBonus = 1;
  if (AbilityId == TEXT("Ability_Orc_Skirmish") ||
      AbilityId == TEXT("Ability_Goblin_Elite")) {
    ExpectedBonus = 2;
  }

  if (Distance > Fighter->Stats.Movement + ExpectedBonus + AttackRange) {
    return false;
  }

  if (Targeting.CommandMode == EBattleCommandMode::AbilityTargetEnemy) {
    FSkaldAbilityContext AbilityContext;
    AbilityContext.AbilityId = AbilityId;
    AbilityContext.TargetFighter = Target;
    AbilityComponent->SetPendingAbilityContext(AbilityContext);
  }

  FText FailureReason;
  if (!AbilityComponent->CanActivateAbility(ESkaldAbilitySlot::Ability1,
                                            &FailureReason)) {
    AbilityComponent->ClearPendingAbilityContext();
    return false;
  }

  if (!AbilityComponent->TryBeginAbility(ESkaldAbilitySlot::Ability1,
                                         FailureReason)) {
    UE_LOG(LogSkaldBattle, Verbose,
           TEXT("[AI] Failed to trigger movement ability %s for %s: %s"),
           *AbilityId.ToString(), *Fighter->GetHumanReadableName(),
           *FailureReason.ToString());
    AbilityComponent->ClearPendingAbilityContext();
    return false;
  }

  if (AbilityCost == ESkaldAbilityCostType::Action) {
    Fighter->TryRestoreAction();
  }

  UE_LOG(LogSkaldBattle, Verbose,
         TEXT("[AI] Triggered movement ability %s for %s"),
         *AbilityId.ToString(), *Fighter->GetHumanReadableName());

  return true;
}

FSkaldAbilityTargetingInfo
ASkaldAIController::ResolveAIAbilityTargeting(FName AbilityId) const {
  FSkaldAbilityTargetingInfo Info;

  static const TMap<FName, FSkaldAbilityTargetingInfo> TargetingPresets = {
      {TEXT("Ability_Human_Skirmish"),
       {EBattleCommandMode::AbilityTargetAlly, INDEX_NONE, false, false, false,
        false}},
      {TEXT("Ability_Orc_Skirmish"),
       {EBattleCommandMode::AbilityTargetEnemy, INDEX_NONE, true, false, false}},
      {TEXT("Ability_Inflicted_Skirmish"),
       {EBattleCommandMode::AbilityTargetEnemy, INDEX_NONE, true, false, false}},
      {TEXT("Ability_Ravpack_Skirmish"),
       {EBattleCommandMode::AbilityTargetEnemy, INDEX_NONE, true, false, false}},
      {TEXT("Ability_Orc_Line"),
       {EBattleCommandMode::AbilityTargetEnemy, INDEX_NONE, true, false, false}},
      {TEXT("Ability_Human_Elite"),
       {EBattleCommandMode::AbilityTargetAlly, 5, false, false, false, false}},
      {TEXT("Ability_Dwarf_Elite"),
       {EBattleCommandMode::AbilityTargetEnemy, 6, true, false, false}},
      {TEXT("Ability_Elf_Line"),
       {EBattleCommandMode::AbilityTargetEnemy, INDEX_NONE, true, false, false}},
      {TEXT("Ability_Undead_Line"),
       {EBattleCommandMode::AbilityTargetEnemy, INDEX_NONE, true, false, false}},
      {TEXT("Ability_Gnoll_Skirmish"),
       {EBattleCommandMode::AbilityTargetEnemy, INDEX_NONE, true, false, false}},
      {TEXT("Ability_Gnoll_Elite"),
       {EBattleCommandMode::AbilityTargetEnemy, INDEX_NONE, true, false, false}},
      {TEXT("Ability_Empire_Skirmish"),
       {EBattleCommandMode::AbilityTargetEnemy, INDEX_NONE, true, false, false}},
      {TEXT("Ability_Empire_Elite"),
       {EBattleCommandMode::AbilityTargetEnemy, INDEX_NONE, true, false, false}},
      {TEXT("Ability_Ravpack_Line"),
       {EBattleCommandMode::AbilityTargetCell, 1, true, false, true}},
      {TEXT("Ability_Elf_Elite"),
       {EBattleCommandMode::AbilityTargetEnemy, 8, true, false, false}},
      {TEXT("Ability_Undead_Skirmish"),
       {EBattleCommandMode::AbilityTargetEnemy, 3, true, false, false}}};

  if (const FSkaldAbilityTargetingInfo *Preset =
          TargetingPresets.Find(AbilityId)) {
    return *Preset;
  }

  return Info;
}

int32 ASkaldAIController::CountEnemiesNearTarget(const AFighterPawn *Center,
                                                 int32 Range) const {
  if (!Center) {
    return 0;
  }

  UWorld *World = GetWorld();
  if (!World) {
    return 0;
  }

  int32 Count = 0;
  for (TActorIterator<AFighterPawn> It(World); It; ++It) {
    AFighterPawn *Candidate = *It;
    if (!Candidate || Candidate == Center || !Candidate->IsAlive()) {
      continue;
    }

    if (Candidate->Faction == Center->Faction) {
      continue;
    }

    const int32 Distance =
        Center->GetFootprintDistanceToFighter(Candidate);
    if (Distance <= Range) {
      ++Count;
    }
  }

  return Count;
}

AFighterPawn *ASkaldAIController::FindBestAttackTarget(
    AFighterPawn *Fighter) const {
  if (!Fighter) {
    return nullptr;
  }

  TArray<AFighterPawn *> Candidates;
  if (!GatherEnemiesInRange(Fighter, Candidates)) {
    return nullptr;
  }

  AFighterPawn *BestTarget = nullptr;
  float BestScore = TNumericLimits<float>::Lowest();
  for (AFighterPawn *Candidate : Candidates) {
    const float Score = EvaluateAttackTargetScore(Fighter, Candidate);
    if (Score > BestScore) {
      BestScore = Score;
      BestTarget = Candidate;
    }
  }

  return BestTarget;
}

int32 ASkaldAIController::ComputeDistanceToNearestEnemy(
    const AFighterPawn *Fighter) const {
  if (!Fighter) {
    return TNumericLimits<int32>::Max();
  }

  UGridOverlayComponent *Grid = Fighter->GetGrid();
  if (!Grid) {
    return TNumericLimits<int32>::Max();
  }

  UWorld *World = GetWorld();
  if (!World) {
    return TNumericLimits<int32>::Max();
  }

  int32 BestDistance = TNumericLimits<int32>::Max();
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
    }
  }

  return BestDistance;
}

float ASkaldAIController::EvaluateFighterActivationPriority(
    AFighterPawn *Fighter) const {
  if (!Fighter) {
    return TNumericLimits<float>::Lowest();
  }

  float Score = 0.f;

  TArray<AFighterPawn *> Targets;
  if (GatherEnemiesInRange(Fighter, Targets)) {
    float BestTargetScore = TNumericLimits<float>::Lowest();
    for (AFighterPawn *Target : Targets) {
      BestTargetScore = FMath::Max(
          BestTargetScore,
          EvaluateAttackTargetScoreInternal(Fighter, Target, false));
    }

    Score += 500.f + BestTargetScore;
  } else {
    const int32 Distance = ComputeDistanceToNearestEnemy(Fighter);
    if (Distance != TNumericLimits<int32>::Max()) {
      Score += FMath::Clamp(200.f - Distance * 10.f, -100.f, 200.f);
    }
  }

  if (Fighter->ActionsRemaining <= 0) {
    Score -= 200.f;
  } else {
    Score += Fighter->ActionsRemaining * 25.f;
  }

  Score += Fighter->Stats.AttackDamage * 2.f;
  Score += Fighter->Stats.Movement * 5.f;
  Score += Fighter->Stats.Health;

  if (USkaldAbilityComponent *AbilityComponent =
          Fighter->GetAbilityComponent()) {
    if (const FSkaldAbilityState *AbilityState =
            AbilityComponent->FindAbilityState(ESkaldAbilitySlot::Ability1)) {
      if (AbilityState->Definition.IsValid() &&
          AbilityComponent->CanActivateAbility(ESkaldAbilitySlot::Ability1)) {
        Score += ComputeAbilityActivationBonus(Fighter, *AbilityState);
      }
    }
  }

  if (Fighter->Stats.AttackRange > 1) {
    Score += 10.f;
  }

  return Score;
}

AFighterPawn *ASkaldAIController::FindNextFriendlyFighter(bool bExpectAttacker) const {
  UWorld *World = GetWorld();
  if (!World) {
    return nullptr;
  }

  AFighterPawn *BestFighter = nullptr;
  float BestScore = TNumericLimits<float>::Lowest();

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

    const float Score = EvaluateFighterActivationPriority(Candidate);
    if (Score > BestScore || !BestFighter) {
      BestScore = Score;
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

  AFighterPawn *Target = FindBestAttackTarget(Fighter);
  if (!Target) {
    return false;
  }

  const int32 ActionsBefore = Fighter->ActionsRemaining;
  const EAIAttackAbilityResult AbilityResult =
      TryUseFactionAbilityBeforeAttack(Fighter, Target);
  if (AbilityResult == EAIAttackAbilityResult::AbilityTriggeredAttackExecuted) {
    return true;
  }

  if (Fighter->ActionsRemaining <= 0) {
    return false;
  }

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

  AFighterPawn *Enemy = FindBestTacticalEnemy(Fighter);
  if (!Enemy) {
    return false;
  }

  TryUseMovementAbility(Fighter, Enemy);

  const FIntPoint StartCell = Fighter->GetCurrentCell();
  const FIntPoint EnemyCell = Enemy->GetCurrentCell();
  if (!Grid->IsCellInBounds(StartCell) || !Grid->IsCellInBounds(EnemyCell)) {
    return false;
  }

  const int32 AttackRange = FMath::Max(1, Fighter->Stats.AttackRange);
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

  auto HasLineOfSightFromAnchor = [&](const FIntPoint &Anchor) {
    const TArray<FIntPoint> CandidateCells = Fighter->GetOccupiedCells(Anchor);
    for (const FIntPoint &SelfCell : CandidateCells) {
      for (const FIntPoint &EnemyCellCoord : EnemyFootprint) {
        const int32 Distance = FMath::Max(
            FMath::Abs(SelfCell.X - EnemyCellCoord.X),
            FMath::Abs(SelfCell.Y - EnemyCellCoord.Y));
        if (Distance > AttackRange) {
          continue;
        }
        if (Grid->HasLineOfSight(SelfCell, EnemyCellCoord)) {
          return true;
        }
      }
    }
    return false;
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

  FIntPoint BestAnchor = StartCell;
  float BestImprovementScore = TNumericLimits<float>::Max();
  bool bFoundCloserDestination = false;
  FIntPoint BestFallbackAnchor = StartCell;
  float BestFallbackScore = TNumericLimits<float>::Max();
  bool bHasFallbackDestination = false;
  FIntPoint BestAttackAnchor = StartCell;
  int32 BestAttackDistance = CurrentDistance;
  int32 BestAttackCost = TNumericLimits<int32>::Max();
  bool bFoundAttackAnchor = false;

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

      const int32 CandidateDistance = ComputeDistanceFromAnchor(Next);
      const bool bWithinRange = CandidateDistance <= AttackRange;
      const bool bHasLineOfSight = bWithinRange && HasLineOfSightFromAnchor(Next);

      if (bHasLineOfSight &&
          (!bFoundAttackAnchor || CandidateDistance < BestAttackDistance ||
           (CandidateDistance == BestAttackDistance && StepCost < BestAttackCost))) {
        BestAttackDistance = CandidateDistance;
        BestAttackCost = StepCost;
        BestAttackAnchor = Next;
        bFoundAttackAnchor = true;
      }

      const float CandidateScore =
          CandidateDistance * MovementHuntDistanceWeight +
          static_cast<float>(StepCost) * MovementHuntStepWeight;

      if (CandidateDistance < CurrentDistance) {
        if (!bFoundCloserDestination || CandidateScore < BestImprovementScore) {
          bFoundCloserDestination = true;
          BestImprovementScore = CandidateScore;
          BestAnchor = Next;
        }
      } else if (CandidateDistance <=
                 CurrentDistance + MovementMaxDetourDistance) {
        if (!bHasFallbackDestination || CandidateScore < BestFallbackScore) {
          bHasFallbackDestination = true;
          BestFallbackScore = CandidateScore;
          BestFallbackAnchor = Next;
        }
      }
    }
  }

  FIntPoint PreferredAnchor = StartCell;
  if (bFoundCloserDestination) {
    PreferredAnchor = BestAnchor;
  } else if (bHasFallbackDestination) {
    PreferredAnchor = BestFallbackAnchor;
  }

  const FIntPoint Destination =
      bFoundAttackAnchor ? BestAttackAnchor : PreferredAnchor;

  if (Destination == StartCell) {
    return false;
  }

  const int32 ActionsBefore = Fighter->ActionsRemaining;
  Fighter->MoveToCell(Destination);
  return Fighter->ActionsRemaining < ActionsBefore;
}

AFighterPawn *ASkaldAIController::FindAllyToSupport(
    const AFighterPawn *Fighter) const {
  if (!Fighter) {
    return nullptr;
  }

  UWorld *World = GetWorld();
  if (!World) {
    return nullptr;
  }

  UGridOverlayComponent *Grid = Fighter->GetGrid();
  if (!Grid) {
    return nullptr;
  }

  AFighterPawn *BestAlly = nullptr;
  int32 BestPriority = TNumericLimits<int32>::Max();
  int32 BestDistanceToSelf = TNumericLimits<int32>::Max();
  int32 BestEnemyDistance = TNumericLimits<int32>::Max();

  for (TActorIterator<AFighterPawn> It(World); It; ++It) {
    AFighterPawn *Candidate = *It;
    if (!Candidate || Candidate == Fighter || !Candidate->IsAlive()) {
      continue;
    }

    if (Candidate->bIsAttacker != Fighter->bIsAttacker) {
      continue;
    }

    if (!ControlsFighter(Candidate)) {
      continue;
    }

    if (Candidate->GetGrid() != Grid) {
      continue;
    }

    const int32 EnemyDistance = ComputeDistanceToNearestEnemy(Candidate);
    const bool bEngaged = EnemyDistance <= 1;
    const bool bThreatened = EnemyDistance <= 3;

    const int32 Priority = bEngaged ? 0 : (bThreatened ? 1 : 2);
    const int32 DistanceToSelf =
        ComputeChebyshevDistance(Grid, Fighter, Candidate);

    if (DistanceToSelf == TNumericLimits<int32>::Max()) {
      continue;
    }

    if (!BestAlly || Priority < BestPriority ||
        (Priority == BestPriority &&
         (DistanceToSelf < BestDistanceToSelf ||
          (DistanceToSelf == BestDistanceToSelf &&
           EnemyDistance < BestEnemyDistance)))) {
      BestPriority = Priority;
      BestDistanceToSelf = DistanceToSelf;
      BestEnemyDistance = EnemyDistance;
      BestAlly = Candidate;
    }
  }

  return BestAlly;
}

bool ASkaldAIController::TryMoveTowardsSupportAlly(AFighterPawn *Fighter) {
  if (!Fighter || Fighter->ActionsRemaining <= 0 || Fighter->Stats.Movement <= 0) {
    return false;
  }

  UGridOverlayComponent *Grid = Fighter->GetGrid();
  if (!Grid) {
    return false;
  }

  AFighterPawn *Ally = FindAllyToSupport(Fighter);
  if (!Ally) {
    return false;
  }

  const FIntPoint StartCell = Fighter->GetCurrentCell();
  const FIntPoint AllyCell = Ally->GetCurrentCell();
  if (!Grid->IsCellInBounds(StartCell) || !Grid->IsCellInBounds(AllyCell)) {
    return false;
  }

  const TArray<FIntPoint> AllyFootprint = Ally->GetOccupiedCells();

  auto ComputeDistanceFromAnchor = [&](const FIntPoint &Anchor) {
    const TArray<FIntPoint> CandidateCells = Fighter->GetOccupiedCells(Anchor);
    int32 BestDistance = TNumericLimits<int32>::Max();
    for (const FIntPoint &SelfCell : CandidateCells) {
      for (const FIntPoint &AllyCellCoord : AllyFootprint) {
        const int32 Distance = FMath::Max(
            FMath::Abs(SelfCell.X - AllyCellCoord.X),
            FMath::Abs(SelfCell.Y - AllyCellCoord.Y));
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

  FIntPoint BestAnchor = StartCell;
  float BestImprovementScore = TNumericLimits<float>::Max();
  bool bFoundCloserDestination = false;
  FIntPoint BestFallbackAnchor = StartCell;
  float BestFallbackScore = TNumericLimits<float>::Max();
  bool bHasFallbackDestination = false;

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

      const int32 CandidateDistance = ComputeDistanceFromAnchor(Next);
      const float CandidateScore =
          CandidateDistance * MovementHuntDistanceWeight +
          static_cast<float>(StepCost) * MovementHuntStepWeight;

      if (CandidateDistance < CurrentDistance) {
        if (!bFoundCloserDestination || CandidateScore < BestImprovementScore) {
          bFoundCloserDestination = true;
          BestImprovementScore = CandidateScore;
          BestAnchor = Next;
        }
      } else if (CandidateDistance <=
                 CurrentDistance + MovementMaxDetourDistance) {
        if (!bHasFallbackDestination || CandidateScore < BestFallbackScore) {
          bHasFallbackDestination = true;
          BestFallbackScore = CandidateScore;
          BestFallbackAnchor = Next;
        }
      }
    }
  }

  FIntPoint Destination = StartCell;
  if (bFoundCloserDestination) {
    Destination = BestAnchor;
  } else if (bHasFallbackDestination) {
    Destination = BestFallbackAnchor;
  }

  if (Destination == StartCell) {
    return false;
  }

  const int32 ActionsBefore = Fighter->ActionsRemaining;
  Fighter->MoveToCell(Destination);
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
      ++ActivationIntentIterationCount;

      if (!ShouldContinueActivation(Fighter)) {
        CompleteFighterActivation();
        return;
      }

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
    if (!bActionTaken) {
      bActionTaken = TryMoveTowardsSupportAlly(Fighter);
    }
    if (bActionTaken && Fighter && Fighter->IsMoving()) {
      bAwaitingMovementCompletion = true;
      ScheduleMovementCompletionPoll();
      return;
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
  bAwaitingMovementCompletion = false;
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
  const bool bPreviouslyInBattleMap = bWasInBattleMap;
  bWasInBattleMap = bInBattleMap;

  if (bInBattleMap) {
    SetupBattleAutomation();
  } else {
    TeardownBattleAutomation();
    if (bPreviouslyInBattleMap) {
      HandleBattleMapExit();
    }
  }
}

void ASkaldAIController::HandleBattleMapExit() {
  bPostBattlePauseActive = false;

  if (!TurnManager) {
    return;
  }

  if (TurnManager->GetCurrentPhase() != ETurnPhase::Attack) {
    return;
  }

  if (!IsActiveTurnController()) {
    return;
  }

  bPostBattleEvaluationPending = true;
}

void ASkaldAIController::ScheduleTryActivateNextFighter() {
  float Delay = ActivationGapDelay;

  if (CachedBattleManager.IsValid() &&
      (CachedBattleManager->IsAwaitingAttackPresentation() ||
       CachedBattleManager->IsAwaitingInitiativeRoll())) {
    // Battle startup can race delegate binding against the initiative and
    // presentation phases. Keep a lightweight retry alive so an AI side that
    // wins initiative cannot miss the single OnRoundStarted notification and
    // leave the battle waiting with no active fighter.
    Delay = FMath::Max(0.1f, FMath::Min(ActivationGapDelay, 0.25f));
  }

  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(ActivationGapTimerHandle);
    World->GetTimerManager().SetTimer(
        ActivationGapTimerHandle, this, &ASkaldAIController::TryActivateNextFighter,
        Delay, false);
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
    ScheduleTryActivateNextFighter();
    return;
  }

  if (CachedBattleManager->IsAwaitingInitiativeRoll()) {
    ScheduleTryActivateNextFighter();
    return;
  }

  if (CachedBattleManager->GetActiveFighter()) {
    return;
  }

  DetermineControlledBattleSide();

  const bool bAttackerTurn = CachedBattleManager->IsAttackerTurn();
  const bool bControlsCurrentSide =
      bAttackerTurn ? bAIControlsAttackerSide : bAIControlsDefenderSide;
  if (!bControlsCurrentSide) {
    return;
  }

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

void ASkaldAIController::OnBeginRetreatSelection(
    int32 DefendingTerritoryID, const TArray<int32> &CandidateTerritoryIDs) {
  Super::OnBeginRetreatSelection(DefendingTerritoryID, CandidateTerritoryIDs);

  if (!HasAuthority()) {
    return;
  }

  if (!bAutoRetreatPending) {
    return;
  }

  const int32 ChosenTerritory =
      ChooseRetreatDestination(CandidateTerritoryIDs, DefendingTerritoryID);
  if (ChosenTerritory != INDEX_NONE) {
    UE_LOG(LogSkaldReady, Log,
           TEXT("AI controller %s confirming retreat to territory %d."),
           *GetName(), ChosenTerritory);
    ServerConfirmRetreatDestination(ChosenTerritory);
  } else {
    UE_LOG(LogSkaldReady, Warning,
           TEXT("AI controller %s could not determine a retreat destination."),
           *GetName());
  }

  bAutoRetreatPending = false;
}

void ASkaldAIController::NotifyRetreatFailed(const FText &Message) {
  bAutoRetreatPending = false;
  Super::NotifyRetreatFailed(Message);
}

ASkaldAIController::EAIPrepareForBattleDecision
ASkaldAIController::DeterminePrepareForBattleDecision(
    const FPrepareForBattlePromptData &PromptData) const {
  ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>();
  if (!PS) {
    return EAIPrepareForBattleDecision::None;
  }

  const int32 PlayerId = PS->GetPlayerId();
  const bool bIsDefender = PlayerId == PromptData.DefenderPlayerID;
  const bool bIsAttacker = PlayerId == PromptData.AttackerPlayerID;

  if (!bIsDefender && !bIsAttacker) {
    return EAIPrepareForBattleDecision::None;
  }

  if (!bIsDefender) {
    return EAIPrepareForBattleDecision::Ready;
  }

  const int32 AttackerArmy = PromptData.AttackerCommittedArmy;
  if (AttackerArmy <= 0) {
    return EAIPrepareForBattleDecision::Ready;
  }

  AWorldMap *WorldMap = Cast<AWorldMap>(UGameplayStatics::GetActorOfClass(
      GetWorld(), AWorldMap::StaticClass()));
  if (!WorldMap) {
    return EAIPrepareForBattleDecision::Ready;
  }

  ATerritory *DefendingTerritory =
      WorldMap->GetTerritoryById(PromptData.DefendingTerritoryID);
  if (!DefendingTerritory || DefendingTerritory->OwningPlayer != PS) {
    return EAIPrepareForBattleDecision::Ready;
  }

  int32 DefenderArmy = PromptData.DefenderArmyCount;
  if (DefenderArmy <= 0) {
    DefenderArmy = DefendingTerritory->ArmyUnits;
  }

  const int32 ArmyDifference = AttackerArmy - DefenderArmy;
  if (ArmyDifference <= 0) {
    return EAIPrepareForBattleDecision::Ready;
  }

  bool bHasCandidate = false;
  for (ATerritory *Neighbor : DefendingTerritory->AdjacentTerritories) {
    if (Neighbor && Neighbor->OwningPlayer == PS) {
      bHasCandidate = true;
      break;
    }
  }

  if (!bHasCandidate) {
    return EAIPrepareForBattleDecision::Ready;
  }

  if (ArmyDifference >= 4) {
    return EAIPrepareForBattleDecision::AttemptRetreat;
  }

  float RetreatChance = 0.0f;
  switch (ArmyDifference) {
  case 1:
    RetreatChance = 0.5f;
    break;
  case 2:
    RetreatChance = 0.7f;
    break;
  case 3:
    RetreatChance = 0.9f;
    break;
  default:
    RetreatChance = 0.0f;
    break;
  }

  if (RetreatChance <= 0.0f) {
    return EAIPrepareForBattleDecision::Ready;
  }

  const float Roll = FMath::FRand();
  if (Roll <= RetreatChance) {
    return EAIPrepareForBattleDecision::AttemptRetreat;
  }

  return EAIPrepareForBattleDecision::Ready;
}

int32 ASkaldAIController::ChooseRetreatDestination(
    const TArray<int32> &CandidateTerritoryIDs,
    int32 DefendingTerritoryID) const {
  if (CandidateTerritoryIDs.Num() == 0) {
    return INDEX_NONE;
  }

  AWorldMap *WorldMap = Cast<AWorldMap>(UGameplayStatics::GetActorOfClass(
      GetWorld(), AWorldMap::StaticClass()));
  if (!WorldMap) {
    return CandidateTerritoryIDs[0];
  }

  ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>();
  if (!PS) {
    return CandidateTerritoryIDs[0];
  }

  ATerritory *DefendingTerritory = WorldMap->GetTerritoryById(DefendingTerritoryID);
  if (!DefendingTerritory) {
    return CandidateTerritoryIDs[0];
  }

  int32 BestTerritoryId = INDEX_NONE;
  int32 BestValue = TNumericLimits<int32>::Lowest();

  for (int32 CandidateId : CandidateTerritoryIDs) {
    ATerritory *Candidate = WorldMap->GetTerritoryById(CandidateId);
    if (!Candidate || Candidate->OwningPlayer != PS) {
      continue;
    }

    const int32 CombinedStrength =
        Candidate->ArmyUnits + DefendingTerritory->ArmyUnits;
    const int32 Value = CombinedStrength * 10 + Candidate->ArmyUnits;

    if (Value > BestValue) {
      BestValue = Value;
      BestTerritoryId = CandidateId;
    }
  }

  return BestTerritoryId == INDEX_NONE && CandidateTerritoryIDs.Num() > 0
             ? CandidateTerritoryIDs[0]
             : BestTerritoryId;
}
