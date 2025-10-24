#pragma once

#include "CoreMinimal.h"
#include "Skald_TurnManager.h"
#include "TurnManagerTest.generated.h"

UCLASS()
class SKALD_API ATestTurnManagerNoTravel : public ATurnManager {
  GENERATED_BODY()

public:
  /** Tracks whether TriggerGridBattle has been invoked. */
  bool bTriggerCalled = false;

  /** Stores the payload used when triggering a battle. */
  FS_BattlePayload CapturedBattle;

  virtual void TriggerGridBattle(const FS_BattlePayload &Battle) override {
    bTriggerCalled = true;
    CapturedBattle = Battle;
  }
};
