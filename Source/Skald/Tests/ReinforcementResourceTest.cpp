#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Skald_TurnManager.h"
#include "Skald_PlayerController.h"
#include "Skald_PlayerState.h"
#include "Territory.h"
#include "WorldMap.h"
#include "Engine\World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkaldReinforcementResourceTest, "Skald.TurnManager.ResourceAccumulation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSkaldReinforcementResourceTest::RunTest(const FString& Parameters) {
  UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
  TestNotNull(TEXT("World created"), World);
  if (!World) {
    return false;
  }

  ATurnManager* TM = World->SpawnActor<ATurnManager>();
  ASkaldPlayerController* PC = World->SpawnActor<ASkaldPlayerController>();
  ASkaldPlayerState* PS = World->SpawnActor<ASkaldPlayerState>();
  AWorldMap* Map = World->SpawnActor<AWorldMap>();
  TestNotNull(TEXT("TurnManager"), TM);
  TestNotNull(TEXT("PlayerController"), PC);
  TestNotNull(TEXT("PlayerState"), PS);
  TestNotNull(TEXT("WorldMap"), Map);
  if (!TM || !PC || !PS || !Map) {
    return false;
  }

  PC->PlayerState = PS;
  PC->SetTurnManager(TM);
  TM->RegisterController(PC);

  ATerritory* T1 = World->SpawnActor<ATerritory>();
  ATerritory* T2 = World->SpawnActor<ATerritory>();
  TestNotNull(TEXT("Territory1"), T1);
  TestNotNull(TEXT("Territory2"), T2);
  if (!T1 || !T2) {
    return false;
  }

  T1->OwningPlayer = PS;
  T1->Resources = 3;
  T2->OwningPlayer = PS;
  T2->Resources = 7;
  Map->Territories = {T1, T2};

  PS->Resources = 0;
  TM->StartTurns();
  TestEqual(TEXT("Resources after first reinforcement"), PS->Resources, 10);

  TM->AdvanceTurn();
  TestEqual(TEXT("Resources after second reinforcement"), PS->Resources, 20);

  return true;
}
