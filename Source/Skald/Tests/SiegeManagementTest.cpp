#include "Misc/AutomationTest.h"

#if defined(WITH_AUTOMATION_TESTS)
#if WITH_AUTOMATION_TESTS
#include "Skald_GameMode.h"
#include "Territory.h"
#include "Tests/SkaldAutomationTestHelpers.h"
#include "UObject/UnrealType.h"
#include "WorldMap.h"

namespace {
void AttachGameModeToWorld(UWorld *World, ASkaldGameMode *GameMode) {
  if (!World || !GameMode) {
    return;
  }

  if (FObjectProperty *Prop =
          FindFProperty<FObjectProperty>(UWorld::StaticClass(),
                                          TEXT("AuthorityGameMode"))) {
    Prop->SetObjectPropertyValue_InContainer(World, GameMode);
  }
}

void SetGameModeWorldMap(ASkaldGameMode *GameMode, AWorldMap *Map) {
  if (!GameMode) {
    return;
  }

  if (FObjectProperty *Prop = FindFProperty<FObjectProperty>(
          ASkaldGameMode::StaticClass(), TEXT("WorldMap"))) {
    Prop->SetObjectPropertyValue_InContainer(GameMode, Map);
  }
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkaldSiegeAssignmentTest,
                                 "Skald.Siege.RestoreFlow",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FSkaldSiegeAssignmentTest::RunTest(const FString &Parameters) {
  Skald::Tests::FScopedAutomationTestWorld TestWorld;
  UWorld *World = TestWorld.Get();
  TestNotNull(TEXT("World created"), World);
  if (!World) {
    return false;
  }

  ASkaldGameMode *GameMode = World->SpawnActor<ASkaldGameMode>();
  AWorldMap *Map = World->SpawnActor<AWorldMap>();
  ATerritory *Source = World->SpawnActor<ATerritory>();
  ATerritory *Target = World->SpawnActor<ATerritory>();
  TestNotNull(TEXT("GameMode"), GameMode);
  TestNotNull(TEXT("WorldMap"), Map);
  TestNotNull(TEXT("SourceTerritory"), Source);
  TestNotNull(TEXT("TargetTerritory"), Target);
  if (!GameMode || !Map || !Source || !Target) {
    return false;
  }

  Source->TerritoryID = 1;
  Target->TerritoryID = 2;
  Map->Territories = {Source, Target};
  Map->RegisterTerritory(Source);
  Map->RegisterTerritory(Target);

  AttachGameModeToWorld(World, GameMode);
  SetGameModeWorldMap(GameMode, Map);

  const int32 SiegeID =
      GameMode->BuildSiegeAtTerritory(Source->TerritoryID,
                                      ESiegeWeapon::BatteringRam);
  TestTrue(TEXT("Siege built"), SiegeID > 0);
  if (SiegeID <= 0) {
    return false;
  }

  TestEqual(TEXT("Territory registers siege"), Source->BuiltSiegeID, SiegeID);

  const int32 ConsumedID = GameMode->ConsumeSiege(Source->TerritoryID);
  TestEqual(TEXT("Consume returns same ID"), ConsumedID, SiegeID);
  TestEqual(TEXT("Source cleared after consume"), Source->BuiltSiegeID, 0);

  const bool bAssigned =
      GameMode->AssignExistingSiegeToTerritory(SiegeID, Target);
  TestTrue(TEXT("AssignExistingSiegeToTerritory succeeds"), bAssigned);
  TestEqual(TEXT("Target now holds siege"), Target->BuiltSiegeID, SiegeID);

  GameMode->RemoveSiege(SiegeID);
  TestEqual(TEXT("Removal clears target"), Target->BuiltSiegeID, 0);

  return true;
}
#endif  // WITH_AUTOMATION_TESTS
#endif  // defined(WITH_AUTOMATION_TESTS)
