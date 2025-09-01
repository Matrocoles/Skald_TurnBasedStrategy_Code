#include "GridOverlayComponent.h"
#include "DrawDebugHelpers.h"

UGridOverlayComponent::UGridOverlayComponent() {
  PrimaryComponentTick.bCanEverTick = false;
}

void UGridOverlayComponent::BeginPlay() {
  Super::BeginPlay();

  Width = 50;
  Height = 50;
  Cells.Init(false, Width * Height);

  if (AActor *Owner = GetOwner()) {
    Origin = Owner->GetActorLocation();
  }
}

FIntPoint
UGridOverlayComponent::WorldToGrid(const FVector &WorldLocation) const {
  FVector Local = WorldLocation - Origin;
  int32 X = FMath::FloorToInt(Local.X / CellSize);
  int32 Y = FMath::FloorToInt(Local.Y / CellSize);
  return FIntPoint(X, Y);
}

FVector UGridOverlayComponent::GridToWorld(const FIntPoint &GridCoord) const {
  return Origin + FVector((GridCoord.X + 0.5f) * CellSize,
                          (GridCoord.Y + 0.5f) * CellSize, 0.f);
}

bool UGridOverlayComponent::IsValidGrid(const FIntPoint &GridCoord) const {
  return GridCoord.X >= 0 && GridCoord.X < Width && GridCoord.Y >= 0 &&
         GridCoord.Y < Height;
}

int32 UGridOverlayComponent::Index(const FIntPoint &GridCoord) const {
  return GridCoord.Y * Width + GridCoord.X;
}

bool UGridOverlayComponent::IsOccupied(const FIntPoint &GridCoord) const {
  if (!IsValidGrid(GridCoord)) {
    return false;
  }
  return Cells[Index(GridCoord)];
}

void UGridOverlayComponent::SetOccupied(const FIntPoint &GridCoord,
                                        bool bOccupied) {
  if (!IsValidGrid(GridCoord)) {
    return;
  }
  Cells[Index(GridCoord)] = bOccupied;
}

void UGridOverlayComponent::HighlightCell(const FIntPoint &GridCoord,
                                          const FColor &Color,
                                          float Duration) const {
  if (!IsValidGrid(GridCoord) || !GetWorld()) {
    return;
  }

  FVector Center = GridToWorld(GridCoord);
  FVector Extent(CellSize * 0.5f, CellSize * 0.5f, 10.f);
  DrawDebugSolidBox(GetWorld(), Center, Extent, Color, false, Duration);
}
