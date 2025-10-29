#include "GridOverlayComponent.h"
#include "CollisionQueryParams.h"
#include "Containers/Queue.h"
#include "DrawDebugHelpers.h"
#include "Engine/EngineTypes.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FighterPawn.h"
#include "GridObstacleComponent.h"
#include "Components/DecalComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "Landscape.h"
#include "LandscapeComponent.h"
#include "Math/RotationMatrix.h"
#include "Math/Quat.h"
#include "UObject/UObjectGlobals.h"
#include "TimerManager.h"

namespace Skald
{
namespace GridOverlay
{
bool IsComponentFromVisibleLevel(const UGridOverlayComponent* GridComponent)
{
  if (!IsValid(GridComponent))
  {
    return false;
  }

  const AActor* Owner = GridComponent->GetOwner();
  if (!IsValid(Owner))
  {
    return false;
  }

  const ULevel* OwnerLevel = Owner->GetLevel();
  if (!OwnerLevel)
  {
    return false;
  }

  if (OwnerLevel->bIsVisible)
  {
    return true;
  }

  const UWorld* World = Owner->GetWorld();
  if (!World)
  {
    return false;
  }

  for (const ULevelStreaming* StreamingLevel : World->GetStreamingLevels())
  {
    if (!StreamingLevel)
    {
      continue;
    }

    if (StreamingLevel->GetLoadedLevel() == OwnerLevel)
    {
      if (StreamingLevel->GetShouldBeVisibleFlag() || StreamingLevel->IsLevelVisible())
      {
        return true;
      }

      return false;
    }
  }

  return false;
}

UGridOverlayComponent* FindActiveGridOverlay(UWorld* World, bool bPreferVisibleLevel)
{
  if (!World)
  {
    return nullptr;
  }

  UGridOverlayComponent* FallbackOverlay = nullptr;

  for (TActorIterator<AActor> It(World); It; ++It)
  {
    if (UGridOverlayComponent* Candidate = It->FindComponentByClass<UGridOverlayComponent>())
    {
      if (!FallbackOverlay)
      {
        FallbackOverlay = Candidate;
      }

      if (!bPreferVisibleLevel)
      {
        return Candidate;
      }

      if (IsComponentFromVisibleLevel(Candidate))
      {
        return Candidate;
      }
    }
  }

  return FallbackOverlay;
}
} // namespace GridOverlay
} // namespace Skald

UGridOverlayComponent::UGridOverlayComponent() {
  PrimaryComponentTick.bCanEverTick = false;
}

int32 UGridOverlayComponent::GetWidth() const { return Width; }

int32 UGridOverlayComponent::GetLength() const { return Length; }

int32 UGridOverlayComponent::GetHeight() const { return Height; }

float UGridOverlayComponent::GetCellSize() const { return CellSize; }

TArray<FGridCell3D> UGridOverlayComponent::GetGeneratedCells() const {
  return GeneratedCells;
}

FVector UGridOverlayComponent::GetOrigin() const { return Origin; }

void UGridOverlayComponent::ApplyRandomizedOrigin() {
  if (bHasRandomizedPlacement || !bRandomizePlacement) {
    return;
  }

  AActor *Owner = GetOwner();
  if (!Owner) {
    return;
  }

  if (!Owner->HasAuthority()) {
    RefreshOriginFromOwner(true);
    return;
  }

  const FVector BaseLocation = Owner->GetActorLocation();
  FVector TargetLocation = BaseLocation;

  if (RandomPlacementBounds.HasArea()) {
    const float MinX = FMath::Min(RandomPlacementBounds.Min.X, RandomPlacementBounds.Max.X);
    const float MaxX = FMath::Max(RandomPlacementBounds.Min.X, RandomPlacementBounds.Max.X);
    const float MinY = FMath::Min(RandomPlacementBounds.Min.Y, RandomPlacementBounds.Max.Y);
    const float MaxY = FMath::Max(RandomPlacementBounds.Min.Y, RandomPlacementBounds.Max.Y);
    const float RandomX = FMath::FRandRange(MinX, MaxX);
    const float RandomY = FMath::FRandRange(MinY, MaxY);
    TargetLocation.X = BaseLocation.X + RandomX;
    TargetLocation.Y = BaseLocation.Y + RandomY;
  } else if (RandomPlacementRadius > KINDA_SMALL_NUMBER) {
    const float Angle = FMath::FRandRange(0.f, 2.f * PI);
    const float Distance = RandomPlacementRadius * FMath::Sqrt(FMath::FRand());
    TargetLocation.X = BaseLocation.X + FMath::Cos(Angle) * Distance;
    TargetLocation.Y = BaseLocation.Y + FMath::Sin(Angle) * Distance;
  }

  if (PlacementTraceHeight > KINDA_SMALL_NUMBER) {
    if (UWorld *World = GetWorld()) {
      const FVector Start = TargetLocation + FVector(0.f, 0.f, PlacementTraceHeight);
      const FVector End = TargetLocation - FVector(0.f, 0.f, PlacementTraceHeight);
      FHitResult Hit;
      FCollisionQueryParams Params(SCENE_QUERY_STAT(GridOverlayPlacementTrace), false, Owner);
      if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params)) {
        TargetLocation.Z = Hit.Location.Z;
      }
    }
  }

  Owner->SetActorLocation(TargetLocation, false, nullptr, ETeleportType::TeleportPhysics);
  RefreshOriginFromOwner(true);
}

void UGridOverlayComponent::RefreshOriginFromOwner(bool bMarkPlacementRandomized) {
  if (AActor *Owner = GetOwner()) {
    const FTransform NewTransform = Owner->GetActorTransform();
    const FVector NewOrigin = NewTransform.GetLocation();
    const bool bOriginChanged = !Origin.Equals(NewOrigin, KINDA_SMALL_NUMBER);

    if (!CachedGridTransform.Equals(NewTransform, KINDA_SMALL_NUMBER)) {
      CachedGridTransform = NewTransform;
      CachedGridInverseTransform = CachedGridTransform.Inverse();
    }

    Origin = NewOrigin;
    if (bMarkPlacementRandomized && bRandomizePlacement) {
      bHasRandomizedPlacement = true;
    }
    if (bHasInitializedGrid && bOriginChanged) {
      RefreshGridDataFromOrigin();
    }
  }
}

bool UGridOverlayComponent::EnsureInstancedMeshComponent(
    UInstancedStaticMeshComponent *&Component, FName ComponentName) {
  AActor *Owner = GetOwner();
  if (!Owner) {
    return false;
  }

  bool bNeedsRegistration = false;

  if (!Component) {
    Component = NewObject<UInstancedStaticMeshComponent>(Owner, ComponentName);
    if (!Component) {
      return false;
    }
    Component->CreationMethod = EComponentCreationMethod::Instance;
    Owner->AddOwnedComponent(Component);
    bNeedsRegistration = true;
  } else if (Component->GetOwner() != Owner) {
    Component->Rename(nullptr, Owner);
    Component->CreationMethod = EComponentCreationMethod::Instance;
    Owner->AddOwnedComponent(Component);
    bNeedsRegistration = true;
  }

  if (bNeedsRegistration) {
    Owner->AddInstanceComponent(Component);
  }

  if (USceneComponent *Root = Owner->GetRootComponent()) {
    if (Component->GetAttachParent() != Root) {
      Component->AttachToComponent(Root,
                                   FAttachmentTransformRules::KeepRelativeTransform);
    }
  }

  Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
  Component->SetGenerateOverlapEvents(false);
  Component->SetCastShadow(false);
  Component->bSelectable = false;
  Component->SetCanEverAffectNavigation(false);
  Component->SetRelativeTransform(FTransform::Identity);

  if (Component->NumCustomDataFloats < 4) {
    Component->NumCustomDataFloats = 4;
  }

  if (!Component->IsRegistered()) {
    Component->RegisterComponent();
  }

  return true;
}

