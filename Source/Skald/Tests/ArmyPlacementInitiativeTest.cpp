#include "Misc/AutomationTest.h"

#if defined(WITH_AUTOMATION_TESTS)
#if WITH_AUTOMATION_TESTS
#include "Skald_AIController.h"
#include "Skald_GameMode.h"
#include "Skald_GameState.h"
#include "Skald_PlayerController.h"
#include "Skald_PlayerState.h"
#include "Skald_TurnManager.h"
#include "Territory.h"
#include "Tests/SkaldAutomationTestHelpers.h"
#include "Tests/SkaldGameModeAutomationAccessor.h"
#include "UObject/UnrealType.h"
#include "WorldMap.h"
#include "Math/UnrealMathUtility.h"

#include "Engine/World.h"

namespace {
void SetObjectProperty(UObject *Target, const TCHAR *PropertyName, UObject *Value) {
  if (!Target) {
    return;
  }
  if (FObjectProperty *Prop = FindFProperty<FObjectProperty>(Target->GetClass(), PropertyName)) {
    Prop->SetObjectPropertyValue_InContainer(Target, Value);
  }
}

int32 GetCurrentTurnManagerIndex(ATurnManager *TurnManager) {
  if (!TurnManager) {
    return INDEX_NONE;
  }
  if (FIntProperty *Prop = FindFProperty<FIntProperty>(ATurnManager::StaticClass(), TEXT("CurrentIndex"))) {
    return Prop->GetPropertyValue_InContainer(TurnManager);
  }
  return INDEX_NONE;
}

void ConfigureController(ASkaldPlayerController *Controller, ASkaldPlayerState *PlayerState,
                         int32 PlayerId, const FString &DisplayName, int32 Initiative) {
  if (!Controller || !PlayerState) {
    return;
  }
  Controller->PlayerState = PlayerState;
  PlayerState->SetOwner(Controller);
  PlayerState->SetPlayerId(PlayerId);
  PlayerState->PlayerDisplayName = DisplayName;
  PlayerState->InitiativeRoll = Initiative;
}

} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArmyPlacementInitiativeOrderTest,
                                 "Skald.Turn.ArmyPlacement.InitiativeOrder",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)

