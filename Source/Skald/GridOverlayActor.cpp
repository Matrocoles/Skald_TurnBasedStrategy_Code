#include "GridOverlayActor.h"
#include "Algo/RandomShuffle.h"
#include "CollisionQueryParams.h"
#include "Components/SceneComponent.h"
#include "Engine/EngineTypes.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "GridObstacleActor.h"
#include "GridObstacleComponent.h"
#include "GridOverlayComponent.h"
#include "Math/RotationMatrix.h"

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

  TArray<FIntPoint> PotentialAnchors;
  PotentialAnchors.Reserve(MaxCells);
  for (int32 CellY = 0; CellY < GridHeight; ++CellY) {
    for (int32 CellX = ReservedColumns; CellX < GridWidth - ReservedColumns; ++CellX) {
      const FIntPoint Cell(CellX, CellY);
      if (!GridComponent->IsCellInBounds(Cell)) {
        continue;
      }
      if (GridComponent->IsOccupied(Cell) || GridComponent->IsObscured(Cell)) {
        continue;
      }
      PotentialAnchors.Add(Cell);
    }
  }

  if (PotentialAnchors.Num() == 0) {
    return;
  }

  if (PotentialAnchors.Num() > 1) {
    for (int32 Index = PotentialAnchors.Num() - 1; Index > 0; --Index) {
      const int32 SwapIndex = RandomStream.RandRange(0, Index);
      if (Index != SwapIndex) {
        PotentialAnchors.Swap(Index, SwapIndex);
      }
    }
  }

  if (PotentialAnchors.Num() < TotalSpawnCount) {
    int32 CellsRemaining = PotentialAnchors.Num();
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

  struct FObstacleFootprint {
    FIntPoint MinOffset = FIntPoint::ZeroValue;
    FIntPoint MaxOffset = FIntPoint::ZeroValue;
  };

  TMap<const UClass *, FObstacleFootprint> FootprintCache;
  FootprintCache.Reserve(32);

  auto GetFootprint = [&](const TSubclassOf<AGridObstacleActor> &ObstacleClass)
                          -> const FObstacleFootprint & {
    static const FObstacleFootprint DefaultFootprint;
    const UClass *ClassPtr = ObstacleClass.Get();
    if (!ClassPtr) {
      return DefaultFootprint;
    }

    if (const FObstacleFootprint *ExistingFootprint = FootprintCache.Find(ClassPtr)) {
      return *ExistingFootprint;
    }

    FObstacleFootprint ComputedFootprint;
    if (const AGridObstacleActor *DefaultActor = ClassPtr->GetDefaultObject<AGridObstacleActor>()) {
      if (const UGridObstacleComponent *ObstacleComponent =
              DefaultActor->FindComponentByClass<UGridObstacleComponent>()) {
        if (ObstacleComponent->bOverrideBlockedCells) {
          ComputedFootprint.MinOffset.X = FMath::Min(ObstacleComponent->CustomBlockedCellsMin.X,
                                                    ObstacleComponent->CustomBlockedCellsMax.X);
          ComputedFootprint.MinOffset.Y = FMath::Min(ObstacleComponent->CustomBlockedCellsMin.Y,
                                                    ObstacleComponent->CustomBlockedCellsMax.Y);
          ComputedFootprint.MaxOffset.X = FMath::Max(ObstacleComponent->CustomBlockedCellsMin.X,
                                                    ObstacleComponent->CustomBlockedCellsMax.X);
          ComputedFootprint.MaxOffset.Y = FMath::Max(ObstacleComponent->CustomBlockedCellsMin.Y,
                                                    ObstacleComponent->CustomBlockedCellsMax.Y);
        }
      }
    }

    return FootprintCache.Add(ClassPtr, ComputedFootprint);
  };

  TArray<FIntPoint> FootprintCells;
  FootprintCells.Reserve(8);

  TSet<FIntPoint> ReservedFootprintCells;
  ReservedFootprintCells.Reserve(TotalSpawnCount * 4);

  for (const FObstacleSpawnPlan &Plan : SpawnPlans) {
    if (!Plan.Settings || !Plan.Settings->HasCandidates()) {
      continue;
    }

    const TArray<TSubclassOf<AGridObstacleActor>> &Candidates = Plan.Settings->ObstacleCandidates;
    if (Candidates.Num() == 0) {
      continue;
    }

    for (int32 SpawnIndex = 0; SpawnIndex < Plan.Count; ++SpawnIndex) {
      if (ReservedFootprintCells.Num() >= PotentialAnchors.Num()) {
        break;
      }

      const int32 ClassIndex = RandomStream.RandRange(0, Candidates.Num() - 1);
      const TSubclassOf<AGridObstacleActor> ObstacleClass = Candidates[ClassIndex];
      if (!ObstacleClass) {
        continue;
      }

      const FObstacleFootprint &Footprint = GetFootprint(ObstacleClass);

      bool bSpawnedObstacle = false;
      for (const FIntPoint &AnchorCell : PotentialAnchors) {
        FootprintCells.Reset();

        const int32 MinCellX = AnchorCell.X + Footprint.MinOffset.X;
        const int32 MaxCellX = AnchorCell.X + Footprint.MaxOffset.X;
        const int32 MinCellY = AnchorCell.Y + Footprint.MinOffset.Y;
        const int32 MaxCellY = AnchorCell.Y + Footprint.MaxOffset.Y;

        for (int32 CellX = MinCellX; CellX <= MaxCellX; ++CellX) {
          for (int32 CellY = MinCellY; CellY <= MaxCellY; ++CellY) {
            FootprintCells.Emplace(CellX, CellY);
          }
        }

        if (!FootprintCells.Contains(AnchorCell)) {
          FootprintCells.Add(AnchorCell);
        }

        bool bBlocked = false;
        for (const FIntPoint &FootprintCell : FootprintCells) {
          if (!GridComponent->IsCellInBounds(FootprintCell)) {
            bBlocked = true;
            break;
          }

          if (ReservedColumns > 0) {
            const bool bInAttackerLane = FootprintCell.X < ReservedColumns;
            const bool bInDefenderLane = FootprintCell.X >= GridWidth - ReservedColumns;
            if (bInAttackerLane || bInDefenderLane) {
              bBlocked = true;
              break;
            }
          }

          if (ReservedFootprintCells.Contains(FootprintCell)) {
            bBlocked = true;
            break;
          }

          if (GridComponent->IsOccupied(FootprintCell) || GridComponent->IsObscured(FootprintCell)) {
            bBlocked = true;
            break;
          }
        }

        if (bBlocked) {
          continue;
        }

        const FVector CellLocation = GridComponent->GridToWorld(AnchorCell);
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
          const FVector SurfaceNormal = HitResult.Normal.IsNearlyZero()
                                            ? FVector::UpVector
                                            : HitResult.Normal.GetSafeNormal();
          SpawnRotation = FRotationMatrix::MakeFromZ(SurfaceNormal).Rotator();
        }

        FActorSpawnParameters SpawnParams;
        SpawnParams.Owner = this;
        SpawnParams.SpawnCollisionHandlingOverride =
            ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

        if (AGridObstacleActor *SpawnedActor =
                World->SpawnActor<AGridObstacleActor>(ObstacleClass, SpawnLocation, SpawnRotation, SpawnParams)) {
          SpawnedObstacleActors.Add(SpawnedActor);
          for (const FIntPoint &FootprintCell : FootprintCells) {
            ReservedFootprintCells.Add(FootprintCell);
            SpawnedObstacleCells.Add(FootprintCell);
          }
          bSpawnedObstacle = true;
          break;
        }
      }

      if (!bSpawnedObstacle) {
        break;
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

