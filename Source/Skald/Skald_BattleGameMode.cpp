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
#include "GridOverlayComponent.h"
#include "Territory.h"
#include "TimerManager.h"
#include "WorldMap.h"

namespace {
bool HasHumanOwnedTerritory(const ASkaldGameState *GameState,
                            const AWorldMap *WorldMap,
                            const USkaldGameInstance *GameInstance) {
  if (!GameState) {
    return false;
  }

  auto IsHumanPlayerById = [GameState](int32 PlayerID) {
    if (PlayerID <= 0) {
      return false;
    }
    if (ASkaldPlayerState *Player = GameState->GetPlayerById(PlayerID)) {
      return !Player->bIsAI;
    }
    return false;
  };

  if (WorldMap) {
    for (ATerritory *Territory : WorldMap->Territories) {
      if (!Territory) {
        continue;
      }
      const int32 OwnerID =
          Territory->OwningPlayer ? Territory->OwningPlayer->GetPlayerId() : 0;
      if (IsHumanPlayerById(OwnerID)) {
        return true;
      }
    }
  }

  if (GameInstance) {
    for (const FS_Territory &TerritoryData :
         GameInstance->CachedWorldMapTerritories) {
      if (IsHumanPlayerById(TerritoryData.OwnerPlayerID)) {
        return true;
      }
    }
  }

  return false;
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
  } else {
    NewState->PlayerDisplayName = FString::Printf(TEXT("Player %d"), PlayerID);
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
    const FSkaldTravelState &TravelState = GI->GetTravelState();
    ExpectedControllers = TravelState.bValid ? TravelState.ExpectedControllers : 0;
    UE_LOG(LogSkald, Log,
           TEXT("BattleGM BeginPlay: ExpectedControllers=%d"),
           ExpectedControllers);
  } else {
    ExpectedControllers = 0;
  }

  const FSkaldTravelState *TravelStatePtr = nullptr;
  if (!WorldMap) {
    for (TActorIterator<AWorldMap> It(GetWorld()); It; ++It) {
      WorldMap = *It;
      break;
    }
  }

  if (GI) {
    TravelStatePtr = &GI->GetTravelState();
  }

  if (WorldMap && TravelStatePtr && TravelStatePtr->bValid &&
      TravelStatePtr->HumanOwnedTerritories.Num() > 0) {
    int32 ReacquiredCount = 0;
    for (ATerritory *Territory : WorldMap->Territories) {
      if (Territory &&
          TravelStatePtr->HumanOwnedTerritories.Contains(Territory->TerritoryID)) {
        ++ReacquiredCount;
      }
    }
    UE_LOG(LogSkald, Log,
           TEXT("BattleGM BeginPlay: Reacquired %d of %d cached human territories"),
           ReacquiredCount, TravelStatePtr->HumanOwnedTerritories.Num());
  }

  SetupPendingBattle();

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

    const bool bHumanHasTerritory =
        HasHumanOwnedTerritory(GS, WorldMap, GI);
    if (!bHumanHasTerritory) {
      const int32 CachedCount = GI ? GI->CachedWorldMapTerritories.Num() : 0;
      UE_LOG(LogSkald, Warning,
             TEXT("BeginPlay: Unable to confirm any human-owned territory after "
                  "travel (WorldMap=%s, CachedTerritories=%d)"),
             *GetNameSafe(WorldMap), CachedCount);
    }
  }

  TryStartBattle();
}

void ASkald_BattleGameMode::SetupPendingBattle() {
  if (!HasAuthority()) {
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

  const bool bHumanHasTerritory =
      HasHumanOwnedTerritory(GS, WorldMap, GI);
  if (!bHumanHasTerritory) {
    const int32 CachedCount = GI ? GI->CachedWorldMapTerritories.Num() : 0;
    UE_LOG(LogSkald, Warning,
           TEXT("SetupPendingBattle: No human-owned territory recorded before "
                "launch (WorldMap=%s, CachedTerritories=%d)"),
           *GetNameSafe(WorldMap), CachedCount);
  }

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

  if (ExpectedControllers > 0) {
    for (auto It = ReadyControllers.CreateIterator(); It; ++It) {
      if (!It->IsValid()) {
        It.RemoveCurrent();
      }
    }
    const ASkaldGameState *GS = GetGameState<ASkaldGameState>();
    const int32 PlayerStates = GS ? GS->PlayerArray.Num() : 0;
    const int32 Controllers = ReadyControllers.Num();
    if (Controllers < ExpectedControllers || PlayerStates < ExpectedControllers) {
      UE_LOG(LogSkald, Log,
             TEXT("TryLaunchBattle: Waiting for controllers. Ready=%d PlayerStates=%d Expected=%d"),
             Controllers, PlayerStates, ExpectedControllers);
      return;
    }
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
    ReadyControllers.Add(NewPlayer);
  }

  TryStartBattle();
}

void ASkald_BattleGameMode::OnAIControllerReady(AAIController *AI) {
  if (AI) {
    ReadyControllers.Add(AI);
  }

  TryStartBattle();
}

void ASkald_BattleGameMode::TryStartBattle() {
  if (bBattleLaunched) {
    return;
  }

  for (auto It = ReadyControllers.CreateIterator(); It; ++It) {
    if (!It->IsValid()) {
      It.RemoveCurrent();
    }
  }

  const int32 Controllers = ReadyControllers.Num();
  const ASkaldGameState *GS = GetGameState<ASkaldGameState>();
  const int32 PlayerStates = GS ? GS->PlayerArray.Num() : 0;

  UE_LOG(LogSkald, Log,
         TEXT("TryStartBattle: ReadyControllers=%d PlayerStates=%d Expected=%d"),
         Controllers, PlayerStates, ExpectedControllers);

  if (ExpectedControllers <= 0) {
    UE_LOG(LogSkald, Warning,
           TEXT("TryStartBattle: ExpectedControllers not provided; launching immediately"));
    TryLaunchBattle();
    return;
  }

  if (Controllers >= ExpectedControllers && PlayerStates >= ExpectedControllers) {
    UE_LOG(LogSkald, Log,
           TEXT("TryStartBattle: All controllers ready, launching battle"));
    TryLaunchBattle();
  }
}

