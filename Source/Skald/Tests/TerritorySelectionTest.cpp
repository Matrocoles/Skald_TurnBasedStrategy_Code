#if WITH_AUTOMATION_TESTS
#include "TerritorySelectionTest.h"

#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "Misc/AutomationTest.h"
#include "Territory.h"
#include "Components/PrimitiveComponent.h"
#include "Tests/AutomationEditorCommon.h"
#include "WorldMap.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerritorySelectionFlowTest,
                                 "Skald.World.TerritorySelection",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)

bool FTerritorySelectionFlowTest::RunTest(const FString &Parameters) {
  UWorld *World = FAutomationEditorCommonUtils::CreateNewMap();
  TestNotNull(TEXT("World created"), World);
  if (!World) {
    return false;
  }

  AWorldMap *Map = World->SpawnActor<AWorldMap>();
  ATerritory *Terr = World->SpawnActor<ATerritory>();
  ATerritorySelectionTestPC *PC =
      World->SpawnActor<ATerritorySelectionTestPC>();
  FString Error;
  ULocalPlayer *LocalPlayer =
      World->GetGameInstance()->CreateLocalPlayer(0, Error, false);
  TestNotNull(TEXT("LocalPlayer"), LocalPlayer);
  if (!LocalPlayer) {
    return false;
  }
  PC->SetPlayer(LocalPlayer);

  TestNotNull(TEXT("WorldMap"), Map);
  TestNotNull(TEXT("Territory"), Terr);
  TestNotNull(TEXT("PlayerController"), PC);
  if (!Map || !Terr || !PC) {
    World->GetGameInstance()->RemoveLocalPlayer(LocalPlayer);
    return false;
  }

  Terr->TerritoryID = 99;
  Map->RegisterTerritory(Terr);

  UPrimitiveComponent *Mesh = Terr->FindComponentByClass<UPrimitiveComponent>();
  TestNotNull(TEXT("Mesh component"), Mesh);
  if (!Mesh) {
    World->GetGameInstance()->RemoveLocalPlayer(LocalPlayer);
    return false;
  }

  Terr->HandleClicked(Mesh, EKeys::LeftMouseButton);

  TestTrue(TEXT("ServerSelectTerritory called"), PC->bServerSelectCalled);
  TestEqual(TEXT("SelectedTerritory updated"), Map->SelectedTerritory, Terr);

  World->GetGameInstance()->RemoveLocalPlayer(LocalPlayer);
  return true;
}
#endif // WITH_AUTOMATION_TESTS
