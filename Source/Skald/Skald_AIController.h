#pragma once

#include "CoreMinimal.h"
#include "Containers/Map.h"
#include "Containers/Queue.h"
#include "Containers/Set.h"
#include "Skald_PlayerController.h"
#include "TimerManager.h"
#include "Skald_AIController.generated.h"

class AFighterPawn;
class ATerritory;
class AWorldMap;
class UGridBattleManager;
class UGridOverlayComponent;
class USkaldAbilityComponent;
struct FSkaldAbilityState;
struct FSkaldAbilityTargetingInfo;

/**
 * Controller handling AI turn logic.
 */
UCLASS()
class SKALD_API ASkaldAIController : public ASkaldPlayerController {
  GENERATED_BODY()

public:
  virtual void BeginPlay() override;
  virtual void StartTurn() override;
  virtual void EndTurn() override;

  virtual void InitializeHUDWidget() override;
  virtual void ShowPrepareForBattlePromptLocal(
      const FPrepareForBattlePromptData &PromptData) override;

  /** Executes the AI retreat/ready decision without invoking HUD widgets. */
  void HandlePrepareForBattlePromptDirect(
      const FPrepareForBattlePromptData &PromptData);

  /** Execute the AI's decision making for the current turn. */
  UFUNCTION(BlueprintCallable, Category = "Turn")
  void MakeAIDecision();

  /**
   * Execute the AI's strategic army placement for the current placement turn.
   *
   * @return Number of units deployed during this placement turn.
   */
  int32 PerformArmyPlacementTurn();

  /** Starts an animated army placement turn during the setup phase. */
  bool BeginArmyPlacementSetupTurn();

protected:
  virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

  virtual void OnBeginRetreatSelection(
      int32 DefendingTerritoryID,
      const TArray<int32> &CandidateTerritoryIDs) override;
  virtual void NotifyRetreatFailed(const FText &Message) override;

private:
  enum class EAIStrategy : uint8 { Offensive, Defensive, Hybrid };

  struct FAnimatedArmyPlacementStep;

  struct FAIStrategicAttackOption {
    ATerritory *Source = nullptr;
    ATerritory *Target = nullptr;
    float Score = 0.f;
    int32 UnitsToSend = 0;
  };

  struct FStrategicContext {
    TArray<ATerritory *> OwnedTerritories;
    TArray<ATerritory *> EnemyTerritories;
    TArray<ATerritory *> EnemyCapitals;
    TSet<ATerritory *> BorderTerritories;
    TSet<ATerritory *> CapitalDefenseRing;
    TSet<ATerritory *> EnemyCapitalApproach;
    TMap<ATerritory *, int32> EnemyPressure;
    ATerritory *Capital = nullptr;
    int32 TotalFriendlyUnits = 0;
    int32 TotalEnemyUnits = 0;
    int32 TotalEnemyBorderUnits = 0;
    int32 FriendlyBorderUnits = 0;
    bool bCapitalThreatened = false;
    bool bCanThreatenEnemyCapital = false;
  };

  enum class EAIBattleActivationIntent : uint8 { Attack, Move };

  void ProcessCurrentPhase();
  void RefreshStrategicContext(AWorldMap *WorldMap, ASkaldPlayerState *PlayerState);
  bool EnsureStrategySelected(AWorldMap *WorldMap, ASkaldPlayerState *PlayerState);
  EAIStrategy SelectStrategyFromContext(const FStrategicContext &Context) const;
  FString DescribeStrategy(EAIStrategy Strategy) const;
  float EvaluateOffensivePriority(const FStrategicContext &Context,
                                  ATerritory *Territory) const;
  float EvaluateDefensivePriority(const FStrategicContext &Context,
                                  ATerritory *Territory) const;
  void ExecuteStrategicArmyPlacement(AWorldMap *WorldMap,
                                     ASkaldPlayerState *PlayerState,
                                     bool bAnimatePlacement = false);
  void ExecuteStrategicReinforcements(AWorldMap *WorldMap,
                                      ASkaldPlayerState *PlayerState);
  bool ExecuteStrategicAttack(AWorldMap *WorldMap,
                              ASkaldPlayerState *PlayerState);
  bool ExecuteStrategicMovement(AWorldMap *WorldMap,
                                ASkaldPlayerState *PlayerState);
  bool EvaluateBestStrategicAttack(AWorldMap *WorldMap,
                                   ASkaldPlayerState *PlayerState,
                                   FAIStrategicAttackOption &OutOption) const;
  bool HandlePostBattleReevaluation(AWorldMap *WorldMap,
                                    ASkaldPlayerState *PlayerState);
  bool ShouldContinueAttackingAfterBattle(
      const FAIStrategicAttackOption &Option) const;
  bool IsActiveTurnController() const;
  int32 DetermineArmyToSend(EAIStrategy Strategy, int32 SourceUnits,
                            int32 TargetUnits) const;
  void ScheduleNextDecisionStep(float DelaySeconds);
  void SchedulePhaseAdvance(float DelaySeconds);
  void ClearDecisionTimers();
  void BroadcastEnemyTurnStatus(const FString &Message);
  void ClearEnemyTurnStatus();
  bool ShouldPauseForBattleTransition() const;
  void StartArmyPlacementAnimation(
      const TArray<FAnimatedArmyPlacementStep> &PlacementOrder,
      ASkaldPlayerState *InPlayerState);
  void HandleArmyPlacementAnimationStep();
  void CompleteArmyPlacementAnimation(bool bAdvancePhase);
  void FinalizeArmyPlacementSetupTurn();

