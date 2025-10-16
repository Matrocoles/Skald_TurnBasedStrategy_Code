#include "PlayerControllerValidationTest.h"

#if WITH_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

#include "Tests/SkaldAutomationTestHelpers.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkaldPlayerControllerValidationFeedbackTest,
                                 "Skald.PlayerController.ValidationFeedback",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)

bool FSkaldPlayerControllerValidationFeedbackTest::RunTest(const FString &)
{
    Skald::Tests::FScopedAutomationTestWorld TestWorld;
    UWorld *World = TestWorld.Get();
    TestNotNull(TEXT("World created"), World);
    if (!World)
    {
        return false;
    }

    ATestPlayerController *PC = World->SpawnActor<ATestPlayerController>();
    TestNotNull(TEXT("PlayerController"), PC);
    if (!PC)
    {
        return false;
    }

    UTestHUDWidget *HUD = NewObject<UTestHUDWidget>(PC);
    PC->SetHUD(HUD);

    PC->HandleAttackRequested(1, 2, 1, false);
    TestTrue(TEXT("Attack error shown"), !HUD->LastError.IsEmpty());

    HUD->LastError.Empty();
    PC->HandleMoveRequested(1, 2, 1);
    TestTrue(TEXT("Move error shown"), !HUD->LastError.IsEmpty());

    return true;
}
#endif // WITH_AUTOMATION_TESTS
