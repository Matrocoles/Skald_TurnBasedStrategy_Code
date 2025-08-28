#include "Misc/AutomationTest.h"
#include "GridBattleManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkaldResolveAttackClampTest, "Skald.GridBattle.ResolveAttackClamp", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSkaldResolveAttackClampTest::RunTest(const FString& Parameters) {
  FFighter Attacker;
  Attacker.Stats.AttackDice = 1;
  Attacker.Stats.DamageDie = 6;

  FFighter Defender;
  Defender.Stats.Health = 5;

  FRandomStream RandomStream(3);
  int32 OutDamage = 0;
  UGridBattleManager::ResolveAttack(Attacker, Defender, OutDamage, RandomStream);

  TestTrue(TEXT("Damage exceeds health"), OutDamage > 5);
  TestEqual(TEXT("Defender health clamped"), Defender.Stats.Health, 0);
  return true;
}
