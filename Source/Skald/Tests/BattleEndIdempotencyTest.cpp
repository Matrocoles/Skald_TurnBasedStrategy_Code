#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "GridBattleManager.h"
#include "UObject/Object.h"

UCLASS()
class USkaldBattleEndListener final : public UObject
{
  GENERATED_BODY()

public:
  int32 NotificationCount = 0;

  UFUNCTION()
  void HandleBattleEnded(ESkaldFaction /*WinningFaction*/, int32 /*AttackerCasualties*/, int32 /*DefenderCasualties*/)
  {
    ++NotificationCount;
  }
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkaldBattleEndIdempotencyTest,
                                 "Skald.Battle.EndBattleIdempotent",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)

bool FSkaldBattleEndIdempotencyTest::RunTest(const FString& Parameters)
{
  UGridBattleManager* Manager = NewObject<UGridBattleManager>();
  USkaldBattleEndListener* Listener = NewObject<USkaldBattleEndListener>();

  TestNotNull(TEXT("Battle manager created"), Manager);
  TestNotNull(TEXT("Listener created"), Listener);
  if (!Manager || !Listener)
  {
    return false;
  }

  Manager->OnBattleEnded.AddDynamic(Listener, &USkaldBattleEndListener::HandleBattleEnded);

  Manager->EndBattle();
  Manager->EndBattle();

  TestEqual(TEXT("OnBattleEnded should broadcast once across duplicate EndBattle calls"),
            Listener->NotificationCount, 1);
  return true;
}

#endif
