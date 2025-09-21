#include "Skald_BattleGameMode.h"

#include "Algo/RandomShuffle.h"
#include "Algo/Sort.h"
#include "AIController.h"
#include "GridBattleManager.h"
#include "Skald.h"
#include "Skald_GameInstance.h"
#include "Skald_GameState.h"
#include "Skald_PlayerState.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FighterPawn.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GridOverlayComponent.h"
#include "Territory.h"
#include "TimerManager.h"
#include "WorldMap.h"

namespace {
bool HasHumanOwnedTerritory(const ASkaldGameState *GameState,
                            const AWorldMap * /*WorldMap*/,
                            const USkaldGameInstance *GameInstance,
                            TSet<int32> &CachedHumanTerritories,
                            const TMap<int32, FS_Territory> *CachedTerritoryMap) {
  const FSkaldTravelState *TravelStatePtr =
      GameInstance ? &GameInstance->GetTravelState() : nullptr;

  if (TravelStatePtr && TravelStatePtr->bValid) {
    for (int32 TerritoryID : TravelStatePtr->HumanOwnedTerritories) {
      if (TerritoryID > 0) {
        CachedHumanTerritories.Add(TerritoryID);
      }
    }
  }

  auto RegisterHumanTerritory = [&](int32 TerritoryID, int32 OwnerID) {
    if (TerritoryID <= 0 || CachedHumanTerritories.Contains(TerritoryID)) {
      return;
    }

    bool bIsHuman = false;
    if (OwnerID > 0 && GameState) {
      if (ASkaldPlayerState *Player = GameState->GetPlayerById(OwnerID)) {
        bIsHuman = !Player->bIsAI;
      }
    }

    if (!bIsHuman && TravelStatePtr && TravelStatePtr->bValid) {
      bIsHuman = TravelStatePtr->HumanOwnedTerritories.Contains(TerritoryID);
    }

    if (bIsHuman) {
      CachedHumanTerritories.Add(TerritoryID);
    }
  };

  if (CachedTerritoryMap) {
    for (const TPair<int32, FS_Territory> &Pair : *CachedTerritoryMap) {
      const FS_Territory &Territory = Pair.Value;
      RegisterHumanTerritory(Territory.TerritoryID, Territory.OwnerPlayerID);
    }
  }

  if (GameInstance) {
    for (const FS_Territory &Territory : GameInstance->CachedWorldMapTerritories) {
      RegisterHumanTerritory(Territory.TerritoryID, Territory.OwnerPlayerID);
    }
  }

  return CachedHumanTerritories.Num() > 0;
}

ASkaldPlayerState *EnsureBattleParticipant(ASkaldGameState *GameState, UWorld *World,
                                           int32 PlayerID, const FString &DisplayName,
                                           ESkaldFaction Faction, bool bIsAI) {
  if (!GameState || !World || PlayerID <= 0) {
    return nullptr;
  }

  if (ASkaldPlayerState *Existing = GameState->GetPlayerById(PlayerID)) {
    if (!DisplayName.IsEmpty()) {
      Existing->PlayerDisplayName = DisplayName;
      Existing->SetPlayerName(DisplayName);
    } else if (Existing->GetPlayerName().IsEmpty()) {
      UE_LOG(LogSkald, Warning,
             TEXT("EnsureBattleParticipant: Missing name for PlayerId %d"),
             PlayerID);
    }
    if (Faction != ESkaldFaction::None) {
      Existing->Faction = Faction;
    }
    Existing->bIsAI = bIsAI;
    return Existing;
  }

  for (APlayerState *BasePS : GameState->PlayerArray) {
    ASkaldPlayerState *Candidate = Cast<ASkaldPlayerState>(BasePS);
    if (!Candidate || Candidate->GetPlayerId() != PlayerID) {
      continue;
    }

    if (!DisplayName.IsEmpty()) {
      Candidate->PlayerDisplayName = DisplayName;
      Candidate->SetPlayerName(DisplayName);
    } else if (Candidate->GetPlayerName().IsEmpty()) {
      UE_LOG(LogSkald, Warning,
             TEXT("EnsureBattleParticipant: Missing name for PlayerId %d"),
             PlayerID);
    }
    if (Faction != ESkaldFaction::None) {
      Candidate->Faction = Faction;
    }
    Candidate->bIsAI = bIsAI;

    bool bAddedToList = false;
    if (!GameState->Players.Contains(Candidate)) {
      GameState->Players.Add(Candidate);
      GameState->Players.RemoveAll([](const ASkaldPlayerState* Player) {
        return Player == nullptr;
      });
      GameState->Players.Sort([](const ASkaldPlayerState& A,
                                 const ASkaldPlayerState& B) {
        return A.GetPlayerId() < B.GetPlayerId();
      });
      bAddedToList = true;
    }
    if (bAddedToList) {
      GameState->OnPlayersUpdated.Broadcast();
      GameState->ForceNetUpdate();
    }

    return Candidate;
  }

  FActorSpawnParameters Params;
  Params.SpawnCollisionHandlingOverride =
      ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
  ASkaldPlayerState *NewState = World->SpawnActor<ASkaldPlayerState>(
      ASkaldPlayerState::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator,
      Params);
  if (!NewState) {
    return nullptr;
  }

  NewState->SetPlayerId(PlayerID);
  if (!DisplayName.IsEmpty()) {
    NewState->PlayerDisplayName = DisplayName;
    NewState->SetPlayerName(DisplayName);
  } else {
    UE_LOG(LogSkald, Warning,
           TEXT("EnsureBattleParticipant: Spawned PlayerState without name (Id=%d)"),
           PlayerID);
  }
  if (Faction != ESkaldFaction::None) {
    NewState->Faction = Faction;
  }
  NewState->bIsAI = bIsAI;

  GameState->AddPlayerState(NewState);
  GameState->ForceNetUpdate();
  return NewState;
}
} // namespace

