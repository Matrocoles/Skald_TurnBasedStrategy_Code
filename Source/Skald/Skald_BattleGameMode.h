#pragma once

#include "CoreMinimal.h"
#include "Skald_GameMode.h"
#include "SkaldTypes.h"
#include "AIController.h"
#include "SkaldLogging.h"
#include "Skald_BattleGameMode.generated.h"

class AAIController;
class ASkaldAIController;
class AController;

/** GameMode dedicated to resolving grid-based battles. */
UCLASS()
class SKALD_API ASkald_BattleGameMode : public ASkaldGameMode {
  GENERATED_BODY()

public:
  /** Attempt to start the tactical battle once both sides have supplied armies. */
  void TryLaunchBattle();

  // Returns true once we’ve detected at least two valid controllers and kicked off setup.
  bool TrySetupBattleWhenReady();

  /** Wrapper that allows external systems to trigger the protected InitGame lifecycle. */
  void InitializeBattleGameMode(const FString &MapName, const FString &Options,
                                FString &ErrorMessage);

  void BeginPreBattleSelection(class ASkaldPlayerState* AttackerPS,
                               class ASkaldPlayerState* DefenderPS,
                               int32 AttackerBudget, int32 DefenderBudget);

  void HandleHumanLockIn(class ASkaldPlayerController* PC,
                         const TArray<FFighterDefinition>& SelectedFighters);

  /** Record that the supplied player has locked in their army selection. */
  void RegisterPlayerLockIn(int32 PlayerId);

protected:
  virtual void InitGame(const FString &Map, const FString &Options,
                        FString &Error) override;
  virtual void BeginPlay() override;
  virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
  virtual void PostLogin(APlayerController *NewPlayer) override;
  virtual void HandleSeamlessTravelPlayer(AController *&C) override;
  virtual void TryInitializeWorldAndStart() override;

public:
  // Called by the AI controller when it’s ready (BeginPlay)
  void OnAIControllerReady(class ASkaldAIController *Controller);

  UFUNCTION(BlueprintCallable)
  void OnControllerReady(AController *Controller);

  /** Move controller pawns near the active battle grid, if available. */
  bool RelocateControllersNearBattleGrid(
      const TArray<AController *> &Controllers,
      const TMap<AController *, bool> *ControllerSides = nullptr) const;

  /**
   * Called when the streamed battle level becomes active so existing
   * controllers can be registered with the battle flow without requiring a
   * travel event.
   */
  void NotifyBattleLevelActivated();

private:
  void SetupPendingBattle();
  void AutoCommitAIArmy(ASkaldPlayerState *PlayerState, int32 Budget) const;
  void SpawnFighterSide(const TArray<FFighterDefinition> &Roster, bool bAsAttacker);
  void TryStartBattle();

  bool AreBothParticipantsLocked() const;
  void TryAdvanceAfterLockIn();
  bool ValidateAndRecordSelection(ASkaldPlayerState *PlayerState,
                                  const TArray<FFighterDefinition> &SelectedFighters,
                                  FString &OutReason);

  void LogParticipantLockState(const TCHAR *Context);

  bool IsSoloMatch() const;
  void PollBattleBootstrap();
  void EnsureBattleControllers();
  void ProcessDeferredControllers();
  void QueueDeferredController(AController *Controller);
  bool ControllerHasStablePlayerId(AController *Controller) const;
  void ProcessStreamingActivation();

  /** Controllers waiting for bootstrap while we rebuild the roster. */
  TArray<TWeakObjectPtr<AController>> DeferredReadyControllers;

  /** Guard to defer OnControllerReady callbacks during bootstrap. */
  bool bEnsuringBattleControllers = false;

  /** Ensures the battle only launches once per travel. */
  bool bBattleLaunched = false;

  // Expected count comes from GameInstance travel state
  UPROPERTY(EditDefaultsOnly, Category = "Battle|Bootstrap")
  int32 ExpectedControllers = 1;

  FTimerHandle WaitForPlayersHandle;
  bool bSetupStarted = false;
  bool bSetupCompleted = false;

  /** Territory IDs owned by human players when travel began. */
  TSet<int32> CachedHumanTerritoryIDs;

  /** Snapshot of overworld territory data captured prior to travel. */
  TMap<int32, FS_Territory> CachedTerritoryMap;

  /** Ensure we only log the cache restoration message once. */
  bool bLoggedTravelCache = false;

  /** Tracks which participant player IDs have finalised their selection. */
  TSet<int32> LockedInPlayers;

  /** True when NotifyBattleLevelActivated ran before BeginPlay completed. */
  bool bPendingStreamingActivation = false;
};

