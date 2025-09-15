#include "Skald_BattleGameMode.h"

#include "Algo/RandomShuffle.h"
#include "GridBattleManager.h"
#include "Skald_GameInstance.h"
#include "Skald_GameState.h"
#include "Skald_PlayerState.h"
#include "Engine/World.h"
#include "Territory.h"
#include "TimerManager.h"
#include "WorldMap.h"

namespace {
constexpr int32 DefaultAIMaxCost = 10;
}

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
    if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
      GI->GridBattleManager = BattleManager;
    }
  }

  if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
    if (GI->GridBattleManager) {
      ASkaldGameState *GS = GetGameState<ASkaldGameState>();
      if (GS) {
        for (APlayerState *BasePS : GS->PlayerArray) {
          ASkaldPlayerState *PS = Cast<ASkaldPlayerState>(BasePS);
          if (!PS || !PS->bIsAI) {
            continue;
          }

          TArray<FFighterDefinition> Definitions =
              GI->GridBattleManager->GetFightersForFaction(PS->Faction);
          if (Definitions.Num() <= 0) {
            PS->bHasLockedIn = true;
            continue;
          }

          const int32 MaxCost = GI->PendingBattle.ArmyCountSent > 0
                                    ? GI->PendingBattle.ArmyCountSent
                                    : DefaultAIMaxCost;
          int32 CurrentCost = 0;

          Algo::RandomShuffle(Definitions);

          TArray<FFighter> Fighters;
          for (const FFighterDefinition &Def : Definitions) {
            if (CurrentCost + Def.Stats.ArmyCost > MaxCost) {
              continue;
            }
            FFighter Fighter;
            Fighter.Stats = Def.Stats;
            Fighter.Faction = PS->Faction;
            Fighters.Add(Fighter);
            CurrentCost += Def.Stats.ArmyCost;
          }

          if (Fighters.Num() > 0) {
            GI->GridBattleManager->InitBattle(Fighters, Fighters);
            GI->GridBattleManager->RollInitiative();
            GI->GridBattleManager->StartRound();
          }

          PS->bHasLockedIn = true;
        }
      }
    }
  }

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

void ASkald_BattleGameMode::TryInitializeWorldAndStart() {
  bWorldInitialized = true;
  bTurnsStarted = true;
  GetWorldTimerManager().ClearTimer(RetryInitTimerHandle);
}