bool FArmyPlacementInitiativeOrderTest::RunTest(const FString &Parameters) {
  Skald::Tests::FScopedAutomationTestWorld TestWorld;
  UWorld *World = TestWorld.Get();
  TestNotNull(TEXT("World created"), World);
  if (!World) {
    return false;
  }

  ASkaldGameMode *GameMode = World->SpawnActor<ASkaldGameMode>();
  ATurnManager *TurnManager = World->SpawnActor<ATurnManager>();
  AWorldMap *Map = World->SpawnActor<AWorldMap>();
  ATerritory *TerritoryA = World->SpawnActor<ATerritory>();
  ATerritory *TerritoryB = World->SpawnActor<ATerritory>();
  ASkaldPlayerController *ControllerA = World->SpawnActor<ASkaldPlayerController>();
  ASkaldPlayerController *ControllerB = World->SpawnActor<ASkaldPlayerController>();
  ASkaldPlayerState *StateA = World->SpawnActor<ASkaldPlayerState>();
  ASkaldPlayerState *StateB = World->SpawnActor<ASkaldPlayerState>();

  TestNotNull(TEXT("GameMode"), GameMode);
  TestNotNull(TEXT("TurnManager"), TurnManager);
  TestNotNull(TEXT("WorldMap"), Map);
  TestNotNull(TEXT("TerritoryA"), TerritoryA);
  TestNotNull(TEXT("TerritoryB"), TerritoryB);
  TestNotNull(TEXT("ControllerA"), ControllerA);
  TestNotNull(TEXT("ControllerB"), ControllerB);
  TestNotNull(TEXT("StateA"), StateA);
  TestNotNull(TEXT("StateB"), StateB);
  if (!GameMode || !TurnManager || !Map || !TerritoryA || !TerritoryB || !ControllerA ||
      !ControllerB || !StateA || !StateB) {
    return false;
  }

  Skald::Tests::AttachGameModeToWorld(World, GameMode);
  GameMode->InitGameState();
  ASkaldGameState *GameState = GameMode->GetGameState<ASkaldGameState>();
  TestNotNull(TEXT("GameState initialised"), GameState);
  if (!GameState) {
    return false;
  }

  ConfigureController(ControllerA, StateA, 1, TEXT("PlayerA"), 6);
  ConfigureController(ControllerB, StateB, 2, TEXT("PlayerB"), 3);
  GameState->AddPlayerState(StateA);
  GameState->AddPlayerState(StateB);

  TerritoryA->TerritoryID = 1;
  TerritoryA->OwningPlayer = StateA;
  TerritoryA->ArmyUnits = 1;
  TerritoryA->bIsCapital = true;
  TerritoryB->TerritoryID = 2;
  TerritoryB->OwningPlayer = StateB;
  TerritoryB->ArmyUnits = 1;
  TerritoryB->bIsCapital = true;
  Map->Territories = {TerritoryA, TerritoryB};

  SetObjectProperty(GameMode, TEXT("TurnManager"), TurnManager);
  SetObjectProperty(GameMode, TEXT("WorldMap"), Map);
  SetObjectProperty(TurnManager, TEXT("CachedWorldMap"), Map);
  SetObjectProperty(ControllerA, TEXT("CachedGameMode"), GameMode);
  SetObjectProperty(ControllerB, TEXT("CachedGameMode"), GameMode);

  TurnManager->RegisterController(ControllerA);
  TurnManager->RegisterController(ControllerB);

  FSkaldGameModeAutomationAccessor::BeginArmyPlacementPhase(GameMode);

  const int32 IndexA = GameState->PlayerArray.IndexOfByKey(StateA);
  const int32 IndexB = GameState->PlayerArray.IndexOfByKey(StateB);
  TestTrue(TEXT("PlayerA index valid"), IndexA != INDEX_NONE);
  TestTrue(TEXT("PlayerB index valid"), IndexB != INDEX_NONE);
  TestEqual(TEXT("PlayerA deployable units"), StateA->DeployableUnits, 40);
  TestEqual(TEXT("PlayerB deployable units"), StateB->DeployableUnits, 40);
  TestEqual(TEXT("Army placement begins with initiative winner"), GameState->CurrentTurnIndex,
            IndexA);

  ControllerA->ServerDeployUnits(TerritoryA->TerritoryID, 10);
  TestEqual(TEXT("PlayerA limited deployment"), StateA->DeployableUnits, 30);
  TestEqual(TEXT("PlayerA territory reinforced"), TerritoryA->ArmyUnits, 11);

  ControllerA->ServerDeployUnits(TerritoryA->TerritoryID, 5);
  TestEqual(TEXT("PlayerA cannot exceed placement cap"), StateA->DeployableUnits, 30);

  ControllerA->EndPhase();
  TestEqual(TEXT("PlayerA leftover units automatically placed"),
            StateA->DeployableUnits, 0);
  TestEqual(TEXT("PlayerA territory received remaining units"), TerritoryA->ArmyUnits, 41);
  TestEqual(TEXT("Second player receives placement turn"), GameState->CurrentTurnIndex, IndexB);

  ControllerB->ServerDeployUnits(TerritoryB->TerritoryID, 10);
  TestEqual(TEXT("PlayerB limited deployment"), StateB->DeployableUnits, 30);
  TestEqual(TEXT("PlayerB territory reinforced"), TerritoryB->ArmyUnits, 11);

  ControllerB->ServerDeployUnits(TerritoryB->TerritoryID, 5);
  TestEqual(TEXT("PlayerB cannot exceed placement cap"), StateB->DeployableUnits, 30);
  ControllerB->EndPhase();
  TestEqual(TEXT("PlayerB leftover units automatically placed"),
            StateB->DeployableUnits, 0);
  TestEqual(TEXT("PlayerB territory received remaining units"), TerritoryB->ArmyUnits, 41);

  TestEqual(TEXT("Turn advanced to reinforcement"), TurnManager->GetCurrentPhase(),
            ETurnPhase::Reinforcement);

  const int32 CurrentIndex = GetCurrentTurnManagerIndex(TurnManager);
  const TArray<ASkaldPlayerController *> Controllers = TurnManager->GetControllers();
  const int32 ExpectedIndex = Controllers.IndexOfByKey(ControllerA);
  TestEqual(TEXT("Initiative winner starts main turn"), CurrentIndex, ExpectedIndex);
  TestEqual(TEXT("GameState turn index reset to initiative winner"), GameState->CurrentTurnIndex,
            IndexA);

  StateA->DeployableUnits = 5;
  TurnManager->BroadcastDeployableUnits(StateA);
  ControllerA->ServerDeployUnits(TerritoryA->TerritoryID, 5);
  TestEqual(TEXT("Placement cap lifted after army phase"), StateA->DeployableUnits, 0);
  TestEqual(TEXT("Territory can exceed ten units during reinforcement"),
            TerritoryA->ArmyUnits, 46);

  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAIArmyPlacementAutoAdvanceTest,
                                 "Skald.Turn.ArmyPlacement.AIAutoAdvance",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)