void ASkald_BattleGameMode::InitGame(const FString &Map, const FString &Options,
                                     FString &Error) {
  Super::InitGame(Map, Options, Error);

  ReadyControllers.Empty();
  CachedHumanTerritoryIDs.Reset();
  CachedTerritoryMap.Reset();
  bPendingBattleSetupComplete = false;
  bLoggedTravelCache = false;
  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(TravelBootstrapHandle);
  }

  if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
    GI->SetTravelPending(false);
    const FSkaldTravelState &TravelState = GI->GetTravelState();
    ExpectedControllers = TravelState.bValid ? TravelState.ExpectedControllers : 0;

    if (TravelState.CachedTerritories.Num() > 0) {
      CachedTerritoryMap.Reserve(TravelState.CachedTerritories.Num());
      for (const FS_Territory &Territory : TravelState.CachedTerritories) {
        CachedTerritoryMap.Add(Territory.TerritoryID, Territory);
      }
      GI->CachedWorldMapTerritories = TravelState.CachedTerritories;
    } else if (GI->CachedWorldMapTerritories.Num() > 0) {
      CachedTerritoryMap.Reserve(GI->CachedWorldMapTerritories.Num());
      for (const FS_Territory &Territory : GI->CachedWorldMapTerritories) {
        CachedTerritoryMap.Add(Territory.TerritoryID, Territory);
      }
    }

    HasHumanOwnedTerritory(GetGameState<ASkaldGameState>(), WorldMap, GI,
                           CachedHumanTerritoryIDs, &CachedTerritoryMap);
    if (CachedHumanTerritoryIDs.Num() > 0) {
      bLoggedTravelCache = true;
      UE_LOG(LogSkald, Log,
             TEXT("BattleGM InitGame: Restored %d human territories from travel cache"),
             CachedHumanTerritoryIDs.Num());
    }

    UE_LOG(LogSkald, Log, TEXT("BattleGM InitGame: ExpectedControllers=%d"),
           ExpectedControllers);
  } else {
    ExpectedControllers = 0;
    UE_LOG(LogSkald, Warning,
           TEXT("BattleGM InitGame: GameInstance unavailable; waiting for controllers"));
  }

  BootstrapFromTravelState();
}

