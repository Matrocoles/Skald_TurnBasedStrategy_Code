#include "GridOverlayActor.h"
#include "Components/SceneComponent.h"
#include "Engine/EngineTypes.h"
#include "Engine/World.h"
#include "GridObstacleActor.h"
#include "GridObstacleComponent.h"
#include "GridOverlayComponent.h"
#include "Math/RotationMatrix.h"
#include "CollisionQueryParams.h"
#include "Engine/HitResult.h"

namespace {
constexpr float kSmallHeightEpsilon = 0.01f;
}

AGridOverlayActor::AGridOverlayActor() {
  PrimaryActorTick.bCanEverTick = false;
  bReplicates = true;
  SetReplicateMovement(true);

  SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
  SetRootComponent(SceneRoot);

  GridComponent = CreateDefaultSubobject<UGridOverlayComponent>(TEXT("GridOverlay"));
}

void AGridOverlayActor::OnConstruction(const FTransform &Transform) {
  Super::OnConstruction(Transform);

  if (GridComponent) {
    GridComponent->ApplyRandomizedOrigin();
    GridComponent->RefreshOriginFromOwner(true);
  }
}

void AGridOverlayActor::OnRep_ReplicatedMovement() {
  Super::OnRep_ReplicatedMovement();

  if (GridComponent) {
    GridComponent->RefreshOriginFromOwner(true);
  }
}

void AGridOverlayActor::BeginPlay() {
  SpawnRandomObstacles();

  Super::BeginPlay();
}

