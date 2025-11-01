#pragma once

#include "CoreMinimal.h"
#include "Skald_PlayerController.h"
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

  const FSkaldBattleReadyState &GetPendingReadyState() const {
    return PendingBattleReadyState;
  }

  void ForcePendingBattleState(const FS_BattlePayload &Battle,
                               const FSkaldBattleReadyState &ReadyState) {
    PendingBattlePreparation = Battle;
    PendingBattleReadyState = ReadyState;
  }

  void InvokeBroadcastPreparePrompt(const FS_BattlePayload &Battle,
                                     const TCHAR *Context) {
    BroadcastPrepareForBattlePrompt(Battle, Context);
  }

  virtual void TriggerGridBattle(const FS_BattlePayload &Battle) override {
    bTriggerCalled = true;
    CapturedBattle = Battle;
  }
};

UCLASS()
class SKALD_API ATestBattlePromptController : public ASkaldPlayerController {
  GENERATED_BODY()

public:
  bool bPromptVisible = false;
  FPrepareForBattlePromptData LastPrompt;

  virtual void ShowPrepareForBattlePromptLocal(
      const FPrepareForBattlePromptData &PromptData) override {
    bPromptVisible = true;
    LastPrompt = PromptData;
  }

  virtual void ClientShowPrepareForBattle_Implementation(
      const FPrepareForBattlePromptData &PromptData) override {
    ShowPrepareForBattlePromptLocal(PromptData);
  }

  virtual void HidePrepareForBattlePromptLocal() override {
    bPromptVisible = false;
  }

  virtual void ClientHidePrepareForBattle_Implementation() override {
    HidePrepareForBattlePromptLocal();
  }
};