void ASkald_BattleGameMode::BeginPlay() {
  Super::BeginPlay();

  ReadyControllers.Empty();

  if (!BattleManager) {
    UClass *ClassToUse = BattleManagerClass ? *BattleManagerClass
                                            : UGridBattleManager::StaticClass();
    BattleManager = NewObject<UGridBattleManager>(this, ClassToUse);
    const int32 Seed =
        static_cast<int32>(FDateTime::Now().GetTicks() & 0x7FFFFFFF);
    BattleManager->SetRandomSeed(Seed);
    BattleManager->OnBattleEnded.AddDynamic(this,
                                           &ASkald_BattleGameMode::HandleBattleEnded);
  }

  USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>();
  if (GI) {
    GI->GridBattleManager = BattleManager;
    if (ExpectedControllers == 0) {
      const FSkaldTravelState &TravelState = GI->GetTravelState();
      ExpectedControllers =
          TravelState.bValid ? TravelState.ExpectedControllers : 0;
    }
  } else {
    ExpectedControllers = 0;
  }

  if (!WorldMap) {
    for (TActorIterator<AWorldMap> It(GetWorld()); It; ++It) {
      WorldMap = *It;
      break;
    }
  }

  if (WorldMap && CachedHumanTerritoryIDs.Num() > 0) {
    int32 ReacquiredCount = 0;
    for (ATerritory *Territory : WorldMap->Territories) {
      if (Territory &&
          CachedHumanTerritoryIDs.Contains(Territory->TerritoryID)) {
        ++ReacquiredCount;
      }
    }
    UE_LOG(LogSkald, Log,
           TEXT("BattleGM BeginPlay: Reacquired %d of %d cached human territories"),
           ReacquiredCount, CachedHumanTerritoryIDs.Num());
  } else if (!WorldMap && CachedHumanTerritoryIDs.Num() > 0) {
    UE_LOG(LogSkald, Verbose,
           TEXT("BattleGM BeginPlay: Cached %d human territories without a world map actor"),
           CachedHumanTerritoryIDs.Num());
  }

  if (ASkaldGameState *GS = GetGameState<ASkaldGameState>()) {
    int32 ControllerCount = 0;
    for (FConstPlayerControllerIterator It =
             GetWorld()->GetPlayerControllerIterator();
         It; ++It) {
      ++ControllerCount;
    }
    if (GS->PlayerArray.Num() != ControllerCount) {
      UE_LOG(LogSkald, Verbose,
             TEXT("BattleGM BeginPlay: PlayerStates=%d ControllerCount=%d"),
             GS->PlayerArray.Num(), ControllerCount);
    }

    HasHumanOwnedTerritory(GS, WorldMap, GI, CachedHumanTerritoryIDs,
                           &CachedTerritoryMap);
  }

  BootstrapFromTravelState();
}

