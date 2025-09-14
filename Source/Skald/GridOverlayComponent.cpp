#include "GridOverlayComponent.h"
#include "Containers/Queue.h"
#include "DrawDebugHelpers.h"
#include "Engine/EngineTypes.h"
#include "Engine/World.h"
#include "FighterPawn.h"
#include "GridObstacleComponent.h"
#include "Landscape.h"
#include "LandscapeComponent.h"

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
        FVector Start = Origin + FVector((X + 0.5f) * CellSize,
                                         (Y + 0.5f) * CellSize, 10000.f);
        FVector End = Start - FVector(0.f, 0.f, 20000.f);
        FHitResult Hit;
        if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic)) {
          CellHeights[Idx] = Hit.Location.Z;
          if (Cast<ULandscapeComponent>(Hit.GetComponent())) {
            HandleLandscapeHit(Hit, FIntPoint(X, Y), Idx);
          }
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
  World.Z = GetCellHeight(GridCoord);
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

bool UGridOverlayComponent::HasLineOfSight(const FIntPoint &Start,
                                           const FIntPoint &End) const {
  FIntPoint Current = Start;
  const int32 x1 = End.X;
  const int32 y1 = End.Y;
  int32 dx = FMath::Abs(x1 - Current.X);
  int32 sx = Current.X < x1 ? 1 : -1;
  int32 dy = -FMath::Abs(y1 - Current.Y);
  int32 sy = Current.Y < y1 ? 1 : -1;
  int32 err = dx + dy;

  while (true) {
    if (Current != Start && IsObscured(Current)) {
      return false;
    }
    if (Current.X == x1 && Current.Y == y1) {
      break;
    }
    int32 e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      Current.X += sx;
    }
    if (e2 <= dx) {
      err += dx;
      Current.Y += sy;
    }
  }
  return true;
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
  if (GetWorld() && bFlushAllPersistentOnClear) {
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
        if (Obstacle->bClimbable) {
          CellHeights[Idx] = Bounds.Max.Z;
          Cells[Idx] = false;
          ObscuredCells[Idx] = false;
        } else {
          if (Obstacle->bBlocksMovement) {
            Cells[Idx] = true;
          }
          if (Obstacle->bBlocksLineOfSight) {
            ObscuredCells[Idx] = true;
          }
        }
      }
    }
  }
}

void UGridOverlayComponent::HandleLandscapeHit(const FHitResult &Hit,
                                               const FIntPoint &Cell,
                                               int32 CellIndex) {
  if (!bTreatLandscapeAsObstacle) {
    return;
  }
  const FVector Normal = Hit.Normal.GetSafeNormal();
  const float Slope = FMath::RadiansToDegrees(
      FMath::Acos(FVector::DotProduct(Normal, FVector::UpVector)));
  if (Slope >= LandscapeSlopeThreshold) {
    Cells[CellIndex] = true;
    ObscuredCells[CellIndex] = true;
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
      HighlightCell(Cell, FColor::Green, 0.f, false);
    }

    if (Distance >= Range) {
      continue;
    }

    static const FIntPoint Directions[4] = {FIntPoint(1, 0), FIntPoint(-1, 0),
                                            FIntPoint(0, 1), FIntPoint(0, -1)};

    for (const FIntPoint &Dir : Directions) {
      const FIntPoint Next = Cell + Dir;
      if (!IsValidGrid(Next) || IsOccupied(Next) || IsObscured(Next) ||
          Visited.Contains(Next)) {
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

  for (int32 Dy = -Range; Dy <= Range; ++Dy) {
    for (int32 Dx = -Range; Dx <= Range; ++Dx) {
      if (FMath::Abs(Dx) + FMath::Abs(Dy) > Range) {
        continue;
      }
      const FIntPoint Target = StartCell + FIntPoint(Dx, Dy);
      if (!IsValidGrid(Target) || Target == StartCell) {
        continue;
      }

      if (HasLineOfSight(StartCell, Target)) {
        HighlightCell(Target, FColor::Red, 0.f, false);
      }
    }
  }
}
