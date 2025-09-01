#include "GridOverlayComponent.h"
#include "Containers/Queue.h"
#include "DrawDebugHelpers.h"
#include "FighterPawn.h"

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
                                          const FColor &Color, float Duration,
                                          bool bPersistent) const {
  if (!IsValidGrid(GridCoord) || !GetWorld()) {
    return;
  }

  FVector Center = GridToWorld(GridCoord);
  FVector Extent(CellSize * 0.5f, CellSize * 0.5f, 10.f);
  DrawDebugSolidBox(GetWorld(), Center, Extent, Color, bPersistent, Duration);
}

void UGridOverlayComponent::ClearHighlights() const {
  if (GetWorld()) {
    FlushPersistentDebugLines(GetWorld());
  }
}

void UGridOverlayComponent::HighlightMovement(AFighterPawn *Fighter) {
  if (!Fighter) {
    return;
  }

  ClearHighlights();

  FIntPoint Origin = WorldToGrid(Fighter->GetActorLocation());
  const int32 Range = Fighter->Stats.Movement;

  TSet<FIntPoint> Visited;
  TQueue<TPair<FIntPoint, int32>> Frontier;
  Visited.Add(Origin);
  Frontier.Enqueue(TPair<FIntPoint, int32>(Origin, 0));

  while (!Frontier.IsEmpty()) {
    TPair<FIntPoint, int32> Node;
    Frontier.Dequeue(Node);
    const FIntPoint Cell = Node.Key;
    const int32 Distance = Node.Value;

    if (Distance > 0) {
      HighlightCell(Cell, FColor::Green, 0.f, true);
    }

    if (Distance >= Range) {
      continue;
    }

    static const FIntPoint Directions[4] = {FIntPoint(1, 0), FIntPoint(-1, 0),
                                            FIntPoint(0, 1), FIntPoint(0, -1)};

    for (const FIntPoint &Dir : Directions) {
      const FIntPoint Next = Cell + Dir;
      if (!IsValidGrid(Next) || IsOccupied(Next) || Visited.Contains(Next)) {
        continue;
      }
      Visited.Add(Next);
      Frontier.Enqueue(TPair<FIntPoint, int32>(Next, Distance + 1));
    }
  }
}

void UGridOverlayComponent::HighlightAttack(AFighterPawn *Fighter) {
  if (!Fighter) {
    return;
  }

  ClearHighlights();

  FIntPoint Origin = WorldToGrid(Fighter->GetActorLocation());
  const int32 Range = Fighter->Stats.AttackRange;

  TSet<FIntPoint> Visited;
  TQueue<TPair<FIntPoint, int32>> Frontier;
  Visited.Add(Origin);
  Frontier.Enqueue(TPair<FIntPoint, int32>(Origin, 0));

  while (!Frontier.IsEmpty()) {
    TPair<FIntPoint, int32> Node;
    Frontier.Dequeue(Node);
    const FIntPoint Cell = Node.Key;
    const int32 Distance = Node.Value;

    if (Distance > 0) {
      HighlightCell(Cell, FColor::Red, 0.f, true);
    }

    if (Distance >= Range) {
      continue;
    }

    static const FIntPoint Directions[4] = {FIntPoint(1, 0), FIntPoint(-1, 0),
                                            FIntPoint(0, 1), FIntPoint(0, -1)};

    for (const FIntPoint &Dir : Directions) {
      const FIntPoint Next = Cell + Dir;
      if (!IsValidGrid(Next) || Visited.Contains(Next)) {
        continue;
      }
      Visited.Add(Next);
      Frontier.Enqueue(TPair<FIntPoint, int32>(Next, Distance + 1));
    }
  }
}
