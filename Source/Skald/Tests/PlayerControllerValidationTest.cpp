#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "PlayerControllerValidationTest.h"

void UTestHUDWidget::ShowErrorMessage(const FString& Message)
{
    LastError = Message;
}

void ATestPlayerController::SetHUD(USkaldMainHUDWidget* InHUD)
{
    MainHudWidget = InHUD;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkaldPlayerControllerValidationFeedbackTest,
                                 "Skald.PlayerController.ValidationFeedback",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)

bool FSkaldPlayerControllerValidationFeedbackTest::RunTest(const FString &)
{
    UWorld *World = FAutomationEditorCommonUtils::CreateNewMap();
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
