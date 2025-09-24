#pragma once

#include "CoreMinimal.h"
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

  /** Execute the AI's decision making for the current turn. */
  UFUNCTION(BlueprintCallable, Category = "Turn")
  void MakeAIDecision();

protected:
  virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
  void SetupBattleAutomation();
  void TeardownBattleAutomation();

  void DetermineControlledBattleSide();
  bool ControlsFighter(const AFighterPawn *Fighter) const;
  bool IsMyTurn() const;

  AFighterPawn *FindNextFriendlyFighter(bool bExpectAttacker) const;
  AFighterPawn *FindNearestEnemy(AFighterPawn *Fighter) const;
  bool TryAttackNearestEnemy(AFighterPawn *Fighter);
  bool TryMoveTowardsNearestEnemy(AFighterPawn *Fighter);
  void ExecuteActivationForFighter(AFighterPawn *Fighter);
  void TryActivateNextFighter();

  int32 ComputeManhattanDistance(UGridOverlayComponent *Grid,
                                 const AFighterPawn *A,
                                 const AFighterPawn *B) const;

  UFUNCTION()
  void HandleActiveFighterChanged(AFighterPawn *NewFighter);

  UFUNCTION()
  void HandleRoundStarted(int32 RoundNumber, ESkaldFaction InitiativeWinner);

  UFUNCTION()
  void HandleBattleEnded(ESkaldFaction WinningFaction, int32 AttackerCasualties,
                         int32 DefenderCasualties);

  /** Cached reference to the battle manager when grid combat is active. */
  TWeakObjectPtr<UGridBattleManager> CachedBattleManager;

  /** Tracks which side of the battle this AI controls. */
  bool bAIControlsAttackerSide = false;
  bool bAIControlsDefenderSide = false;

  /** Prevents recursive handling when resolving an activation. */
  bool bProcessingActivation = false;

  /** Timer used to retry battle automation binding while the manager spawns. */
  FTimerHandle BattleAutomationPollHandle;
};

