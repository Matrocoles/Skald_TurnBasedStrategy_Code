#include "GridOverlayComponent.h"
#include "CollisionQueryParams.h"
#include "Containers/Queue.h"
#include "DrawDebugHelpers.h"
#include "Engine/EngineTypes.h"
#include "Engine/World.h"
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
#include "UObject/UObjectGlobals.h"
#include "TimerManager.h"

UGridOverlayComponent::UGridOverlayComponent() {
  PrimaryComponentTick.bCanEverTick = false;
}

int32 UGridOverlayComponent::GetWidth() const { return Width; }

int32 UGridOverlayComponent::GetHeight() const { return Height; }

float UGridOverlayComponent::GetCellSize() const { return CellSize; }

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
    const FVector NewOrigin = Owner->GetActorLocation();
    const bool bOriginChanged = !Origin.Equals(NewOrigin, KINDA_SMALL_NUMBER);
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

  EnsureBaseGridComponentSetup();
  if (!bUseDecalHighlights) {
    EnsureHighlightComponentSetup();
  }
}

void UGridOverlayComponent::BeginPlay() {
  Super::BeginPlay();

  RefreshOriginFromOwner(false);

  const int32 TotalCells = Width * Height;
  Cells.Init(false, TotalCells);
  ObscuredCells.Init(false, TotalCells);
  DynamicOccupiedCells.Init(false, TotalCells);
  CellHeights.Init(Origin.Z, TotalCells);
  CellRotations.Init(FQuat::Identity, TotalCells);
  BaseGridInstanceIndices.Init(INDEX_NONE, TotalCells);

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
  const int32 TotalCells = Width * Height;
  if (TotalCells <= 0) {
    Cells.Empty();
    ObscuredCells.Empty();
    CellHeights.Empty();
    CellRotations.Empty();
    DynamicOccupiedCells.Empty();
    RebuildBaseGridInstances();
    return;
  }

  Cells.Init(false, TotalCells);
  ObscuredCells.Init(false, TotalCells);
  CellHeights.Init(Origin.Z, TotalCells);
  CellRotations.Init(FQuat::Identity, TotalCells);

  DynamicOccupiedCells.Init(false, TotalCells);

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
}

