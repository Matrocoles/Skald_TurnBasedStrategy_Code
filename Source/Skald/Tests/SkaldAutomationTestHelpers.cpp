#include "Tests/SkaldAutomationTestHelpers.h"

#if defined(WITH_AUTOMATION_TESTS)
#if WITH_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/World.h"

namespace Skald::Tests
{
UWorld* CreateAutomationTestWorld()
{
    if (!GEngine)
    {
        return nullptr;
    }

    FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    WorldContext.SetCurrentWorld(World);

    World->InitializeNewWorld(UWorld::InitializationValues()
                                  .AllowAudioPlayback(false)
                                  .CreateNavigation(false)
                                  .CreateAISystem(false)
                                  .ShouldSimulatePhysics(false)
                                  .SetTransactional(false));

    World->BeginPlay();

    return World;
}

void DestroyAutomationTestWorld(UWorld* World)
{
    if (!World)
    {
        return;
    }

    World->CleanupWorld();

    if (GEngine)
    {
        if (FWorldContext* WorldContext = GEngine->GetWorldContextFromWorld(World))
        {
            GEngine->DestroyWorldContext(World);
        }
    }

    World->DestroyWorld(false);
}

FScopedAutomationTestWorld::FScopedAutomationTestWorld()
    : World(CreateAutomationTestWorld())
{
}

FScopedAutomationTestWorld::~FScopedAutomationTestWorld()
{
    DestroyAutomationTestWorld(World);
    World = nullptr;
}
} // namespace Skald::Tests

#endif  // WITH_AUTOMATION_TESTS
#endif  // defined(WITH_AUTOMATION_TESTS)

