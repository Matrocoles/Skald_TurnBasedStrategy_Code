#include "GridObstacleComponent.h"
#include "GridOverlayComponent.h"

UGridObstacleComponent::UGridObstacleComponent() {
  PrimaryComponentTick.bCanEverTick = false;
}

void UGridObstacleComponent::BeginPlay() {
  Super::BeginPlay();

  if (UWorld *World = GetWorld()) {
    if (UGridOverlayComponent *Grid =
            Skald::GridOverlay::FindActiveGridOverlay(World)) {
      Grid->RegisterObstacle(this);
    }
  }
}