bool UGridOverlayComponent::EnsureHighlightMeshComponentExists() {
  return EnsureInstancedMeshComponent(HighlightMeshComponent, TEXT("HighlightMesh"));
}

bool UGridOverlayComponent::EnsureBaseGridMeshComponentExists() {
  return EnsureInstancedMeshComponent(BaseGridMeshComponent, TEXT("GridMesh"));
}

void UGridOverlayComponent::ConfigureInstancedComponent(
    UInstancedStaticMeshComponent *Component, UStaticMesh *Mesh,
    UMaterialInterface *Material) {
  if (!Component) {
    return;
  }

  if (Mesh && Component->GetStaticMesh() != Mesh) {
    Component->SetStaticMesh(Mesh);
  }

  if (Material && Component->GetMaterial(0) != Material) {
    Component->SetMaterial(0, Material);
  }
}

void UGridOverlayComponent::OnRegister() {
  Super::OnRegister();

  RefreshOriginFromOwner();

  if (bUseDecalBaseGrid) {
    ClearBaseGridDecals();
  } else {
    EnsureBaseGridComponentSetup();
  }
  if (!bUseDecalHighlights) {
    EnsureHighlightComponentSetup();
  }
}

void UGridOverlayComponent::BeginPlay() {
  Super::BeginPlay();

  RefreshOriginFromOwner(false);

  const int32 TotalCells = Width * Length;
  Cells.Init(false, TotalCells);
  ObscuredCells.Init(false, TotalCells);
  DynamicOccupiedCells.Init(false, TotalCells);
  CellHeights.Init(Origin.Z, TotalCells);
  CellRotations.Init(FQuat::Identity, TotalCells);
  ColumnMaxHeights.Init(Origin.Z, TotalCells);
  ColumnHasClimbable.Init(false, TotalCells);
  ColumnTouchesTerrain.Init(false, TotalCells);
  if (bUseDecalBaseGrid) {
    BaseGridInstanceIndices.Empty();
    ClearBaseGridDecals();
  } else {
    BaseGridInstanceIndices.Init(INDEX_NONE, TotalCells);
  }

  SampleEnvironmentAtOrigin();

  bHasInitializedGrid = true;

  if (PendingObstacles.Num() > 0) {
    const TArray<TWeakObjectPtr<UGridObstacleComponent>> ObstaclesToProcess =
        PendingObstacles;
    PendingObstacles.Empty();
    for (const TWeakObjectPtr<UGridObstacleComponent> &WeakObstacle :
         ObstaclesToProcess) {
      if (WeakObstacle.IsValid()) {
        RegisterObstacle(WeakObstacle.Get());
      }
    }
  }

  RebuildBaseGridInstances();

  BuildGeneratedCells();

  if (PendingOccupancyUpdates.Num() > 0) {
    const TArray<FPendingGridOccupancyUpdate> OccupancyToApply =
        PendingOccupancyUpdates;
    PendingOccupancyUpdates.Empty();
    for (const FPendingGridOccupancyUpdate &Update : OccupancyToApply) {
      SetOccupied(Update.GridCoord, Update.bOccupied);
    }
  }
}

void UGridOverlayComponent::RefreshGridDataFromOrigin() {
  const int32 TotalCells = Width * Length;
  if (TotalCells <= 0) {
    Cells.Empty();
    ObscuredCells.Empty();
    CellHeights.Empty();
    CellRotations.Empty();
    DynamicOccupiedCells.Empty();
    ColumnMaxHeights.Empty();
    ColumnHasClimbable.Empty();
    ColumnTouchesTerrain.Empty();
    GeneratedCells.Empty();
    RebuildBaseGridInstances();
    return;
  }

  Cells.Init(false, TotalCells);
  ObscuredCells.Init(false, TotalCells);
  CellHeights.Init(Origin.Z, TotalCells);
  CellRotations.Init(FQuat::Identity, TotalCells);

  DynamicOccupiedCells.Init(false, TotalCells);
  ColumnMaxHeights.Init(Origin.Z, TotalCells);
  ColumnHasClimbable.Init(false, TotalCells);
  ColumnTouchesTerrain.Init(false, TotalCells);

  SampleEnvironmentAtOrigin();

  for (int32 Index = Obstacles.Num() - 1; Index >= 0; --Index) {
    UGridObstacleComponent *Obstacle = Obstacles[Index];
    if (!IsValid(Obstacle)) {
      Obstacles.RemoveAtSwap(Index);
      continue;
    }

    ApplyObstacleToGrid(Obstacle);
  }

  RebuildBaseGridInstances();
  BuildGeneratedCells();
}

void UGridOverlayComponent::SampleCellAt(const FIntPoint &GridCoord, float TraceHalfHeight,
                                         UWorld *World) {
  if (!IsValidGrid(GridCoord)) {
    return;
  }

  const int32 Idx = Index(GridCoord);
  if (!CellHeights.IsValidIndex(Idx) || !CellRotations.IsValidIndex(Idx)) {
    return;
  }

  UWorld *EffectiveWorld = World ? World : GetWorld();
  if (!EffectiveWorld) {
    return;
  }

  const FVector LocalCenter =
      FVector((GridCoord.X + 0.5f) * CellSize, (GridCoord.Y + 0.5f) * CellSize, 0.f);
  const FVector WorldCenter =
      CachedGridTransform.TransformPositionNoScale(LocalCenter);

  const FVector TraceOffset(0.f, 0.f, TraceHalfHeight);
  const FVector Start = WorldCenter + TraceOffset;
  const FVector End = WorldCenter - TraceOffset;

  FHitResult Hit;
  if (EffectiveWorld->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic)) {
    CellHeights[Idx] = Hit.Location.Z;
    CellRotations[Idx] =
        FRotationMatrix::MakeFromZ(Hit.Normal.GetSafeNormal()).ToQuat();

    if (ColumnMaxHeights.IsValidIndex(Idx)) {
      ColumnMaxHeights[Idx] =
          FMath::Max(ColumnMaxHeights[Idx], CellHeights[Idx] + CellSize);
    }

    if (ColumnTouchesTerrain.IsValidIndex(Idx)) {
      ColumnTouchesTerrain[Idx] = true;
    }

    if (Cast<ULandscapeComponent>(Hit.GetComponent())) {
      HandleLandscapeHit(Hit, GridCoord, Idx);
    }
  } else {
    CellHeights[Idx] = WorldCenter.Z;
    CellRotations[Idx] = FQuat::Identity;
    if (ColumnMaxHeights.IsValidIndex(Idx)) {
      ColumnMaxHeights[Idx] =
          FMath::Max(ColumnMaxHeights[Idx], CellHeights[Idx] + CellSize);
    }
  }
}

void UGridOverlayComponent::SampleEnvironmentAtOrigin() {
  if (Width <= 0 || Length <= 0) {
    return;
  }

  UWorld *World = GetWorld();
  if (!World) {
    return;
  }

  for (int32 Y = 0; Y < Length; ++Y) {
    for (int32 X = 0; X < Width; ++X) {
      SampleCellAt(FIntPoint(X, Y), SamplingTraceHalfHeight, World);
    }
  }
}

FIntPoint
UGridOverlayComponent::WorldToGrid(const FVector &WorldLocation) const {
  const FVector Local =
      CachedGridInverseTransform.TransformPosition(WorldLocation);
  int32 X = FMath::FloorToInt(Local.X / CellSize);
  int32 Y = FMath::FloorToInt(Local.Y / CellSize);
  return FIntPoint(X, Y);
}

