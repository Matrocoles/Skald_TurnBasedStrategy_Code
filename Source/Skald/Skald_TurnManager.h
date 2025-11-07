#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SkaldTypes.h"
#include "TimerManager.h"
#include "UObject/SoftObjectPath.h"
#include "Skald_TurnManager.generated.h"

class ASkaldPlayerController;
class ASkaldPlayerState;
class AWorldMap;
class UWorld;
class USkaldGameInstance;
class UGridBattleManager;

USTRUCT(BlueprintType)
struct FBattleMapDescriptor {
  GENERATED_BODY()

public:
  FBattleMapDescriptor() : bStreamAsSubLevel(true) {}

  UPROPERTY(EditAnywhere, BlueprintReadWrite)
  TSoftObjectPtr<UWorld> Map;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, meta =
                (Tooltip =
                     "If true the map will be streamed instead of travelling"))
  bool bStreamAsSubLevel;
};

// Broadcast whenever the overall world state changes so HUDs can refresh.
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSkaldWorldStateChanged);

/**
 * Handles turn sequencing for all registered player controllers.
 */
UCLASS(Blueprintable, BlueprintType)
class SKALD_API ATurnManager : public AActor {
  GENERATED_BODY()

public:
  ATurnManager();

  virtual void BeginPlay() override;

  virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

  virtual void GetLifetimeReplicatedProps(
      TArray<FLifetimeProperty> &OutLifetimeProps) const override;

  UFUNCTION(BlueprintCallable, Category = "Turn")
  void RegisterController(ASkaldPlayerController *Controller);

  void RestoreControllerOrderFromSnapshots(const TArray<FS_PlayerData> &Snapshots);

  /** Attempt to resume the saved turn state captured before travelling. */
  bool AttemptResumeSavedTurnState();

  /** Begin the pre-game army placement phase. */
  UFUNCTION(BlueprintCallable, Category = "Turn")
  void StartArmyPlacementPhase();

  UFUNCTION(BlueprintCallable, Category = "Turn")
  void StartTurns(ASkaldPlayerController *StartingController = nullptr);

  UFUNCTION(BlueprintCallable, Category = "Turn")
  void AdvanceTurn();

  /** Called when all reinforcements have been deployed to transition
   *  the active player into the attack phase. */
  UFUNCTION(BlueprintCallable, Category = "Turn")
  void BeginAttackPhase();

  /** Move to the next phase in the turn sequence. */
  UFUNCTION(BlueprintCallable, Category = "Turn")
  void AdvancePhase();

  /** Resolve the active phase immediately, progressing when appropriate. */
  UFUNCTION(BlueprintCallable, Category = "Turn")
  void EndCurrentPhase();

  /** Update all players' HUDs with the specified player's deployable units. */
  UFUNCTION(BlueprintCallable, Category = "Turn")
  void BroadcastDeployableUnits(class ASkaldPlayerState *ForPlayer);

  /** Update all HUDs with the specified player's resources. */
  UFUNCTION(BlueprintCallable, Category = "Turn")
  void BroadcastResources(class ASkaldPlayerState *ForPlayer);

  UFUNCTION(BlueprintCallable, Category = "Turn")
  void SortControllersByInitiative();

  /**
   * Distribute any remaining deployable units for the specified player across
   * their owned territories using the world map.
   */
  int32 DistributeArmyPlacementUnits(ASkaldPlayerState *PlayerState);

  /** Returns true if the specified player can still perform a movement action this phase. */
  bool CanPerformMovementAction(int32 PlayerID, FString *OutError = nullptr) const;

  /** Record a completed movement action for the specified player. */
  void RecordMovementAction(int32 PlayerID);

  /** Query the number of remaining movement actions for the specified player. */
  int32 GetMovementActionsRemaining(int32 PlayerID) const;

  /** Request that both players confirm readiness before travelling to battle. */
  UFUNCTION(BlueprintCallable, Category = "Battle")
  void RequestPrepareBattle(const FS_BattlePayload &Battle);

  /** Process a confirmed attack and transition the turn system into the ready phase. */
  void HandleAttackConfirmed(const FS_BattlePayload &Battle);

