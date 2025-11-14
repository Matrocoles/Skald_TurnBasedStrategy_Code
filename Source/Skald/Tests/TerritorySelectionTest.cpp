#include "TerritorySelectionTest.h"

#if defined(WITH_AUTOMATION_TESTS)
#if WITH_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "Territory.h"
#include "Components/PrimitiveComponent.h"
#include "Tests/SkaldAutomationTestHelpers.h"
#include "WorldMap.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerritorySelectionFlowTest,
                                 "Skald.World.TerritorySelection",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)

bool FTerritorySelectionFlowTest::RunTest(const FString &Parameters) {
  Skald::Tests::FScopedAutomationTestWorld TestWorld;
  UWorld *World = TestWorld.Get();
  TestNotNull(TEXT("World created"), World);
  if (!World) {
    return false;
  }

  AWorldMap *Map = World->SpawnActor<AWorldMap>();
  ATerritory *Terr = World->SpawnActor<ATerritory>();
  ATerritory *OtherTerr = World->SpawnActor<ATerritory>();
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
  TestNotNull(TEXT("Second Territory"), OtherTerr);
  TestNotNull(TEXT("PlayerController"), PC);
  if (!Map || !Terr || !OtherTerr || !PC) {
    World->GetGameInstance()->RemoveLocalPlayer(LocalPlayer);
    return false;
  }

  Terr->TerritoryID = 99;
  Map->RegisterTerritory(Terr);
  OtherTerr->TerritoryID = 123;
  Map->RegisterTerritory(OtherTerr);

  UPrimitiveComponent *Mesh = Terr->FindComponentByClass<UPrimitiveComponent>();
  TestNotNull(TEXT("Mesh component"), Mesh);
  if (!Mesh) {
    World->GetGameInstance()->RemoveLocalPlayer(LocalPlayer);
    return false;
  }

  Terr->HandleClicked(Mesh, EKeys::LeftMouseButton);

  TestTrue(TEXT("ServerSelectTerritory called"), PC->bServerSelectCalled);
  TestEqual(TEXT("SelectedTerritory updated"), Map->SelectedTerritory, Terr);

  const int32 RemotePlayerId = 4242;
  Map->SelectTerritory(OtherTerr, false, RemotePlayerId);
  TestEqual(TEXT("Remote selection cached"),
            Map->GetSelectionForPlayer(RemotePlayerId), OtherTerr);
  TestEqual(TEXT("Local selection unaffected by remote player"),
            Map->SelectedTerritory, Terr);

  Map->SelectTerritory(nullptr, false, RemotePlayerId);
  TestNull(TEXT("Remote deselection cleared cache"),
           Map->GetSelectionForPlayer(RemotePlayerId));

  World->GetGameInstance()->RemoveLocalPlayer(LocalPlayer);
  return true;
}
#endif  // WITH_AUTOMATION_TESTS
#endif  // defined(WITH_AUTOMATION_TESTS)