FVector UGridOverlayComponent::GridToWorld(const FIntPoint &GridCoord) const {
  const FVector LocalCenter =
      FVector((GridCoord.X + 0.5f) * CellSize, (GridCoord.Y + 0.5f) * CellSize,
              0.f);
  FVector World = CachedGridTransform.TransformPosition(LocalCenter);
  World.Z = GetCellHeight(GridCoord);
  return World;
}

bool UGridOverlayComponent::IsValidGrid(const FIntPoint &GridCoord) const {
  return GridCoord.X >= 0 && GridCoord.X < Width && GridCoord.Y >= 0 &&
         GridCoord.Y < Length;
}

int32 UGridOverlayComponent::Index(const FIntPoint &GridCoord) const {
  return GridCoord.Y * Width + GridCoord.X;
}

bool UGridOverlayComponent::IsOccupied(const FIntPoint &GridCoord) const {
  if (!IsValidGrid(GridCoord)) {
    return false;
  }
  const int32 Idx = Index(GridCoord);
  const bool bBlocked = Cells.IsValidIndex(Idx) ? Cells[Idx] : false;
  const bool bDynamic =
      DynamicOccupiedCells.IsValidIndex(Idx) ? DynamicOccupiedCells[Idx] : false;
  return bBlocked || bDynamic;
}

bool UGridOverlayComponent::IsObscured(const FIntPoint &GridCoord) const {
  if (!IsValidGrid(GridCoord)) {
    return false;
  }
  const int32 Idx = Index(GridCoord);
  return ObscuredCells.IsValidIndex(Idx) ? ObscuredCells[Idx] : false;
}

bool UGridOverlayComponent::IsCellInBounds(const FIntPoint &GridCoord) const {
  return IsValidGrid(GridCoord);
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
  const int32 Idx = Index(GridCoord);
  return CellHeights.IsValidIndex(Idx) ? CellHeights[Idx] : Origin.Z;
}

void UGridOverlayComponent::SetOccupied(const FIntPoint &GridCoord,
                                        bool bOccupied) {
  if (!IsValidGrid(GridCoord)) {
    return;
  }
  if (!bHasInitializedGrid) {
    for (FPendingGridOccupancyUpdate &Pending : PendingOccupancyUpdates) {
      if (Pending.GridCoord == GridCoord) {
        Pending.bOccupied = bOccupied;
        return;
      }
    }
    PendingOccupancyUpdates.Add(FPendingGridOccupancyUpdate(GridCoord, bOccupied));
    return;
  }

  const int32 Idx = Index(GridCoord);
  if (!DynamicOccupiedCells.IsValidIndex(Idx)) {
    return;
  }

  DynamicOccupiedCells[Idx] = bOccupied;
  UpdateBaseGridVisual(GridCoord);
}

UDecalComponent *UGridOverlayComponent::AddTrapMarker(const FIntPoint &GridCoord) {
  if (!IsValidGrid(GridCoord)) {
    return nullptr;
  }

  AActor *Owner = GetOwner();
  if (!Owner) {
    return nullptr;
  }

  UMaterialInterface *BaseMaterial = TrapMarkerMaterial;
  if (!BaseMaterial) {
    BaseMaterial = GetHighlightDecalMaterial();
  }

  if (!BaseMaterial) {
    return nullptr;
  }

  UDecalComponent *Decal = nullptr;
  if (TWeakObjectPtr<UDecalComponent> *Existing = TrapMarkers.Find(GridCoord)) {
    Decal = Existing->Get();
    if (!Decal) {
      TrapMarkers.Remove(GridCoord);
      TrapMarkerMaterials.Remove(GridCoord);
    }
  }

  if (!Decal) {
    Decal = NewObject<UDecalComponent>(Owner);
    if (!Decal) {
      return nullptr;
    }

    Decal->CreationMethod = EComponentCreationMethod::Instance;
    Decal->SetMobility(EComponentMobility::Movable);
    Decal->FadeScreenSize = HighlightDecalFadeScreenSize;
    Owner->AddOwnedComponent(Decal);
    Owner->AddInstanceComponent(Decal);

    if (USceneComponent *Root = Owner->GetRootComponent()) {
      Decal->AttachToComponent(Root,
                               FAttachmentTransformRules::KeepWorldTransform);
    }

    Decal->RegisterComponent();
    TrapMarkers.Add(GridCoord, Decal);
  }

  UMaterialInstanceDynamic *DynamicMaterial = nullptr;
  if (TWeakObjectPtr<UMaterialInstanceDynamic> *ExistingMaterial =
          TrapMarkerMaterials.Find(GridCoord)) {
    DynamicMaterial = ExistingMaterial->Get();
    if (!DynamicMaterial) {
      TrapMarkerMaterials.Remove(GridCoord);
    }
  }

  if (!DynamicMaterial) {
    DynamicMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
    if (DynamicMaterial) {
      TrapMarkerMaterials.Add(GridCoord, DynamicMaterial);
    }
  }

  if (DynamicMaterial) {
    Decal->SetDecalMaterial(DynamicMaterial);
    DynamicMaterial->SetVectorParameterValue(TrapMarkerColorParameter,
                                             TrapMarkerColor);
  } else {
    Decal->SetDecalMaterial(BaseMaterial);
  }

  const int32 ArrayIndex = Index(GridCoord);
  const FQuat CellRotation = GetEffectiveCellRotation(ArrayIndex);
  const FVector CellNormal =
      CellRotation.RotateVector(FVector::UpVector).GetSafeNormal();
  const FVector CellTangent =
      CellRotation.RotateVector(FVector::ForwardVector).GetSafeNormal();
  const FVector WorldCenter = GridToWorld(GridCoord);
  FVector DecalLocation =
      WorldCenter + CellNormal * (TrapMarkerHeightOffset +
                                  TrapMarkerProjectionDepth * 0.5f);
  const auto DecalBasis =
      UE::Math::TRotationMatrix<double>::MakeFromXZ(-CellNormal, CellTangent);
  const FQuat DecalQuat = DecalBasis.ToQuat();
  const FRotator DecalRotation = DecalQuat.Rotator();

  const float EffectiveCellSize = FMath::Max(CellSize, KINDA_SMALL_NUMBER);
  const float HalfSize = 0.5f * EffectiveCellSize * TrapMarkerSizeMultiplier;
  Decal->DecalSize = FVector(TrapMarkerProjectionDepth, HalfSize, HalfSize);
  Decal->FadeScreenSize = HighlightDecalFadeScreenSize;
  Decal->SetWorldLocationAndRotation(DecalLocation, DecalRotation);
  Decal->SetFadeOut(0.f, 0.f, false);

  return Decal;
}

void UGridOverlayComponent::RemoveTrapMarker(const FIntPoint &GridCoord) {
  if (TWeakObjectPtr<UDecalComponent> *Existing = TrapMarkers.Find(GridCoord)) {
    if (UDecalComponent *Decal = Existing->Get()) {
      Decal->DestroyComponent();
    }
    TrapMarkers.Remove(GridCoord);
  }

  TrapMarkerMaterials.Remove(GridCoord);
}

bool UGridOverlayComponent::HasTrapMarker(const FIntPoint &GridCoord) const {
  if (const TWeakObjectPtr<UDecalComponent> *Existing = TrapMarkers.Find(GridCoord)) {
    return Existing->IsValid();
  }

  return false;
}

bool UGridOverlayComponent::EnsureHighlightComponentSetup() {
  if (!EnsureHighlightMeshComponentExists()) {
    return false;
  }

  UStaticMesh *MeshToUse = HighlightMesh ? HighlightMesh : GridMesh;
  UMaterialInterface *MaterialToUse =
      HighlightMaterial ? HighlightMaterial : GridMaterial;

  ConfigureInstancedComponent(HighlightMeshComponent, MeshToUse, MaterialToUse);

  return HighlightMeshComponent->GetStaticMesh() != nullptr;
}