bool FAIArmyPlacementAutoAdvanceTest::RunTest(const FString &Parameters) {
  Skald::Tests::FScopedAutomationTestWorld TestWorld;
  UWorld *World = TestWorld.Get();
  TestNotNull(TEXT("World created"), World);
  if (!World) {
    return false;
  }

  ASkaldGameMode *GameMode = World->SpawnActor<ASkaldGameMode>();
  ATurnManager *TurnManager = World->SpawnActor<ATurnManager>();
  AWorldMap *Map = World->SpawnActor<AWorldMap>();
  ATerritory *TerritoryAI = World->SpawnActor<ATerritory>();
  ATerritory *TerritoryHuman = World->SpawnActor<ATerritory>();
  ASkaldAIController *AIController = World->SpawnActor<ASkaldAIController>();
  ASkaldPlayerController *HumanController = World->SpawnActor<ASkaldPlayerController>();
  ASkaldPlayerState *AIState = World->SpawnActor<ASkaldPlayerState>();
  ASkaldPlayerState *HumanState = World->SpawnActor<ASkaldPlayerState>();

  TestNotNull(TEXT("GameMode"), GameMode);
  TestNotNull(TEXT("TurnManager"), TurnManager);
  TestNotNull(TEXT("WorldMap"), Map);
  TestNotNull(TEXT("AI Territory"), TerritoryAI);
  TestNotNull(TEXT("Human Territory"), TerritoryHuman);
  TestNotNull(TEXT("AI Controller"), AIController);
  TestNotNull(TEXT("Human Controller"), HumanController);
  TestNotNull(TEXT("AI State"), AIState);
  TestNotNull(TEXT("Human State"), HumanState);
  if (!GameMode || !TurnManager || !Map || !TerritoryAI || !TerritoryHuman || !AIController ||
      !HumanController || !AIState || !HumanState) {
    return false;
  }

  Skald::Tests::AttachGameModeToWorld(World, GameMode);
  GameMode->InitGameState();
  ASkaldGameState *GameState = GameMode->GetGameState<ASkaldGameState>();
  TestNotNull(TEXT("GameState initialised"), GameState);
  if (!GameState) {
    return false;
  }

  ConfigureController(AIController, AIState, 1, TEXT("AI"), 6);
  ConfigureController(HumanController, HumanState, 2, TEXT("Human"), 3);
  AIState->bIsAI = true;
  GameState->AddPlayerState(AIState);
  GameState->AddPlayerState(HumanState);

  TerritoryAI->TerritoryID = 1;
  TerritoryAI->OwningPlayer = AIState;
  TerritoryAI->ArmyUnits = 1;
  TerritoryAI->bIsCapital = true;
  TerritoryHuman->TerritoryID = 2;
  TerritoryHuman->OwningPlayer = HumanState;
  TerritoryHuman->ArmyUnits = 1;
  TerritoryHuman->bIsCapital = true;
  Map->Territories = {TerritoryAI, TerritoryHuman};

  SetObjectProperty(GameMode, TEXT("TurnManager"), TurnManager);
  SetObjectProperty(GameMode, TEXT("WorldMap"), Map);
  SetObjectProperty(TurnManager, TEXT("CachedWorldMap"), Map);
  SetObjectProperty(AIController, TEXT("CachedGameMode"), GameMode);
  SetObjectProperty(HumanController, TEXT("CachedGameMode"), GameMode);

  TurnManager->RegisterController(AIController);
  TurnManager->RegisterController(HumanController);

  FSkaldGameModeAutomationAccessor::BeginArmyPlacementPhase(GameMode);

  const int32 HumanIndex = GameState->PlayerArray.IndexOfByKey(HumanState);
  TestTrue(TEXT("Human player index valid"), HumanIndex != INDEX_NONE);
  TestEqual(TEXT("AI leftover units automatically placed"), AIState->DeployableUnits, 0);
  TestEqual(TEXT("AI territory received remaining units"), TerritoryAI->ArmyUnits, 41);
  TestEqual(TEXT("Human player's placement turn"), GameState->CurrentTurnIndex, HumanIndex);

  HumanController->ServerDeployUnits(TerritoryHuman->TerritoryID, 10);
  TestEqual(TEXT("Human limited deployment"), HumanState->DeployableUnits, 30);
  TestEqual(TEXT("Human territory reinforced"), TerritoryHuman->ArmyUnits, 11);

  HumanController->ServerDeployUnits(TerritoryHuman->TerritoryID, 5);
  TestEqual(TEXT("Human cannot exceed placement cap"), HumanState->DeployableUnits, 30);
  HumanController->EndPhase();

  TestEqual(TEXT("Human leftover units automatically placed"), HumanState->DeployableUnits, 0);
  TestEqual(TEXT("Human territory received remaining units"), TerritoryHuman->ArmyUnits, 41);

  TestEqual(TEXT("Main turn advanced to reinforcement"), TurnManager->GetCurrentPhase(),
            ETurnPhase::Reinforcement);
  const int32 CurrentIndex = GetCurrentTurnManagerIndex(TurnManager);
  const TArray<ASkaldPlayerController *> Controllers = TurnManager->GetControllers();
  const int32 ExpectedIndex = Controllers.IndexOfByKey(AIController);
  TestEqual(TEXT("AI with initiative begins normal turn"), CurrentIndex, ExpectedIndex);

  const int32 AIIndex = GameState->PlayerArray.IndexOfByKey(AIState);
  TestEqual(TEXT("GameState turn index returned to AI"), GameState->CurrentTurnIndex, AIIndex);

  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAIArmyPlacementFailsafeRespectsHumanTest,
                                 "Skald.Turn.ArmyPlacement.FailsafeRespectsHuman",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)

