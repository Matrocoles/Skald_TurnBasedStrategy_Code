#include "Components/TextBlock.h"
#include "Misc/AutomationTest.h"
#include "Skald_PlayerController.h"
#include "Skald_PlayerState.h"
#include "Skald_TurnManager.h"
#include "Territory.h"
#include "Tests/AutomationEditorCommon.h"
#include "UI/SkaldMainHUDWidget.h"
#include "UObject/UnrealType.h"
#include "WorldMap.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkaldFullTurnFlowTest, "Skald.Turn.FullFlow",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)

bool FSkaldFullTurnFlowTest::RunTest(const FString &Parameters) {
  UWorld *World = FAutomationEditorCommonUtils::CreateNewMap();
  TestNotNull(TEXT("World created"), World);
  if (!World) {
    return false;
  }

  ATurnManager *TM = World->SpawnActor<ATurnManager>();
  ASkaldPlayerController *PC = World->SpawnActor<ASkaldPlayerController>();
  ASkaldPlayerState *PS = World->SpawnActor<ASkaldPlayerState>();
  AWorldMap *Map = World->SpawnActor<AWorldMap>();
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

  USkaldMainHUDWidget *HUD = NewObject<USkaldMainHUDWidget>(PC);
  HUD->SetOwningPlayer(PC);
  HUD->EndingTurnText = NewObject<UTextBlock>(HUD);
  HUD->InitiativeText = NewObject<UTextBlock>(HUD);
  HUD->DeployableUnitsText = NewObject<UTextBlock>(HUD);
  HUD->ResourcesText = NewObject<UTextBlock>(HUD);

  FObjectProperty *HUDProp = FindFProperty<FObjectProperty>(
      ASkaldPlayerController::StaticClass(), TEXT("MainHudWidget"));
  FObjectProperty *HUDRefProp = FindFProperty<FObjectProperty>(
      ASkaldPlayerController::StaticClass(), TEXT("HUDRef"));
  HUDProp->SetObjectPropertyValue_InContainer(PC, HUD);
  HUDRefProp->SetObjectPropertyValue_InContainer(PC, HUD);

  HUD->OnEngineeringRequested.AddDynamic(
      PC, &ASkaldPlayerController::HandleEngineeringRequested);
  HUD->OnDigTreasureRequested.AddDynamic(
      PC, &ASkaldPlayerController::HandleDigTreasureRequested);
  HUD->OnMoveRequested.AddDynamic(PC,
                                  &ASkaldPlayerController::HandleMoveRequested);

  ATerritory *T1 = World->SpawnActor<ATerritory>();
  ATerritory *T2 = World->SpawnActor<ATerritory>();
  TestNotNull(TEXT("Territory1"), T1);
  TestNotNull(TEXT("Territory2"), T2);
  if (!T1 || !T2) {
    return false;
  }

  T1->TerritoryID = 1;
  T1->bIsCapital = true;
  T1->OwningPlayer = PS;
  T1->bHasTreasure = true;
  T1->ArmyStrength = 5;
  T2->TerritoryID = 2;
  T2->OwningPlayer = PS;
  T2->ArmyStrength = 0;
  T1->AdjacentTerritories = {T2};
  T2->AdjacentTerritories = {T1};
  Map->Territories = {T1, T2};

  PS->Resources = 20;

  TM->StartTurns();
  TestEqual(TEXT("Start in reinforcement"), TM->GetCurrentPhase(),
            ETurnPhase::Reinforcement);

  PC->EndPhase(); // to attack
  PC->EndPhase(); // to engineering
  TestEqual(TEXT("Engineering phase"), TM->GetCurrentPhase(),
            ETurnPhase::Engineering);
  HUD->OnTerritoryClickedUI(T1);
  TestEqual(TEXT("Resources after engineering"), PS->Resources, 10);

  PC->EndPhase(); // to treasure
  TestEqual(TEXT("Treasure phase"), TM->GetCurrentPhase(),
            ETurnPhase::Treasure);
  HUD->OnTerritoryClickedUI(T1);
  TestEqual(TEXT("Resources after treasure"), PS->Resources, 15);
  TestFalse(TEXT("Treasure cleared"), T1->bHasTreasure);

  PC->EndPhase(); // to movement
  TestEqual(TEXT("Movement phase"), TM->GetCurrentPhase(),
            ETurnPhase::Movement);
  HUD->OnTerritoryClickedUI(T1);
  HUD->OnTerritoryClickedUI(T2);
  HUD->SubmitMove(T1->TerritoryID, T2->TerritoryID, 2);
  TestEqual(TEXT("Source after move"), T1->ArmyStrength, 3);
  TestEqual(TEXT("Target after move"), T2->ArmyStrength, 2);

  PC->EndPhase(); // to end turn
  TestEqual(TEXT("EndTurn phase"), TM->GetCurrentPhase(), ETurnPhase::EndTurn);
  TestEqual(TEXT("EndingTurn visible"), HUD->EndingTurnText->GetVisibility(),
            ESlateVisibility::Visible);

  PC->EndPhase(); // to revolt
  TestEqual(TEXT("Revolt phase"), TM->GetCurrentPhase(), ETurnPhase::Revolt);
  TestEqual(TEXT("EndingTurn hidden"), HUD->EndingTurnText->GetVisibility(),
            ESlateVisibility::Collapsed);

  PC->EndTurn();
  TestEqual(TEXT("Cycle back to reinforcement"), TM->GetCurrentPhase(),
            ETurnPhase::Reinforcement);

  return true;
}
