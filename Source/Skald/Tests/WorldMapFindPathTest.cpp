#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "WorldMap.h"
#include "Territory.h"
#include "Skald_PlayerState.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldMapFindPathValidTest, "Skald.WorldMap.FindPath.Valid", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWorldMapFindPathValidTest::RunTest(const FString& Parameters) {
  UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
  TestNotNull(TEXT("World created"), World);
  if (!World) {
    return false;
  }

  AWorldMap* Map = World->SpawnActor<AWorldMap>();
  ASkaldPlayerState* PS = World->SpawnActor<ASkaldPlayerState>();
  ATerritory* T1 = World->SpawnActor<ATerritory>();
  ATerritory* T2 = World->SpawnActor<ATerritory>();
  ATerritory* T3 = World->SpawnActor<ATerritory>();
  TestNotNull(TEXT("Map"), Map);
  TestNotNull(TEXT("PlayerState"), PS);
  TestNotNull(TEXT("Territory1"), T1);
  TestNotNull(TEXT("Territory2"), T2);
  TestNotNull(TEXT("Territory3"), T3);
  if (!Map || !PS || !T1 || !T2 || !T3) {
    return false;
  }

  T1->OwningPlayer = PS;
  T2->OwningPlayer = PS;
  T3->OwningPlayer = PS;
  T1->AdjacentTerritories = {T2};
  T2->AdjacentTerritories = {T1, T3};
  T3->AdjacentTerritories = {T2};

  TArray<ATerritory*> Path;
  const bool bFound = Map->FindPath(T1, T3, Path);
  TestTrue(TEXT("Path found"), bFound);
  TestEqual(TEXT("Path length"), Path.Num(), 3);
  TestTrue(TEXT("Starts at source"), Path[0] == T1);
  TestTrue(TEXT("Ends at destination"), Path[2] == T3);

  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldMapFindPathBlockedTest, "Skald.WorldMap.FindPath.Blocked", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWorldMapFindPathBlockedTest::RunTest(const FString& Parameters) {
  UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
  TestNotNull(TEXT("World created"), World);
  if (!World) {
    return false;
  }

  AWorldMap* Map = World->SpawnActor<AWorldMap>();
  ASkaldPlayerState* PS1 = World->SpawnActor<ASkaldPlayerState>();
  ASkaldPlayerState* PS2 = World->SpawnActor<ASkaldPlayerState>();
  ATerritory* T1 = World->SpawnActor<ATerritory>();
  ATerritory* T2 = World->SpawnActor<ATerritory>();
  ATerritory* T3 = World->SpawnActor<ATerritory>();
  TestNotNull(TEXT("Map"), Map);
  TestNotNull(TEXT("PlayerState1"), PS1);
  TestNotNull(TEXT("PlayerState2"), PS2);
  TestNotNull(TEXT("Territory1"), T1);
  TestNotNull(TEXT("Territory2"), T2);
  TestNotNull(TEXT("Territory3"), T3);
  if (!Map || !PS1 || !PS2 || !T1 || !T2 || !T3) {
    return false;
  }

  T1->OwningPlayer = PS1;
  T2->OwningPlayer = PS2;
  T3->OwningPlayer = PS1;
  T1->AdjacentTerritories = {T2};
  T2->AdjacentTerritories = {T1, T3};
  T3->AdjacentTerritories = {T2};

  TArray<ATerritory*> Path;
  const bool bFound = Map->FindPath(T1, T3, Path);
  TestFalse(TEXT("Path blocked"), bFound);
  TestEqual(TEXT("No path"), Path.Num(), 0);

  return true;
}