bool UGridOverlayComponent::EnsureBaseGridComponentSetup() {
  if (bUseDecalBaseGrid) {
    return false;
  }

  if (!bDrawBaseGrid) {
    return false;
  }

  if (!EnsureBaseGridMeshComponentExists()) {
    return false;
  }

  UStaticMesh *MeshToUse = GridMesh ? GridMesh : HighlightMesh;
  if (!MeshToUse) {
    return false;
  }

  UMaterialInterface *MaterialToUse = GridMaterial ? GridMaterial : HighlightMaterial;
  ConfigureInstancedComponent(BaseGridMeshComponent, MeshToUse, MaterialToUse);

  return BaseGridMeshComponent->GetStaticMesh() != nullptr;
}

void UGridOverlayComponent::ApplyHighlightColor(int32 InstanceIndex,
                                                const FLinearColor &Color) {
  if (!HighlightMeshComponent || HighlightMeshComponent->NumCustomDataFloats < 4 ||
      InstanceIndex == INDEX_NONE) {
    return;
  }

  HighlightMeshComponent->SetCustomDataValue(InstanceIndex, 0, Color.R, false);
  HighlightMeshComponent->SetCustomDataValue(InstanceIndex, 1, Color.G, false);
  HighlightMeshComponent->SetCustomDataValue(InstanceIndex, 2, Color.B, false);
  HighlightMeshComponent->SetCustomDataValue(InstanceIndex, 3, Color.A, true);
}

UMaterialInterface *UGridOverlayComponent::GetHighlightDecalMaterial() const {
  if (HighlightDecalMaterial) {
    return HighlightDecalMaterial;
  }

  return HighlightMaterial;
}

void UGridOverlayComponent::ScheduleDecalRemoval(const FIntPoint &GridCoord,
                                                 UDecalComponent *Decal) {
  if (!Decal) {
    return;
  }

  UWorld *World = GetWorld();
  if (!World) {
    return;
  }

  ClearDecalRemovalTimer(GridCoord);

  FTimerDelegate Delegate;
  Delegate.BindUObject(this, &UGridOverlayComponent::OnHighlightDecalFadeFinished,
                       GridCoord, TWeakObjectPtr<UDecalComponent>(Decal));

  FTimerHandle &Handle = HighlightedDecalRemovalTimers.FindOrAdd(GridCoord);
  const float ExpirationDelay = HighlightDecalLifeSpan + HighlightDecalFadeDuration;
  World->GetTimerManager().SetTimer(Handle, Delegate, ExpirationDelay, false);
}

void UGridOverlayComponent::ClearDecalRemovalTimer(const FIntPoint &GridCoord) {
  if (HighlightedDecalRemovalTimers.Num() == 0) {
    return;
  }

  FTimerHandle *ExistingHandle = HighlightedDecalRemovalTimers.Find(GridCoord);
  if (!ExistingHandle) {
    return;
  }

  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(*ExistingHandle);
  }

  HighlightedDecalRemovalTimers.Remove(GridCoord);
}

void UGridOverlayComponent::OnHighlightDecalFadeFinished(
    FIntPoint GridCoord, TWeakObjectPtr<UDecalComponent> DecalWeak) {
  HighlightedDecalRemovalTimers.Remove(GridCoord);

  bool bRemovedDecalEntry = false;
  if (TWeakObjectPtr<UDecalComponent> *ExistingDecal =
          HighlightedDecals.Find(GridCoord)) {
    if (ExistingDecal->Get() == DecalWeak.Get()) {
      HighlightedDecals.Remove(GridCoord);
      bRemovedDecalEntry = true;
    }
  }

  if (bRemovedDecalEntry) {
    HighlightedDecalMaterials.Remove(GridCoord);
  }

  if (UDecalComponent *Decal = DecalWeak.Get()) {
    Decal->DestroyComponent();
  }
}

void UGridOverlayComponent::HighlightCellWithDecal(const FIntPoint &GridCoord,
                                                   const FColor &Color) {
  AActor *Owner = GetOwner();
  if (!Owner) {
    return;
  }

  UMaterialInterface *BaseMaterial = GetHighlightDecalMaterial();
  if (!BaseMaterial) {
    return;
  }

  UDecalComponent *Decal = nullptr;
  if (TWeakObjectPtr<UDecalComponent> *Existing = HighlightedDecals.Find(GridCoord)) {
    Decal = Existing->Get();
    if (!Decal) {
      HighlightedDecals.Remove(GridCoord);
      HighlightedDecalMaterials.Remove(GridCoord);
      ClearDecalRemovalTimer(GridCoord);
    }
  }

  if (!Decal) {
    Decal = NewObject<UDecalComponent>(Owner);
    if (!Decal) {
      return;
    }

    Decal->CreationMethod = EComponentCreationMethod::Instance;
    Decal->SetMobility(EComponentMobility::Movable);
    Decal->FadeScreenSize = HighlightDecalFadeScreenSize;
    Owner->AddOwnedComponent(Decal);
    Owner->AddInstanceComponent(Decal);

    if (USceneComponent *Root = Owner->GetRootComponent()) {
      Decal->AttachToComponent(Root, FAttachmentTransformRules::KeepWorldTransform);
    }

    Decal->RegisterComponent();
    HighlightedDecals.Add(GridCoord, Decal);
  }

  UMaterialInstanceDynamic *DynamicMaterial = nullptr;
  if (TWeakObjectPtr<UMaterialInstanceDynamic> *ExistingMaterial =
          HighlightedDecalMaterials.Find(GridCoord)) {
    DynamicMaterial = ExistingMaterial->Get();
    if (!DynamicMaterial) {
      HighlightedDecalMaterials.Remove(GridCoord);
    }
  }

  if (!DynamicMaterial) {
    DynamicMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);

    if (DynamicMaterial) {
      HighlightedDecalMaterials.Add(GridCoord, DynamicMaterial);
    }
  }

  if (DynamicMaterial) {
    Decal->SetDecalMaterial(DynamicMaterial);
  } else {
    Decal->SetDecalMaterial(BaseMaterial);
  }

  const int32 ArrayIndex = Index(GridCoord);
  const FQuat CellRotation = GetEffectiveCellRotation(ArrayIndex);
  const FVector CellNormal = CellRotation.RotateVector(FVector::UpVector).GetSafeNormal();
  const FVector CellTangent =
      CellRotation.RotateVector(FVector::ForwardVector).GetSafeNormal();
  const FVector WorldCenter = GridToWorld(GridCoord);
  FVector DecalLocation =
      WorldCenter + CellNormal * (HighlightHeightOffset + HighlightDecalProjectionDepth * 0.5f);
  const auto DecalBasis =
      UE::Math::TRotationMatrix<double>::MakeFromXZ(-CellNormal, CellTangent);
  const FQuat DecalQuat = DecalBasis.ToQuat();
  const FRotator DecalRotation = DecalQuat.Rotator();

  const float EffectiveCellSize = FMath::Max(CellSize, KINDA_SMALL_NUMBER);
  const float HalfSize = 0.5f * EffectiveCellSize * HighlightDecalSizeMultiplier;
  Decal->DecalSize = FVector(HighlightDecalProjectionDepth, HalfSize, HalfSize);
  Decal->FadeScreenSize = HighlightDecalFadeScreenSize;
  Decal->SetWorldLocationAndRotation(DecalLocation, DecalRotation);

  ClearDecalRemovalTimer(GridCoord);
  Decal->SetFadeOut(0.f, 0.f, false);

  if (HighlightDecalLifeSpan > 0.f && HighlightDecalFadeDuration > 0.f) {
    Decal->SetFadeOut(HighlightDecalLifeSpan, HighlightDecalFadeDuration, false);
    ScheduleDecalRemoval(GridCoord, Decal);
  }

  if (DynamicMaterial) {
    DynamicMaterial->SetVectorParameterValue(HighlightDecalColorParameter,
                                             FLinearColor(Color));
  }
}

