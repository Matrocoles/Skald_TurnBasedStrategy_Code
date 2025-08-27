#include "Misc/AutomationTest.h"
#include "UI/SkaldMainHUDWidget.h"
#include "UObject/Class.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkaldMainHUDWidgetBindingsTest, "Skald.UI.BindingsRemain", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSkaldMainHUDWidgetBindingsTest::RunTest(const FString& Parameters)
{
    const UClass* WidgetClass = USkaldMainHUDWidget::StaticClass();
    TestNotNull(TEXT("AttackButton property should exist"), WidgetClass->FindPropertyByName(GET_MEMBER_NAME_CHECKED(USkaldMainHUDWidget, AttackButton)));
    TestNotNull(TEXT("MoveButton property should exist"), WidgetClass->FindPropertyByName(GET_MEMBER_NAME_CHECKED(USkaldMainHUDWidget, MoveButton)));
    TestNotNull(TEXT("EndTurnButton property should exist"), WidgetClass->FindPropertyByName(GET_MEMBER_NAME_CHECKED(USkaldMainHUDWidget, EndTurnButton)));
    TestNotNull(TEXT("EndPhaseButton property should exist"), WidgetClass->FindPropertyByName(GET_MEMBER_NAME_CHECKED(USkaldMainHUDWidget, EndPhaseButton)));
    return true;
}
