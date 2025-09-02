#include "GridObstacleComponent.h"
#include "GridOverlayComponent.h"
#include "EngineUtils.h"

UGridObstacleComponent::UGridObstacleComponent() {
  PrimaryComponentTick.bCanEverTick = false;
}

void UGridObstacleComponent::BeginPlay() {
  Super::BeginPlay();

  if (UWorld *World = GetWorld()) {
    for (TActorIterator<AActor> It(World); It; ++It) {
      if (UGridOverlayComponent *Grid =
              It->FindComponentByClass<UGridOverlayComponent>()) {
        Grid->RegisterObstacle(this);
        break;
      }
    }
  }
}