bool FAIArmyPlacementFailsafeRespectsHumanTest::RunTest(
    const FString &Parameters) {
  Skald::Tests::FScopedAutomationTestWorld TestWorld;
  UWorld *World = TestWorld.Get();
  TestNotNull(TEXT("World created"), World);
  if (!World) {
    return false;
  }

  ASkaldGameMode *GameMode = World->SpawnActor<ASkaldGameMode>();
  ATurnManager *TurnManager = World->SpawnActor<ATurnManager>();
  AWorldMap *Map = World->SpawnActor<AWorldMap>();
  ATerritory *TerritoryAI = World->SpawnActor<ATerritory>();
  ATerritory *TerritoryHuman = World->SpawnActor<ATerritory>();
  ASkaldAIController *AIController = World->SpawnActor<ASkaldAIController>();
  ASkaldPlayerController *HumanController =
      World->SpawnActor<ASkaldPlayerController>();
  ASkaldPlayerState *AIState = World->SpawnActor<ASkaldPlayerState>();
  ASkaldPlayerState *HumanState = World->SpawnActor<ASkaldPlayerState>();

  TestNotNull(TEXT("GameMode"), GameMode);
  TestNotNull(TEXT("TurnManager"), TurnManager);
  TestNotNull(TEXT("WorldMap"), Map);
  TestNotNull(TEXT("AI Territory"), TerritoryAI);
  TestNotNull(TEXT("Human Territory"), TerritoryHuman);
  TestNotNull(TEXT("AI Controller"), AIController);
  TestNotNull(TEXT("Human Controller"), HumanController);
  TestNotNull(TEXT("AI State"), AIState);
  TestNotNull(TEXT("Human State"), HumanState);
  if (!GameMode || !TurnManager || !Map || !TerritoryAI || !TerritoryHuman ||
      !AIController || !HumanController || !AIState || !HumanState) {
    return false;
  }

  Skald::Tests::AttachGameModeToWorld(World, GameMode);
  GameMode->InitGameState();
  ASkaldGameState *GameState = GameMode->GetGameState<ASkaldGameState>();
  TestNotNull(TEXT("GameState initialised"), GameState);
  if (!GameState) {
    return false;
  }

  ConfigureController(AIController, AIState, 1, TEXT("AI"), 6);
  ConfigureController(HumanController, HumanState, 2, TEXT("Human"), 3);
  AIState->bIsAI = true;
  GameState->AddPlayerState(AIState);
  GameState->AddPlayerState(HumanState);

  TerritoryAI->TerritoryID = 1;
  TerritoryAI->OwningPlayer = AIState;
  TerritoryAI->ArmyUnits = 1;
  TerritoryAI->bIsCapital = true;
  TerritoryHuman->TerritoryID = 2;
  TerritoryHuman->OwningPlayer = HumanState;
  TerritoryHuman->ArmyUnits = 1;
  TerritoryHuman->bIsCapital = true;
  Map->Territories = {TerritoryAI, TerritoryHuman};

  SetObjectProperty(GameMode, TEXT("TurnManager"), TurnManager);
  SetObjectProperty(GameMode, TEXT("WorldMap"), Map);
  SetObjectProperty(TurnManager, TEXT("CachedWorldMap"), Map);
  SetObjectProperty(AIController, TEXT("CachedGameMode"), GameMode);
  SetObjectProperty(HumanController, TEXT("CachedGameMode"), GameMode);

  TurnManager->RegisterController(AIController);
  TurnManager->RegisterController(HumanController);

  FSkaldGameModeAutomationAccessor::BeginArmyPlacementPhase(GameMode);

  const int32 HumanIndex = GameState->PlayerArray.IndexOfByKey(HumanState);
  TestTrue(TEXT("Human player index valid"), HumanIndex != INDEX_NONE);
  TestEqual(TEXT("Human receives placement turn"), GameState->CurrentTurnIndex,
            HumanIndex);
  TestEqual(TEXT("Army placement phase active"), TurnManager->GetCurrentPhase(),
            ETurnPhase::ArmyPlacement);

  FSkaldGameModeAutomationAccessor::HandleArmyPlacementFailsafe(GameMode);

  TestEqual(TEXT("Failsafe does not skip human"), GameState->CurrentTurnIndex,
            HumanIndex);
  TestEqual(TEXT("Phase unchanged after failsafe"),
            TurnManager->GetCurrentPhase(), ETurnPhase::ArmyPlacement);
  TestEqual(TEXT("Human still has units to place"), HumanState->DeployableUnits,
            40);

  HumanController->ServerDeployUnits(TerritoryHuman->TerritoryID, 10);
  TestEqual(TEXT("Human limited deployment"), HumanState->DeployableUnits, 30);
  TestEqual(TEXT("Human territory reinforced after failsafe"), TerritoryHuman->ArmyUnits, 11);

  HumanController->ServerDeployUnits(TerritoryHuman->TerritoryID, 5);
  TestEqual(TEXT("Human cannot exceed placement cap after failsafe"),
            HumanState->DeployableUnits, 30);

  HumanController->EndPhase();

  TestEqual(TEXT("Human leftover units automatically placed after failsafe"),
            HumanState->DeployableUnits, 0);
  TestEqual(TEXT("Human territory received remaining units after failsafe"),
            TerritoryHuman->ArmyUnits, 41);

  TestEqual(TEXT("Turns advance after human finishes"),
            TurnManager->GetCurrentPhase(), ETurnPhase::Reinforcement);

  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInitializeWorldSingleInitiativeRollTest,
                                 "Skald.Turn.ArmyPlacement.SingleInitiativeRoll",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)

