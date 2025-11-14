#include "MoatGridObstacleActor.h"

#include "GridObstacleComponent.h"
#include "GridOverlayComponent.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"

AMoatGridObstacleActor::AMoatGridObstacleActor() {
  bReplicates = true;
  bAlwaysRelevant = true;

  if (GridObstacle) {
    GridObstacle->bBlocksMovement = false;
    GridObstacle->bBlocksLineOfSight = false;
    GridObstacle->bAddsDifficultTerrain = true;
  }
}

void AMoatGridObstacleActor::OnConstruction(const FTransform &Transform) {
  Super::OnConstruction(Transform);

  if (GridObstacle) {
    GridObstacle->bAddsDifficultTerrain = true;
  }
}

void AMoatGridObstacleActor::BeginPlay() {
  Super::BeginPlay();

  if (HasAuthority()) {
    SetMoatEnabled(false);
  }
}

void AMoatGridObstacleActor::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty> &OutLifetimeProps) const {
  Super::GetLifetimeReplicatedProps(OutLifetimeProps);

  DOREPLIFETIME(AMoatGridObstacleActor, bMoatActive);
}

void AMoatGridObstacleActor::SetMoatEnabled(bool bEnabled) {
  if (bMoatActive == bEnabled) {
    if (HasAuthority()) {
      RefreshObstacleRegistration();
    }
    return;
  }

  bMoatActive = bEnabled;
  UpdateMoatVisuals();

  if (HasAuthority()) {
    RefreshObstacleRegistration();
    ForceNetUpdate();
  }
}

bool AMoatGridObstacleActor::SupportsTerritory(int32 TerritoryID) const {
  if (SupportedTerritoryIDs.Num() == 0) {
    return true;
  }

  return SupportedTerritoryIDs.Contains(TerritoryID);
}

void AMoatGridObstacleActor::OnRep_MoatActive() {
  UpdateMoatVisuals();
}

void AMoatGridObstacleActor::UpdateMoatVisuals() {
  SetActorHiddenInGame(!bMoatActive);
  SetActorEnableCollision(bMoatActive);

  if (MeshComponent) {
    MeshComponent->SetVisibility(bMoatActive, true);
  }
}

void AMoatGridObstacleActor::RefreshObstacleRegistration() {
  if (!HasAuthority() || !GridObstacle) {
    return;
  }

  UGridOverlayComponent *Grid = RegisteredGrid.Get();
  if (!Grid) {
    if (UWorld *World = GetWorld()) {
      Grid = Skald::GridOverlay::FindActiveGridOverlay(World);
    }
  }

  if (!Grid) {
    bObstacleRegistered = false;
    RegisteredGrid.Reset();
    return;
  }

  if (bMoatActive) {
    Grid->RegisterObstacle(GridObstacle);
    bObstacleRegistered = true;
    RegisteredGrid = Grid;
  } else {
    Grid->UnregisterObstacle(GridObstacle);
    bObstacleRegistered = false;
    RegisteredGrid.Reset();
  }
}
