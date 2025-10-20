#include "Misc/AutomationTest.h"

#if defined(WITH_AUTOMATION_TESTS)
#if WITH_AUTOMATION_TESTS
#include "GridBattleManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkaldResolveAttackClampTest, "Skald.GridBattle.ResolveAttackClamp", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSkaldResolveAttackClampTest::RunTest(const FString& Parameters) {
  FFighter Attacker;
  Attacker.Stats.AttackDice = 1;
  Attacker.Stats.AttackDamage = 6;

  FFighter Defender;
  Defender.Stats.Health = 5;

  FRandomStream RandomStream(3);
  int32 OutDamage = 0;
  FDiceRollResult OutResult;
  const bool bDefenderSurvived =
      UGridBattleManager::ResolveAttack(Attacker, Defender, OutDamage,
                                        RandomStream, OutResult);

  TestFalse(TEXT("Defender should be defeated"), bDefenderSurvived);
  TestEqual(TEXT("Damage is clamped to defender health"), OutDamage, 5);
  TestEqual(TEXT("Defender health clamped"), Defender.Stats.Health, 0);
  TestEqual(TEXT("Total damage recorded"), OutResult.TotalDamage, 5);
  TestEqual(TEXT("Ending health recorded"), OutResult.EndingHealth, 0);
  return true;
}
#endif  // WITH_AUTOMATION_TESTS
#endif  // defined(WITH_AUTOMATION_TESTS)