  /** Transition into the grid based battle mode using the provided payload. */
  UFUNCTION(BlueprintCallable, Category = "Battle")
  virtual void TriggerGridBattle(const FS_BattlePayload &Battle);

  /** Apply the outcome of a completed grid battle to the world map. */
  UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Battle")
  void ResolveGridBattleResult();

  /** Multicast request instructing all clients to stream the specified battle level. */
  UFUNCTION(NetMulticast, Reliable)
  void MulticastStreamBattleLevel(const FSoftObjectPath &BattleLevelPath,
                                  const FSkaldTravelState &TravelState,
                                  const FS_BattlePayload &BattlePayload);

  /** Multicast notification to prepare clients for travelling to a battle map
   *  when a streaming level is not being used. */
  UFUNCTION(NetMulticast, Reliable)
  void MulticastPrepareBattleTravel(const FSkaldTravelState &TravelState,
                                    const FS_BattlePayload &BattlePayload);

  /** Multicast notification whenever the pending battle readiness changes. */
  UFUNCTION(NetMulticast, Reliable)
  void MulticastOnReadyStateChanged(const FSkaldBattleReadyState &ReadyState,
                                    const FS_BattlePayload &BattlePayload);

  /** Multicast the battle map active state so clients can update immediately. */
  UFUNCTION(NetMulticast, Reliable)
  void MulticastSetBattleMapActive(bool bInBattleMap);

  /** Multicast the results of a resolved battle to all clients. */
  UFUNCTION(NetMulticast, Reliable)
  void ClientBattleResolved(int32 WinningPlayerID, int32 AttackerCasualties,
                            int32 DefenderCasualties, int32 FromTerritoryID,
                            int32 TargetTerritoryID, int32 NewOwnerPlayerID,
                            int32 SourceArmy, int32 TargetArmy);

  /** Access the controllers array in its current initiative order. */
  UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Turn")
  TArray<ASkaldPlayerController *> GetControllers() const;

  /** Retrieve the cached world map actor, if one is available. */
  UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Turn")
  AWorldMap *GetCachedWorldMapActor() const { return CachedWorldMap; }

  /** Returns true when a battle preparation is waiting on player readiness. */
  UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Battle")
  bool HasPendingBattlePreparation() const;

  /** Capture a snapshot of all territories currently registered on the world map. */
  bool CaptureWorldSnapshot(TArray<FS_Territory> &OutSnapshot,
                            TSet<int32> *OutHumanOwnedTerritories = nullptr);

  /** Return the number of registered controllers. */
  UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Turn")
  int32 GetControllerCount() const { return Controllers.Num(); }

  /** Retrieve the index of the currently active controller. */
  UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Turn")
  int32 GetCurrentControllerIndex() const { return CurrentIndex; }

  /** Retrieve the current phase of play. */
  UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Turn")
  ETurnPhase GetCurrentPhase() const { return CurrentPhase; }

  /** Returns true when the turn sequence has already been initialised. */
  UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Turn")
  bool HasTurnsStarted() const { return bHasTurnsStarted; }

  /** Mark the specified player as ready to travel to the battle map. */
  UFUNCTION(BlueprintCallable, Category = "Battle")
  void NotifyPlayerReadyForBattle(int32 PlayerID, bool bReady);

  /** Event fired when the world state has changed. */
  UPROPERTY(BlueprintAssignable, Category = "Turn")
  FSkaldWorldStateChanged OnWorldStateChanged;

  /** Maps that can be used for grid based battles. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Battle")
  TArray<TSoftObjectPtr<UWorld>> BattleMaps;

  /** Optional per-map configuration including streaming preferences. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Battle",
            meta = (TitleProperty = "Map"))
  TArray<FBattleMapDescriptor> BattleMapEntries;

protected:
  UPROPERTY()
  TArray<TWeakObjectPtr<ASkaldPlayerController>> Controllers;

  UPROPERTY(BlueprintReadOnly, Category = "Turn")
  int32 CurrentIndex;

  UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Turn")
  ETurnPhase CurrentPhase = ETurnPhase::Reinforcement;

  UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Turn")
  FS_BattlePayload PendingBattle;
  FS_BattlePayload PendingBattlePreparation;
  FSkaldBattleReadyState PendingBattleReadyState;

  void CommitPendingBattleReadyState(const TCHAR *Context);
  bool TryAutoReadyAI(const TCHAR *Context);
  /** Payload for the next battle waiting on travel/resolution to finish. */
  FS_BattlePayload DeferredPendingBattle;