UMaterialInterface *UGridOverlayComponent::GetBaseGridDecalMaterial() const {
  if (BaseGridDecalMaterial) {
    return BaseGridDecalMaterial;
  }

  if (HighlightDecalMaterial) {
    return HighlightDecalMaterial;
  }

  return nullptr;
}

void UGridOverlayComponent::ClearBaseGridDecals() {
  for (TWeakObjectPtr<UDecalComponent> &DecalPtr : BaseGridDecalComponents) {
    if (UDecalComponent *Decal = DecalPtr.Get()) {
      Decal->DestroyComponent();
    }
  }

  BaseGridDecalComponents.Empty();
  BaseGridDecalMaterials.Empty();
}

void UGridOverlayComponent::ApplyBaseGridDecalColor(int32 CellIndex,
                                                    const FLinearColor &Color) {
  if (!BaseGridDecalMaterials.IsValidIndex(CellIndex)) {
    return;
  }

  if (UMaterialInstanceDynamic *DynamicMaterial =
          BaseGridDecalMaterials[CellIndex].Get()) {
    DynamicMaterial->SetVectorParameterValue(BaseGridDecalColorParameter, Color);
  }
}

void UGridOverlayComponent::ApplyBaseGridColor(int32 InstanceIndex,
                                               const FLinearColor &Color) {
  if (!BaseGridMeshComponent || BaseGridMeshComponent->NumCustomDataFloats < 4 ||
      InstanceIndex == INDEX_NONE) {
    return;
  }

  BaseGridMeshComponent->SetCustomDataValue(InstanceIndex, 0, Color.R, false);
  BaseGridMeshComponent->SetCustomDataValue(InstanceIndex, 1, Color.G, false);
  BaseGridMeshComponent->SetCustomDataValue(InstanceIndex, 2, Color.B, false);
  BaseGridMeshComponent->SetCustomDataValue(InstanceIndex, 3, Color.A, true);
}

FLinearColor UGridOverlayComponent::GetBaseGridColor(int32 CellIndex) const {
  if (CellIndex < 0) {
    return DefaultCellColor;
  }

  if (DynamicOccupiedCells.IsValidIndex(CellIndex) &&
      DynamicOccupiedCells[CellIndex]) {
    return OccupiedCellColor;
  }

  if (Cells.IsValidIndex(CellIndex) && Cells[CellIndex]) {
    return BlockedCellColor;
  }

  if (ObscuredCells.IsValidIndex(CellIndex) && ObscuredCells[CellIndex]) {
    return ObscuredCellColor;
  }

  return DefaultCellColor;
}

FQuat UGridOverlayComponent::GetEffectiveCellRotation(int32 ArrayIndex) const {
  if (bNoCellRotation) {
    return FQuat::Identity;
  }

  return CellRotations.IsValidIndex(ArrayIndex) ? CellRotations[ArrayIndex]
                                                : FQuat::Identity;
}

void UGridOverlayComponent::UpdateBaseGridVisual(const FIntPoint &GridCoord) {
  if (!IsValidGrid(GridCoord)) {
    return;
  }

  const int32 ArrayIndex = Index(GridCoord);
  if (bUseDecalBaseGrid) {
    ApplyBaseGridDecalColor(ArrayIndex, GetBaseGridColor(ArrayIndex));
    return;
  }

  if (!BaseGridMeshComponent || !BaseGridInstanceIndices.IsValidIndex(ArrayIndex)) {
    return;
  }

  const int32 InstanceIndex = BaseGridInstanceIndices[ArrayIndex];
  if (InstanceIndex == INDEX_NONE) {
    return;
  }

  ApplyBaseGridColor(InstanceIndex, GetBaseGridColor(ArrayIndex));
}

void UGridOverlayComponent::RebuildBaseGridInstances() {
  if (!bDrawBaseGrid) {
    if (bUseDecalBaseGrid) {
      ClearBaseGridDecals();
    } else if (BaseGridMeshComponent) {
      BaseGridMeshComponent->ClearInstances();
    }
    BaseGridInstanceIndices.Empty();
    return;
  }

  const int32 TotalCells = Width * Length;
  if (TotalCells <= 0) {
    if (bUseDecalBaseGrid) {
      ClearBaseGridDecals();
    } else if (BaseGridMeshComponent) {
      BaseGridMeshComponent->ClearInstances();
    }
    BaseGridInstanceIndices.Empty();
    return;
  }

  if (bUseDecalBaseGrid) {
    UMaterialInterface *DecalMaterial = GetBaseGridDecalMaterial();
    if (!DecalMaterial) {
      UE_LOG(LogTemp, Warning,
             TEXT("GridOverlayComponent %s is configured to use decal base grid but no decal material is set."),
             *GetNameSafe(GetOwner()));
      ClearBaseGridDecals();
      return;
    }

    ClearBaseGridDecals();
    BaseGridDecalComponents.SetNum(TotalCells);
    BaseGridDecalMaterials.SetNum(TotalCells);

    AActor *Owner = GetOwner();
    if (!Owner) {
      return;
    }

    USceneComponent *Root = Owner->GetRootComponent();
    const float EffectiveCellSize = FMath::Max(CellSize, KINDA_SMALL_NUMBER);
    const float HalfSize = 0.5f * EffectiveCellSize * BaseGridDecalSizeMultiplier;

    for (int32 Y = 0; Y < Length; ++Y) {
      for (int32 X = 0; X < Width; ++X) {
        const FIntPoint Cell(X, Y);
        const int32 ArrayIndex = Index(Cell);

        UDecalComponent *Decal = nullptr;
        if (BaseGridDecalComponents.IsValidIndex(ArrayIndex)) {
          Decal = BaseGridDecalComponents[ArrayIndex].Get();
        }

        if (!Decal) {
          Decal = NewObject<UDecalComponent>(Owner);
          if (!Decal) {
            continue;
          }

          Decal->CreationMethod = EComponentCreationMethod::Instance;
          Decal->SetMobility(EComponentMobility::Movable);
          Decal->FadeScreenSize = BaseGridDecalFadeScreenSize;
          Owner->AddOwnedComponent(Decal);
          Owner->AddInstanceComponent(Decal);

          if (Root) {
            Decal->AttachToComponent(Root, FAttachmentTransformRules::KeepWorldTransform);
          }

          Decal->RegisterComponent();
          BaseGridDecalComponents[ArrayIndex] = Decal;
        }

        UMaterialInstanceDynamic *DynamicMaterial = nullptr;
        if (BaseGridDecalMaterials.IsValidIndex(ArrayIndex)) {
          DynamicMaterial = BaseGridDecalMaterials[ArrayIndex].Get();
        }

        if (!DynamicMaterial) {
          DynamicMaterial = UMaterialInstanceDynamic::Create(DecalMaterial, this);
          if (DynamicMaterial) {
            BaseGridDecalMaterials[ArrayIndex] = DynamicMaterial;
          }
        }

        if (DynamicMaterial) {
          Decal->SetDecalMaterial(DynamicMaterial);
        } else {
          Decal->SetDecalMaterial(DecalMaterial);
        }

        const FQuat CellRotation = GetEffectiveCellRotation(ArrayIndex);
        const FVector CellNormal =
            CellRotation.RotateVector(FVector::UpVector).GetSafeNormal();
        const FVector CellTangent =
            CellRotation.RotateVector(FVector::ForwardVector).GetSafeNormal();
        const FVector WorldCenter = GridToWorld(Cell);
        const FVector DecalLocation =
            WorldCenter +
            CellNormal * (GridHeightOffset + BaseGridDecalProjectionDepth * 0.5f);
        const auto DecalBasis =
            UE::Math::TRotationMatrix<double>::MakeFromXZ(-CellNormal, CellTangent);
        const FQuat DecalQuat = DecalBasis.ToQuat();
        const FRotator DecalRotation = DecalQuat.Rotator();

        Decal->DecalSize =
            FVector(BaseGridDecalProjectionDepth, HalfSize, HalfSize);
        Decal->FadeScreenSize = BaseGridDecalFadeScreenSize;
        Decal->SetWorldLocationAndRotation(DecalLocation, DecalRotation);
        Decal->SetFadeOut(0.f, 0.f, false);

        ApplyBaseGridDecalColor(ArrayIndex, GetBaseGridColor(ArrayIndex));
      }
    }

    return;
  }

  ClearBaseGridDecals();

  if (!EnsureBaseGridComponentSetup()) {
    return;
  }

  BaseGridMeshComponent->ClearInstances();
  BaseGridInstanceIndices.Init(INDEX_NONE, TotalCells);

  const FTransform ComponentTransform = BaseGridMeshComponent->GetComponentTransform();
  const float EffectiveCellSize = FMath::Max(CellSize, KINDA_SMALL_NUMBER);
  const FVector InstanceScale(EffectiveCellSize / 100.f, EffectiveCellSize / 100.f, 1.f);

    for (int32 Y = 0; Y < Length; ++Y) {
      for (int32 X = 0; X < Width; ++X) {
        const FIntPoint Cell(X, Y);
      const FVector WorldCenter =
          GridToWorld(Cell) + FVector(0.f, 0.f, GridHeightOffset);
      const int32 ArrayIndex = Index(Cell);
      const FQuat CellRotation = GetEffectiveCellRotation(ArrayIndex);
      const FTransform WorldTransform(CellRotation, WorldCenter, InstanceScale);
      const FTransform RelativeTransform =
          WorldTransform.GetRelativeTransform(ComponentTransform);

      const int32 InstanceIndex = BaseGridMeshComponent->AddInstance(RelativeTransform);
      if (BaseGridInstanceIndices.IsValidIndex(ArrayIndex)) {
        BaseGridInstanceIndices[ArrayIndex] = InstanceIndex;
        ApplyBaseGridColor(InstanceIndex, GetBaseGridColor(ArrayIndex));
      }
    }
  }
}

