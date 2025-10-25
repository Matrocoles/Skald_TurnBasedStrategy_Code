#include "GridObstacleComponent.h"
#include "GameFramework/Actor.h"
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

bool UGridObstacleComponent::GetCustomGridFootprint(const UGridOverlayComponent *Grid,
                                                    FIntPoint &OutMin,
                                                    FIntPoint &OutMax) const {
  OutMin = FIntPoint::ZeroValue;
  OutMax = FIntPoint::ZeroValue;

  if (!bOverrideBlockedCells || !Grid) {
    return false;
  }

  const AActor *Owner = GetOwner();
  if (!Owner) {
    return false;
  }

  const FIntPoint Anchor = Grid->WorldToGrid(Owner->GetActorLocation());
  const FIntPoint RawMin = Anchor + CustomBlockedCellsMin;
  const FIntPoint RawMax = Anchor + CustomBlockedCellsMax;

  OutMin.X = FMath::Min(RawMin.X, RawMax.X);
  OutMin.Y = FMath::Min(RawMin.Y, RawMax.Y);
  OutMax.X = FMath::Max(RawMin.X, RawMax.X);
  OutMax.Y = FMath::Max(RawMin.Y, RawMax.Y);

  return true;
}

