#include "Skald_BattleGameMode.h"

#include "Algo/RandomShuffle.h"
#include "GridBattleManager.h"
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

void ASkald_BattleGameMode::BeginPlay() {
  Super::BeginPlay();

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

  if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
    GI->GridBattleManager = BattleManager;
  }

  SetupPendingBattle();

  if (ASkaldGameState *GS = GetGameState<ASkaldGameState>()) {
    int32 ControllerCount = 0;
    for (FConstPlayerControllerIterator It =
             GetWorld()->GetPlayerControllerIterator();
         It; ++It) {
      ++ControllerCount;
    }
    ensureMsgf(GS->PlayerArray.Num() == ControllerCount,
               TEXT("PlayerCount %d != ControllerCount %d after travel"),
               GS->PlayerArray.Num(), ControllerCount);

    bool bHumanHasTerritory = false;
    if (WorldMap) {
      for (APlayerState *BasePS : GS->PlayerArray) {
        ASkaldPlayerState *PS = Cast<ASkaldPlayerState>(BasePS);
        if (!PS || PS->bIsAI) {
          continue;
        }
        for (ATerritory *Territory : WorldMap->Territories) {
          if (WorldMap->IsOwnedBy(Territory, PS)) {
            bHumanHasTerritory = true;
            break;
          }
        }
        if (bHumanHasTerritory) {
          break;
        }
      }
    }
    ensureMsgf(
        bHumanHasTerritory,
        TEXT("Human player does not own any territory after travel"));
  }
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

  const FS_BattlePayload Battle = GI->PendingBattle;
  ASkaldPlayerState *AttackerPS = GS->GetPlayerById(Battle.AttackerPlayerID);
  ASkaldPlayerState *DefenderPS = GS->GetPlayerById(Battle.DefenderPlayerID);

  const int32 AttackerBudget = FMath::Max(0, Battle.ArmyCountSent);
  const int32 DefenderBudget =
      Battle.DefenderArmyCount > 0 ? Battle.DefenderArmyCount : Battle.ArmyCountSent;

  BeginPreBattleSelection(AttackerPS, DefenderPS, AttackerBudget, DefenderBudget);

  AutoCommitAIArmy(AttackerPS, AttackerBudget);
  AutoCommitAIArmy(DefenderPS, DefenderBudget);

  TryLaunchBattle();
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

  const FS_BattlePayload &Battle = GI->PendingBattle;
  ASkaldPlayerState *AttackerPS = GS->GetPlayerById(Battle.AttackerPlayerID);
  ASkaldPlayerState *DefenderPS = GS->GetPlayerById(Battle.DefenderPlayerID);

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

