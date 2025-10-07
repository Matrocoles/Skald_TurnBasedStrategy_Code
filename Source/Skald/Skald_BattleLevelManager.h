#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "SkaldTypes.h"
#include "Skald_BattleLevelManager.generated.h"

class ULevelStreamingDynamic;
class USkaldGameInstance;
class ULevel;
class UWorld;

/**
 * Helper object responsible for loading and unloading tactical battle levels
 * without leaving the persistent overworld map. The manager streams battle
 * maps in as sub-levels so gameplay state in the primary world remains intact.
 */
UCLASS()
class SKALD_API USkaldBattleLevelManager : public UObject {
  GENERATED_BODY()

public:
  void Initialise(USkaldGameInstance *InOwner);

  /** Attempt to stream in the specified battle level. Returns true when the
   * request was issued successfully. */
  bool RequestBattleLevel(UWorld *World, const TSoftObjectPtr<UWorld> &BattleLevel,
                          const FS_BattlePayload &BattlePayload);

  /** Unload any active streamed battle level. */
  void ReleaseBattleLevel();

  /** Returns whether a streamed battle level is currently active. */
  bool IsBattleLevelActive() const { return ActiveStreamingLevel.IsValid(); }

private:
  void HandleLevelLoaded();
  void HandleLevelUnloaded();
  void HandleLevelAddedToWorld(ULevel *InLevel, UWorld *InWorld);
  void HandleLevelRemovedFromWorld(ULevel *InLevel, UWorld *InWorld);
  bool DoesEventMatchActiveLevel(ULevel *InLevel, UWorld *InWorld) const;
  void RegisterWorldDelegates();
  void UnregisterWorldDelegates();

  TWeakObjectPtr<USkaldGameInstance> OwningInstance;
  TWeakObjectPtr<ULevelStreamingDynamic> ActiveStreamingLevel;
  TSoftObjectPtr<UWorld> RequestedBattleLevel;
  FDelegateHandle LevelAddedToWorldHandle;
  FDelegateHandle LevelRemovedFromWorldHandle;
  FS_BattlePayload PendingPayload;
  bool bActiveLevelShouldBeLoaded = false;
};
