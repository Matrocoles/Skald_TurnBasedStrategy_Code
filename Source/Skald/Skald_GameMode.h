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
  TArray<FS_PlayerData> PlayersData;

  /** Setup initial territories, armies, and initiative. */
  UFUNCTION(BlueprintCallable, Category = "GameMode")
  void InitializeWorld();

  /** Allow players to position initial armies based on initiative. */
  UFUNCTION(BlueprintCallable, Category = "GameMode")
  void BeginArmyPlacementPhase();

private:
  /** Timer that triggers auto-start of the turn sequence. */
  FTimerHandle StartGameTimerHandle;

  /** Tracks whether turns have already begun to avoid duplicates. */
  bool bTurnsStarted;

  /** Whether the world has been initialized and territories assigned. */
  bool bWorldInitialized;

  /** Index of the controller currently placing armies. */
  int32 PlacementIndex = 0;
};
