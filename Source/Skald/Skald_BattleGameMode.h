#pragma once

#include "CoreMinimal.h"
#include "Skald_GameMode.h"
#include "SkaldTypes.h"
#include "AIController.h"
#include "Skald_BattleGameMode.generated.h"

class AAIController;
class AController;

DECLARE_LOG_CATEGORY_EXTERN(LogSkaldBattle, Log, All);

/** GameMode dedicated to resolving grid-based battles. */
UCLASS()
class SKALD_API ASkald_BattleGameMode : public ASkaldGameMode {
  GENERATED_BODY()

public:
  /** Attempt to start the tactical battle once both sides have supplied armies. */
  void TryLaunchBattle();

protected:
  virtual void InitGame(const FString &Map, const FString &Options,
                        FString &Error) override;
  virtual void BeginPlay() override;
  virtual void PostLogin(APlayerController *NewPlayer) override;
  virtual void TryInitializeWorldAndStart() override;

public:
  // Called by the AI controller when it’s ready (BeginPlay)
  void OnAIControllerReady(AAIController *Controller);

  UFUNCTION(BlueprintCallable)
  void OnControllerReady(AController *Controller);

private:
  void SetupPendingBattle();
  void AutoCommitAIArmy(ASkaldPlayerState *PlayerState, int32 Budget) const;
  void SpawnFighterSide(const TArray<FFighterDefinition> &Roster, bool bAsAttacker);
  void TryStartBattle();

  bool IsSoloMatch() const;
  void PollBattleBootstrap();

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
};

