#include "Misc/AutomationTest.h"
#include "WorldMap.h"
#include "Tests/AutomationEditorCommon.h"
#include "Engine\World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldMapMissingAssetsTest, "Skald.WorldMap.MissingAssetsLogged", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldMapMissingAssetsTest::RunTest(const FString& Parameters)
{
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    TestNotNull(TEXT("World should be created"), World);
    if (!World)
    {
        return false;
    }

    AWorldMap* Map = World->SpawnActor<AWorldMap>();
    TestNotNull(TEXT("WorldMap spawned"), Map);
    if (!Map)
    {
        return false;
    }

    AddExpectedError(TEXT("WorldMap requires valid TerritoryClass and TerritoryTable."), EAutomationExpectedErrorFlags::Contains);
    Map->BeginPlay();
    return true;
}
