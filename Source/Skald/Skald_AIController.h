#pragma once

#include "CoreMinimal.h"
#include "Containers/Map.h"
#include "Containers/Queue.h"
#include "Containers/Set.h"
#include "Skald_PlayerController.h"
#include "TimerManager.h"
#include "Skald_AIController.generated.h"

class AFighterPawn;
class AWorldMap;
class UGridBattleManager;
class UGridOverlayComponent;

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

  /** Execute the AI's decision making for the current turn. */
  UFUNCTION(BlueprintCallable, Category = "Turn")
  void MakeAIDecision();

  /**
   * Execute the AI's strategic army placement for the current placement turn.
   *
   * @return Number of units deployed during this placement turn.
   */
  int32 PerformArmyPlacementTurn();

protected:
  virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
  enum class EAIStrategy : uint8 { Offensive, Defensive, Hybrid };

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

  enum class EAIBattleActivationIntent : uint8 { UseAbility, Attack, Move };

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
                                     ASkaldPlayerState *PlayerState);
  void ExecuteStrategicReinforcements(AWorldMap *WorldMap,
                                      ASkaldPlayerState *PlayerState);
  bool ExecuteStrategicAttack(AWorldMap *WorldMap,
                              ASkaldPlayerState *PlayerState);
  bool ExecuteStrategicMovement(AWorldMap *WorldMap,
                                ASkaldPlayerState *PlayerState);
  int32 DetermineArmyToSend(EAIStrategy Strategy, int32 SourceUnits,
                            int32 TargetUnits) const;
  void ScheduleNextDecisionStep(float DelaySeconds);
  void SchedulePhaseAdvance(float DelaySeconds);
  void ClearDecisionTimers();
  void BroadcastEnemyTurnStatus(const FString &Message);
  void ClearEnemyTurnStatus();
  bool ShouldPauseForBattleTransition() const;

  void SetupBattleAutomation();
  void TeardownBattleAutomation();

  ASkald_BattleGameMode *ResolveBattleGameMode() const;

  void DetermineControlledBattleSide();
  bool ControlsFighter(const AFighterPawn *Fighter) const;
  bool IsMyTurn() const;

  AFighterPawn *FindNextFriendlyFighter(bool bExpectAttacker) const;
  AFighterPawn *FindNearestEnemy(AFighterPawn *Fighter) const;
  bool TryExecuteBestAbility(AFighterPawn *Fighter, bool &bOutTriggeredAttack);
  bool TryExecuteBestAttack(AFighterPawn *Fighter);
  bool TryMoveTowardsNearestEnemy(AFighterPawn *Fighter);
  bool ShouldStrafeBeforeAttacking(const AFighterPawn *Fighter) const;
  float EvaluateBestAttackScore(const AFighterPawn *Fighter) const;
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
  void CompleteFighterActivation();
  virtual void HandleBattleMapStateChanged(bool bInBattleMap) override;

  int32 ComputeChebyshevDistance(UGridOverlayComponent *Grid,
                                 const AFighterPawn *A,
                                 const AFighterPawn *B) const;
  float EvaluateBestAttackScoreFromAnchor(const AFighterPawn *Fighter,
                                          const FIntPoint &Anchor,
                                          AFighterPawn **OutTarget,
                                          int32 RangeOverride = INDEX_NONE) const;
  bool FindBestTrapPlacement(AFighterPawn *Fighter, const FName AbilityId,
                             const FSkaldAbilityTargetingInfo &Targeting,
                             int32 RangeOverride, FIntPoint &OutCell,
                             float &OutScore) const;
  float EvaluatePassiveAttackBonus(const AFighterPawn *Fighter,
                                   const AFighterPawn *Target) const;

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

  /** Tracks the number of decision steps processed this turn. */
  int32 DecisionIterationCount = 0;

  /** Tracks whether a phase advance should occur on the next decision step. */
  bool bPendingPhaseAdvance = false;

  /** Tracks whether the current turn's strategy has been evaluated. */
  bool bStrategyEvaluatedThisTurn = false;

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

  /** Timer used to retry battle automation binding while the manager spawns. */
  FTimerHandle BattleAutomationPollHandle;

  /** Timer driving queued fighter actions. */
  FTimerHandle FighterActionTimerHandle;

  /** Timer enforcing a short delay before the next activation. */
  FTimerHandle ActivationGapTimerHandle;

  /** Fighter currently being processed by the AI. */
  TWeakObjectPtr<AFighterPawn> PendingActivationFighter;

  /** Tracks whether the pending fighter has moved during this activation. */
  bool bPendingFighterMovedThisActivation = false;

  /** Ordered list of intents to process for the pending fighter. */
  TQueue<EAIBattleActivationIntent> PendingActivationIntents;

  /** True while waiting for an async attack sequence to resolve. */
  bool bAwaitingQueuedAttackResolution = false;

  /** Ability identifiers already attempted during the current activation. */
  TSet<FName> AbilitiesTriedThisActivation;

  /** True while waiting for a fighter's move animation to complete. */
  bool bAwaitingMovementCompletion = false;

  /** Safety counter preventing infinite activation loops. */
  int32 ActivationIntentIterationCount = 0;

  /** Polling interval used while waiting for movement completion visuals. */
  static constexpr float MovementCompletionPollInterval = 0.1f;
};

