#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS
#include "SkaldTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkaldCapitalAttackRequirementTest, "Skald.Attack.CapitalRequirement", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSkaldCapitalAttackRequirementTest::RunTest(const FString& Parameters)
{
    TestTrue(TEXT("Non-capital attack bypasses requirement"), SkaldHelpers::MeetsCapitalAttackRequirement(false, 1));
    TestFalse(TEXT("Capital attack below requirement is invalid"), SkaldHelpers::MeetsCapitalAttackRequirement(true, SkaldConstants::CapitalAttackArmyRequirement - 1));
    TestTrue(TEXT("Capital attack meeting requirement is valid"), SkaldHelpers::MeetsCapitalAttackRequirement(true, SkaldConstants::CapitalAttackArmyRequirement));
    return true;
}
#endif // WITH_AUTOMATION_TESTS
