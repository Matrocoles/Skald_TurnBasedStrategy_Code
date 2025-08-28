#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "SkaldTypes.h"
#include "Skald_GameMode.generated.h"

struct FTimerHandle;
class ATurnManager;
class ASkaldGameState;
class ASkaldPlayerController;
class ASkaldPlayerState;
class AWorldMap;
class APlayerController;
class USkaldSaveGame;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSkaldGameOver, ASkaldPlayerState *,
                                            Winner);

/**
 * GameMode responsible for managing player login and spawning the turn manager.
 */
UCLASS(Blueprintable, BlueprintType)
class SKALD_API ASkaldGameMode : public AGameModeBase {
  GENERATED_BODY()

public:
  ASkaldGameMode();

  virtual void BeginPlay() override;
  virtual void PostLogin(APlayerController *NewPlayer) override;

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

  /** Handle a player confirming their name and faction selection. */
  void HandlePlayerLockedIn(ASkaldPlayerState *PS);

protected:
  /** Handles turn sequencing for the match. */
  UPROPERTY(BlueprintReadOnly, Category = "GameMode")
  ATurnManager *TurnManager;

  /** Holds all territory actors for the current map. */
  UPROPERTY(BlueprintReadOnly, Category = "GameMode")
  AWorldMap *WorldMap;

  /** Data describing each player in the match. Pre-sized so blueprints can
   * safely write to indices without hitting invalid array warnings. */
  UPROPERTY(BlueprintReadOnly, Category = "Players")
  TArray<FS_PlayerData> PlayerDataArray;

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
  int32 BuildSiegeAtTerritory(int32 TerritoryID, E_SiegeWeapons Type);

  /** Consume a built siege from a territory for an attack. */
  UFUNCTION(BlueprintCallable, Category = "Siege")
  int32 ConsumeSiege(int32 TerritoryID);

  /** Update cached player resource values. */
  void UpdatePlayerResources(ASkaldPlayerState *Player);

private:
  /** Timer that triggers auto-start of the turn sequence. */
  FTimerHandle StartGameTimerHandle;

  /** Tracks whether turns have already begun to avoid duplicates. */
  bool bTurnsStarted;

  /** Whether the world has been initialized and territories assigned. */
  bool bWorldInitialized;

  /** Controllers waiting for world initialization before registration. */
  TArray<ASkaldPlayerController *> PendingControllers;

  /** Index of the controller currently placing armies. */
  int32 PlacementIndex = 0;

  /** Register a newly connected player and update player data. */
  void RegisterPlayer(ASkaldPlayerController *PC);

  /** Populate remaining slots with AI players in singleplayer. */
  void PopulateAIPlayers();

  /** Notify HUDs of the current player roster. */
  void RefreshHUDs();

  /** Attempt to initialise the world and start the game flow. */
  void TryInitializeWorldAndStart();
};
