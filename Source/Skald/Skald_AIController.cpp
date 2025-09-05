#include "Skald_AIController.h"
#include "Kismet/GameplayStatics.h"
#include "Skald.h"
#include "Skald_PlayerState.h"
#include "Skald_TurnManager.h"
#include "SkaldTypes.h"
#include "Territory.h"
#include "WorldMap.h"
#include <limits>

namespace {
constexpr int32 MaxAIIterations = 100;
}

void ASkaldAIController::StartTurn() { MakeAIDecision(); }

void ASkaldAIController::MakeAIDecision() {
  if (!TurnManager) {
    EndTurn();
    return;
  }

  ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>();
  if (!PS) {
    EndTurn();
    return;
  }

  AWorldMap *WorldMap = Cast<AWorldMap>(
      UGameplayStatics::GetActorOfClass(GetWorld(), AWorldMap::StaticClass()));
  if (!WorldMap) {
    EndTurn();
    return;
  }

  ETurnPhase Phase = TurnManager->GetCurrentPhase();
  int32 IterationCount = 0;
  while (Phase != ETurnPhase::EndTurn && IterationCount++ < MaxAIIterations) {
    const ETurnPhase PrevPhase = Phase;

    if (Phase == ETurnPhase::Reinforcement) {
      TArray<ATerritory *> OwnedTerritories;
      for (ATerritory *Territory : WorldMap->Territories) {
        if (Territory && Territory->OwningPlayer == PS) {
          OwnedTerritories.Add(Territory);
        }
      }

      int32 SpreadIndex = 0;
      while (PS->DeployableUnits > 0 && OwnedTerritories.Num() > 0) {
        ATerritory *TargetTerritory =
            OwnedTerritories[SpreadIndex % OwnedTerritories.Num()];
        ++TargetTerritory->ArmyUnits;
        TargetTerritory->RefreshAppearance();
        --PS->DeployableUnits;
        --PS->Resources;
        ++SpreadIndex;
      }
      TurnManager->BroadcastDeployableUnits(PS);
      TurnManager->BroadcastResources(PS);

      TurnManager->AdvancePhase();
    } else if (Phase == ETurnPhase::Attack) {
      ATerritory *BestSource = nullptr;
      ATerritory *BestTarget = nullptr;
      int32 WeakestStrength = std::numeric_limits<int32>::max();

      for (ATerritory *Source : WorldMap->Territories) {
        if (!Source || Source->OwningPlayer != PS || Source->ArmyUnits <= 1) {
          continue;
        }

        for (ATerritory *Neighbor : Source->AdjacentTerritories) {
          if (!Neighbor || Neighbor->OwningPlayer == PS) {
            continue;
          }

          if (Neighbor->ArmyUnits < WeakestStrength) {
            BestSource = Source;
            BestTarget = Neighbor;
            WeakestStrength = Neighbor->ArmyUnits;
          }
        }
      }

      if (BestSource && BestTarget && BestSource->ArmyUnits > 1) {
        const int32 ArmySent = BestSource->ArmyUnits - 1;
        HandleAttackRequested(BestSource->TerritoryID, BestTarget->TerritoryID,
                              ArmySent, false);
      }

      TurnManager->AdvancePhase();
    } else if (Phase == ETurnPhase::Engineering ||
               Phase == ETurnPhase::Treasure) {
      TurnManager->AdvancePhase();
    } else if (Phase == ETurnPhase::Movement) {
      ATerritory *BestSource = nullptr;
      ATerritory *BestTarget = nullptr;
      int32 WeakestStrength = std::numeric_limits<int32>::max();

      for (ATerritory *Source : WorldMap->Territories) {
        if (!Source || Source->OwningPlayer != PS || Source->ArmyUnits <= 1) {
          continue;
        }

        for (ATerritory *Neighbor : Source->AdjacentTerritories) {
          if (!Neighbor || Neighbor->OwningPlayer != PS) {
            continue;
          }

          if (Neighbor->ArmyUnits < WeakestStrength) {
            BestSource = Source;
            BestTarget = Neighbor;
            WeakestStrength = Neighbor->ArmyUnits;
          }
        }
      }

      if (BestSource && BestTarget) {
        int32 TroopsToMove = BestSource->ArmyUnits / 2;
        TroopsToMove = FMath::Clamp(TroopsToMove, 1, BestSource->ArmyUnits - 1);
        HandleMoveRequested(BestSource->TerritoryID, BestTarget->TerritoryID,
                            TroopsToMove);
      }

      TurnManager->AdvancePhase();
    } else {
      UE_LOG(LogSkald, Warning,
             TEXT("MakeAIDecision encountered unexpected phase %s"),
             *UEnum::GetValueAsString(Phase));
      break;
    }

    Phase = TurnManager->GetCurrentPhase();
    if (Phase == PrevPhase) {
      UE_LOG(LogSkald, Warning,
             TEXT("MakeAIDecision phase %s did not advance; breaking"),
             *UEnum::GetValueAsString(Phase));
      break;
    }
  }

  if (IterationCount >= MaxAIIterations) {
    UE_LOG(LogSkald, Warning, TEXT("MakeAIDecision hit iteration limit (%d)"),
           MaxAIIterations);
  } else {
    UE_LOG(LogSkald, Log, TEXT("MakeAIDecision completed in %d iterations"),
           IterationCount);
  }

  EndTurn();
}

