#include "Misc/AutomationTest.h"
#include "SkaldTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkaldCapitalAttackRequirementTest, "Skald.Attack.CapitalRequirement", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSkaldCapitalAttackRequirementTest::RunTest(const FString& Parameters)
{
    using namespace SkaldConstants;
    TestTrue(TEXT("Non-capital attack bypasses requirement"), SkaldHelpers::MeetsCapitalAttackRequirement(false, 1));
    TestFalse(TEXT("Capital attack below requirement is invalid"), SkaldHelpers::MeetsCapitalAttackRequirement(true, CapitalAttackArmyRequirement - 1));
    TestTrue(TEXT("Capital attack meeting requirement is valid"), SkaldHelpers::MeetsCapitalAttackRequirement(true, CapitalAttackArmyRequirement));
    return true;
}
