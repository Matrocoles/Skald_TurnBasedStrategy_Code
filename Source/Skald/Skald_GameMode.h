#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "SkaldTypes.h"
#include "TimerManager.h"
#include "GridBattleManager.h"
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
#endif // WITH_AUTOMATION_TESTS

public:
  ASkaldGameMode();
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

  /** Whether the world has already been initialised. */
  bool IsWorldInitialized() const { return bWorldInitialized; }

  /** Handle a player confirming their name and faction selection. */
  void HandlePlayerLockedIn(ASkaldPlayerState *PS);

  /** Capture the current overworld territory state for later restoration. */
  void CacheWorldMapSnapshot();

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

protected:
  /** Timer used to retry initialization until readiness checks pass. */
  FTimerHandle RetryInitTimerHandle;

  /** Tracks whether turns have already begun to avoid duplicates. */
  bool bTurnsStarted;

  /** Whether the world has been initialized and territories assigned. */
  bool bWorldInitialized;

private:
  /** Timer that triggers auto-start of the turn sequence. */
  FTimerHandle StartGameTimerHandle;

  /** Flag to avoid spawning AI players multiple times. */
  bool bAIPlayersSpawned;

  /** Controllers whose PlayerState is not yet valid, queued for retry. */
  TArray<ASkaldPlayerController *> PendingControllers;

  /** Index of the controller currently placing armies. */
  int32 PlacementIndex = 0;

  /** Controller that opened army placement with the highest initiative roll. */
  TWeakObjectPtr<ASkaldPlayerController> ArmyPlacementLeader;

  /** Register a newly connected player and update player data. */
  void RegisterPlayer(ASkaldPlayerController *PC);

  /** Populate remaining slots with AI players in singleplayer. */
  void PopulateAIPlayers();

  /** Notify HUDs of the current player roster. */
  void RefreshHUDs();

  /** Remove invalid player states before re-registering controllers. */
  void CleanupStalePlayerStates();
};