  UPROPERTY()
  AWorldMap *CachedWorldMap;

  /** Retry handle used when deferring battle resolution until the world map has been restored. */
  FTimerHandle PendingBattleResolutionRetryHandle;

  /** Retry handle used when a battle travel request must wait for a territory snapshot. */
  FTimerHandle PendingBattleTravelRetryHandle;

  /** Retry handle used when waiting for the battle manager to become available on the battle map. */
  FTimerHandle BattleEndBindingRetryHandle;

  /** Delay handle ensuring the battle result remains visible before returning to the overworld. */
  FTimerHandle BattleReturnDelayHandle;

  /** Retry handle used when a phase broadcast must wait for initialization gates to clear. */
  FTimerHandle PhaseBroadcastRetryHandle;

  /** Tracks whether a broadcast retry has already been scheduled for the current phase. */
  bool bPhaseBroadcastRetryActive = false;

  /** Phase value that will be re-broadcast once initialization barriers lift. */
  ETurnPhase PendingPhaseBroadcast = ETurnPhase::Reinforcement;

  /** Cached battle manager that currently has a battle end delegate bound. */
  TWeakObjectPtr<class UGridBattleManager> BoundBattleManager;

  /** Number of movement actions completed during the current movement phase, keyed by player ID. */
  TMap<int32, int32> MovementActionsTaken;

  /** Clear the movement action counter for the active player. */
  void ResetMovementActionsForActivePlayer();

  /** Resolve the player ID for the active controller, or INDEX_NONE when unavailable. */
  int32 GetActivePlayerId() const;

  /** Attempt to continue travelling to the battle map after capturing the world snapshot. */
  void RetryPendingBattleTravel();

  UFUNCTION()
  void HandleGridBattleEnded(ESkaldFaction WinningFaction, int32 AttackerCasualties, int32 DefenderCasualties);

  UFUNCTION()
  void HandleBattleMapStateChanged(bool bInBattleMap);

  void AttemptBindBattleEnd(USkaldGameInstance *GameInstance, int32 Attempt = 0);

  void ClearBattleEndBinding(USkaldGameInstance *GameInstance);

  void CompleteBattleConclusion();

  /** Notify controllers and HUDs of a phase change. */
  bool BroadcastCurrentPhase();

  void BroadcastPrepareForBattlePrompt(const FS_BattlePayload &Battle,
                                       const TCHAR *LogContext =
                                           TEXT("BroadcastPrepareForBattlePrompt"));
  void BeginReadyPhase(const FS_BattlePayload &Battle,
                       const TCHAR *Context = TEXT("BeginReadyPhase"));
  bool TryAdvanceFromReadyToBattle(const TCHAR *Context =
                                       TEXT("TryAdvanceFromReadyToBattle"));

  /** Schedule a retry when phase broadcasts are gated by travel or initialization state. */
  void QueuePhaseBroadcastRetry(ETurnPhase Phase);

  /**
   * Calculate and apply reinforcements and resource gains for the specified
   * player state based on owned territories.
   */
  void ApplyReinforcementsAndResources(ASkaldPlayerState *PS,
                                       const TCHAR *Caller);

  /** Internal: set GameState.CurrentTurnIndex (and broadcast) to match CurrentIndex. */
  void SyncGameStateTurnIndex();

  /** Attempt to restore a saved turn state captured before travelling. */
  bool TryResumeSavedTurnState(USkaldGameInstance *GameInstance = nullptr);

  /** Ensure the cached world map pointer references a valid actor. */
  AWorldMap *ResolveWorldMap();

  /** Capture the most recent grid battle resolution before travelling back. */
  bool CapturePendingBattleResolution(USkaldGameInstance *GameInstance);

  bool bBattleReturnPending = false;

  /** Tracks whether StartTurns (or a resume) has successfully kicked off the turn loop. */
  bool bHasTurnsStarted = false;
};
