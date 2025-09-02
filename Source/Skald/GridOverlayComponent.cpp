#include "GridOverlayComponent.h"
#include "Containers/Queue.h"
#include "DrawDebugHelpers.h"
#include "FighterPawn.h"
#include "GridObstacleComponent.h"
#include "Engine/World.h"
#include "Engine/EngineTypes.h"

UGridOverlayComponent::UGridOverlayComponent() {
  PrimaryComponentTick.bCanEverTick = false;
}

void UGridOverlayComponent::BeginPlay() {
  Super::BeginPlay();

  Width = 50;
  Height = 50;
  Cells.Init(false, Width * Height);
  ObscuredCells.Init(false, Width * Height);

  if (AActor *Owner = GetOwner()) {
    Origin = Owner->GetActorLocation();
  }

  CellHeights.Init(Origin.Z, Width * Height);

  if (UWorld *World = GetWorld()) {
    for (int32 Y = 0; Y < Height; ++Y) {
      for (int32 X = 0; X < Width; ++X) {
        const int32 Idx = Index(FIntPoint(X, Y));
        FVector Start =
            Origin + FVector((X + 0.5f) * CellSize, (Y + 0.5f) * CellSize, 10000.f);
        FVector End = Start - FVector(0.f, 0.f, 20000.f);
        FHitResult Hit;
        if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic)) {
          CellHeights[Idx] = Hit.Location.Z;
        } else {
          CellHeights[Idx] = Origin.Z;
        }
      }
    }
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
  FVector World = Origin + FVector((GridCoord.X + 0.5f) * CellSize,
                                   (GridCoord.Y + 0.5f) * CellSize, 0.f);
  if (IsValidGrid(GridCoord) && CellHeights.IsValidIndex(Index(GridCoord))) {
    World.Z = CellHeights[Index(GridCoord)];
  }
  return World;
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

bool UGridOverlayComponent::IsObscured(const FIntPoint &GridCoord) const {
  if (!IsValidGrid(GridCoord)) {
    return false;
  }
  return ObscuredCells[Index(GridCoord)];
}

float UGridOverlayComponent::GetCellHeight(const FIntPoint &GridCoord) const {
  if (!IsValidGrid(GridCoord)) {
    return Origin.Z;
  }
  return CellHeights[Index(GridCoord)];
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
  Center.Z += Extent.Z; // raise the box above the ground
  DrawDebugSolidBox(GetWorld(), Center, Extent, Color, bPersistent, Duration);
}

void UGridOverlayComponent::ClearHighlights() const {
  if (GetWorld()) {
    FlushPersistentDebugLines(GetWorld());
  }
}

void UGridOverlayComponent::RegisterObstacle(UGridObstacleComponent *Obstacle) {
  if (!Obstacle) {
    return;
  }
  Obstacles.Add(Obstacle);
  if (AActor *Owner = Obstacle->GetOwner()) {
    const FBox Bounds = Owner->GetComponentsBoundingBox(true);
    const FIntPoint Min = WorldToGrid(Bounds.Min);
    const FIntPoint Max = WorldToGrid(Bounds.Max);
    for (int32 Y = Min.Y; Y <= Max.Y; ++Y) {
      for (int32 X = Min.X; X <= Max.X; ++X) {
        const FIntPoint Cell(X, Y);
        if (!IsValidGrid(Cell)) {
          continue;
        }
        const int32 Idx = Index(Cell);
        if (Obstacle->bBlocksMovement && !Obstacle->bClimbable) {
          Cells[Idx] = true;
        }
        ObscuredCells[Idx] = true;
        if (Obstacle->bClimbable) {
          CellHeights[Idx] = Bounds.Max.Z;
          ObscuredCells[Idx] = false;
        }
      }
    }
  }
}

void UGridOverlayComponent::HighlightMovement(AFighterPawn *Fighter) {
  if (!Fighter) {
    return;
  }

  ClearHighlights();

  FIntPoint StartCell = WorldToGrid(Fighter->GetActorLocation());
  const int32 Range = Fighter->Stats.Movement;

  TSet<FIntPoint> Visited;
  TQueue<TPair<FIntPoint, int32>> Frontier;
  Visited.Add(StartCell);
  Frontier.Enqueue(TPair<FIntPoint, int32>(StartCell, 0));

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

  FIntPoint StartCell = WorldToGrid(Fighter->GetActorLocation());
  const int32 Range = Fighter->Stats.AttackRange;

  TSet<FIntPoint> Visited;
  TQueue<TPair<FIntPoint, int32>> Frontier;
  Visited.Add(StartCell);
  Frontier.Enqueue(TPair<FIntPoint, int32>(StartCell, 0));

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
