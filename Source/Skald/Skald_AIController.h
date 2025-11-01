#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "Skald_PlayerController.h"
#include "TimerManager.h"
#include "Skald_AIController.generated.h"

class AFighterPawn;
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

protected:
  virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
  enum class EAIBattleActivationIntent : uint8 { Attack, Move };

  void ProcessCurrentPhase();
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
  bool TryAttackNearestEnemy(AFighterPawn *Fighter);
  bool TryMoveTowardsNearestEnemy(AFighterPawn *Fighter);
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

  /** Ordered list of intents to process for the pending fighter. */
  TQueue<EAIBattleActivationIntent> PendingActivationIntents;

  /** True while waiting for an async attack sequence to resolve. */
  bool bAwaitingQueuedAttackResolution = false;

  /** True while waiting for a fighter's move animation to complete. */
  bool bAwaitingMovementCompletion = false;

  /** Safety counter preventing infinite activation loops. */
  int32 ActivationIntentIterationCount = 0;

  /** Polling interval used while waiting for movement completion visuals. */
  static constexpr float MovementCompletionPollInterval = 0.1f;
};

