#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "GridBattleManager.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkaldBattleEndIdempotencyTest,
                                 "Skald.Battle.EndBattleIdempotent",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)

bool FSkaldBattleEndIdempotencyTest::RunTest(const FString& Parameters)
{
  UGridBattleManager* Manager = NewObject<UGridBattleManager>();
  int32 NotificationCount = 0;

  TestNotNull(TEXT("Battle manager created"), Manager);
  if (!Manager)
  {
    return false;
  }

  Manager->OnBattleEnded.AddLambda([&NotificationCount](ESkaldFaction /*WinningFaction*/, int32 /*AttackerCasualties*/, int32 /*DefenderCasualties*/)
  {
    ++NotificationCount;
  });

  Manager->EndBattle();
  Manager->EndBattle();

  TestEqual(TEXT("OnBattleEnded should broadcast once across duplicate EndBattle calls"),
            NotificationCount, 1);
  return true;
}

#endif
