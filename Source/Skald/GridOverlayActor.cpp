#include "GridOverlayActor.h"
#include "Components/SceneComponent.h"
#include "Engine/EngineTypes.h"
#include "Engine/World.h"
#include "GridObstacleActor.h"
#include "GridOverlayComponent.h"
#include "CollisionQueryParams.h"
#include "Net/UnrealNetwork.h"

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

void AGridOverlayActor::BeginPlay() {
  if (HasAuthority()) {
    if (GridComponent) {
      GridComponent->ApplyRandomizedOrigin();
    }

    ReplicatedGridOrigin = GetActorLocation();
  }

  SpawnRandomObstacles();

  Super::BeginPlay();
}

void AGridOverlayActor::OnRep_ReplicatedMovement() {
  Super::OnRep_ReplicatedMovement();

  if (GridComponent) {
    GridComponent->RefreshOriginFromOwner(true);
  }
}

void AGridOverlayActor::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty> &OutLifetimeProps) const {
  Super::GetLifetimeReplicatedProps(OutLifetimeProps);

  DOREPLIFETIME(AGridOverlayActor, ReplicatedGridOrigin);
}

void AGridOverlayActor::OnRep_RandomizedGridOrigin() {
  SetActorLocation(ReplicatedGridOrigin, false, nullptr,
                   ETeleportType::TeleportPhysics);

  if (GridComponent) {
    GridComponent->RefreshOriginFromOwner(true);
  }
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

  if (ObstacleCandidates.Num() == 0) {
    return;
  }

  const int32 MaxCells = GridWidth * GridHeight;
  const int32 ClampedMin = FMath::Clamp(MinObstacleCount, 0, MaxCells);
  const int32 ClampedMax = FMath::Clamp(MaxObstacleCount, ClampedMin, MaxCells);
  if (ClampedMax <= 0) {
    return;
  }

  FRandomStream RandomStream;
  if (bOverrideObstacleSpawnSeed) {
    RandomStream.Initialize(ObstacleSpawnSeed);
  } else {
    RandomStream.GenerateNewSeed();
  }

  const int32 ObstaclesToSpawn = RandomStream.RandRange(ClampedMin, ClampedMax);
  if (ObstaclesToSpawn <= 0) {
    return;
  }

  SpawnedObstacleActors.Reset();
  SpawnedObstacleCells.Empty();

  const float ClampedMinHeightOffset = FMath::Min(MinObstacleHeightOffset, MaxObstacleHeightOffset);
  const float ClampedMaxHeightOffset = FMath::Max(MinObstacleHeightOffset, MaxObstacleHeightOffset);

  TSet<FIntPoint> CandidateCells;
  CandidateCells.Reserve(ObstaclesToSpawn);

  const int32 MaxIterations = ObstaclesToSpawn * 10;
  int32 IterationCount = 0;
  while (CandidateCells.Num() < ObstaclesToSpawn && IterationCount < MaxIterations) {
    IterationCount++;

    const int32 CellX = RandomStream.RandRange(0, GridWidth - 1);
    const int32 CellY = RandomStream.RandRange(0, GridHeight - 1);
    const FIntPoint Cell(CellX, CellY);

    if (!GridComponent->IsCellInBounds(Cell)) {
      continue;
    }

    CandidateCells.Add(Cell);
  }

  for (const FIntPoint &Cell : CandidateCells) {
    const int32 ClassIndex = RandomStream.RandRange(0, ObstacleCandidates.Num() - 1);
    const TSubclassOf<AGridObstacleActor> ObstacleClass = ObstacleCandidates[ClassIndex];
    if (!ObstacleClass) {
      continue;
    }

    const FVector CellLocation = GridComponent->GridToWorld(Cell);
    const FVector TraceOffset = FVector::UpVector * FMath::Max(ObstacleTraceHeight, kSmallHeightEpsilon);
    const FVector TraceStart = CellLocation + TraceOffset;
    const FVector TraceEnd = CellLocation - TraceOffset;

    FHitResult HitResult;
    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SpawnRandomObstacles), false, this);
    const bool bHitGround = World->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, ECC_WorldStatic, QueryParams);

    FVector SpawnLocation = bHitGround ? HitResult.Location : CellLocation;
    const float HeightOffset = RandomStream.FRandRange(ClampedMinHeightOffset, ClampedMaxHeightOffset);
    SpawnLocation.Z += HeightOffset;

    const FRotator SpawnRotation = bHitGround ? HitResult.Normal.Rotation() : FRotator::ZeroRotator;

    FActorSpawnParameters SpawnParams;
    SpawnParams.Owner = this;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

    if (AGridObstacleActor *SpawnedActor = World->SpawnActor<AGridObstacleActor>(ObstacleClass, SpawnLocation, SpawnRotation, SpawnParams)) {
      SpawnedObstacleActors.Add(SpawnedActor);
      SpawnedObstacleCells.Add(Cell);
    }
  }

  bObstaclesSpawned = SpawnedObstacleActors.Num() > 0 && !bIsEditorPreviewWorld;
}

void AGridOverlayActor::ClearSpawnedObstacles() {
  for (const TWeakObjectPtr<AGridObstacleActor> &ObstaclePtr : SpawnedObstacleActors) {
    if (ObstaclePtr.IsValid()) {
      AGridObstacleActor *Obstacle = ObstaclePtr.Get();
      if (Obstacle && Obstacle->IsValidLowLevelFast()) {
        Obstacle->Destroy();
      }
    }
  }

  SpawnedObstacleActors.Reset();
  SpawnedObstacleCells.Empty();
  bObstaclesSpawned = false;
}

