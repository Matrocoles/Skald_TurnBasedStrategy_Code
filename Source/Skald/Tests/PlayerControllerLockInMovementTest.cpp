#if WITH_AUTOMATION_TESTS
#include "ChoosePlayerWidget.h"
#include "Misc/AutomationTest.h"
#include "Skald_PlayerController.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkaldPlayerControllerLockInMovementTest,
                                 "Skald.PlayerController.LockInMovement",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)

bool FSkaldPlayerControllerLockInMovementTest::RunTest(const FString &)
{
    UWorld *World = FAutomationEditorCommonUtils::CreateNewMap();
    TestNotNull(TEXT("World created"), World);
    if (!World)
    {
        return false;
    }

    ASkaldPlayerController *PC = World->SpawnActor<ASkaldPlayerController>();
    TestNotNull(TEXT("PlayerController"), PC);
    if (!PC)
    {
        return false;
    }

    UChoosePlayerWidget *Widget = NewObject<UChoosePlayerWidget>(PC);
    FObjectProperty *ChooseProp = FindFProperty<FObjectProperty>(
        ASkaldPlayerController::StaticClass(), TEXT("ChoosePlayerWidget"));
    ChooseProp->SetObjectPropertyValue_InContainer(PC, Widget);

    PC->SetInputMode(FInputModeUIOnly());
    PC->SetIgnoreMoveInput(true);
    PC->SetIgnoreLookInput(true);

    TestTrue(TEXT("Movement disabled before lock in"), PC->IsMoveInputIgnored());
    TestTrue(TEXT("Look disabled before lock in"), PC->IsLookInputIgnored());

    PC->HandlePlayerLockedIn();

    TestFalse(TEXT("Movement enabled after lock in"), PC->IsMoveInputIgnored());
    TestFalse(TEXT("Look enabled after lock in"), PC->IsLookInputIgnored());

    return true;
}
#endif // WITH_AUTOMATION_TESTS
