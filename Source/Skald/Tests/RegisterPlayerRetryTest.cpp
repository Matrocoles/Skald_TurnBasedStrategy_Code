#include "Misc/AutomationTest.h"

#if defined(WITH_AUTOMATION_TESTS)
#if WITH_AUTOMATION_TESTS

#include "Engine/World.h"
#include "Skald_GameMode.h"
#include "Skald_GameState.h"
#include "Skald_PlayerController.h"
#include "Skald_PlayerState.h"
#include "Skald_TurnManager.h"
#include "Tests/SkaldAutomationTestHelpers.h"
#include "Tests/SkaldGameModeAutomationAccessor.h"
#include "UObject/UnrealType.h"

namespace
{
void AttachGameModeToWorld(UWorld* World, ASkaldGameMode* GameMode)
{
    if (!World || !GameMode)
    {
        return;
    }

    if (FObjectProperty* Prop = FindFProperty<FObjectProperty>(UWorld::StaticClass(), TEXT("AuthorityGameMode")))
    {
        Prop->SetObjectPropertyValue_InContainer(World, GameMode);
    }
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegisterPlayerRetriesPlayerStateTest,
                                 "Skald.GameMode.RegisterPlayerRetriesPlayerState",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRegisterPlayerRetriesPlayerStateTest::RunTest(const FString& Parameters)
{
    Skald::Tests::FScopedAutomationTestWorld TestWorld;
    UWorld* World = TestWorld.Get();
    TestNotNull(TEXT("World created"), World);
    if (!World)
    {
        return false;
    }

    ASkaldGameMode* GameMode = World->SpawnActor<ASkaldGameMode>();
    ATurnManager* TurnManager = World->SpawnActor<ATurnManager>();
    ASkaldPlayerController* Controller = World->SpawnActor<ASkaldPlayerController>();
    ASkaldPlayerState* PlayerState = World->SpawnActor<ASkaldPlayerState>();
    TestNotNull(TEXT("GameMode"), GameMode);
    TestNotNull(TEXT("TurnManager"), TurnManager);
    TestNotNull(TEXT("Controller"), Controller);
    TestNotNull(TEXT("PlayerState"), PlayerState);
    if (!GameMode || !TurnManager || !Controller || !PlayerState)
    {
        return false;
    }

    AttachGameModeToWorld(World, GameMode);
    GameMode->InitGameState();
    ASkaldGameState* GameState = GameMode->GetGameState<ASkaldGameState>();
    TestNotNull(TEXT("GameState initialised"), GameState);
    if (!GameState)
    {
        return false;
    }

    TestEqual(TEXT("TurnManager starts empty"), TurnManager->GetControllerCount(), 0);

    FSkaldGameModeAutomationAccessor::RegisterPlayer(GameMode, Controller);

    auto TickWorld = [&]()
    {
        World->Tick(ELevelTick::LEVELTICK_All, 0.1f);
    };

    for (int32 Index = 0; Index < 3; ++Index)
    {
        TickWorld();
    }

    TestEqual(TEXT("Controller pending while PlayerState missing"), TurnManager->GetControllerCount(), 0);

    Controller->PlayerState = PlayerState;
    PlayerState->SetOwner(Controller);
    PlayerState->SetPlayerId(1);
    GameState->AddPlayerState(PlayerState);

    bool bControllerRegistered = false;
    for (int32 Index = 0; Index < 5 && !bControllerRegistered; ++Index)
    {
        TickWorld();
        bControllerRegistered = TurnManager->GetControllers().Contains(Controller);
    }

    TestTrue(TEXT("Controller registered after PlayerState replication"), bControllerRegistered);

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
#endif  // defined(WITH_AUTOMATION_TESTS)

