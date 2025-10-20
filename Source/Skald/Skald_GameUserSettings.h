#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameUserSettings.h"
#include "Skald_GameUserSettings.generated.h"

/**
 * Custom user settings storing pacing options for AI behaviour.
 */
UCLASS()
class SKALD_API USkaldGameUserSettings : public UGameUserSettings {
  GENERATED_BODY()

public:
  USkaldGameUserSettings();

  /** Accessor that casts the engine user settings to the custom type. */
  static USkaldGameUserSettings *GetSkaldGameUserSettings();

  float GetEnemyTurnStepDelay() const { return EnemyTurnStepDelay; }
  float GetBattleActionDelay() const { return BattleActionDelay; }

  void SetEnemyTurnStepDelay(float InDelay);
  void SetBattleActionDelay(float InDelay);

protected:
  virtual void SetToDefaults() override;

  /** Time between AI decision steps on the overworld. */
  UPROPERTY(Config, EditAnywhere, Category = "AI", meta = (ClampMin = "0.0"))
  float EnemyTurnStepDelay;

  /** Time between AI-controlled actions during battles. */
  UPROPERTY(Config, EditAnywhere, Category = "AI", meta = (ClampMin = "1.0"))
  float BattleActionDelay;
};