  void SetupBattleAutomation();
  void TeardownBattleAutomation();

  ASkald_BattleGameMode *ResolveBattleGameMode() const;

  void DetermineControlledBattleSide();
  bool ControlsFighter(const AFighterPawn *Fighter) const;
  bool IsMyTurn() const;

  AFighterPawn *FindNextFriendlyFighter(bool bExpectAttacker) const;
  AFighterPawn *FindNearestEnemy(AFighterPawn *Fighter) const;
  AFighterPawn *FindBestAttackTarget(AFighterPawn *Fighter) const;
  float EvaluateAttackTargetScore(const AFighterPawn *Attacker,
                                  const AFighterPawn *Target) const;
  float EvaluateAttackTargetScoreInternal(const AFighterPawn *Attacker,
                                          const AFighterPawn *Target,
                                          bool bIncludeRandom) const;
  bool GatherEnemiesInRange(AFighterPawn *Fighter,
                            TArray<AFighterPawn *> &OutTargets) const;
  bool TryAttackNearestEnemy(AFighterPawn *Fighter);
  bool TryMoveTowardsNearestEnemy(AFighterPawn *Fighter);
  bool TryMoveTowardsSupportAlly(AFighterPawn *Fighter);
  AFighterPawn *FindAllyToSupport(const AFighterPawn *Fighter) const;
  float ComputeAbilityActivationBonus(AFighterPawn *Fighter,
                                      const FSkaldAbilityState &AbilityState) const;
  float ComputeAbilityAttackScoreBonus(AFighterPawn *Fighter,
                                       AFighterPawn *Target,
                                       const FSkaldAbilityState &AbilityState) const;
  bool ShouldTriggerAbilityForAttack(AFighterPawn *Fighter,
                                     AFighterPawn *Target,
                                     const FSkaldAbilityState &AbilityState,
                                     const FSkaldAbilityTargetingInfo &Targeting) const;
  enum class EAIAttackAbilityResult : uint8 {
    None,
    AbilityTriggeredNoAttack,
    AbilityTriggeredAttackExecuted
  };
  EAIAttackAbilityResult TryUseFactionAbilityBeforeAttack(
      AFighterPawn *Fighter, AFighterPawn *Target);
  bool TryUseMovementAbility(AFighterPawn *Fighter, AFighterPawn *Target);
  FSkaldAbilityTargetingInfo ResolveAIAbilityTargeting(FName AbilityId) const;
  int32 CountEnemiesNearTarget(const AFighterPawn *Center, int32 Range) const;
  void ExecuteActivationForFighter(AFighterPawn *Fighter);
  void TryActivateNextFighter();
  void QueueActivationIntent(AFighterPawn *Fighter,
                             EAIBattleActivationIntent Intent);
  void ProcessQueuedActivationIntent();
  void ScheduleMovementCompletionPoll();
  void HandleQueuedAttackFinalized();
  void ClearActivationTimers();
  void ScheduleNextActivationAttempt();
  void ScheduleTryActivateNextFighter();
  bool ShouldContinueActivation(const AFighterPawn *Fighter) const;
  int32 ComputeDistanceToNearestEnemy(const AFighterPawn *Fighter) const;
  float EvaluateFighterActivationPriority(AFighterPawn *Fighter) const;
  void CompleteFighterActivation();
  virtual void HandleBattleMapStateChanged(bool bInBattleMap) override;
  void HandleBattleMapExit();

  void ProcessPrepareForBattlePrompt(
      const FPrepareForBattlePromptData &PromptData);

  int32 ComputeChebyshevDistance(UGridOverlayComponent *Grid,
                                 const AFighterPawn *A,
                                 const AFighterPawn *B) const;

  virtual void HandleActiveFighterChanged(AFighterPawn *NewFighter) override;

  virtual void HandleRoundStarted(int32 RoundNumber,
                                  ESkaldFaction InitiativeWinner) override;

  virtual void HandleBattleEnded(ESkaldFaction WinningFaction,
                                 int32 AttackerCasualties,
                                 int32 DefenderCasualties) override;

  /** Cached reference to the battle manager when grid combat is active. */
  TWeakObjectPtr<UGridBattleManager> CachedBattleManager;

  /** Tracks which side of the battle this AI controls. */
  bool bAIControlsAttackerSide = false;
  bool bAIControlsDefenderSide = false;

  /** Prevents recursive handling when resolving an activation. */
  bool bProcessingActivation = false;

