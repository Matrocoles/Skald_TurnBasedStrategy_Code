#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GridBattleManager.h"
#include "SkaldTypes.h"
#include "TimerManager.h"
#include "Containers/Set.h"
#include "UObject/WeakObjectPtr.h"
#include "Skald_GameMode.generated.h"
class ATurnManager;
class ASkaldGameState;
class ASkaldPlayerController;
class ASkaldPlayerState;
class AWorldMap;
class APlayerController;
class USkaldSaveGame;

#if defined(WITH_AUTOMATION_TESTS) && WITH_AUTOMATION_TESTS
class FArmyPlacementInitiativeOrderTest;
class FAIArmyPlacementAutoAdvanceTest;
#endif // WITH_AUTOMATION_TESTS
struct FSkaldGameModeAutomationAccessor;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSkaldGameOver, ASkaldPlayerState *,
                                            Winner);

/**
 * GameMode responsible for managing player login and spawning the turn manager.
 */
UCLASS(Blueprintable, BlueprintType)
class SKALD_API ASkaldGameMode : public AGameModeBase {
  GENERATED_BODY()

#if defined(WITH_AUTOMATION_TESTS) && WITH_AUTOMATION_TESTS
  // Allow automation tests to drive protected game flow entry points.
  friend class FArmyPlacementInitiativeOrderTest;
  friend class FAIArmyPlacementAutoAdvanceTest;
  friend class FInitializeWorldSingleInitiativeRollTest;
#endif // WITH_AUTOMATION_TESTS
  friend struct FSkaldGameModeAutomationAccessor;
  friend class USkaldGameInstance;

public:
  ASkaldGameMode();
  virtual void InitGame(const FString &Map, const FString &Options,
                        FString &Error) override;
  virtual void PostLogin(APlayerController *NewPlayer) override;
  virtual void Logout(AController *Exiting) override;
  virtual void HandleSeamlessTravelPlayer(AController *&C) override;

  /** Advance army placement to the next controller. */
  void AdvanceArmyPlacement();

  /** Populate a save game object with the current match state. */
  UFUNCTION(BlueprintCallable, Category = "SaveGame")
  void FillSaveGame(USkaldSaveGame *SaveGameObject) const;

  /** Restore match state from a previously loaded save game. */
  void ApplyLoadedGame(USkaldSaveGame *LoadedGame);

  /** Check if only one player remains and handle victory. */
  UFUNCTION(BlueprintCallable, Category = "GameMode")
  void CheckVictoryConditions();

  /** Event fired when a winner has been determined. */
  UPROPERTY(BlueprintAssignable, Category = "GameMode")
  FSkaldGameOver OnGameOver;

  /** Retrieve the active turn manager controlling turn order. */
  UFUNCTION(BlueprintCallable, BlueprintPure, Category = "GameMode")
  ATurnManager *GetTurnManager() const { return TurnManager; }

  /** Retrieve read-only player data snapshots for external systems. */
  const TArray<FS_PlayerData> &GetPlayerDataSnapshots() const {
    return PlayerDataArray;
  }

  /** Whether the world has already been initialised. */
  bool IsWorldInitialized() const { return bWorldInitialized; }

  /** Whether the game is waiting on players to roll strategic initiative. */
  bool IsAwaitingStrategicInitiative() const {
    return bAwaitingStrategicInitiativeInput;
  }

  /** Handle a player confirming their name and faction selection. */
  void HandlePlayerLockedIn(ASkaldPlayerState *PS);

  /** Initiate pre-battle fighter selection for both sides. */
  UFUNCTION(BlueprintCallable, Category="Skald|Battle")
  void BeginPreBattleSelection(ASkaldPlayerState* AttackerPS, ASkaldPlayerState* DefenderPS,
                               int32 AttackerBudget, int32 DefenderBudget);

public:
  // Manager instance created at runtime and owned by the GameMode
  UPROPERTY(Transient)
  UGridBattleManager* BattleManager = nullptr;

  // Optional override class (else uses UGridBattleManager::StaticClass())
  UPROPERTY(EditDefaultsOnly, Category="Battle")
  TSubclassOf<UGridBattleManager> BattleManagerClass = UGridBattleManager::StaticClass();

protected:
  virtual void BeginPlay() override;

  UFUNCTION()
  void HandleBattleEnded(ESkaldFaction Winner, int32 AttackerCasualties, int32 DefenderCasualties);

  /** Class used when spawning the runtime turn manager. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameMode")
  TSubclassOf<ATurnManager> TurnManagerClass;

  /** Handles turn sequencing for the match. */
  UPROPERTY(BlueprintReadOnly, Category = "GameMode")
  ATurnManager *TurnManager;

  /** Holds all territory actors for the current map. */
  UPROPERTY(BlueprintReadOnly, Category = "GameMode")
  AWorldMap *WorldMap;

  /** Data describing each player in the match. */
  UPROPERTY(BlueprintReadOnly, Category = "Players")
  TArray<FS_PlayerData> PlayerDataArray;

  /** Minimum number of players required to start the match. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Players",
            meta = (ClampMin = "1"))
  int32 MinPlayerCount = 1;

  /** Controller class used when spawning AI players. */
  UPROPERTY(EditDefaultsOnly, Category = "Players")
  TSubclassOf<APlayerController> AIControllerClass;

  /** All siege equipment constructed on the map. */
  UPROPERTY(BlueprintReadOnly, Category = "Siege")
  TArray<FS_Siege> SiegePool;