void UGridOverlayComponent::SampleEnvironmentAtOrigin() {
  if (Width <= 0 || Height <= 0) {
    return;
  }

  UWorld *World = GetWorld();
  if (!World) {
    return;
  }

  for (int32 Y = 0; Y < Height; ++Y) {
    for (int32 X = 0; X < Width; ++X) {
      const int32 Idx = Index(FIntPoint(X, Y));
      FVector Start = Origin +
                      FVector((X + 0.5f) * CellSize, (Y + 0.5f) * CellSize, 10000.f);
      FVector End = Start - FVector(0.f, 0.f, 20000.f);
      FHitResult Hit;
      if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic)) {
        if (CellHeights.IsValidIndex(Idx)) {
          CellHeights[Idx] = Hit.Location.Z;
        }
        if (CellRotations.IsValidIndex(Idx)) {
          CellRotations[Idx] =
              FRotationMatrix::MakeFromZ(Hit.Normal.GetSafeNormal()).ToQuat();
        }
        if (Cast<ULandscapeComponent>(Hit.GetComponent())) {
          HandleLandscapeHit(Hit, FIntPoint(X, Y), Idx);
        }
      } else {
        if (CellHeights.IsValidIndex(Idx)) {
          CellHeights[Idx] = Origin.Z;
        }
        if (CellRotations.IsValidIndex(Idx)) {
          CellRotations[Idx] = FQuat::Identity;
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
  const FQuat CellRotation =
      CellRotations.IsValidIndex(ArrayIndex) ? CellRotations[ArrayIndex]
                                             : FQuat::Identity;
  const FVector CellNormal = CellRotation.RotateVector(FVector::UpVector).GetSafeNormal();
  const FVector CellTangent =
      CellRotation.RotateVector(FVector::ForwardVector).GetSafeNormal();
  const FVector WorldCenter = GridToWorld(GridCoord);
  FVector DecalLocation =
      WorldCenter + CellNormal * (HighlightHeightOffset + HighlightDecalProjectionDepth * 0.5f);
  const FRotationMatrix DecalBasis = FRotationMatrix::MakeFromXZ(-CellNormal, CellTangent);
  const FRotator DecalRotation = DecalBasis.Rotator();

  const float EffectiveCellSize = FMath::Max(CellSize, KINDA_SMALL_NUMBER);
  const float HalfSize = 0.5f * EffectiveCellSize * HighlightDecalSizeMultiplier;
  Decal->DecalSize = FVector(HighlightDecalProjectionDepth, HalfSize, HalfSize);
  Decal->FadeScreenSize = HighlightDecalFadeScreenSize;
  Decal->SetWorldLocationAndRotation(DecalLocation, DecalRotation);

  if (HighlightDecalLifeSpan > 0.f && HighlightDecalFadeDuration > 0.f) {
    Decal->SetFadeOut(HighlightDecalLifeSpan, HighlightDecalFadeDuration, false);
    ScheduleDecalRemoval(GridCoord, Decal);
  } else {
    Decal->ResetFade();
    ClearDecalRemovalTimer(GridCoord);
  }

  if (DynamicMaterial) {
    DynamicMaterial->SetVectorParameterValue(HighlightDecalColorParameter,
                                             FLinearColor(Color));
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

void UGridOverlayComponent::UpdateBaseGridVisual(const FIntPoint &GridCoord) {
  if (!BaseGridMeshComponent || !IsValidGrid(GridCoord)) {
    return;
  }

  const int32 ArrayIndex = Index(GridCoord);
  if (!BaseGridInstanceIndices.IsValidIndex(ArrayIndex)) {
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
    if (BaseGridMeshComponent) {
      BaseGridMeshComponent->ClearInstances();
    }
    BaseGridInstanceIndices.Empty();
    return;
  }

  if (!EnsureBaseGridComponentSetup()) {
    return;
  }

  const int32 TotalCells = Width * Height;
  if (TotalCells <= 0) {
    BaseGridMeshComponent->ClearInstances();
    BaseGridInstanceIndices.Empty();
    return;
  }

  BaseGridMeshComponent->ClearInstances();
  BaseGridInstanceIndices.Init(INDEX_NONE, TotalCells);

  const FTransform ComponentTransform = BaseGridMeshComponent->GetComponentTransform();
  const float EffectiveCellSize = FMath::Max(CellSize, KINDA_SMALL_NUMBER);
  const FVector InstanceScale(EffectiveCellSize / 100.f, EffectiveCellSize / 100.f, 1.f);

  for (int32 Y = 0; Y < Height; ++Y) {
    for (int32 X = 0; X < Width; ++X) {
      const FIntPoint Cell(X, Y);
      const FVector WorldCenter =
          GridToWorld(Cell) + FVector(0.f, 0.f, GridHeightOffset);
      const int32 ArrayIndex = Index(Cell);
      const FQuat CellRotation =
          CellRotations.IsValidIndex(ArrayIndex) ? CellRotations[ArrayIndex]
                                                 : FQuat::Identity;
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
  const FQuat CellRotation =
      CellRotations.IsValidIndex(ArrayIndex) ? CellRotations[ArrayIndex]
                                             : FQuat::Identity;
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
    return;
  }

  const FIntPoint Cell = WorldToGrid(Fighter->GetActorLocation());
  HighlightCell(Cell, SelectionHighlightColor.ToFColor(true), 0.f, false);
}

void UGridOverlayComponent::ClearHighlights() {
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

#if WITH_EDITOR
  if (GetWorld() && bFlushAllPersistentOnClear) {
    FlushPersistentDebugLines(GetWorld());
  }
#endif
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

void UGridOverlayComponent::ApplyObstacleToGrid(UGridObstacleComponent *Obstacle) {
  if (!Obstacle || !bHasInitializedGrid) {
    return;
  }

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
        if (!Cells.IsValidIndex(Idx) || !ObscuredCells.IsValidIndex(Idx)) {
          continue;
        }
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
        UpdateBaseGridVisual(Cell);
      }
    }
  }
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

  UpdateBaseGridVisual(GridCoord);
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
}

void UGridOverlayComponent::HighlightMovement(AFighterPawn *Fighter) {
  if (!Fighter) {
    return;
  }

  ClearHighlights();

  const FIntPoint StartCell = WorldToGrid(Fighter->GetActorLocation());
  const int32 Range = Fighter->Stats.Movement;

  const FColor SelectionColor = SelectionHighlightColor.ToFColor(true);
  const FColor MovementColor = MovementHighlightColor.ToFColor(true);

  HighlightCell(StartCell, SelectionColor, 0.f, false);

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
      HighlightCell(Cell, MovementColor, 0.f, false);
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

  const FIntPoint StartCell = WorldToGrid(Fighter->GetActorLocation());
  const int32 Range = Fighter->Stats.AttackRange;

  const FColor SelectionColor = SelectionHighlightColor.ToFColor(true);
  const FColor AttackColor = AttackHighlightColor.ToFColor(true);

  HighlightCell(StartCell, SelectionColor, 0.f, false);

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
        HighlightCell(Target, AttackColor, 0.f, false);
      }
    }
  }
}
