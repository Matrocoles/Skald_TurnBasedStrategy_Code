#include "Misc/AutomationTest.h"

#if defined(WITH_AUTOMATION_TESTS) && WITH_AUTOMATION_TESTS
#include "PlayerControllerValidationTest.h"

#include "Tests/SkaldAutomationTestHelpers.h"
#include "WorldMap.h"
#include "Territory.h"
#include "Skald_PlayerState.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSkaldDeployUnitsValidationFeedbackTest,
    "Skald.PlayerController.DeployValidationFeedback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSkaldDeployUnitsValidationFeedbackTest::RunTest(const FString&)
{
    Skald::Tests::FScopedAutomationTestWorld TestWorld;
    UWorld* World = TestWorld.Get();
    TestNotNull(TEXT("World created"), World);
    if (!World)
    {
        return false;
    }

    ATestPlayerController* PC = World->SpawnActor<ATestPlayerController>();
    TestNotNull(TEXT("PlayerController"), PC);
    if (!PC)
    {
        return false;
    }

    UTestHUDWidget* HUD = NewObject<UTestHUDWidget>(PC);
    PC->SetHUD(HUD);

    // Missing world map
    PC->ServerDeployUnits(1, 1);
    TestTrue(TEXT("Missing map error"), HUD->LastError.Contains(TEXT("World map")));

    // World map exists but territory invalid
    HUD->LastError.Empty();
    AWorldMap* WM = World->SpawnActor<AWorldMap>();
    PC->ServerDeployUnits(1, 1);
    TestTrue(TEXT("Invalid territory error"),
             HUD->LastError.Contains(TEXT("Invalid territory")));

    // Setup players and territory for ownership/units tests
    HUD->LastError.Empty();
    ATerritory* Terr = World->SpawnActor<ATerritory>();
    ASkaldPlayerState* PS1 = World->SpawnActor<ASkaldPlayerState>();
    ASkaldPlayerState* PS2 = World->SpawnActor<ASkaldPlayerState>();
    PC->PlayerState = PS1;
    PS1->SetPlayerId(1);
    PS2->SetPlayerId(2);
    Terr->TerritoryID = 42;
    Terr->OwningPlayer = PS2;
    WM->Territories = {Terr};
    PS1->DeployableUnits = 5;

    PC->ServerDeployUnits(Terr->TerritoryID, 1);
    TestTrue(TEXT("Not owner error"), HUD->LastError.Contains(TEXT("own")));

    // Insufficient units
    HUD->LastError.Empty();
    Terr->OwningPlayer = PS1;
    Terr->bIsCapital = false;
    PS1->DeployableUnits = 5;
    PC->ServerDeployUnits(Terr->TerritoryID, 1);
    TestTrue(TEXT("Capital requirement error"),
             HUD->LastError.Contains(TEXT("capital")));

    // Set as capital and retry insufficient units
    HUD->LastError.Empty();
    Terr->bIsCapital = true;
    PS1->DeployableUnits = 0;
    PC->ServerDeployUnits(Terr->TerritoryID, 1);
    TestTrue(TEXT("Insufficient units error"),
             HUD->LastError.Contains(TEXT("deployable units")));

    return true;
}
#endif // defined(WITH_AUTOMATION_TESTS) && WITH_AUTOMATION_TESTS