bool FInitializeWorldSingleInitiativeRollTest::RunTest(const FString &Parameters) {
  Skald::Tests::FScopedAutomationTestWorld TestWorld;
  UWorld *World = TestWorld.Get();
  TestNotNull(TEXT("World created"), World);
  if (!World) {
    return false;
  }

  ASkaldGameMode *GameMode = World->SpawnActor<ASkaldGameMode>();
  AWorldMap *Map = World->SpawnActor<AWorldMap>();
  ATurnManager *TurnManager = World->SpawnActor<ATurnManager>();
  ATerritory *TerritoryA = World->SpawnActor<ATerritory>();
  ATerritory *TerritoryB = World->SpawnActor<ATerritory>();
  ASkaldPlayerController *ControllerA = World->SpawnActor<ASkaldPlayerController>();
  ASkaldPlayerController *ControllerB = World->SpawnActor<ASkaldPlayerController>();
  ASkaldPlayerState *StateA = World->SpawnActor<ASkaldPlayerState>();
  ASkaldPlayerState *StateB = World->SpawnActor<ASkaldPlayerState>();

  TestNotNull(TEXT("GameMode"), GameMode);
  TestNotNull(TEXT("WorldMap"), Map);
  TestNotNull(TEXT("TurnManager"), TurnManager);
  TestNotNull(TEXT("TerritoryA"), TerritoryA);
  TestNotNull(TEXT("TerritoryB"), TerritoryB);
  TestNotNull(TEXT("ControllerA"), ControllerA);
  TestNotNull(TEXT("ControllerB"), ControllerB);
  TestNotNull(TEXT("StateA"), StateA);
  TestNotNull(TEXT("StateB"), StateB);
  if (!GameMode || !Map || !TurnManager || !TerritoryA || !TerritoryB ||
      !ControllerA || !ControllerB || !StateA || !StateB) {
    return false;
  }

  Skald::Tests::AttachGameModeToWorld(World, GameMode);
  GameMode->InitGameState();
  ASkaldGameState *GameState = GameMode->GetGameState<ASkaldGameState>();
  TestNotNull(TEXT("GameState initialised"), GameState);
  if (!GameState) {
    return false;
  }

  ConfigureController(ControllerA, StateA, 1, TEXT("PlayerA"), 0);
  ConfigureController(ControllerB, StateB, 2, TEXT("PlayerB"), 0);
  GameState->AddPlayerState(StateA);
  GameState->AddPlayerState(StateB);

  TerritoryA->TerritoryID = 1;
  TerritoryA->bIsCapital = true;
  TerritoryB->TerritoryID = 2;
  TerritoryB->bIsCapital = true;
  Map->Territories = {TerritoryA, TerritoryB};

  SetObjectProperty(GameMode, TEXT("TurnManager"), TurnManager);
  SetObjectProperty(GameMode, TEXT("WorldMap"), Map);
  SetObjectProperty(TurnManager, TEXT("CachedWorldMap"), Map);

  const bool bFirstInit = GameMode->InitializeWorld();
  TestTrue(TEXT("Initial world initialisation succeeded"), bFirstInit);
  if (!bFirstInit) {
    return false;
  }

  TMap<ASkaldPlayerState *, int32> InitialRolls;
  for (APlayerState *PSBase : GameState->PlayerArray) {
    if (ASkaldPlayerState *PS = Cast<ASkaldPlayerState>(PSBase)) {
      TestTrue(TEXT("Initial initiative roll assigned"), PS->InitiativeRoll > 0);
      InitialRolls.Add(PS, PS->InitiativeRoll);
    }
  }
  TestTrue(TEXT("Captured initiative rolls for all players"),
           InitialRolls.Num() == GameState->PlayerArray.Num());
  if (InitialRolls.Num() != GameState->PlayerArray.Num()) {
    return false;
  }

  const bool bSecondInit = GameMode->InitializeWorld();
  TestTrue(TEXT("Second world initialisation succeeded"), bSecondInit);
  if (!bSecondInit) {
    return false;
  }

  for (const auto &Pair : InitialRolls) {
    ASkaldPlayerState *PS = Pair.Key;
    const int32 ExpectedRoll = Pair.Value;
    TestEqual(TEXT("Initiative roll preserved"), PS->InitiativeRoll,
              ExpectedRoll);
  }

  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkaldGameModeRetryInitTimerTest,
                                 "Skald.GameMode.RetryInitTimerPersists",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)

