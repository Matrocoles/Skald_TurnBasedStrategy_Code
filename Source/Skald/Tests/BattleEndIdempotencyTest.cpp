#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "GridBattleManager.h"
#include "BattleEndIdempotencyTestListener.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkaldBattleEndIdempotencyTest,
                                 "Skald.Battle.EndBattleIdempotent",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)

bool FSkaldBattleEndIdempotencyTest::RunTest(const FString& Parameters)
{
  UGridBattleManager* Manager = NewObject<UGridBattleManager>();
  UBattleEndIdempotencyTestListener* Listener = NewObject<UBattleEndIdempotencyTestListener>();

  TestNotNull(TEXT("Battle manager created"), Manager);
  if (!Manager)
  {
    return false;
  }

  TestNotNull(TEXT("Listener created"), Listener);
  if (!Listener)
  {
    return false;
  }

  Manager->OnBattleEnded.AddDynamic(Listener,
                                    &UBattleEndIdempotencyTestListener::HandleBattleEnded);

  Manager->EndBattle();
  Manager->EndBattle();

  TestEqual(TEXT("OnBattleEnded should broadcast once across duplicate EndBattle calls"),
            Listener->NotificationCount, 1);
  return true;
}

#endif
