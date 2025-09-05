#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Engine/World.h"
#include "Skald_AIController.h"
#include "Skald_PlayerState.h"
#include "Skald_TurnManager.h"
#include "WorldMap.h"
#include "Territory.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkaldAIDecisionFlowTest, "Skald.AI.DecisionFlow", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSkaldAIDecisionFlowTest::RunTest(const FString& Parameters)
{
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    TestNotNull(TEXT("World created"), World);
    if (!World)
    {
        return false;
    }

    // Create core actors
    ATurnManager* TM = World->SpawnActor<ATurnManager>();
    ASkaldAIController* PC1 = World->SpawnActor<ASkaldAIController>();
    ASkaldPlayerState* PS1 = World->SpawnActor<ASkaldPlayerState>();
    ASkaldPlayerState* PS2 = World->SpawnActor<ASkaldPlayerState>();
    AWorldMap* Map = World->SpawnActor<AWorldMap>();

    TestNotNull(TEXT("TurnManager"), TM);
    TestNotNull(TEXT("AI Controller"), PC1);
    TestNotNull(TEXT("AI PlayerState"), PS1);
    TestNotNull(TEXT("WorldMap"), Map);
    if (!TM || !PC1 || !PS1 || !Map)
    {
        return false;
    }

    PC1->PlayerState = PS1;
    PS1->DeployableUnits = 4;
    PS1->Resources = 4;
    PC1->SetTurnManager(TM);

    // Territories setup
    ATerritory* TA = World->SpawnActor<ATerritory>();
    ATerritory* TB = World->SpawnActor<ATerritory>();
    ATerritory* TC = World->SpawnActor<ATerritory>();
    TestNotNull(TEXT("Territory A"), TA);
    TestNotNull(TEXT("Territory B"), TB);
    TestNotNull(TEXT("Territory C"), TC);
    if (!TA || !TB || !TC)
    {
        return false;
    }

    TA->TerritoryID = 1; TA->OwningPlayer = PS1; TA->ArmyUnits = 10;
    TB->TerritoryID = 2; TB->OwningPlayer = PS2; TB->ArmyUnits = 1;
    TC->TerritoryID = 3; TC->OwningPlayer = PS1; TC->ArmyUnits = 1;

    TA->AdjacentTerritories = {TB, TC};
    TB->AdjacentTerritories = {TA};
    TC->AdjacentTerritories = {TA};

    Map->Territories = {TA, TB, TC};

    FMath::RandInit(1);
    PC1->StartTurn();

    TestEqual(TEXT("Deployable units spent"), PS1->DeployableUnits, 0);
    TestEqual(TEXT("Resources spent"), PS1->Resources, 0);
    TestEqual(TEXT("Attack captured territory"), TB->OwningPlayer, PS1);
    TestTrue(TEXT("Movement reinforced"), TA->ArmyUnits > 1);
    TestEqual(TEXT("Turn ended"), TM->GetCurrentPhase(), ETurnPhase::EndTurn);

    return true;
}