bool FSkaldGameModeRetryInitTimerTest::RunTest(const FString &Parameters) {
  Skald::Tests::FScopedAutomationTestWorld TestWorld;
  UWorld *World = TestWorld.Get();
  TestNotNull(TEXT("World created"), World);
  if (!World) {
    return false;
  }

  ASkaldGameMode *GameMode = World->SpawnActor<ASkaldGameMode>();
  ATurnManager *TurnManager = World->SpawnActor<ATurnManager>();
  ASkaldPlayerController *Controller =
      World->SpawnActor<ASkaldPlayerController>();
  ASkaldPlayerState *PlayerState = World->SpawnActor<ASkaldPlayerState>();

  TestNotNull(TEXT("GameMode"), GameMode);
  TestNotNull(TEXT("TurnManager"), TurnManager);
  TestNotNull(TEXT("Controller"), Controller);
  TestNotNull(TEXT("PlayerState"), PlayerState);
  if (!GameMode || !TurnManager || !Controller || !PlayerState) {
    return false;
  }

  Skald::Tests::AttachGameModeToWorld(World, GameMode);
  GameMode->InitGameState();
  ASkaldGameState *GameState = GameMode->GetGameState<ASkaldGameState>();
  TestNotNull(TEXT("GameState initialised"), GameState);
  if (!GameState) {
    return false;
  }

  SetObjectProperty(GameMode, TEXT("TurnManager"), TurnManager);

  Controller->PlayerState = PlayerState;
  PlayerState->SetOwner(Controller);
  PlayerState->bIsAI = true;
  GameState->AddPlayerState(PlayerState);

  GameMode->TryInitializeWorldAndStart();

  const bool bTimerActive =
      FSkaldGameModeAutomationAccessor::IsRetryInitTimerActive(GameMode);
  const float TimerRate =
      FSkaldGameModeAutomationAccessor::GetRetryInitTimerRate(GameMode);

  TestTrue(TEXT("Retry init timer remains scheduled"), bTimerActive);
  TestTrue(TEXT("Retry init timer uses positive delay"), TimerRate > 0.f);
  TestTrue(TEXT("Retry init timer matches retry delay"),
           FMath::IsNearlyEqual(TimerRate, 0.01f, KINDA_SMALL_NUMBER));

  return true;
}
#endif  // WITH_AUTOMATION_TESTS
#endif  // defined(WITH_AUTOMATION_TESTS)