void AGridOverlayActor::SpawnRandomObstacles() {
  if (!bSpawnRandomObstacles) {
    return;
  }

  if (!GridComponent) {
    return;
  }

  UWorld *World = GetWorld();
  if (!World) {
    return;
  }

  const EWorldType::Type WorldType = World->WorldType;
  const bool bIsEditorPreviewWorld =
      (WorldType == EWorldType::Editor || WorldType == EWorldType::EditorPreview ||
       WorldType == EWorldType::Inactive);

  if (bObstaclesSpawned && !bIsEditorPreviewWorld) {
    return;
  }

  if (!HasAuthority() && !bIsEditorPreviewWorld) {
    return;
  }

  if (bIsEditorPreviewWorld && SpawnedObstacleActors.Num() > 0) {
    ClearSpawnedObstacles();
  }

  const int32 GridWidth = GridComponent->GetWidth();
  const int32 GridHeight = GridComponent->GetHeight();
  if (GridWidth <= 0 || GridHeight <= 0) {
    return;
  }

  const int32 ReservedColumns = bRespectFighterSpawnLanes
                                     ? FMath::Clamp(ReservedSpawnColumnWidth, 0, GridWidth / 2)
                                     : 0;
  const int32 UsableColumns = GridWidth - (ReservedColumns * 2);
  if (UsableColumns <= 0) {
    return;
  }

  const TArray<const FGridObstacleSpawnList *> ObstacleLists = {
      &LargeObstacles, &MediumObstacles, &SmallObstacles, &MiscObstacles,
      &EnvironmentTrees, &EnvironmentCliffs};

  bool bHasSpawnCandidates = false;
  for (const FGridObstacleSpawnList *List : ObstacleLists) {
    if (List && List->HasCandidates()) {
      bHasSpawnCandidates = true;
      break;
    }
  }

  if (!bHasSpawnCandidates) {
    return;
  }

  const int32 MaxCells = UsableColumns * GridHeight;
  if (MaxCells <= 0) {
    return;
  }

  FRandomStream RandomStream;
  if (bOverrideObstacleSpawnSeed) {
    RandomStream.Initialize(ObstacleSpawnSeed);
  } else {
    RandomStream.GenerateNewSeed();
  }

  struct FObstacleSpawnPlan {
    const FGridObstacleSpawnList *Settings = nullptr;
    int32 Count = 0;
  };

  TArray<FObstacleSpawnPlan> SpawnPlans;
  SpawnPlans.Reserve(ObstacleLists.Num());
  for (const FGridObstacleSpawnList *List : ObstacleLists) {
    FObstacleSpawnPlan Plan;
    Plan.Settings = List;
    SpawnPlans.Add(Plan);
  }

  int32 RemainingCells = MaxCells;
  int32 TotalSpawnCount = 0;
  for (FObstacleSpawnPlan &Plan : SpawnPlans) {
    if (!Plan.Settings || !Plan.Settings->HasCandidates() || RemainingCells <= 0) {
      Plan.Count = 0;
      continue;
    }

    const int32 ClampedMin = FMath::Clamp(Plan.Settings->MinObstacleCount, 0, RemainingCells);
    const int32 ClampedMax = FMath::Clamp(Plan.Settings->MaxObstacleCount, ClampedMin, RemainingCells);
    if (ClampedMax <= 0) {
      Plan.Count = 0;
      continue;
    }

    const int32 Count = RandomStream.RandRange(ClampedMin, ClampedMax);
    Plan.Count = Count;
    RemainingCells = FMath::Max(RemainingCells - Count, 0);
    TotalSpawnCount += Count;
  }

  if (TotalSpawnCount <= 0) {
    return;
  }

  SpawnedObstacleActors.Reset();
  SpawnedObstacleCells.Empty();

  const float ClampedMinHeightOffset = FMath::Min(MinObstacleHeightOffset, MaxObstacleHeightOffset);
  const float ClampedMaxHeightOffset = FMath::Max(MinObstacleHeightOffset, MaxObstacleHeightOffset);

  TSet<FIntPoint> CandidateCells;
  CandidateCells.Reserve(TotalSpawnCount);

  const int32 MaxIterations = TotalSpawnCount * 10;
  int32 IterationCount = 0;
  while (CandidateCells.Num() < TotalSpawnCount && IterationCount < MaxIterations) {
    IterationCount++;

    const int32 MinCellX = ReservedColumns;
    const int32 MaxCellX = GridWidth - ReservedColumns - 1;
    if (MaxCellX < MinCellX) {
      break;
    }

    const int32 CellX = RandomStream.RandRange(MinCellX, MaxCellX);
    const int32 CellY = RandomStream.RandRange(0, GridHeight - 1);
    const FIntPoint Cell(CellX, CellY);

    if (!GridComponent->IsCellInBounds(Cell)) {
      continue;
    }

    if (ReservedColumns > 0) {
      const bool bInAttackerLane = CellX < ReservedColumns;
      const bool bInDefenderLane = CellX >= GridWidth - ReservedColumns;
      if (bInAttackerLane || bInDefenderLane) {
        continue;
      }
    }

    if (CandidateCells.Contains(Cell)) {
      continue;
    }

    if (GridComponent->IsOccupied(Cell) || GridComponent->IsObscured(Cell)) {
      continue;
    }

    CandidateCells.Add(Cell);
  }

  if (CandidateCells.Num() == 0) {
    return;
  }

  if (CandidateCells.Num() < TotalSpawnCount) {
    int32 CellsRemaining = CandidateCells.Num();
    for (FObstacleSpawnPlan &Plan : SpawnPlans) {
      if (CellsRemaining <= 0) {
        Plan.Count = 0;
        continue;
      }

      Plan.Count = FMath::Clamp(Plan.Count, 0, CellsRemaining);
      CellsRemaining -= Plan.Count;
    }
  }

  TotalSpawnCount = 0;
  for (const FObstacleSpawnPlan &Plan : SpawnPlans) {
    TotalSpawnCount += Plan.Count;
  }

  if (TotalSpawnCount <= 0) {
    return;
  }

  TArray<FIntPoint> AvailableCells = CandidateCells.Array();
  TSet<FIntPoint> ReservedCells;

  const auto ComputeBlockedCells = [GridComponent](AGridObstacleActor *Obstacle,
                                                   TArray<FIntPoint> &OutCells) -> bool {
    if (!Obstacle || !GridComponent) {
      return false;
    }

    FIntPoint Min = FIntPoint::ZeroValue;
    FIntPoint Max = FIntPoint::ZeroValue;
    bool bHasCustomFootprint = false;

    if (const UGridObstacleComponent *ObstacleComponent =
            Obstacle->FindComponentByClass<UGridObstacleComponent>()) {
      bHasCustomFootprint = ObstacleComponent->GetCustomGridFootprint(GridComponent, Min, Max);
    }

    if (!bHasCustomFootprint) {
      const FBox Bounds = Obstacle->GetComponentsBoundingBox(true);
      if (Bounds.IsValid) {
        const FIntPoint RawMin = GridComponent->WorldToGrid(Bounds.Min);
        const FIntPoint RawMax = GridComponent->WorldToGrid(Bounds.Max);
        Min.X = FMath::Min(RawMin.X, RawMax.X);
        Min.Y = FMath::Min(RawMin.Y, RawMax.Y);
        Max.X = FMath::Max(RawMin.X, RawMax.X);
        Max.Y = FMath::Max(RawMin.Y, RawMax.Y);
      } else {
        const FIntPoint Anchor = GridComponent->WorldToGrid(Obstacle->GetActorLocation());
        Min = Anchor;
        Max = Anchor;
      }
    }

    bool bAllCellsValid = true;
    for (int32 Y = Min.Y; Y <= Max.Y; ++Y) {
      for (int32 X = Min.X; X <= Max.X; ++X) {
        const FIntPoint Cell(X, Y);
        if (!GridComponent->IsCellInBounds(Cell)) {
          bAllCellsValid = false;
          break;
        }
        OutCells.Add(Cell);
      }
      if (!bAllCellsValid) {
        break;
      }
    }

    if (!bAllCellsValid) {
      OutCells.Reset();
      return false;
    }

    return OutCells.Num() > 0;
  };

  for (const FObstacleSpawnPlan &Plan : SpawnPlans) {
    if (!Plan.Settings || !Plan.Settings->HasCandidates()) {
      continue;
    }

    const TArray<TSubclassOf<AGridObstacleActor>> &Candidates = Plan.Settings->ObstacleCandidates;
    if (Candidates.Num() == 0) {
      continue;
    }

    int32 SuccessfulSpawnCount = 0;
    int32 Attempts = 0;
    const int32 MaxAttempts = FMath::Max(AvailableCells.Num() * 4, 0);

    while (SuccessfulSpawnCount < Plan.Count && AvailableCells.Num() > 0 && Attempts < MaxAttempts) {
      Attempts++;

      const int32 CellIndex = RandomStream.RandRange(0, AvailableCells.Num() - 1);
      const FIntPoint Cell = AvailableCells[CellIndex];
      AvailableCells.RemoveAtSwap(CellIndex);

      const int32 ClassIndex = RandomStream.RandRange(0, Candidates.Num() - 1);
      const TSubclassOf<AGridObstacleActor> ObstacleClass = Candidates[ClassIndex];
      if (!ObstacleClass) {
        continue;
      }

      const FVector CellLocation = GridComponent->GridToWorld(Cell);
      const FVector TraceOffset = FVector::UpVector * FMath::Max(ObstacleTraceHeight, kSmallHeightEpsilon);
      const FVector TraceStart = CellLocation + TraceOffset;
      const FVector TraceEnd = CellLocation - TraceOffset;

      FHitResult HitResult;
      FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SpawnRandomObstacles), false, this);
      const bool bHitGround =
          World->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, ECC_WorldStatic, QueryParams);

      FVector SpawnLocation = bHitGround ? HitResult.Location : CellLocation;
      const float HeightOffset = RandomStream.FRandRange(ClampedMinHeightOffset, ClampedMaxHeightOffset);
      SpawnLocation.Z += HeightOffset;

      FRotator SpawnRotation = FRotator::ZeroRotator;
      if (bHitGround) {
        const FVector SurfaceNormal =
            HitResult.Normal.IsNearlyZero() ? FVector::UpVector : HitResult.Normal.GetSafeNormal();
        SpawnRotation = FRotationMatrix::MakeFromZ(SurfaceNormal).Rotator();
      }

      FActorSpawnParameters SpawnParams;
      SpawnParams.Owner = this;
      SpawnParams.SpawnCollisionHandlingOverride =
          ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

      TArray<FIntPoint> BlockedCells;
      if (AGridObstacleActor *SpawnedActor =
              World->SpawnActor<AGridObstacleActor>(ObstacleClass, SpawnLocation, SpawnRotation, SpawnParams)) {
        const bool bHasBlockedCells = ComputeBlockedCells(SpawnedActor, BlockedCells);

        bool bHasConflict = !bHasBlockedCells;
        if (!bHasConflict) {
          for (const FIntPoint &BlockedCell : BlockedCells) {
            if (ReservedCells.Contains(BlockedCell) || GridComponent->IsOccupied(BlockedCell) ||
                GridComponent->IsObscured(BlockedCell)) {
              bHasConflict = true;
              break;
            }
          }
        }

        if (bHasConflict) {
          SpawnedActor->Destroy();
          continue;
        }

        for (const FIntPoint &BlockedCell : BlockedCells) {
          ReservedCells.Add(BlockedCell);
          AvailableCells.RemoveSingleSwap(BlockedCell);
        }

        SpawnedObstacleActors.Add(SpawnedActor);
        for (const FIntPoint &BlockedCell : BlockedCells) {
          SpawnedObstacleCells.Add(BlockedCell);
        }

        SuccessfulSpawnCount++;
      }
    }
  }

  bObstaclesSpawned = SpawnedObstacleActors.Num() > 0 && !bIsEditorPreviewWorld;
}

void AGridOverlayActor::ClearSpawnedObstacles() {
  if (GridComponent && SpawnedObstacleCells.Num() > 0) {
    for (const FIntPoint &Cell : SpawnedObstacleCells) {
      GridComponent->ClearStaticObstacleAtCell(Cell);
    }
  }

  for (const TObjectPtr<AGridObstacleActor> &ObstaclePtr : SpawnedObstacleActors) {
    if (AGridObstacleActor *Obstacle = ObstaclePtr.Get()) {
      if (Obstacle->IsValidLowLevelFast()) {
        Obstacle->Destroy();
      }
    }
  }

  SpawnedObstacleActors.Reset();
  SpawnedObstacleCells.Empty();
  bObstaclesSpawned = false;
}

