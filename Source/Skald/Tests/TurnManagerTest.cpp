#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Skald_TurnManager.h"
#include "Skald_PlayerController.h"
#include "Skald_PlayerState.h"
#include "Engine\World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkaldTurnManagerPhaseTest, "Skald.TurnManager.PhaseTransitions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSkaldTurnManagerPhaseTest::RunTest(const FString& Parameters) {
  UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
  TestNotNull(TEXT("World created"), World);
  if (!World) {
    return false;
  }

  ATurnManager* TM = World->SpawnActor<ATurnManager>();
  ASkaldPlayerController* PC = World->SpawnActor<ASkaldPlayerController>();
  ASkaldPlayerState* PS = World->SpawnActor<ASkaldPlayerState>();
  TestNotNull(TEXT("TurnManager"), TM);
  TestNotNull(TEXT("PlayerController"), PC);
  TestNotNull(TEXT("PlayerState"), PS);
  if (!TM || !PC || !PS) {
    return false;
  }

  PC->PlayerState = PS;
  TM->RegisterController(PC);
  TM->StartTurns();

  TestEqual(TEXT("Initial phase"), TM->GetCurrentPhase(), ETurnPhase::Reinforcement);
  TM->AdvancePhase();
  TestEqual(TEXT("After reinforcement"), TM->GetCurrentPhase(), ETurnPhase::Attack);
  TM->AdvancePhase();
  TestEqual(TEXT("After attack"), TM->GetCurrentPhase(), ETurnPhase::Engineering);
  TM->AdvancePhase();
  TestEqual(TEXT("After engineering"), TM->GetCurrentPhase(), ETurnPhase::Treasure);
  TM->AdvancePhase();
  TestEqual(TEXT("After treasure"), TM->GetCurrentPhase(), ETurnPhase::Movement);
  TM->AdvancePhase();
  TestEqual(TEXT("After movement"), TM->GetCurrentPhase(), ETurnPhase::EndTurn);
  TM->AdvancePhase();
  TestEqual(TEXT("After end turn"), TM->GetCurrentPhase(), ETurnPhase::Revolt);
  TM->AdvancePhase();
  TestEqual(TEXT("Stays in revolt"), TM->GetCurrentPhase(), ETurnPhase::Revolt);

  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkaldTurnManagerInitiativeTest, "Skald.TurnManager.InitiativeSort", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSkaldTurnManagerInitiativeTest::RunTest(const FString& Parameters) {
  UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
  TestNotNull(TEXT("World created"), World);
  if (!World) {
    return false;
  }

  ATurnManager* TM = World->SpawnActor<ATurnManager>();
  ASkaldPlayerController* PC1 = World->SpawnActor<ASkaldPlayerController>();
  ASkaldPlayerController* PC2 = World->SpawnActor<ASkaldPlayerController>();
  ASkaldPlayerState* PS1 = World->SpawnActor<ASkaldPlayerState>();
  ASkaldPlayerState* PS2 = World->SpawnActor<ASkaldPlayerState>();
  TestNotNull(TEXT("TurnManager"), TM);
  TestNotNull(TEXT("PlayerController1"), PC1);
  TestNotNull(TEXT("PlayerController2"), PC2);
  TestNotNull(TEXT("PlayerState1"), PS1);
  TestNotNull(TEXT("PlayerState2"), PS2);
  if (!TM || !PC1 || !PC2 || !PS1 || !PS2) {
    return false;
  }

  PC1->PlayerState = PS1;
  PC2->PlayerState = PS2;
  PS1->InitiativeRoll = 2;
  PS2->InitiativeRoll = 5;

  TM->RegisterController(PC1);
  TM->RegisterController(PC2);
  TM->SortControllersByInitiative();

  const TArray<ASkaldPlayerController*> Controllers = TM->GetControllers();
  TestEqual(TEXT("Controller count"), Controllers.Num(), 2);
  TestTrue(TEXT("Highest initiative first"), Controllers[0] == PC2);
  TestTrue(TEXT("Lowest initiative second"), Controllers[1] == PC1);

  return true;
}
