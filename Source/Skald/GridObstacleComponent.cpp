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

void UGridObstacleComponent::OnComponentDestroyed(bool bDestroyingHierarchy) {
  if (UWorld *World = GetWorld()) {
    if (UGridOverlayComponent *Grid =
            Skald::GridOverlay::FindActiveGridOverlay(World)) {
      Grid->UnregisterObstacle(this);
    }
  }

  Super::OnComponentDestroyed(bDestroyingHierarchy);
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

  const FBox Bounds = Owner->GetComponentsBoundingBox(true);
  const FVector AnchorLocation = Bounds.IsValid
                                     ? Bounds.GetCenter()
                                     : Owner->GetActorLocation();

  const FIntPoint Anchor = Grid->WorldToGrid(AnchorLocation);
  const FIntPoint RawMin = Anchor + CustomBlockedCellsMin;
  const FIntPoint RawMax = Anchor + CustomBlockedCellsMax;

  OutMin.X = FMath::Min(RawMin.X, RawMax.X);
  OutMin.Y = FMath::Min(RawMin.Y, RawMax.Y);
  OutMax.X = FMath::Max(RawMin.X, RawMax.X);
  OutMax.Y = FMath::Max(RawMin.Y, RawMax.Y);

  return true;
}

bool UGridObstacleComponent::HasCustomTraceHalfHeight() const {
  return ObstacleTraceHalfHeight > 0.f;
}

float UGridObstacleComponent::GetTraceHalfHeightOrDefault(float DefaultTraceHalfHeight) const {
  return HasCustomTraceHalfHeight() ? ObstacleTraceHalfHeight : DefaultTraceHalfHeight;
}

