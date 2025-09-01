
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Skald_TurnManager.h"
#include "Skald_PlayerState.h"
#include "Territory.h"
#include "WorldMap.h"
#include "DeployUnitsReplicationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkaldArmyPoolCalculationTest, "Skald.TurnManager.ArmyPoolCalculation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSkaldArmyPoolCalculationTest::RunTest(const FString& Parameters) {
  UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
  TestNotNull(TEXT("World created"), World);
  if (!World) {
    return false;
  }

  ATurnManager* TM = World->SpawnActor<ATurnManager>();
  ADeployTestPlayerController* PC = World->SpawnActor<ADeployTestPlayerController>();
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
  TM->RegisterController(PC);
  UDeployTestHUDWidget* HUD = NewObject<UDeployTestHUDWidget>(PC);
  PC->SetHUD(HUD);

  // start with 5 owned territories
  TArray<ATerritory*> Terrs;
  for (int32 i = 0; i < 5; ++i) {
    ATerritory* T = World->SpawnActor<ATerritory>();
    TestNotNull(TEXT("Territory"), T);
    if (!T) {
      return false;
    }
    T->OwningPlayer = PS;
    Terrs.Add(T);
  }
  Map->Territories = Terrs;

  TM->StartTurns();
  TestEqual(TEXT("Army pool after start"), PS->ArmyPool, 2);
  TestEqual(TEXT("HUD after start"), HUD->LastUnits, 2);

  // add two more territories and advance turn to recalc
  for (int32 i = 0; i < 2; ++i) {
    ATerritory* T = World->SpawnActor<ATerritory>();
    TestNotNull(TEXT("ExtraTerritory"), T);
    if (!T) {
      return false;
    }
    T->OwningPlayer = PS;
    Terrs.Add(T);
  }
  Map->Territories = Terrs;

  TM->AdvanceTurn();
  TestEqual(TEXT("Army pool after advance"), PS->ArmyPool, 3);
  TestEqual(TEXT("HUD after advance"), HUD->LastUnits, 3);

  return true;
}