void UGridOverlayComponent::RebuildGridVisuals() { RebuildBaseGridInstances(); }

void UGridOverlayComponent::HighlightCell(const FIntPoint &GridCoord,
                                          const FColor &Color, float /*Duration*/,
                                          bool /*bPersistent*/) {
  if (!IsValidGrid(GridCoord)) {
    return;
  }

  if (bUseDecalHighlights) {
    HighlightCellWithDecal(GridCoord, Color);
    return;
  }

  if (!EnsureHighlightComponentSetup()) {
    return;
  }

  const FVector WorldCenter =
      GridToWorld(GridCoord) + FVector(0.f, 0.f, HighlightHeightOffset);
  const float EffectiveCellSize = FMath::Max(CellSize, KINDA_SMALL_NUMBER);
  const FVector InstanceScale(EffectiveCellSize / 100.f,
                              EffectiveCellSize / 100.f, 1.f);
  const int32 ArrayIndex = Index(GridCoord);
  const FQuat CellRotation = GetEffectiveCellRotation(ArrayIndex);
  const FTransform WorldTransform(CellRotation, WorldCenter, InstanceScale);
  const FTransform ComponentTransform = HighlightMeshComponent->GetComponentTransform();
  const FTransform RelativeTransform =
      WorldTransform.GetRelativeTransform(ComponentTransform);

  int32 InstanceIndex = INDEX_NONE;
  if (int32 *ExistingIndex = HighlightedInstances.Find(GridCoord)) {
    InstanceIndex = *ExistingIndex;
    HighlightMeshComponent->UpdateInstanceTransform(InstanceIndex, RelativeTransform,
                                                    false, true, true);
  } else {
    InstanceIndex = HighlightMeshComponent->AddInstance(RelativeTransform);
    if (InstanceIndex == INDEX_NONE) {
      return;
    }
    HighlightedInstances.Add(GridCoord, InstanceIndex);
  }

  ApplyHighlightColor(InstanceIndex, FLinearColor(Color));
}

void UGridOverlayComponent::HighlightSelection(AFighterPawn *Fighter) {
  if (!Fighter) {
    ClearSelectionHighlight();
    return;
  }

  PersistentlyHighlightedFighter = Fighter;
  ClearHighlights(true);
}

void UGridOverlayComponent::ClearSelectionHighlight() {
  PersistentlyHighlightedFighter.Reset();
  ClearHighlights(false);
}

void UGridOverlayComponent::ClearHighlights(bool bMaintainPersistentSelection) {
  HighlightedInstances.Empty();
  if (HighlightMeshComponent) {
    HighlightMeshComponent->ClearInstances();
  }

  if (HighlightedDecals.Num() > 0) {
    for (auto It = HighlightedDecals.CreateIterator(); It; ++It) {
      ClearDecalRemovalTimer(It.Key());
      if (UDecalComponent *Decal = It.Value().Get()) {
        Decal->DestroyComponent();
      }
    }
    HighlightedDecals.Empty();
  }
  HighlightedDecalMaterials.Empty();

  if (HighlightedDecalRemovalTimers.Num() > 0) {
    if (UWorld *World = GetWorld()) {
      FTimerManager &TimerManager = World->GetTimerManager();
      for (auto &TimerPair : HighlightedDecalRemovalTimers) {
        TimerManager.ClearTimer(TimerPair.Value);
      }
    }
    HighlightedDecalRemovalTimers.Empty();
  }

  if (!bMaintainPersistentSelection) {
    PersistentlyHighlightedFighter.Reset();
  }

#if WITH_EDITOR
  if (GetWorld() && bFlushAllPersistentOnClear) {
    FlushPersistentDebugLines(GetWorld());
  }
#endif

  if (bMaintainPersistentSelection) {
    RefreshPersistentSelectionHighlight();
  }
}

void UGridOverlayComponent::RefreshPersistentSelectionHighlight() {
  if (!PersistentlyHighlightedFighter.IsValid()) {
    return;
  }

  AFighterPawn *Fighter = PersistentlyHighlightedFighter.Get();
  if (!Fighter) {
    PersistentlyHighlightedFighter.Reset();
    return;
  }

  const FColor SelectionColor = SelectionHighlightColor.ToFColor(true);
  for (const FIntPoint &Cell : Fighter->GetOccupiedCells()) {
    HighlightCell(Cell, SelectionColor, 0.f, false);
  }
}

void UGridOverlayComponent::RegisterObstacle(UGridObstacleComponent *Obstacle) {
  if (!Obstacle) {
    return;
  }
  if (!bHasInitializedGrid) {
    PendingObstacles.AddUnique(Obstacle);
    return;
  }

  Obstacles.AddUnique(Obstacle);
  ApplyObstacleToGrid(Obstacle);
}

