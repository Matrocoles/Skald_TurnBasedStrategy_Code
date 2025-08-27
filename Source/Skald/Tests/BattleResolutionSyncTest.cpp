#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Skald_TurnManager.h"
#include "Skald_PlayerState.h"
#include "Territory.h"
#include "WorldMap.h"

// Helper object to bind to the turn manager's dynamic multicast delegate
// which cannot directly accept lambda functions.
UCLASS()
class UWorldStateChangedListener : public UObject
{
  GENERATED_BODY()

public:
  bool bBroadcasted = false;

  UFUNCTION()
  void HandleBroadcast()
  {
    bBroadcasted = true;
  }
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkaldBattleResolutionSyncTest, "Skald.Multiplayer.BattleResolutionSync", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSkaldBattleResolutionSyncTest::RunTest(const FString& Parameters) {
  UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
  TestNotNull(TEXT("World created"), World);
  if (!World) {
    return false;
  }

  ATurnManager* TM = World->SpawnActor<ATurnManager>();
  AWorldMap* WM = World->SpawnActor<AWorldMap>();
  ATerritory* Source = World->SpawnActor<ATerritory>();
  ATerritory* Target = World->SpawnActor<ATerritory>();
  ASkaldPlayerState* PS1 = World->SpawnActor<ASkaldPlayerState>();
  ASkaldPlayerState* PS2 = World->SpawnActor<ASkaldPlayerState>();

  TestNotNull(TEXT("TurnManager"), TM);
  TestNotNull(TEXT("WorldMap"), WM);
  TestNotNull(TEXT("Source"), Source);
  TestNotNull(TEXT("Target"), Target);
  TestNotNull(TEXT("Player1"), PS1);
  TestNotNull(TEXT("Player2"), PS2);
  if (!TM || !WM || !Source || !Target || !PS1 || !PS2) {
    return false;
  }

  PS1->SetPlayerId(1);
  PS2->SetPlayerId(2);

  Source->TerritoryID = 1;
  Source->ArmyStrength = 10;
  Source->OwningPlayer = PS1;

  Target->TerritoryID = 2;
  Target->ArmyStrength = 5;
  Target->OwningPlayer = PS2;

  WM->Territories.Add(Source);
  WM->Territories.Add(Target);

  // Use an object instance to capture the broadcast because AddLambda is not
  // supported on dynamic multicast delegates.
  UWorldStateChangedListener* Listener = NewObject<UWorldStateChangedListener>();
  TM->OnWorldStateChanged.AddDynamic(Listener, &UWorldStateChangedListener::HandleBroadcast);

  TM->ClientBattleResolved(1, 3, 5, 1, 2, 1, 5, 2);

  TestTrue(TEXT("Broadcast fired"), Listener->bBroadcasted);
  TestEqual(TEXT("Source army"), Source->ArmyStrength, 5);
  TestEqual(TEXT("Target army"), Target->ArmyStrength, 2);
  TestTrue(TEXT("Target owner"), Target->OwningPlayer == PS1);

  return true;
}