  /** Tracks whether the AI is waiting for a battle travel transition. */
  bool bAwaitingBattleTransition = false;

  /** True when a battle just concluded and the AI should pause before attacking. */
  bool bPostBattleEvaluationPending = false;

  /** Tracks whether the post-battle pause timer is active. */
  bool bPostBattlePauseActive = false;

  /** Tracks the number of decision steps processed this turn. */
  int32 DecisionIterationCount = 0;

  /** Tracks whether a phase advance should occur on the next decision step. */
  bool bPendingPhaseAdvance = false;

  /** True while the AI is animating its army placement choices. */
  bool bAnimatingArmyPlacement = false;

  /** Tracks whether the AI is animating during the pre-turn placement phase. */
  bool bArmyPlacementSetupInProgress = false;

  /** Tracks whether the current turn's strategy has been evaluated. */
  bool bStrategyEvaluatedThisTurn = false;

  /** Tracks the number of attacks launched during the current attack phase. */
  int32 AttacksInitiatedThisPhase = 0;

  /** Cached state of whether the battle map was previously active. */
  bool bWasInBattleMap = false;

  /** Maximum strategic attacks permitted in a single attack phase. */
  static constexpr int32 MaxStrategicAttacksPerPhase = 4;

  /** Cached strategic context reused throughout the turn. */
  FStrategicContext CachedStrategicContext;

  /** Current turn strategy guiding world-map decisions. */
  EAIStrategy CurrentStrategy = EAIStrategy::Hybrid;

  /** Time between AI phase processing steps on the world map. */
  UPROPERTY(EditAnywhere, Category = "Turn|AI", meta = (ClampMin = "5.0"))
  float EnemyTurnStepDelay = 5.0f;

  /** Additional delay after AI reinforcements deploy to allow UI updates. */
  UPROPERTY(EditAnywhere, Category = "Turn|AI", meta = (ClampMin = "0.0"))
  float ReinforcementPostDeployDelay = 2.0f;

  /** Delay between each animated AI army placement. */
  UPROPERTY(EditAnywhere, Category = "Turn|AI", meta = (ClampMin = "0.0"))
  float AnimatedArmyPlacementDelay = 0.4f;

  /** Time between polls while waiting for a battle travel transition. */
  UPROPERTY(EditAnywhere, Category = "Turn|AI", meta = (ClampMin = "0.0"))
  float EnemyBattleTransitionPollDelay = 1.0f;

  /** Delay between individual AI-controlled actions during grid battles. */
  UPROPERTY(EditAnywhere, Category = "Battle|AI", meta = (ClampMin = "0.0"))
  float BattleActionDelay = 1.0f;

  /** Delay applied between fighter activations to provide pacing. */
  UPROPERTY(EditAnywhere, Category = "Battle|AI", meta = (ClampMin = "0.0"))
  float ActivationGapDelay = 0.5f;

  /** Timer driving world-map decision pacing. */
  FTimerHandle EnemyTurnStepTimerHandle;
  FTimerHandle ArmyPlacementAnimationHandle;

  /** Timer used to retry battle automation binding while the manager spawns. */
  FTimerHandle BattleAutomationPollHandle;

  /** Timer driving queued fighter actions. */
  FTimerHandle FighterActionTimerHandle;

  /** Timer enforcing a short delay before the next activation. */
  FTimerHandle ActivationGapTimerHandle;

  /** Fighter currently being processed by the AI. */
  TWeakObjectPtr<AFighterPawn> PendingActivationFighter;

  /** Ordered list of intents to process for the pending fighter. */
  TQueue<EAIBattleActivationIntent> PendingActivationIntents;

  /** True while waiting for an async attack sequence to resolve. */
  bool bAwaitingQueuedAttackResolution = false;

  enum class EAIPrepareForBattleDecision : uint8 {
    None,
    Ready,
    AttemptRetreat
  };

  EAIPrepareForBattleDecision DeterminePrepareForBattleDecision(
      const FPrepareForBattlePromptData &PromptData) const;
  int32 ChooseRetreatDestination(const TArray<int32> &CandidateTerritoryIDs,
                                 int32 DefendingTerritoryID) const;
  bool bAutoRetreatPending = false;

  /** True while waiting for a fighter's move animation to complete. */
  bool bAwaitingMovementCompletion = false;

  /** Safety counter preventing infinite activation loops. */
  int32 ActivationIntentIterationCount = 0;

  /** Polling interval used while waiting for movement completion visuals. */
  static constexpr float MovementCompletionPollInterval = 0.1f;

  struct FAnimatedArmyPlacementStep {
    TWeakObjectPtr<ATerritory> Territory;
    int32 Units = 0;
  };

  /** Ordered list of pending animated placement targets. */
  TArray<FAnimatedArmyPlacementStep> PendingArmyPlacementTargets;

  /** PlayerState associated with the current animated placement sequence. */
  TWeakObjectPtr<ASkaldPlayerState> AnimatedPlacementPlayerState;
};

