#pragma once

#include "Misc/AutomationTest.h"

#if defined(WITH_AUTOMATION_TESTS)
#if WITH_AUTOMATION_TESTS

#include "CoreMinimal.h"

class UWorld;
class ASkaldGameMode;

namespace Skald::Tests
{
UWorld* CreateAutomationTestWorld();
void DestroyAutomationTestWorld(UWorld* World);
void AttachGameModeToWorld(UWorld* World, ASkaldGameMode* GameMode);

class FScopedAutomationTestWorld
{
public:
    FScopedAutomationTestWorld();
    ~FScopedAutomationTestWorld();

    UWorld* Get() const { return World; }
    UWorld* operator->() const { return World; }
    operator UWorld*() const { return World; }

private:
    UWorld* World;
};
}

#endif  // WITH_AUTOMATION_TESTS
#endif  // defined(WITH_AUTOMATION_TESTS)