void UGridOverlayComponent::UnregisterObstacle(UGridObstacleComponent *Obstacle) {
  if (!Obstacle) {
    return;
  }

  PendingObstacles.RemoveAll(
      [Obstacle](const TWeakObjectPtr<UGridObstacleComponent> &WeakObstacle) {
        return WeakObstacle.Get() == Obstacle;
      });

  const int32 RemovedCount = Obstacles.RemoveSingleSwap(Obstacle);
  if (RemovedCount <= 0 || !bHasInitializedGrid) {
    return;
  }

  TArray<FIntPoint> ClearedCells;

  if (AActor *Owner = Obstacle->GetOwner()) {
    FIntPoint Min = FIntPoint::ZeroValue;
    FIntPoint Max = FIntPoint::ZeroValue;
    bool bHasCustomFootprint = Obstacle->GetCustomGridFootprint(this, Min, Max);

    if (!bHasCustomFootprint) {
      const FBox Bounds = Owner->GetComponentsBoundingBox(true);
      if (Bounds.IsValid) {
        const FIntPoint RawMin = WorldToGrid(Bounds.Min);
        const FIntPoint RawMax = WorldToGrid(Bounds.Max);
        Min.X = FMath::Min(RawMin.X, RawMax.X);
        Min.Y = FMath::Min(RawMin.Y, RawMax.Y);
        Max.X = FMath::Max(RawMin.X, RawMax.X);
        Max.Y = FMath::Max(RawMin.Y, RawMax.Y);
      } else {
        const FIntPoint Anchor = WorldToGrid(Owner->GetActorLocation());
        Min = Anchor;
        Max = Anchor;
      }
    }

    for (int32 Y = Min.Y; Y <= Max.Y; ++Y) {
      for (int32 X = Min.X; X <= Max.X; ++X) {
        const FIntPoint Cell(X, Y);
        if (!IsValidGrid(Cell)) {
          continue;
        }

        ClearedCells.Add(Cell);

        const int32 Idx = Index(Cell);
        if (Obstacle->bBlocksMovement && Cells.IsValidIndex(Idx)) {
          Cells[Idx] = false;
        }
        if (Obstacle->bBlocksLineOfSight && ObscuredCells.IsValidIndex(Idx)) {
          ObscuredCells[Idx] = false;
        }
      }
    }
  }

  for (int32 Index = Obstacles.Num() - 1; Index >= 0; --Index) {
    UGridObstacleComponent *RemainingObstacle = Obstacles[Index];
    if (!IsValid(RemainingObstacle)) {
      Obstacles.RemoveAtSwap(Index);
      continue;
    }

    ApplyObstacleToGrid(RemainingObstacle);
  }

  for (const FIntPoint &Cell : ClearedCells) {
    UpdateBaseGridVisual(Cell);
  }
}

void UGridOverlayComponent::ApplyObstacleToGrid(UGridObstacleComponent *Obstacle) {
  if (!Obstacle || !bHasInitializedGrid) {
    return;
  }

  if (AActor *Owner = Obstacle->GetOwner()) {
    const FBox Bounds = Owner->GetComponentsBoundingBox(true);
    FIntPoint Min;
    FIntPoint Max;
    if (!Obstacle->GetCustomGridFootprint(this, Min, Max)) {
      Min = WorldToGrid(Bounds.Min);
      Max = WorldToGrid(Bounds.Max);
    }
    const bool bResampleCells = Obstacle->HasCustomTraceHalfHeight();
    const float EffectiveTraceHalfHeight =
        Obstacle->GetTraceHalfHeightOrDefault(SamplingTraceHalfHeight);
    UWorld *World = bResampleCells ? GetWorld() : nullptr;

    for (int32 Y = Min.Y; Y <= Max.Y; ++Y) {
      for (int32 X = Min.X; X <= Max.X; ++X) {
        const FIntPoint Cell(X, Y);
        if (!IsValidGrid(Cell)) {
          continue;
        }
        const int32 Idx = Index(Cell);
        if (!Cells.IsValidIndex(Idx) || !ObscuredCells.IsValidIndex(Idx)) {
          continue;
        }

        if (bResampleCells) {
          SampleCellAt(Cell, EffectiveTraceHalfHeight, World);
        }

        if (ColumnMaxHeights.IsValidIndex(Idx)) {
          ColumnMaxHeights[Idx] =
              FMath::Max(ColumnMaxHeights[Idx], Bounds.Max.Z);
        }

        if (Obstacle->bClimbable) {
          CellHeights[Idx] = Bounds.Max.Z;
          Cells[Idx] = false;
          ObscuredCells[Idx] = false;
          if (ColumnHasClimbable.IsValidIndex(Idx)) {
            ColumnHasClimbable[Idx] = true;
          }
        } else {
          if (Obstacle->bBlocksMovement) {
            Cells[Idx] = true;
          }
          if (Obstacle->bBlocksLineOfSight) {
            ObscuredCells[Idx] = true;
          }
        }
        UpdateBaseGridVisual(Cell);
      }
    }
  }

  BuildGeneratedCells();
}

void UGridOverlayComponent::ClearStaticObstacleAtCell(
    const FIntPoint &GridCoord) {
  if (!bHasInitializedGrid || !IsValidGrid(GridCoord)) {
    return;
  }

  const int32 Idx = Index(GridCoord);
  if (Cells.IsValidIndex(Idx)) {
    Cells[Idx] = false;
  }
  if (ObscuredCells.IsValidIndex(Idx)) {
    ObscuredCells[Idx] = false;
  }
  if (ColumnHasClimbable.IsValidIndex(Idx)) {
    ColumnHasClimbable[Idx] = false;
  }
  if (ColumnMaxHeights.IsValidIndex(Idx) && CellHeights.IsValidIndex(Idx)) {
    ColumnMaxHeights[Idx] = CellHeights[Idx] + CellSize;
  }

  UpdateBaseGridVisual(GridCoord);
  BuildGeneratedCells();
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
    if (Cells.IsValidIndex(CellIndex)) {
      Cells[CellIndex] = true;
    }
    if (ObscuredCells.IsValidIndex(CellIndex)) {
      ObscuredCells[CellIndex] = true;
    }
    UpdateBaseGridVisual(Cell);
  }

  if (ColumnTouchesTerrain.IsValidIndex(CellIndex)) {
    ColumnTouchesTerrain[CellIndex] = true;
  }
}

void UGridOverlayComponent::BuildGeneratedCells() {
  GeneratedCells.Reset();

  if (Width <= 0 || Length <= 0 || Height <= 0) {
    return;
  }

  const float EffectiveCellSize = FMath::Max(CellSize, KINDA_SMALL_NUMBER);
  GeneratedCells.Reserve(Width * Length * Height);

  for (int32 Y = 0; Y < Length; ++Y) {
    for (int32 X = 0; X < Width; ++X) {
      const FIntPoint Cell(X, Y);
      const int32 ColumnIndex = Index(Cell);
      if (!CellHeights.IsValidIndex(ColumnIndex)) {
        continue;
      }

      const float BaseHeight = CellHeights[ColumnIndex];
      const float MaxHeight = ColumnMaxHeights.IsValidIndex(ColumnIndex)
                                  ? ColumnMaxHeights[ColumnIndex]
                                  : (BaseHeight + EffectiveCellSize);
      const float ColumnSpan =
          FMath::Max(MaxHeight - BaseHeight, EffectiveCellSize);
      const int32 Layers =
          FMath::Clamp(FMath::CeilToInt(ColumnSpan / EffectiveCellSize), 1, Height);

      const bool bBlocked = Cells.IsValidIndex(ColumnIndex) && Cells[ColumnIndex];
      const bool bObscured =
          ObscuredCells.IsValidIndex(ColumnIndex) && ObscuredCells[ColumnIndex];
      const bool bAllowsClimb = ColumnHasClimbable.IsValidIndex(ColumnIndex) &&
                                ColumnHasClimbable[ColumnIndex];

      FVector BaseCenter = GridToWorld(Cell);
      BaseCenter.Z = BaseHeight;

      for (int32 Layer = 0; Layer < Layers; ++Layer) {
        FGridCell3D GeneratedCell;
        GeneratedCell.GridCoord = FIntVector(X, Y, Layer);
        const float FloorHeight = BaseHeight + Layer * EffectiveCellSize;
        GeneratedCell.FloorHeight = FloorHeight;
        GeneratedCell.CeilingHeight = FloorHeight + EffectiveCellSize;

        FVector LayerCenter = BaseCenter;
        LayerCenter.Z = FloorHeight + 0.5f * EffectiveCellSize;
        GeneratedCell.Center = LayerCenter;
        GeneratedCell.bBlocked = bBlocked;
        GeneratedCell.bObscured = bObscured;
        GeneratedCell.bAllowsClimb = bAllowsClimb;

        GeneratedCells.Add(GeneratedCell);
      }
    }
  }
}

