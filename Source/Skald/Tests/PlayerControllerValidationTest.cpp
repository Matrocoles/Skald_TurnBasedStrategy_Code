#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Skald_PlayerController.h"
#include "UI/SkaldMainHUDWidget.h"
#include "PlayerControllerValidationTest.generated.h"

UCLASS()
class UTestHUDWidget : public USkaldMainHUDWidget {
  GENERATED_BODY()
public:
  FString LastError;
  virtual void ShowErrorMessage(const FString &Message) override {
    LastError = Message;
  }
};

UCLASS()
class ATestPlayerController : public ASkaldPlayerController {
  GENERATED_BODY()
public:
  void SetHUD(USkaldMainHUDWidget *InHUD) { MainHudWidget = InHUD; }
};

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
