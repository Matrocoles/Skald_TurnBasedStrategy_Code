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

  virtual void GetLifetimeReplicatedProps(
      TArray<FLifetimeProperty> &OutLifetimeProps) const override;

  UFUNCTION(BlueprintCallable, Category = "Turn")
  void RegisterController(ASkaldPlayerController *Controller);

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

  /** Transition into the grid based battle mode using the provided payload. */
  UFUNCTION(BlueprintCallable, Category = "Battle")
  void TriggerGridBattle(const FS_BattlePayload &Battle);

  /** Apply the outcome of a completed grid battle to the world map. */
  UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Battle")
  void ResolveGridBattleResult();

  /** Multicast request instructing all clients to stream the specified battle level. */
  UFUNCTION(NetMulticast, Reliable)
  void MulticastStreamBattleLevel(const FSoftObjectPath &BattleLevelPath,
                                  const FSkaldTravelState &TravelState,
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

  /** Return the number of registered controllers. */
  UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Turn")
  int32 GetControllerCount() const { return Controllers.Num(); }

  /** Retrieve the current phase of play. */
  UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Turn")
  ETurnPhase GetCurrentPhase() const { return CurrentPhase; }

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

  UPROPERTY()
  AWorldMap *CachedWorldMap;

  /** Retry handle used when deferring battle resolution until the world map has been restored. */
  FTimerHandle PendingBattleResolutionRetryHandle;

  UFUNCTION()
  void HandleGridBattleEnded(ESkaldFaction WinningFaction, int32 AttackerCasualties, int32 DefenderCasualties);

  /** Notify controllers and HUDs of a phase change. */
  void BroadcastCurrentPhase();

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
};