bool UGridOverlayComponent::CanTraverseVertical(const FIntPoint &From,
                                                const FIntPoint &To) const {
  const float FromHeight = GetCellHeight(From);
  const float ToHeight = GetCellHeight(To);
  const float HeightDelta = FMath::Abs(ToHeight - FromHeight);

  if (HeightDelta <= KINDA_SMALL_NUMBER) {
    return true;
  }

  const float MaxVertical = CellSize * FMath::Max(Height, 1);
  if (HeightDelta > MaxVertical) {
    return false;
  }

  const int32 FromIndex = Index(From);
  const int32 ToIndex = Index(To);

  const bool bFromClimbable = ColumnHasClimbable.IsValidIndex(FromIndex) &&
                              ColumnHasClimbable[FromIndex];
  const bool bToClimbable = ColumnHasClimbable.IsValidIndex(ToIndex) &&
                            ColumnHasClimbable[ToIndex];
  const bool bFromTerrain = ColumnTouchesTerrain.IsValidIndex(FromIndex) &&
                            ColumnTouchesTerrain[FromIndex];
  const bool bToTerrain = ColumnTouchesTerrain.IsValidIndex(ToIndex) &&
                          ColumnTouchesTerrain[ToIndex];

  return bFromClimbable || bToClimbable || bFromTerrain || bToTerrain;
}

void UGridOverlayComponent::HighlightMovement(AFighterPawn *Fighter) {
  if (!Fighter) {
    return;
  }

  ClearHighlights();

  const FIntPoint StartCell = Fighter->GetCurrentCell();
  const int32 Range = Fighter->Stats.Movement;

  const FColor SelectionColor = SelectionHighlightColor.ToFColor(true);
  const FColor MovementColor = MovementHighlightColor.ToFColor(true);

  auto HighlightFootprint = [&](const FIntPoint &Anchor, const FColor &Color) {
    const TArray<FIntPoint> Footprint = Fighter->GetOccupiedCells(Anchor);
    for (const FIntPoint &Cell : Footprint) {
      HighlightCell(Cell, Color, 0.f, false);
    }
  };

  HighlightFootprint(StartCell, SelectionColor);

  TSet<FIntPoint> Visited;
  TQueue<TPair<FIntPoint, int32>> Frontier;
  Visited.Add(StartCell);
  Frontier.Enqueue(TPair<FIntPoint, int32>(StartCell, 0));

  TSet<FIntPoint> IgnoredCells;
  const TArray<FIntPoint> CurrentFootprint = Fighter->GetOccupiedCells();
  for (const FIntPoint &Cell : CurrentFootprint) {
    IgnoredCells.Add(Cell);
  }

  auto CanOccupyAnchor = [&](const FIntPoint &Anchor) {
    const TArray<FIntPoint> CandidateCells = Fighter->GetOccupiedCells(Anchor);
    for (const FIntPoint &Cell : CandidateCells) {
      if (!IsCellInBounds(Cell) || IsObscured(Cell)) {
        return false;
      }
      if (IsOccupied(Cell) && !IgnoredCells.Contains(Cell)) {
        return false;
      }
    }
    return true;
  };

  while (!Frontier.IsEmpty()) {
    TPair<FIntPoint, int32> Node;
    Frontier.Dequeue(Node);
    const FIntPoint Cell = Node.Key;
    const int32 Distance = Node.Value;

    if (Distance > 0) {
      HighlightFootprint(Cell, MovementColor);
    }

    if (Distance >= Range) {
      continue;
    }

    static const FIntPoint Directions[8] = {
        FIntPoint(1, 0),  FIntPoint(-1, 0), FIntPoint(0, 1),  FIntPoint(0, -1),
        FIntPoint(1, 1),  FIntPoint(1, -1), FIntPoint(-1, 1), FIntPoint(-1, -1)};

    for (const FIntPoint &Dir : Directions) {
      const FIntPoint Next = Cell + Dir;
      if (Visited.Contains(Next)) {
        continue;
      }
      if (!CanTraverseVertical(Cell, Next)) {
        continue;
      }
      if (!CanOccupyAnchor(Next)) {
        continue;
      }
      if (Dir.X != 0 && Dir.Y != 0) {
        const FIntPoint StepX(Dir.X, 0);
        const FIntPoint StepY(0, Dir.Y);
        const TArray<FIntPoint> FromCells = Fighter->GetOccupiedCells(Cell);
        const TArray<FIntPoint> NextCells = Fighter->GetOccupiedCells(Next);
        TSet<FIntPoint> NextCellSet;
        NextCellSet.Reserve(NextCells.Num());
        for (const FIntPoint &NextCell : NextCells) {
          NextCellSet.Add(NextCell);
        }
        auto IsBlocked = [&](const FIntPoint &CheckCell) {
          if (!IsCellInBounds(CheckCell) || IsObscured(CheckCell)) {
            return true;
          }
          if (IsOccupied(CheckCell) && !IgnoredCells.Contains(CheckCell) &&
              !NextCellSet.Contains(CheckCell)) {
            return true;
          }
          return false;
        };
        bool bDiagonalClear = true;
        for (const FIntPoint &FromCell : FromCells) {
          if (IsBlocked(FromCell + StepX) || IsBlocked(FromCell + StepY)) {
            bDiagonalClear = false;
            break;
          }
        }
        if (!bDiagonalClear) {
          continue;
        }
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

  const FIntPoint StartCell = Fighter->GetCurrentCell();
  const int32 Range = Fighter->Stats.AttackRange;

  const FColor SelectionColor = SelectionHighlightColor.ToFColor(true);
  const FColor AttackColor = AttackHighlightColor.ToFColor(true);

  const TArray<FIntPoint> Footprint = Fighter->GetOccupiedCells(StartCell);
  TSet<FIntPoint> Occupied;
  for (const FIntPoint &Cell : Footprint) {
    Occupied.Add(Cell);
    HighlightCell(Cell, SelectionColor, 0.f, false);
  }

  const int32 GridWidth = GetWidth();
  const int32 GridHeight = GetLength();

  for (int32 Y = 0; Y < GridHeight; ++Y) {
    for (int32 X = 0; X < GridWidth; ++X) {
      const FIntPoint Target(X, Y);
      if (Occupied.Contains(Target)) {
        continue;
      }

      bool bWithinRange = false;
      for (const FIntPoint &SelfCell : Footprint) {
        const int32 Distance = FMath::Max(
            FMath::Abs(SelfCell.X - Target.X), FMath::Abs(SelfCell.Y - Target.Y));
        if (Distance > Range) {
          continue;
        }
        if (HasLineOfSight(SelfCell, Target)) {
          bWithinRange = true;
          break;
        }
      }

      if (bWithinRange) {
        HighlightCell(Target, AttackColor, 0.f, false);
      }
    }
  }
}
