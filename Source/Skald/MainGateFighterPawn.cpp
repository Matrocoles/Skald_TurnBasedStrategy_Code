#include "MainGateFighterPawn.h"
#include "GridOverlayComponent.h"
#include "Skald_GameInstance.h"
#include "GridBattleManager.h"
#include "SkaldTypes.h"
#include "Net/UnrealNetwork.h"

AMainGateFighterPawn::AMainGateFighterPawn() {
  bAutoSkipActivation = true;
  BaseGateHealth = SkaldConstants::DefaultMainGateHealth;
  ConfiguredStats = Stats;
  ConfiguredStats.Health = BaseGateHealth;
  Stats = ConfiguredStats;
  bGateActive = true;
}

void AMainGateFighterPawn::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty> &OutLifetimeProps) const {
  Super::GetLifetimeReplicatedProps(OutLifetimeProps);
  DOREPLIFETIME(AMainGateFighterPawn, bGateActive);
}

void AMainGateFighterPawn::OnConstruction(const FTransform &Transform) {
  Super::OnConstruction(Transform);

  ConfiguredStats = Stats;
  if (BaseGateHealth <= 0) {
    BaseGateHealth = ConfiguredStats.Health > 0
                         ? ConfiguredStats.Health
                         : SkaldConstants::DefaultMainGateHealth;
  }

  ConfiguredStats.Health = BaseGateHealth;
  Stats = ConfiguredStats;
  InitializeMaxHealth(ConfiguredStats.Health);
}

void AMainGateFighterPawn::BeginPlay() {
  Super::BeginPlay();

  ConfiguredStats = Stats;
  ConfiguredStats.Health = BaseGateHealth > 0 ? BaseGateHealth
                                              : ConfiguredStats.Health;
  ApplyBattleStats(ConfiguredStats);
  SetGateEnabled(false);
}

bool AMainGateFighterPawn::SupportsTerritory(int32 TerritoryID) const {
  if (SupportedTerritoryIDs.Num() == 0) {
    return true;
  }

  return SupportedTerritoryIDs.Contains(TerritoryID);
}

FFighterStats
AMainGateFighterPawn::BuildBattleStats(int32 FortificationBonus) const {
  FFighterStats Result = ConfiguredStats;
  const int32 EffectiveBase =
      BaseGateHealth > 0 ? BaseGateHealth : ConfiguredStats.Health;
  Result.Health =
      FMath::Max(1, EffectiveBase + FMath::Max(0, FortificationBonus));
  return Result;
}

void AMainGateFighterPawn::ApplyBattleStats(const FFighterStats &BattleStats) {
  const FFighterStats PreviousStats = Stats;
  Stats = BattleStats;
  InitializeMaxHealth(BattleStats.Health);
  OnRep_Stats(PreviousStats);
  ForceNetUpdate();
}

void AMainGateFighterPawn::RestoreConfiguredStats() {
  ApplyBattleStats(ConfiguredStats);
}

void AMainGateFighterPawn::UpdateGridOccupancy(bool bOccupied) {
  if (UGridOverlayComponent *Grid = GetGrid()) {
    const TArray<FIntPoint> Cells = GetOccupiedCells();
    for (const FIntPoint &Cell : Cells) {
      Grid->SetOccupied(Cell, bOccupied);
    }

    if (bOccupied) {
      CurrentCell = Grid->WorldToGrid(GetActorLocation());
      AlignToCurrentCell();
    }
  }
}

void AMainGateFighterPawn::SetGateEnabled(bool bEnabled) {
  if (bGateActive == bEnabled) {
    return;
  }

  bGateActive = bEnabled;
  OnRep_GateActive();

  if (HasAuthority()) {
    ForceNetUpdate();
  }
}

void AMainGateFighterPawn::OnRep_GateActive() {
  SetActorHiddenInGame(!bGateActive);
  SetActorEnableCollision(bGateActive);
  SetActorTickEnabled(bGateActive);
  UpdateGridOccupancy(bGateActive);

  if (USkaldGameInstance *GI =
          Cast<USkaldGameInstance>(GetGameInstance())) {
    if (GI->GridBattleManager) {
      if (bGateActive) {
        GI->GridBattleManager->RegisterFighter(this, bIsAttacker);
      } else {
        GI->GridBattleManager->UnregisterFighter(this);
      }
    }
  }

  if (!bGateActive) {
    RestoreConfiguredStats();
  }
}