void ASkald_BattleGameMode::SetupPendingBattle() {
  if (!HasAuthority()) {
    return;
  }

  if (bPendingBattleSetupComplete) {
    return;
  }

  bBattleLaunched = false;

  USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>();
  if (!GI || !GI->GridBattleManager) {
    return;
  }

  ASkaldGameState *GS = GetGameState<ASkaldGameState>();
  if (!GS) {
    return;
  }

  UWorld *World = GetWorld();
  if (!World) {
    return;
  }

  HasHumanOwnedTerritory(GS, WorldMap, GI, CachedHumanTerritoryIDs,
                         &CachedTerritoryMap);

  const FS_BattlePayload Battle = GI->PendingBattle;
  ASkaldPlayerState *AttackerPS = EnsureBattleParticipant(
      GS, World, Battle.AttackerPlayerID, Battle.AttackerDisplayName,
      Battle.AttackerFaction, Battle.bAttackerIsAI);
  ASkaldPlayerState *DefenderPS = EnsureBattleParticipant(
      GS, World, Battle.DefenderPlayerID, Battle.DefenderDisplayName,
      Battle.DefenderFaction, Battle.bDefenderIsAI);

  const int32 AttackerBudget = FMath::Max(0, Battle.ArmyCountSent);
  const int32 DefenderBudget =
      Battle.DefenderArmyCount > 0 ? Battle.DefenderArmyCount : Battle.ArmyCountSent;

  BeginPreBattleSelection(AttackerPS, DefenderPS, AttackerBudget, DefenderBudget);

  AutoCommitAIArmy(AttackerPS, AttackerBudget);
  AutoCommitAIArmy(DefenderPS, DefenderBudget);

  // Mark setup complete before attempting to start the battle to avoid
  // TryStartBattle -> BootstrapFromTravelState -> SetupPendingBattle recursion.
  bPendingBattleSetupComplete = true;

  TryStartBattle();
}

void ASkald_BattleGameMode::BootstrapFromTravelState() {
  if (!HasAuthority()) {
    return;
  }

  UWorld *World = GetWorld();
  if (!World) {
    return;
  }

  USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>();
  if (!GI) {
    return;
  }

  const FSkaldTravelState &TravelState = GI->GetTravelState();
  if (!TravelState.bValid && ExpectedControllers <= 0) {
    if (ASkaldGameState *GS = GetGameState<ASkaldGameState>()) {
      ExpectedControllers = GS->PlayerArray.Num();
    }
  } else if (TravelState.bValid && ExpectedControllers <= 0) {
    ExpectedControllers = TravelState.ExpectedControllers;
  }

  HasHumanOwnedTerritory(GetGameState<ASkaldGameState>(), WorldMap, GI,
                         CachedHumanTerritoryIDs, &CachedTerritoryMap);
  if (!bLoggedTravelCache && CachedHumanTerritoryIDs.Num() > 0) {
    UE_LOG(LogSkald, Log,
           TEXT("BattleGM InitGame: Restored %d human territories from travel cache"),
           CachedHumanTerritoryIDs.Num());
    bLoggedTravelCache = true;
  }

  ASkaldGameState *GS = GetGameState<ASkaldGameState>();
  const int32 PlayerStates = GS ? GS->PlayerArray.Num() : 0;
  if (ExpectedControllers <= 0 && PlayerStates > 0) {
    ExpectedControllers = PlayerStates;
  }

  if (ExpectedControllers > 0 && PlayerStates < ExpectedControllers) {
    World->GetTimerManager().SetTimer(TravelBootstrapHandle, this,
                                      &ASkald_BattleGameMode::BootstrapFromTravelState,
                                      0.25f, false);
    return;
  }

  World->GetTimerManager().ClearTimer(TravelBootstrapHandle);

  if (!bPendingBattleSetupComplete) {
    SetupPendingBattle();
  }

  TryStartBattle();
}

void ASkald_BattleGameMode::AutoCommitAIArmy(ASkaldPlayerState *PlayerState,
                                             int32 Budget) const {
  if (!PlayerState || !PlayerState->bIsAI || !BattleManager) {
    return;
  }

  TArray<FFighterDefinition> Definitions =
      BattleManager->GetFightersForFaction(PlayerState->Faction);
  if (Definitions.Num() == 0) {
    PlayerState->PendingArmy.Reset();
    PlayerState->bArmyLockedIn = true;
    return;
  }

  Algo::RandomShuffle(Definitions);

  TArray<FFighterDefinition> Selection;
  int32 CurrentCost = 0;
  for (const FFighterDefinition &Def : Definitions) {
    const int32 Cost = FMath::Max(Def.Stats.ArmyCost, 0);
    if (Cost > 0 && Budget >= 0 && CurrentCost + Cost > Budget) {
      continue;
    }

    Selection.Add(Def);
    if (Cost > 0) {
      CurrentCost += Cost;
    }

    if (Budget > 0 && CurrentCost >= Budget) {
      break;
    }
  }

  PlayerState->PendingArmy = Selection;
  PlayerState->bArmyLockedIn = true;
}

