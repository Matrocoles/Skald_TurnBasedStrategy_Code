#include "DeploySelectionCachingTest.h"

#if defined(WITH_AUTOMATION_TESTS)
#if WITH_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

#include "Tests/SkaldAutomationTestHelpers.h"
#include "Skald_PlayerController.h"
#include "Skald_PlayerState.h"
#include "Skald_TurnManager.h"
#include "WorldMap.h"
#include "Territory.h"
#include "UI/DeployWidget.h"

// Verify that territory selection for deployment persists even if the
// local player state is not yet assigned when the territory is clicked.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSkaldDeploySelectionCachingTest,
    "Skald.UI.DeploySelectionCaching",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSkaldDeploySelectionCachingTest::RunTest(const FString& Parameters)
{
    Skald::Tests::FScopedAutomationTestWorld TestWorld;
    UWorld* World = TestWorld.Get();
    TestNotNull(TEXT("World created"), World);
    if (!World)
    {
        return false;
    }

    ATurnManager* TM = World->SpawnActor<ATurnManager>();
    ASkaldPlayerController* PC = World->SpawnActor<ASkaldPlayerController>();
    ASkaldPlayerState* PS = World->SpawnActor<ASkaldPlayerState>();
    AWorldMap* Map = World->SpawnActor<AWorldMap>();
    ATerritory* Terr = World->SpawnActor<ATerritory>();

    TestNotNull(TEXT("TurnManager"), TM);
    TestNotNull(TEXT("PlayerController"), PC);
    TestNotNull(TEXT("PlayerState"), PS);
    TestNotNull(TEXT("WorldMap"), Map);
    TestNotNull(TEXT("Territory"), Terr);
    if (!TM || !PC || !PS || !Map || !Terr)
    {
        return false;
    }

    PC->SetTurnManager(TM);
    TM->RegisterController(PC);

    UTestSkaldMainHUDWidget* HUD = NewObject<UTestSkaldMainHUDWidget>(PC);
    HUD->SetOwningPlayer(PC);
    HUD->DeployWidgetClass = UDeployWidget::StaticClass();

    Terr->TerritoryID = 7;
    Terr->OwningPlayer = PS;
    Terr->ArmyUnits = 2;
    Map->Territories = {Terr};

    // Simulate clicking the territory before the player state is assigned to the controller
    HUD->OnTerritoryClickedUI(Terr);
    TestEqual(TEXT("Selection stored without local player state"), HUD->SelectedSourceID, Terr->TerritoryID);

    // Assign player state and give deployable units
    PC->PlayerState = PS;
    PS->SetPlayerId(1);
    PS->DeployableUnits = 1;

    HUD->HandleDeployClicked();
    TestNotNull(TEXT("Deploy widget created"), HUD->GetActiveDeployWidget());

    return true;
}
#endif  // WITH_AUTOMATION_TESTS
#endif  // defined(WITH_AUTOMATION_TESTS)
