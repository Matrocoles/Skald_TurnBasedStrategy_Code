#include "Misc/AutomationTest.h"
#include "WorldMap.h"
#include "Tests/AutomationEditorCommon.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldMapDefaultTableTest, "Skald.WorldMap.DefaultTableUnset", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldMapDefaultTableTest::RunTest(const FString& Parameters)
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

    TestNull(TEXT("TerritoryTable should be unset"), Map->TerritoryTable);
    return true;
}