void ASkald_BattleGameMode::SpawnFighterSide(const TArray<FFighterDefinition> &Roster,
                                             bool bAsAttacker) {
  if (!BattleManager || !GetWorld()) {
    return;
  }

  USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>();
  if (!GI) {
    return;
  }

  UGridOverlayComponent *Grid = nullptr;
  for (TActorIterator<AActor> It(GetWorld()); It; ++It) {
    if (UGridOverlayComponent *Found =
            It->FindComponentByClass<UGridOverlayComponent>()) {
      Grid = Found;
      break;
    }
  }

  const int32 Edge = 3;
  const int32 MaxX = UGridBattleManager::GridSize - 1;
  const int32 MaxY = UGridBattleManager::GridSize - 1;

  for (const FFighterDefinition &Def : Roster) {
    FIntPoint Cell;
    Cell.Y = GI->CombatRandomStream.RandRange(0, MaxY);
    Cell.X = bAsAttacker ? GI->CombatRandomStream.RandRange(0, Edge - 1)
                         : GI->CombatRandomStream.RandRange(MaxX - (Edge - 1), MaxX);

    const FVector SpawnLoc = Grid ? Grid->GridToWorld(Cell) : FVector::ZeroVector;
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride =
        ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
    AFighterPawn *Pawn = GetWorld()->SpawnActor<AFighterPawn>(
        AFighterPawn::StaticClass(), SpawnLoc, FRotator::ZeroRotator, Params);
    if (Pawn) {
      Pawn->Stats = Def.Stats;
      Pawn->bIsAttacker = bAsAttacker;
      BattleManager->RegisterFighter(Pawn, bAsAttacker);
    }
  }
}

void ASkald_BattleGameMode::TryLaunchBattle() {
  if (bBattleLaunched || !HasAuthority()) {
    return;
  }

  USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>();
  if (!GI || !GI->GridBattleManager) {
    return;
  }

  ASkaldGameState *GS = GetGameState<ASkaldGameState>();
  if (!GS) {
    return;
  }

  UWorld *World = GetWorld();
  if (!World) {
    return;
  }

  const FS_BattlePayload &Battle = GI->PendingBattle;
  ASkaldPlayerState *AttackerPS = EnsureBattleParticipant(
      GS, World, Battle.AttackerPlayerID, Battle.AttackerDisplayName,
      Battle.AttackerFaction, Battle.bAttackerIsAI);
  ASkaldPlayerState *DefenderPS = EnsureBattleParticipant(
      GS, World, Battle.DefenderPlayerID, Battle.DefenderDisplayName,
      Battle.DefenderFaction, Battle.bDefenderIsAI);

  if ((AttackerPS && !AttackerPS->bArmyLockedIn) ||
      (DefenderPS && !DefenderPS->bArmyLockedIn)) {
    return;
  }

  TArray<FFighterDefinition> AttackerDefs =
      AttackerPS ? AttackerPS->PendingArmy : TArray<FFighterDefinition>();
  TArray<FFighterDefinition> DefenderDefs =
      DefenderPS ? DefenderPS->PendingArmy : TArray<FFighterDefinition>();

  TArray<FFighter> Attackers;
  Attackers.Reserve(AttackerDefs.Num());
  for (const FFighterDefinition &Def : AttackerDefs) {
    FFighter Fighter;
    Fighter.Stats = Def.Stats;
    Fighter.Faction = AttackerPS ? AttackerPS->Faction : Def.Faction;
    Attackers.Add(Fighter);
  }

  TArray<FFighter> Defenders;
  Defenders.Reserve(DefenderDefs.Num());
  for (const FFighterDefinition &Def : DefenderDefs) {
    FFighter Fighter;
    Fighter.Stats = Def.Stats;
    Fighter.Faction = DefenderPS ? DefenderPS->Faction : Def.Faction;
    Defenders.Add(Fighter);
  }

  GI->GridBattleManager->InitBattle(Attackers, Defenders);

  SpawnFighterSide(AttackerDefs, /*bAsAttacker=*/true);
  SpawnFighterSide(DefenderDefs, /*bAsAttacker=*/false);

  GI->GridBattleManager->RollInitiative();
  GI->GridBattleManager->StartRound();

  if (AttackerPS) {
    AttackerPS->bArmyLockedIn = false;
    AttackerPS->PendingArmy.Reset();
    AttackerPS->PendingArmyBudget = 0;
  }
  if (DefenderPS) {
    DefenderPS->bArmyLockedIn = false;
    DefenderPS->PendingArmy.Reset();
    DefenderPS->PendingArmyBudget = 0;
  }

  bBattleLaunched = true;
}

