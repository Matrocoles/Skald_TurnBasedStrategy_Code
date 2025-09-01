#include "DeployUnitsReplicationTest.h"

#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Skald_TurnManager.h"
#include "Skald_PlayerState.h"
#include "WorldMap.h"
#include "Territory.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkaldDeployReplicationTest,
                                 "Skald.Multiplayer.DeployReplication",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)

bool FSkaldDeployReplicationTest::RunTest(const FString& Parameters)
{
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    TestNotNull(TEXT("World created"), World);
    if (!World)
    {
        return false;
    }

    ATurnManager* TM = World->SpawnActor<ATurnManager>();
    AWorldMap* WM = World->SpawnActor<AWorldMap>();
    ADeployTestPlayerController* PC1 = World->SpawnActor<ADeployTestPlayerController>();
    ADeployTestPlayerController* PC2 = World->SpawnActor<ADeployTestPlayerController>();
    ASkaldPlayerState* PS1 = World->SpawnActor<ASkaldPlayerState>();
    ASkaldPlayerState* PS2 = World->SpawnActor<ASkaldPlayerState>();
    ATerritory* Terr = World->SpawnActor<ATerritory>();

    TestNotNull(TEXT("TurnManager"), TM);
    TestNotNull(TEXT("WorldMap"), WM);
    TestNotNull(TEXT("PlayerController1"), PC1);
    TestNotNull(TEXT("PlayerController2"), PC2);
    TestNotNull(TEXT("PlayerState1"), PS1);
    TestNotNull(TEXT("PlayerState2"), PS2);
    TestNotNull(TEXT("Territory"), Terr);
    if (!TM || !WM || !PC1 || !PC2 || !PS1 || !PS2 || !Terr)
    {
        return false;
    }

    PC1->PlayerState = PS1;
    PC2->PlayerState = PS2;
    PS1->SetPlayerId(1);
    PS2->SetPlayerId(2);
    TM->RegisterController(PC1);
    TM->RegisterController(PC2);

    Terr->TerritoryID = 1;
    Terr->OwningPlayer = PS1;
    Terr->ArmyUnits = 5;
    WM->Territories = {Terr};
    PS1->DeployableUnits = 10;

    UDeployTestHUDWidget* HUD1 = NewObject<UDeployTestHUDWidget>(PC1);
    UDeployTestHUDWidget* HUD2 = NewObject<UDeployTestHUDWidget>(PC2);
    PC1->SetHUD(HUD1);
    PC2->SetHUD(HUD2);

    PC1->ServerDeployUnits(Terr->TerritoryID, 3);

    TestEqual(TEXT("Territory army updated"), Terr->ArmyUnits, 8);
    TestEqual(TEXT("Player deployable units updated"), PS1->DeployableUnits, 7);
    TestEqual(TEXT("HUD1 updated"), HUD1->LastUnits, 7);
    TestEqual(TEXT("HUD2 updated"), HUD2->LastUnits, 7);

    return true;
}

