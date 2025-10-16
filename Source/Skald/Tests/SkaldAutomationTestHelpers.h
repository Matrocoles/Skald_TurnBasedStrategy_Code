#pragma once

#if WITH_AUTOMATION_TESTS

#include "CoreMinimal.h"

class UWorld;

namespace Skald::Tests
{
UWorld* CreateAutomationTestWorld();
void DestroyAutomationTestWorld(UWorld* World);

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

#endif // WITH_AUTOMATION_TESTS