void ASkald_BattleGameMode::TryInitializeWorldAndStart() {
  bWorldInitialized = true;
  bTurnsStarted = true;
  GetWorldTimerManager().ClearTimer(RetryInitTimerHandle);
}

void ASkald_BattleGameMode::PostLogin(APlayerController *NewPlayer) {
  Super::PostLogin(NewPlayer);

  if (NewPlayer) {
    OnControllerReady(NewPlayer);
  }
}

void ASkald_BattleGameMode::OnAIControllerReady(AAIController *Controller) {
  OnControllerReady(Controller);
}

void ASkald_BattleGameMode::OnControllerReady(AController *Controller) {
  if (!IsValid(Controller)) {
    return;
  }

  for (auto It = ReadyControllers.CreateIterator(); It; ++It) {
    if (!It->IsValid()) {
      It.RemoveCurrent();
    }
  }

  ReadyControllers.Add(Controller);

  UE_LOG(LogSkald, Log,
         TEXT("OnControllerReady: %s ReadyControllers=%d Expected=%d"),
         *GetNameSafe(Controller), ReadyControllers.Num(), ExpectedControllers);

  BootstrapFromTravelState();
  TryStartBattle();
}

void ASkald_BattleGameMode::TryStartBattle() {
  if (bBattleLaunched) {
    return;
  }

  if (!bPendingBattleSetupComplete) {
    BootstrapFromTravelState();
    if (!bPendingBattleSetupComplete) {
      return;
    }
  }

  for (auto It = ReadyControllers.CreateIterator(); It; ++It) {
    if (!It->IsValid()) {
      It.RemoveCurrent();
    }
  }

  const ASkaldGameState *GS = GetGameState<ASkaldGameState>();
  const int32 PlayerStates = GS ? GS->PlayerArray.Num() : 0;
  if (ExpectedControllers <= 0 && PlayerStates > 0) {
    ExpectedControllers = PlayerStates;
  }

  TSet<int32> ReadyPlayerIDs;
  for (const TWeakObjectPtr<AController> &WeakController : ReadyControllers) {
    if (const AController *ReadyController = WeakController.Get()) {
      if (const APlayerState *PS = ReadyController->PlayerState) {
        ReadyPlayerIDs.Add(PS->GetPlayerId());
      }
    }
  }

  const int32 ReadyPlayerStates = ReadyPlayerIDs.Num();
  UE_LOG(LogSkald, Log,
         TEXT("TryStartBattle: ReadyPlayerStates=%d Expected=%d"),
         ReadyPlayerStates, ExpectedControllers);

  if (ExpectedControllers <= 0 || PlayerStates < ExpectedControllers ||
      ReadyPlayerStates < ExpectedControllers) {
    return;
  }

  TryLaunchBattle();
}

