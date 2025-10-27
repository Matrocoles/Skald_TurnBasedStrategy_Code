#include "Skald_GameUserSettings.h"
#include "Engine/Engine.h"

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
  EnemyTurnStepDelay = FMath::Max(MinimumEnemyTurnStepDelay, InDelay);
}

void USkaldGameUserSettings::SetBattleActionDelay(float InDelay)
{
  BattleActionDelay = FMath::Max(MinimumBattleActionDelay, InDelay);
}

void USkaldGameUserSettings::SetToDefaults()
{
  Super::SetToDefaults();

  EnemyTurnStepDelay = DefaultEnemyTurnStepDelay;
  BattleActionDelay = DefaultBattleActionDelay;
}
