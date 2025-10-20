#include "Skald_GameUserSettings.h"
#include "Engine/Engine.h"

namespace
{
constexpr float DefaultEnemyTurnStepDelay = 0.75f;
constexpr float DefaultBattleActionDelay = 1.0f;
}

USkaldGameUserSettings::USkaldGameUserSettings()
    : EnemyTurnStepDelay(DefaultEnemyTurnStepDelay),
      BattleActionDelay(DefaultBattleActionDelay) {}

USkaldGameUserSettings *USkaldGameUserSettings::GetSkaldGameUserSettings()
{
  if (GEngine)
  {
    if (UGameUserSettings *Settings = GEngine->GetGameUserSettings())
    {
      return Cast<USkaldGameUserSettings>(Settings);
    }
  }
  return nullptr;
}

void USkaldGameUserSettings::SetEnemyTurnStepDelay(float InDelay)
{
  EnemyTurnStepDelay = FMath::Max(0.0f, InDelay);
}

void USkaldGameUserSettings::SetBattleActionDelay(float InDelay)
{
  BattleActionDelay = FMath::Max(1.0f, InDelay);
}

void USkaldGameUserSettings::SetToDefaults()
{
  Super::SetToDefaults();

  EnemyTurnStepDelay = DefaultEnemyTurnStepDelay;
  BattleActionDelay = DefaultBattleActionDelay;
}