  /** Next unique identifier for siege equipment. */
  int32 NextSiegeID = 1;

  /**
   * Setup initial territories, armies, and initiative.
   * Returns true if the world was successfully initialised with at least one
   * player present.
   */
  UFUNCTION(BlueprintCallable, Category = "GameMode")
  bool InitializeWorld();

  /** Allow players to position initial armies based on initiative. */
  UFUNCTION(BlueprintCallable, Category = "GameMode")
  void BeginArmyPlacementPhase();

public:
  /** Build siege equipment during the engineering phase. */
  UFUNCTION(BlueprintCallable, Category = "Siege")
  int32 BuildSiegeAtTerritory(int32 TerritoryID, ESiegeWeapon Type);

  /** Consume a built siege from a territory for an attack. */
  UFUNCTION(BlueprintCallable, Category = "Siege")
  int32 ConsumeSiege(int32 TerritoryID);

  /** Update cached player resource values. */
  void UpdatePlayerResources(ASkaldPlayerState *Player);

  /** Attempt to initialise the world and start the game flow. */
  virtual void TryInitializeWorldAndStart();

  /** Record that a controller has confirmed the strategic initiative roll. */
  void ConfirmStrategicInitiativeRoll(ASkaldPlayerController *Controller);

protected:
  /** Timer used to retry initialization until readiness checks pass. */
  FTimerHandle RetryInitTimerHandle;

  /** Tracks whether turns have already begun to avoid duplicates. */
  bool bTurnsStarted;

  /** Whether the world has been initialized and territories assigned. */
  bool bWorldInitialized;

private:
  /** Attempt to resolve the world map actor, retrying until it becomes available. */
  void RequestWorldMapRetry();

  /** Find the world map actor if available. */
  bool TryResolveWorldMap();

  /** Callback used to poll for the world map actor while it is still spawning. */
  void HandleWorldMapRetry();

  /** Timer handle used while polling for a world map actor. */
  FTimerHandle WorldMapRetryHandle;

  /** Timer that triggers auto-start of the turn sequence. */
  FTimerHandle StartGameTimerHandle;

  /** Flag to avoid spawning AI players multiple times. */
  bool bAIPlayersSpawned;

  /** Whether an overworld snapshot should be captured after world init completes. */
  bool bPendingInitialSnapshot = false;

  /** Controllers whose PlayerState is not yet valid, queued for retry. */
  TArray<ASkaldPlayerController *> PendingControllers;

  /** Index of the controller currently placing armies. */
  int32 PlacementIndex = 0;

  /** Controller that opened army placement with the highest initiative roll. */
  TWeakObjectPtr<ASkaldPlayerController> ArmyPlacementLeader;

  /** Delay handle used to let HUDs update before the next placement turn. */
  FTimerHandle ArmyPlacementAutoAdvanceHandle;

  /** Failsafe to ensure AI army placement advances the phase. */
  FTimerHandle ArmyPlacementFailsafeHandle;

  /** Retry handle used when army placement starts before all controllers register. */
  FTimerHandle ArmyPlacementStartupRetryHandle;

  /** Guard to avoid logging the failsafe warning multiple times. */
  bool bArmyPlacementFailsafeTriggered = false;

  /** Register a newly connected player and update player data. */
  void RegisterPlayer(ASkaldPlayerController *PC);

  /** Populate remaining slots with AI players in singleplayer. */
  void PopulateAIPlayers();

  /** Notify HUDs of the current player roster. */
  void RefreshHUDs();

  /** Remove invalid player states before re-registering controllers. */
  void CleanupStalePlayerStates();

  /** Ensure player IDs remain contiguous after roster mutations. */
  void NormalizePlayerStateIds();

  /** Callback fired by the failsafe timer if the AI does not advance. */
  void HandleArmyPlacementFailsafe();

  /** Triggered after a brief delay to advance to the next placement turn. */
  void HandleArmyPlacementAutoAdvance();

  /** Ensure a turn manager exists, spawning or reusing one as required. */
  ATurnManager *ResolveTurnManager();

  /** Determine how many controllers should participate in army placement. */
  int32 ResolveExpectedControllerCount() const;

  /** Prompt players to roll strategic initiative before world initialization. */
  void BeginStrategicInitiativePhase();

  /** Remove stale or disconnecting players from the pending confirmation set. */
  void RemovePendingStrategicInitiativePlayer(ASkaldPlayerState *PlayerState);

  /** Strip invalid weak pointers left behind after controller teardown. */
  void PrunePendingStrategicInitiativePlayers();

  /** Re-evaluate the initiative phase after the pending set mutates. */
  void HandlePendingStrategicInitiativeUpdate();

  /** Finalise the initiative phase once all confirmations arrive. */
  void ResolveStrategicInitiativePhase();

  /** Dispatch initiative results to the appropriate controller. */
  void NotifyStrategicInitiativeRoll(ASkaldPlayerController *Controller,
                                     int32 RoundNumber, int32 RollValue,
                                     bool bWonInitiative);

  /** Perform any deferred work that must happen once the overworld is ready. */
  void HandleWorldInitializationComplete();

  /** Track players still needing to confirm the initiative roll. */
  TSet<TWeakObjectPtr<ASkaldPlayerState>> PendingStrategicInitiativePlayers;

  /** Whether the game is waiting for players to trigger the initiative roll. */
  bool bAwaitingStrategicInitiativeInput = false;

  /** Whether the strategic initiative prompt has been shown this cycle. */
  bool bStrategicInitiativePromptIssued = false;
};
