#include "FighterPawn.h"
#include "Blueprint/UserWidget.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/DecalComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Containers/Queue.h"
#include "Containers/Set.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "Curves/CurveFloat.h"
#include "GridBattleManager.h"
#include "GridOverlayComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "Skald_GameInstance.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "UI/FighterActivationWidget.h"
#include "UI/FighterHealthWidget.h"

namespace
{
constexpr int32 ActionsPerActivation = 2;
constexpr float ActivationWidgetScale = 0.1f;
constexpr float WidgetMirrorSeparation = 0.5f;
constexpr float AutoHealthHoldFallbackDelay = 2.f;
// Delay applied before the first queued attack roll resolves so dice feedback
// appears in sync with the slower UI/VFX reveal cadence after the player
// commits to an attack.
constexpr float QueuedAttackFirstRollDelaySeconds = 0.48f;
// Consistent pacing between any additional dice in the same queued attack to
// mirror the UI reveal spacing.
constexpr float QueuedAttackAdditionalRollDelaySeconds = 0.4f;
}

AFighterPawn::AFighterPawn() : MaxHealth(0) {
  PrimaryActorTick.bCanEverTick = true;
  PrimaryActorTick.bStartWithTickEnabled = true;

  bReplicates = true;
  SetReplicateMovement(true);

  CollisionComponent =
      CreateDefaultSubobject<UCapsuleComponent>(TEXT("CollisionComponent"));
  CollisionComponent->InitCapsuleSize(40.f, 88.f);
  CollisionComponent->SetCollisionProfileName(
      UCollisionProfile::Pawn_ProfileName);
  CollisionComponent->SetCanEverAffectNavigation(false);
  RootComponent = CollisionComponent;

  DisplayMesh =
      CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DisplayMesh"));
  DisplayMesh->SetupAttachment(CollisionComponent);
  DisplayMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

  HealthWidget = CreateDefaultSubobject<UWidgetComponent>(TEXT("HealthWidget"));
  HealthWidget->SetupAttachment(DisplayMesh);
  HealthWidget->SetTwoSided(true);
  HealthWidget->SetRelativeLocation(
      FVector(0.f, WidgetMirrorSeparation, 250.f));
  HealthWidget->SetRelativeRotation(FRotator(0.f, 90.f, 0.f));
  HealthWidget->SetRelativeScale3D(FVector(0.2f, 1.f, 0.5f));
  HealthWidget->SetCollisionEnabled(ECollisionEnabled::NoCollision);
  HealthWidget->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
  HealthWidget->SetGenerateOverlapEvents(false);
  HealthWidget->SetCanEverAffectNavigation(false);
  HealthWidget->SetWidgetSpace(EWidgetSpace::World);
  HealthWidget->SetDrawAtDesiredSize(true);

  HealthWidgetBack =
      CreateDefaultSubobject<UWidgetComponent>(TEXT("HealthWidgetBack"));
  HealthWidgetBack->SetupAttachment(DisplayMesh);
  HealthWidgetBack->SetTwoSided(true);
  HealthWidgetBack->SetRelativeLocation(
      FVector(0.f, -WidgetMirrorSeparation, 250.f));
  HealthWidgetBack->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
  HealthWidgetBack->SetRelativeScale3D(FVector(0.2f, 1.f, 0.5f));
  HealthWidgetBack->SetCollisionEnabled(ECollisionEnabled::NoCollision);
  HealthWidgetBack->SetCollisionProfileName(
      UCollisionProfile::NoCollision_ProfileName);
  HealthWidgetBack->SetGenerateOverlapEvents(false);
  HealthWidgetBack->SetCanEverAffectNavigation(false);
  HealthWidgetBack->SetWidgetSpace(EWidgetSpace::World);
  HealthWidgetBack->SetDrawAtDesiredSize(true);

  ActivationWidget =
      CreateDefaultSubobject<UWidgetComponent>(TEXT("ActivationWidget"));
  ActivationWidget->SetupAttachment(DisplayMesh);
  ActivationWidget->SetTwoSided(true);
  ActivationWidget->SetWidgetSpace(EWidgetSpace::World);
  ActivationWidget->SetDrawAtDesiredSize(true);
  ActivationWidget->SetRelativeLocation(
      FVector(0.f, WidgetMirrorSeparation, 520.f));
  ActivationWidget->SetRelativeRotation(FRotator(0.f, 90.f, 0.f));
  ActivationWidget->SetRelativeScale3D(FVector(ActivationWidgetScale));
  ActivationWidget->SetCollisionEnabled(ECollisionEnabled::NoCollision);
  ActivationWidget->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
  ActivationWidget->SetGenerateOverlapEvents(false);
  ActivationWidget->SetCanEverAffectNavigation(false);
  ActivationWidget->SetVisibility(false);

  ActivationWidgetBack =
      CreateDefaultSubobject<UWidgetComponent>(TEXT("ActivationWidgetBack"));
  ActivationWidgetBack->SetupAttachment(DisplayMesh);
  ActivationWidgetBack->SetTwoSided(true);
  ActivationWidgetBack->SetWidgetSpace(EWidgetSpace::World);
  ActivationWidgetBack->SetDrawAtDesiredSize(true);
  ActivationWidgetBack->SetRelativeLocation(
      FVector(0.f, -WidgetMirrorSeparation, 520.f));
  ActivationWidgetBack->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
  ActivationWidgetBack->SetRelativeScale3D(FVector(ActivationWidgetScale));
  ActivationWidgetBack->SetCollisionEnabled(ECollisionEnabled::NoCollision);
  ActivationWidgetBack->SetCollisionProfileName(
      UCollisionProfile::NoCollision_ProfileName);
  ActivationWidgetBack->SetGenerateOverlapEvents(false);
  ActivationWidgetBack->SetCanEverAffectNavigation(false);
  ActivationWidgetBack->SetVisibility(false);

  SelectionDecal =
      CreateDefaultSubobject<UDecalComponent>(TEXT("SelectionDecal"));
  SelectionDecal->SetupAttachment(CollisionComponent);
  SelectionDecal->SetRelativeRotation(FRotator(-90.f, 0.f, 0.f));
  SelectionDecal->SetRelativeLocation(
      FVector(0.f, 0.f,
              -CollisionComponent->GetUnscaledCapsuleHalfHeight() +
                  SelectionDecalFloorOffset));
  SelectionDecal->DecalSize = SelectionDecalSizeSingleCell;
  SelectionDecal->SetHiddenInGame(true);
  SelectionDecal->SetVisibility(false);
  SelectionDecal->SetCanEverAffectNavigation(false);

  TargetedDecal =
      CreateDefaultSubobject<UDecalComponent>(TEXT("TargetedDecal"));
  TargetedDecal->SetupAttachment(CollisionComponent);
  TargetedDecal->SetRelativeRotation(FRotator(-90.f, 0.f, 0.f));
  TargetedDecal->SetRelativeLocation(
      FVector(0.f, 0.f,
              -CollisionComponent->GetUnscaledCapsuleHalfHeight() +
                  TargetedDecalFloorOffset));
  TargetedDecal->DecalSize = TargetedDecalSizeSingleCell;
  TargetedDecal->SetHiddenInGame(true);
  TargetedDecal->SetVisibility(false);
  TargetedDecal->SetCanEverAffectNavigation(false);

  MovementAudioComponent =
      CreateDefaultSubobject<UAudioComponent>(TEXT("MovementAudioComponent"));
  MovementAudioComponent->SetupAttachment(CollisionComponent);
  MovementAudioComponent->SetAutoActivate(false);
  MovementAudioComponent->bAutoActivate = false;
  MovementAudioComponent->bAutoDestroy = false;

  HealthWidgetTemplate = UFighterHealthWidget::StaticClass();

  ActivationWidgetTemplate = UFighterActivationWidget::StaticClass();

  ActionsRemaining = 0;
  bHasActivatedThisRound = false;
  bIsCurrentlyActive = false;
  CurrentCell = FIntPoint::ZeroValue;
  FighterId = NAME_None;

  ApplyFootprintScale();
  UpdateMeshOffset();
  UpdateSelectionIndicatorTransform();
  UpdateTargetedIndicatorTransform();
  RefreshSelectionIndicatorMaterial();
  RefreshTargetedIndicatorMaterial();
}

void AFighterPawn::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty> &OutLifetimeProps) const {
  Super::GetLifetimeReplicatedProps(OutLifetimeProps);

  DOREPLIFETIME(AFighterPawn, Stats);
  DOREPLIFETIME(AFighterPawn, FighterId);
  DOREPLIFETIME(AFighterPawn, Faction);
  DOREPLIFETIME(AFighterPawn, FighterPortrait);
  DOREPLIFETIME(AFighterPawn, bIsAttacker);
  DOREPLIFETIME(AFighterPawn, ActionsRemaining);
  DOREPLIFETIME(AFighterPawn, bHasActivatedThisRound);
  DOREPLIFETIME(AFighterPawn, bIsCurrentlyActive);
  DOREPLIFETIME(AFighterPawn, bIsMoving);
  DOREPLIFETIME(AFighterPawn, GridFootprint);
  DOREPLIFETIME(AFighterPawn, CurrentCell);
  DOREPLIFETIME(AFighterPawn, SpawnFacingYawDelta);
  DOREPLIFETIME(AFighterPawn, MaxHealth);
}

void AFighterPawn::OnConstruction(const FTransform &Transform) {
  Super::OnConstruction(Transform);
  ApplyFootprintScale();
  UpdateMeshOffset();
  UpdateSelectionIndicatorSize();
  UpdateSelectionIndicatorTransform();
  RefreshSelectionIndicatorMaterial();
  UpdateTargetedIndicatorSize();
  UpdateTargetedIndicatorTransform();
  RefreshTargetedIndicatorMaterial();
  SetTargetedIndicatorVisible(false);
  RefreshDisplayMeshYawOffset();
  const float IncomingSpawnYaw = Transform.GetRotation().Rotator().Yaw;
  const bool bShouldOverride = ShouldOverrideSpawnFacingYaw();
  SpawnFacingYawDelta =
      bShouldOverride
          ? FRotator::NormalizeAxis(SpawnFacingYaw - IncomingSpawnYaw)
          : 0.f;
  const float DesiredYaw = bShouldOverride ? SpawnFacingYaw : IncomingSpawnYaw;
  ApplyFacingYaw(DesiredYaw);

  RefreshMovementAudioComponent();
}

void AFighterPawn::BeginPlay() {
  Super::BeginPlay();

  InitializeDisplayMeshMaterials();
  ApplyHitFlash(0.f);
  LastKnownHealth = Stats.Health;
  bHasRecordedHealth = true;

  if (HasAuthority() && MaxHealth <= 0) {
    MaxHealth = FMath::Max(Stats.Health, 1);
  }

  const auto InitializeHealthWidgetComponent = [&](UWidgetComponent *Component) {
    if (!Component) {
      return;
    }

    if (HealthWidgetTemplate) {
      Component->SetWidgetClass(HealthWidgetTemplate);
    }
    Component->InitWidget();

    if (UUserWidget *HealthWidgetInstance =
            Cast<UUserWidget>(Component->GetUserWidgetObject())) {
      HealthWidgetInstance->SetVisibility(ESlateVisibility::HitTestInvisible);
      if (UFighterHealthWidget *FighterHealthWidget =
              Cast<UFighterHealthWidget>(HealthWidgetInstance)) {
        FighterHealthWidget->SetHealthValues(Stats.Health, MaxHealth);
      }
    }
  };

  InitializeHealthWidgetComponent(HealthWidget);
  InitializeHealthWidgetComponent(HealthWidgetBack);

  if (ActivationWidget) {
    if (ActivationWidgetTemplate) {
      ActivationWidget->SetWidgetClass(ActivationWidgetTemplate);
    }
    ActivationWidget->InitWidget();
  }

  if (ActivationWidgetBack) {
    if (ActivationWidgetTemplate) {
      ActivationWidgetBack->SetWidgetClass(ActivationWidgetTemplate);
    }
    ActivationWidgetBack->InitWidget();
  }

  OnHealthChanged.AddDynamic(this, &AFighterPawn::HandleHealthChanged);
  OnHealthChanged.Broadcast(Stats.Health);

  BroadcastActionsRemaining();
  UpdateActivationIndicator();
  UpdateSelectionIndicatorSize();
  UpdateSelectionIndicatorTransform();
  RefreshSelectionIndicatorMaterial();
  UpdateTargetedIndicatorSize();
  UpdateTargetedIndicatorTransform();
  RefreshTargetedIndicatorMaterial();
  SetSelectionIndicatorVisible(false);
  SetTargetedIndicatorVisible(false);

  RefreshMovementAudioComponent();

  UpdateMeshOffset();
  RefreshDisplayMeshYawOffset();

  if (UGridOverlayComponent *Grid = GetGrid()) {
    CurrentCell = Grid->WorldToGrid(GetActorLocation());
    AlignToCurrentCell();

    const TArray<FIntPoint> OccupiedCells = GetOccupiedCells();
    for (const FIntPoint &Cell : OccupiedCells) {
      Grid->SetOccupied(Cell, true);
    }
  }

  MovementTargetLocation = GetActorLocation();

  // Ensure the configured spawn facing is applied on all clients while
  // respecting the display mesh's relative rotation.
  const bool bShouldOverride = ShouldOverrideSpawnFacingYaw();
  if (!bShouldOverride) {
    SpawnFacingYawDelta = 0.f;
  } else if (!HasAuthority() &&
             FMath::IsNearlyZero(SpawnFacingYawDelta, KINDA_SMALL_NUMBER)) {
    const float IncomingSpawnYaw = GetCurrentWorldFacingYaw();
    SpawnFacingYawDelta = FRotator::NormalizeAxis(SpawnFacingYaw - IncomingSpawnYaw);
  }
  const float DesiredYaw =
      bShouldOverride ? SpawnFacingYaw : GetCurrentWorldFacingYaw();
  ApplyFacingYaw(DesiredYaw);

  if (UWorld *World = GetWorld()) {
    if (USkaldGameInstance *GI =
            Cast<USkaldGameInstance>(World->GetGameInstance())) {
      if (GI->GridBattleManager) {
        GI->GridBattleManager->RegisterFighter(this, bIsAttacker);
      }
    }
  }
}

UTexture2D *AFighterPawn::GetPortraitTexture() const {
  if (FighterPortrait.IsValid()) {
    return FighterPortrait.Get();
  }

  if (!FighterPortrait.IsNull()) {
    return FighterPortrait.LoadSynchronous();
  }

  return nullptr;
}

void AFighterPawn::InitializeMaxHealth(int32 InMaxHealth) {
  MaxHealth = FMath::Max(1, InMaxHealth);
  UpdateHealthDisplay(Stats.Health);
}

void AFighterPawn::Tick(float DeltaSeconds) {
  Super::Tick(DeltaSeconds);

  UpdateHitFlash(DeltaSeconds);

  const bool bShouldRunMovement =
      bIsMoving && (HasAuthority() || IsLocallyControlled());

  if (!bShouldRunMovement) {
    return;
  }

  const FVector CurrentLocation = GetActorLocation();
  const float EffectiveSpeed = FMath::Max(MovementSpeed, KINDA_SMALL_NUMBER);
  const FVector NextLocation =
      FMath::VInterpConstantTo(CurrentLocation, MovementTargetLocation, DeltaSeconds,
                               EffectiveSpeed);
  SetActorLocation(NextLocation);

  const float EffectiveTolerance =
      FMath::Max(MovementStopTolerance, KINDA_SMALL_NUMBER);
  if (FVector::DistSquared(NextLocation, MovementTargetLocation) <=
      FMath::Square(EffectiveTolerance)) {
    SetActorLocation(MovementTargetLocation);
    SetIsMoving(false);
    MovementTargetLocation = GetActorLocation();
  }
}

void AFighterPawn::BeginActivation() {
  if (!IsAlive()) {
    return;
  }

  ActionsRemaining = ActionsPerActivation;
  bIsCurrentlyActive = true;
  bHasActivatedThisRound = true;

  BroadcastActionsRemaining();
  UpdateActivationIndicator();

  if (UGridOverlayComponent *Grid = GetGrid()) {
    Grid->HighlightSelection(this);
  }
}

void AFighterPawn::ResetActivationState() {
  ActionsRemaining = ActionsPerActivation;
  bHasActivatedThisRound = false;
  bIsCurrentlyActive = false;

  BroadcastActionsRemaining();
  UpdateActivationIndicator();
}

void AFighterPawn::FinishActivation() {
  ActionsRemaining = 0;
  bIsCurrentlyActive = false;

  BroadcastActionsRemaining();
  UpdateActivationIndicator();

  if (UGridOverlayComponent *Grid = GetGrid()) {
    Grid->ClearSelectionHighlight();
  }
}

UGridOverlayComponent *AFighterPawn::GetGrid() const {
  if (IsValid(CachedGrid) &&
      Skald::GridOverlay::IsComponentFromVisibleLevel(CachedGrid)) {
    return CachedGrid;
  }

  CachedGrid = nullptr;

  if (UWorld *World = GetWorld()) {
    CachedGrid = Skald::GridOverlay::FindActiveGridOverlay(World);
  }

  return CachedGrid;
}

void AFighterPawn::SetSelectionIndicatorVisible(bool bVisible) {
  if (!SelectionDecal) {
    return;
  }

  SelectionDecal->SetVisibility(bVisible);
  SelectionDecal->SetHiddenInGame(!bVisible);
}

void AFighterPawn::SetTargetedIndicatorVisible(bool bVisible) {
  if (!TargetedDecal) {
    return;
  }

  TargetedDecal->SetVisibility(bVisible);
  TargetedDecal->SetHiddenInGame(!bVisible);
}

FIntPoint AFighterPawn::GetCurrentCell() const { return CurrentCell; }

int32 AFighterPawn::GetFootprintSideLength() const {
  return GridFootprint == EFighterPawnFootprint::FourCells ? 2 : 1;
}

TArray<FIntPoint> AFighterPawn::GetOccupiedCells(const FIntPoint &Anchor) const {
  const int32 SideLength = GetFootprintSideLength();
  TArray<FIntPoint> Cells;
  Cells.Reserve(SideLength * SideLength);

  for (int32 Y = 0; Y < SideLength; ++Y) {
    for (int32 X = 0; X < SideLength; ++X) {
      Cells.Add(Anchor + FIntPoint(X, Y));
    }
  }

  return Cells;
}

bool AFighterPawn::OccupiesCell(const FIntPoint &Cell) const {
  const TArray<FIntPoint> OccupiedCells = GetOccupiedCells();
  return OccupiedCells.Contains(Cell);
}

int32 AFighterPawn::GetFootprintDistanceToCell(const FIntPoint &Cell,
                                               FIntPoint *OutClosestCell) const {
  const TArray<FIntPoint> OccupiedCells = GetOccupiedCells();
  int32 BestDistance = TNumericLimits<int32>::Max();
  FIntPoint BestCell = CurrentCell;

  for (const FIntPoint &SelfCell : OccupiedCells) {
    const int32 Distance = FMath::Max(
        FMath::Abs(SelfCell.X - Cell.X), FMath::Abs(SelfCell.Y - Cell.Y));
    if (Distance < BestDistance) {
      BestDistance = Distance;
      BestCell = SelfCell;
      if (Distance == 0) {
        break;
      }
    }
  }

  if (OutClosestCell) {
    *OutClosestCell = BestCell;
  }

  return BestDistance;
}

int32 AFighterPawn::GetFootprintDistanceToFighter(
    const AFighterPawn *Other, FIntPoint *OutSelfCell,
    FIntPoint *OutOtherCell) const {
  if (!Other) {
    if (OutSelfCell) {
      *OutSelfCell = CurrentCell;
    }
    if (OutOtherCell) {
      *OutOtherCell = FIntPoint::ZeroValue;
    }
    return TNumericLimits<int32>::Max();
  }

  const TArray<FIntPoint> SelfCells = GetOccupiedCells();
  const TArray<FIntPoint> OtherCells = Other->GetOccupiedCells();

  int32 BestDistance = TNumericLimits<int32>::Max();
  FIntPoint BestSelf = CurrentCell;
  FIntPoint BestOther = Other->GetCurrentCell();

  for (const FIntPoint &SelfCell : SelfCells) {
    for (const FIntPoint &OtherCell : OtherCells) {
      const int32 Distance = FMath::Max(
          FMath::Abs(SelfCell.X - OtherCell.X),
          FMath::Abs(SelfCell.Y - OtherCell.Y));
      if (Distance < BestDistance) {
        BestDistance = Distance;
        BestSelf = SelfCell;
        BestOther = OtherCell;
        if (Distance == 0) {
          break;
        }
      }
    }
    if (BestDistance == 0) {
      break;
    }
  }

  if (OutSelfCell) {
    *OutSelfCell = BestSelf;
  }
  if (OutOtherCell) {
    *OutOtherCell = BestOther;
  }

  return BestDistance;
}

bool AFighterPawn::HasLineOfSightToFighter(
    const AFighterPawn *Other, int32 Range, UGridOverlayComponent *Grid,
    FIntPoint *OutSelfCell, FIntPoint *OutOtherCell) const {
  if (!Grid || !Other) {
    return false;
  }

  const TArray<FIntPoint> SelfCells = GetOccupiedCells();
  const TArray<FIntPoint> OtherCells = Other->GetOccupiedCells();

  bool bFoundLine = false;
  int32 BestDistance = TNumericLimits<int32>::Max();
  FIntPoint BestSelf = CurrentCell;
  FIntPoint BestOther = Other->GetCurrentCell();

  for (const FIntPoint &SelfCell : SelfCells) {
    for (const FIntPoint &OtherCell : OtherCells) {
      const int32 Distance = FMath::Max(
          FMath::Abs(SelfCell.X - OtherCell.X),
          FMath::Abs(SelfCell.Y - OtherCell.Y));
      if (Distance > Range) {
        continue;
      }
      if (!Grid->HasLineOfSight(SelfCell, OtherCell)) {
        continue;
      }

      if (!bFoundLine || Distance < BestDistance) {
        BestDistance = Distance;
        BestSelf = SelfCell;
        BestOther = OtherCell;
        bFoundLine = true;
        if (BestDistance == 0) {
          break;
        }
      }
    }
    if (BestDistance == 0) {
      break;
    }
  }

  if (!bFoundLine) {
    return false;
  }

  if (OutSelfCell) {
    *OutSelfCell = BestSelf;
  }
  if (OutOtherCell) {
    *OutOtherCell = BestOther;
  }

  return true;
}

void AFighterPawn::ApplyFootprintScale() {
  if (DisplayMesh) {
    const float Scale =
        GridFootprint == EFighterPawnFootprint::FourCells ? 2.f : 1.f;
    DisplayMesh->SetRelativeScale3D(FVector(Scale));
  }

  UpdateSelectionIndicatorSize();
  UpdateTargetedIndicatorSize();
}

void AFighterPawn::UpdateSelectionIndicatorSize() {
  if (!SelectionDecal) {
    return;
  }

  const FVector DesiredSize =
      GridFootprint == EFighterPawnFootprint::FourCells
          ? SelectionDecalSizeFourCells
          : SelectionDecalSizeSingleCell;
  SelectionDecal->DecalSize = DesiredSize;
}

void AFighterPawn::UpdateSelectionIndicatorTransform() {
  if (!SelectionDecal || !CollisionComponent) {
    return;
  }

  const float CapsuleHalfHeight = CollisionComponent->GetScaledCapsuleHalfHeight();
  const float VerticalOffset = -CapsuleHalfHeight + SelectionDecalFloorOffset;
  SelectionDecal->SetRelativeLocation(FVector(0.f, 0.f, VerticalOffset));
}

void AFighterPawn::RefreshSelectionIndicatorMaterial() {
  if (!SelectionDecal) {
    return;
  }

  SelectionDecal->SetDecalMaterial(SelectionDecalMaterial);
}

void AFighterPawn::UpdateTargetedIndicatorSize() {
  if (!TargetedDecal) {
    return;
  }

  const FVector DesiredSize =
      GridFootprint == EFighterPawnFootprint::FourCells
          ? TargetedDecalSizeFourCells
          : TargetedDecalSizeSingleCell;
  TargetedDecal->DecalSize = DesiredSize;
}

void AFighterPawn::UpdateTargetedIndicatorTransform() {
  if (!TargetedDecal || !CollisionComponent) {
    return;
  }

  const float CapsuleHalfHeight = CollisionComponent->GetScaledCapsuleHalfHeight();
  const float VerticalOffset = -CapsuleHalfHeight + TargetedDecalFloorOffset;
  TargetedDecal->SetRelativeLocation(FVector(0.f, 0.f, VerticalOffset));
}

void AFighterPawn::RefreshTargetedIndicatorMaterial() {
  if (!TargetedDecal) {
    return;
  }

  TargetedDecal->SetDecalMaterial(TargetedDecalMaterial);
}

void AFighterPawn::RefreshDisplayMeshYawOffset() {
  if (DisplayMesh) {
    DisplayMeshYawOffset =
        FRotator::NormalizeAxis(DisplayMesh->GetRelativeRotation().Yaw);
  } else {
    DisplayMeshYawOffset = 0.f;
  }
}

FVector AFighterPawn::GetAlignedWorldLocation(const FIntPoint &Anchor) const {
  if (UGridOverlayComponent *Grid = GetGrid()) {
    const TArray<FIntPoint> Cells = GetOccupiedCells(Anchor);
    FVector AccumulatedLocation = FVector::ZeroVector;
    int32 CellCount = 0;

    for (const FIntPoint &Cell : Cells) {
      AccumulatedLocation += Grid->GridToWorld(Cell);
      ++CellCount;
    }

    if (CellCount > 0) {
      AccumulatedLocation /= static_cast<float>(CellCount);
      AccumulatedLocation.Z += GetSimpleCollisionHalfHeight();
      return AccumulatedLocation;
    }
  }

  return GetActorLocation();
}

void AFighterPawn::AlignToCurrentCell() {
  if (UGridOverlayComponent *Grid = GetGrid()) {
    const FIntPoint DerivedCell = Grid->WorldToGrid(GetActorLocation());
    if (DerivedCell != CurrentCell) {
      return;
    }

    const FVector AlignedLocation = GetAlignedWorldLocation(CurrentCell);
    SetActorLocation(AlignedLocation);
    MovementTargetLocation = AlignedLocation;
  }
}

void AFighterPawn::MoveToCell(FIntPoint TargetCell) {
  if (!bIsCurrentlyActive || ActionsRemaining <= 0) {
    return;
  }
  const int32 Distance = FMath::Max(
      FMath::Abs(TargetCell.X - CurrentCell.X),
      FMath::Abs(TargetCell.Y - CurrentCell.Y));
  if (Distance > Stats.Movement) {
    return;
  }
  UGridOverlayComponent *Grid = GetGrid();
  const FIntPoint PreviousCell = CurrentCell;

  TArray<FIntPoint> PreviousCells;
  TArray<FIntPoint> TargetCells;

  if (Grid) {
    PreviousCells = GetOccupiedCells();
    TargetCells = GetOccupiedCells(TargetCell);

    bool bCanOccupyTarget = true;
    for (const FIntPoint &Cell : TargetCells) {
      if (!Grid->IsCellInBounds(Cell) || Grid->IsObscured(Cell)) {
        bCanOccupyTarget = false;
        break;
      }
      const bool bCellPreviouslyOccupied = PreviousCells.Contains(Cell);
      if (!bCellPreviouslyOccupied && Grid->IsOccupied(Cell)) {
        bCanOccupyTarget = false;
        break;
      }
    }

    if (!bCanOccupyTarget) {
      return;
    }

    bool bTargetReachable = CurrentCell == TargetCell;

    if (!bTargetReachable) {
      TSet<FIntPoint> IgnoredCells;
      for (const FIntPoint &Cell : PreviousCells) {
        IgnoredCells.Add(Cell);
      }

      auto CanOccupyAnchor = [&](const FIntPoint &Anchor) {
        const TArray<FIntPoint> CandidateCells = GetOccupiedCells(Anchor);
        for (const FIntPoint &Cell : CandidateCells) {
          if (!Grid->IsCellInBounds(Cell) || Grid->IsObscured(Cell)) {
            return false;
          }
          if (Grid->IsOccupied(Cell) && !IgnoredCells.Contains(Cell)) {
            return false;
          }
        }
        return true;
      };

      static const FIntPoint Directions[8] = {
          FIntPoint(1, 0),  FIntPoint(-1, 0), FIntPoint(0, 1),  FIntPoint(0, -1),
          FIntPoint(1, 1),  FIntPoint(1, -1), FIntPoint(-1, 1), FIntPoint(-1, -1)};

      TSet<FIntPoint> Visited;
      TQueue<TPair<FIntPoint, int32>> Frontier;
      Visited.Add(CurrentCell);
      Frontier.Enqueue(TPair<FIntPoint, int32>(CurrentCell, 0));

      while (!Frontier.IsEmpty()) {
        TPair<FIntPoint, int32> Node;
        Frontier.Dequeue(Node);

        const FIntPoint Cell = Node.Key;
        const int32 DistanceFromStart = Node.Value;

        if (Cell == TargetCell) {
          bTargetReachable = true;
          break;
        }

        if (DistanceFromStart >= Stats.Movement) {
          continue;
        }

        for (const FIntPoint &Dir : Directions) {
          const FIntPoint Next = Cell + Dir;
          if (Visited.Contains(Next)) {
            continue;
          }
          if (!CanOccupyAnchor(Next)) {
            continue;
          }
          if (Dir.X != 0 && Dir.Y != 0) {
            const TArray<FIntPoint> FromCells = GetOccupiedCells(Cell);
            const TArray<FIntPoint> NextCells = GetOccupiedCells(Next);
            TSet<FIntPoint> NextCellSet;
            NextCellSet.Reserve(NextCells.Num());
            for (const FIntPoint &NextCell : NextCells) {
              NextCellSet.Add(NextCell);
            }

            auto IsBlocked = [&](const FIntPoint &CheckCell) {
              if (!Grid->IsCellInBounds(CheckCell) || Grid->IsObscured(CheckCell)) {
                return true;
              }
              if (Grid->IsOccupied(CheckCell) && !IgnoredCells.Contains(CheckCell) &&
                  !NextCellSet.Contains(CheckCell)) {
                return true;
              }
              return false;
            };

            bool bDiagonalClear = true;
            for (const FIntPoint &FromCell : FromCells) {
              if (IsBlocked(FromCell + FIntPoint(Dir.X, 0)) ||
                  IsBlocked(FromCell + FIntPoint(0, Dir.Y))) {
                bDiagonalClear = false;
                break;
              }
            }

            if (!bDiagonalClear) {
              continue;
            }
          }

          Visited.Add(Next);
          Frontier.Enqueue(TPair<FIntPoint, int32>(Next, DistanceFromStart + 1));
        }
      }
    }

    if (!bTargetReachable) {
      return;
    }

    for (const FIntPoint &Cell : PreviousCells) {
      Grid->SetOccupied(Cell, false);
    }
  }

  CurrentCell = TargetCell;
  FVector NewLocation = Grid ? GetAlignedWorldLocation(TargetCell)
                             : GetActorLocation();
  MovementTargetLocation = NewLocation;
  RefreshDisplayMeshYawOffset();
  FaceTowardsCells(PreviousCell, TargetCell);
  FaceTowardsLocation(NewLocation);

  const float EffectiveTolerance =
      FMath::Max(MovementStopTolerance, KINDA_SMALL_NUMBER);
  const bool bAlreadyAtTarget =
      GetActorLocation().Equals(MovementTargetLocation, EffectiveTolerance);

  if (MovementSpeed <= KINDA_SMALL_NUMBER || bAlreadyAtTarget) {
    SetActorLocation(MovementTargetLocation);
    MovementTargetLocation = GetActorLocation();
    SetIsMoving(false);
  } else {
    SetIsMoving(true);
  }
  ActionsRemaining = FMath::Max(0, ActionsRemaining - 1);

  BroadcastActionsRemaining();

  if (Grid) {
    for (const FIntPoint &Cell : TargetCells) {
      Grid->SetOccupied(Cell, true);
    }
    Grid->ClearHighlights();
  }
}

void AFighterPawn::PerformAttack(AFighterPawn *Target) {
  if (!bIsCurrentlyActive || ActionsRemaining <= 0 || !Target ||
      !Target->IsAlive()) {
    return;
  }

  if (IsResolvingQueuedAttack()) {
    if (UWorld *World = GetWorld()) {
      if (USkaldGameInstance *GameInstance =
              Cast<USkaldGameInstance>(World->GetGameInstance())) {
        if (UGridBattleManager *BattleManager =
                GameInstance->GridBattleManager) {
          BattleManager->ReportAttackRejected(
              this, Target,
              NSLOCTEXT("SkaldBattle", "AttackInProgress", "Attack in progress."));
        }
      }
    }
    return;
  }

  FIntPoint ClosestSelfCell = CurrentCell;
  FIntPoint ClosestTargetCell = Target->GetCurrentCell();
  const int32 Distance =
      GetFootprintDistanceToFighter(Target, &ClosestSelfCell, &ClosestTargetCell);
  if (Distance > Stats.AttackRange) {
    return;
  }

  UGridOverlayComponent *Grid = GetGrid();
  if (Grid &&
      !HasLineOfSightToFighter(Target, Stats.AttackRange, Grid, &ClosestSelfCell,
                               &ClosestTargetCell)) {
    return;
  }

  FRandomStream *RandomStream = nullptr;
  if (UWorld *World = GetWorld()) {
    if (USkaldGameInstance *GameInstance =
            Cast<USkaldGameInstance>(World->GetGameInstance())) {
      RandomStream = &GameInstance->CombatRandomStream;
    }
  }
  if (!RandomStream) {
    return;
  }

  FDiceRollResult DiceResult =
      UGridBattleManager::ResolveAttackDice(Stats, Target->Stats, *RandomStream);
  DiceResult.HighStakesFaction = Faction;

  StartQueuedAttack(Target, MoveTemp(DiceResult));

  ActionsRemaining = FMath::Max(0, ActionsRemaining - 1);

  BroadcastActionsRemaining();

  RefreshDisplayMeshYawOffset();
  FaceTowardsCells(CurrentCell, Target->CurrentCell);
  FaceTowardsLocation(Target->GetActorLocation());

  if (Grid) {
    Grid->ClearHighlights();
  }
}

bool AFighterPawn::IsResolvingQueuedAttack() const {
  return PendingAttackTarget.IsValid() || bHasPendingDiceResult;
}

void AFighterPawn::StartQueuedAttack(AFighterPawn *Target,
                                     FDiceRollResult &&DiceResult) {
  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(AttackRollTimerHandle);
  }

  PendingAttackDiceResult = MoveTemp(DiceResult);
  PendingAttackOutcomeIndex = 0;
  PendingAttackTarget = Target;
  bPendingAttackTargetDied = false;
  bHasProcessedPendingRoll = false;
  bHasPendingDiceResult = true;

  if (AFighterPawn *TargetPawn = PendingAttackTarget.Get()) {
    TargetPawn->HandleIncomingAttackStarted();
  }

  if (!PendingAttackDiceResult.DiceOutcomes.IsValidIndex(0)) {
    FinalizeQueuedAttack();
    return;
  }

  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().SetTimer(
        AttackRollTimerHandle, this, &AFighterPawn::ResolveNextAttackRoll,
        QueuedAttackFirstRollDelaySeconds, false);
  } else {
    ResolveNextAttackRoll();
  }
}

void AFighterPawn::ResolveNextAttackRoll() {
  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(AttackRollTimerHandle);
  }

  if (!PendingAttackDiceResult.DiceOutcomes.IsValidIndex(
          PendingAttackOutcomeIndex)) {
    FinalizeQueuedAttack();
    return;
  }

  AFighterPawn *Target = PendingAttackTarget.Get();
  if (!Target) {
    FinalizeQueuedAttack();
    return;
  }

  const FDiceRollOutcome &Outcome =
      PendingAttackDiceResult.DiceOutcomes[PendingAttackOutcomeIndex];
  bHasProcessedPendingRoll = true;

  if (Outcome.bHit) {
    const int32 DamageToApply =
        FMath::Min(Outcome.Damage, Target->Stats.Health);
    Target->Stats.Health =
        FMath::Max(0, Target->Stats.Health - DamageToApply);
    if (Target->Stats.Health <= 0) {
      bPendingAttackTargetDied = true;
    }
  }

  Target->OnHealthChanged.Broadcast(Target->Stats.Health);

  ++PendingAttackOutcomeIndex;

  const bool bHasMoreRolls =
      PendingAttackDiceResult.DiceOutcomes.IsValidIndex(
          PendingAttackOutcomeIndex);
  const bool bTargetAlive = Target->IsAlive();

  if (bHasMoreRolls && bTargetAlive) {
    if (UWorld *WorldPtr = GetWorld()) {
      const float NextDelay = QueuedAttackAdditionalRollDelaySeconds;
      WorldPtr->GetTimerManager().SetTimer(
          AttackRollTimerHandle, this, &AFighterPawn::ResolveNextAttackRoll,
          NextDelay, false);
    } else {
      ResolveNextAttackRoll();
    }
  } else {
    FinalizeQueuedAttack();
  }
}

void AFighterPawn::FinalizeQueuedAttack() {
  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(AttackRollTimerHandle);
  }

  AFighterPawn *Target = PendingAttackTarget.Get();
  if (Target) {
    if (!bPendingAttackTargetDied && !bHasProcessedPendingRoll) {
      Target->OnHealthChanged.Broadcast(Target->Stats.Health);
    }

    if (bHasPendingDiceResult) {
      PendingAttackDiceResult.EndingHealth = Target->Stats.Health;

      if (USkaldGameInstance *GI =
              Cast<USkaldGameInstance>(GetGameInstance())) {
        if (UGridBattleManager *BattleManager = GI->GridBattleManager) {
          BattleManager->ReportAttackResolution(this, Target,
                                               PendingAttackDiceResult);
        }
      }
    }
  }

  ClearQueuedAttackState(true);

  if (Target && !Target->IsAlive() && !Target->IsActorBeingDestroyed()) {
    if (UWorld *WorldPtr = GetWorld()) {
      const TWeakObjectPtr<AFighterPawn> TargetPtr = Target;
      WorldPtr->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda(
          [TargetPtr]() {
            if (AFighterPawn *TargetPawn = TargetPtr.Get()) {
              if (!TargetPawn->IsActorBeingDestroyed()) {
                TargetPawn->Destroy();
              }
            }
          }));
    } else {
      Target->Destroy();
    }
  }
}

void AFighterPawn::CancelQueuedAttack() {
  if (!IsResolvingQueuedAttack()) {
    return;
  }

  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(AttackRollTimerHandle);
  }

  ClearQueuedAttackState(true);
}

void AFighterPawn::ClearQueuedAttackState(bool bBroadcastFinalized) {
  if (AFighterPawn *TargetPawn = PendingAttackTarget.Get()) {
    TargetPawn->HandleIncomingAttackFinished();
  }

  bHasPendingDiceResult = false;
  PendingAttackDiceResult = FDiceRollResult();

  PendingAttackOutcomeIndex = 0;
  PendingAttackTarget.Reset();
  bPendingAttackTargetDied = false;
  bHasProcessedPendingRoll = false;

  if (bBroadcastFinalized) {
    OnQueuedAttackFinalized.Broadcast();
  }
}

void AFighterPawn::HandleIncomingAttackStarted() {
  ++ActiveIncomingAttackCount;
  SetTargetedIndicatorVisible(true);
}

void AFighterPawn::HandleIncomingAttackFinished() {
  if (ActiveIncomingAttackCount > 0) {
    --ActiveIncomingAttackCount;
  }

  if (ActiveIncomingAttackCount <= 0) {
    ActiveIncomingAttackCount = 0;
    SetTargetedIndicatorVisible(false);
  }
}

void AFighterPawn::InitializeDisplayMeshMaterials() {
  if (!DisplayMesh) {
    return;
  }

  bool bHasValidMIDs = CachedDisplayMeshMIDs.Num() > 0;
  if (bHasValidMIDs) {
    for (UMaterialInstanceDynamic *MID : CachedDisplayMeshMIDs) {
      if (!MID) {
        bHasValidMIDs = false;
        break;
      }
    }
  }

  if (bHasValidMIDs) {
    return;
  }

  CachedDisplayMeshMIDs.Reset();
  const int32 MaterialCount = DisplayMesh->GetNumMaterials();
  CachedDisplayMeshMIDs.Reserve(MaterialCount);

  for (int32 Index = 0; Index < MaterialCount; ++Index) {
    if (UMaterialInterface *Material = DisplayMesh->GetMaterial(Index)) {
      if (UMaterialInstanceDynamic *MID =
              DisplayMesh->CreateDynamicMaterialInstance(Index, Material)) {
        CachedDisplayMeshMIDs.Add(MID);
        if (MID) {
          MID->SetScalarParameterValue(HitFlashParameterName, 0.f);
        }
      }
    }
  }
}

void AFighterPawn::TriggerHitFlash(float DamageRatio) {
  InitializeDisplayMeshMaterials();

  if (CachedDisplayMeshMIDs.Num() == 0) {
    return;
  }

  const float ClampedRatio = FMath::Clamp(DamageRatio, 0.f, 1.f);
  HitFlashStrength = FMath::GetMappedRangeValueClamped(FVector2D(0.f, 1.f),
                                                       FVector2D(0.45f, 1.f),
                                                       ClampedRatio);
  HitFlashElapsed = 0.f;
  bHitFlashActive = true;

  const float InitialValue = HitFlashCurve ? HitFlashCurve->GetFloatValue(0.f)
                                           : 1.f;
  ApplyHitFlash(InitialValue * HitFlashStrength);
}

void AFighterPawn::PlayImpactFlashForDamage(int32 DamageAmount) {
  const int32 ClampedDamage = FMath::Max(DamageAmount, 0);
  const int32 EffectiveMaxHealth = FMath::Max(MaxHealth, 1);
  const float DamageRatio = static_cast<float>(ClampedDamage) /
                            static_cast<float>(EffectiveMaxHealth);
  TriggerHitFlash(DamageRatio);
}

void AFighterPawn::UpdateHitFlash(float DeltaSeconds) {
  if (!bHitFlashActive) {
    return;
  }

  const float SafeDelta = FMath::Max(DeltaSeconds, 0.f);
  HitFlashElapsed += SafeDelta;

  float EffectiveDuration = HitFlashDuration;
  if (HitFlashCurve) {
    float MinTime = 0.f;
    float MaxTime = 0.f;
    HitFlashCurve->GetTimeRange(MinTime, MaxTime);
    EffectiveDuration = FMath::Max(MaxTime, KINDA_SMALL_NUMBER);
  }

  EffectiveDuration = FMath::Max(EffectiveDuration, KINDA_SMALL_NUMBER);
  const float Normalised = FMath::Clamp(HitFlashElapsed / EffectiveDuration,
                                        0.f, 1.f);

  const float CurveValue = HitFlashCurve
                               ? HitFlashCurve->GetFloatValue(HitFlashElapsed)
                               : 1.f - Normalised;
  ApplyHitFlash(FMath::Max(0.f, CurveValue) * HitFlashStrength);

  if (HitFlashElapsed >= EffectiveDuration) {
    bHitFlashActive = false;
    ApplyHitFlash(0.f);
  }
}

void AFighterPawn::ApplyHitFlash(float NormalisedValue) {
  if (CachedDisplayMeshMIDs.Num() == 0) {
    return;
  }

  const float ClampedValue = FMath::Clamp(NormalisedValue, 0.f, 1.5f);
  for (UMaterialInstanceDynamic *MID : CachedDisplayMeshMIDs) {
    if (MID) {
      MID->SetScalarParameterValue(HitFlashParameterName, ClampedValue);
    }
  }
}

void AFighterPawn::UpdateMeshOffset() {
  if (DisplayMesh && CollisionComponent) {
    const float HalfHeight = CollisionComponent->GetUnscaledCapsuleHalfHeight();
    DisplayMesh->SetRelativeLocation(FVector(0.f, 0.f, -HalfHeight));
  }
}

bool AFighterPawn::IsAlive() const { return Stats.Health > 0; }

void AFighterPawn::UpdateHealthDisplay(int32 NewHealth) {
  const int32 SafeMax = MaxHealth > 0 ? MaxHealth : FMath::Max(1, NewHealth);

  if (!bHasRecordedHealth) {
    LastKnownHealth = NewHealth;
    bHasRecordedHealth = true;
  } else if (NewHealth < LastKnownHealth) {
    const int32 Damage = LastKnownHealth - NewHealth;
    const int32 EffectiveMax = FMath::Max(SafeMax, 1);
    const float DamageRatio = static_cast<float>(Damage) /
                              static_cast<float>(EffectiveMax);
    TriggerHitFlash(DamageRatio);
  }

  const auto ApplyHealthToComponent = [&](UWidgetComponent *Component) {
    if (!Component) {
      return;
    }

    if (UFighterHealthWidget *Widget =
            Cast<UFighterHealthWidget>(Component->GetUserWidgetObject())) {
      Widget->SetHealthValues(NewHealth, SafeMax);
    }
  };

  ApplyHealthToComponent(HealthWidget);
  ApplyHealthToComponent(HealthWidgetBack);

  LastKnownHealth = NewHealth;
}

void AFighterPawn::HandleHealthChanged(int32 NewHealth) {
  PendingHealthDisplayValue = NewHealth;
  bHasPendingHealthDisplay = true;

  const bool bHasPreviousHealth = bHasRecordedHealth;
  const bool bDamageTaken = bHasPreviousHealth && NewHealth < LastKnownHealth;

  if (bDamageTaken && !bHoldHealthDisplay) {
    bHoldHealthDisplay = true;
    bAutoHealthDisplayHoldActive = true;
    bHealthDisplayHoldClaimed = false;

    if (UWorld *World = GetWorld()) {
      World->GetTimerManager().ClearTimer(AutoHealthHoldTimerHandle);
      World->GetTimerManager().SetTimer(
          AutoHealthHoldTimerHandle, this,
          &AFighterPawn::HandleAutoHealthHoldExpired,
          AutoHealthHoldFallbackDelay, false);
    }

    return;
  }

  if (!bHoldHealthDisplay) {
    bHasPendingHealthDisplay = false;
    UpdateHealthDisplay(NewHealth);
  }
}

void AFighterPawn::HoldHealthDisplay(int32 DisplayHealth) {
  bHoldHealthDisplay = true;
  bHealthDisplayHoldClaimed = true;
  bAutoHealthDisplayHoldActive = false;

  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(AutoHealthHoldTimerHandle);
  }

  UpdateHealthDisplay(FMath::Max(0, DisplayHealth));
}

void AFighterPawn::ReleaseHealthDisplayHold() {
  const bool bWasHeld = bHoldHealthDisplay;
  bHoldHealthDisplay = false;
  bAutoHealthDisplayHoldActive = false;
  bHealthDisplayHoldClaimed = false;

  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(AutoHealthHoldTimerHandle);
  }

  if (!bHasPendingHealthDisplay) {
    if (bWasHeld) {
      UpdateHealthDisplay(Stats.Health);
    }
    return;
  }

  const int32 HealthToDisplay = PendingHealthDisplayValue;
  bHasPendingHealthDisplay = false;
  UpdateHealthDisplay(HealthToDisplay);
}

void AFighterPawn::HandleAutoHealthHoldExpired() {
  if (!bAutoHealthDisplayHoldActive || bHealthDisplayHoldClaimed) {
    return;
  }

  bAutoHealthDisplayHoldActive = false;
  ReleaseHealthDisplayHold();
}

void AFighterPawn::Destroyed() {
  CancelQueuedAttack();
  FinishActivation();
  if (UGridOverlayComponent *Grid = GetGrid()) {
    const TArray<FIntPoint> OccupiedCells = GetOccupiedCells();
    for (const FIntPoint &Cell : OccupiedCells) {
      Grid->SetOccupied(Cell, false);
    }
  }
  if (UWorld *World = GetWorld()) {
    if (USkaldGameInstance *GI =
            Cast<USkaldGameInstance>(World->GetGameInstance())) {
      if (GI->GridBattleManager) {
        GI->GridBattleManager->UnregisterFighter(this);
      }
    }
  }
  CurrentCell = FIntPoint::ZeroValue;
  Super::Destroyed();
}

void AFighterPawn::OnRep_Stats(const FFighterStats &OldStats) {
  if (!bHasRecordedHealth) {
    LastKnownHealth = OldStats.Health > 0 ? OldStats.Health : Stats.Health;
    bHasRecordedHealth = true;
  }

  if (Stats.Health != OldStats.Health) {
    const bool bHadListeners = OnHealthChanged.IsBound();
    OnHealthChanged.Broadcast(Stats.Health);
    if (!bHadListeners) {
      UpdateHealthDisplay(Stats.Health);
    }
  } else if (!OnHealthChanged.IsBound()) {
    UpdateHealthDisplay(Stats.Health);
  }
}

void AFighterPawn::OnRep_MaxHealth() { UpdateHealthDisplay(Stats.Health); }

void AFighterPawn::OnRep_ActionsRemaining() { BroadcastActionsRemaining(); }

void AFighterPawn::OnRep_GridFootprint() {
  ApplyFootprintScale();
  UpdateMeshOffset();
  UpdateSelectionIndicatorTransform();
  UpdateTargetedIndicatorTransform();
  AlignToCurrentCell();
}

void AFighterPawn::OnRep_HasActivatedThisRound() { UpdateActivationIndicator(); }

void AFighterPawn::OnRep_IsCurrentlyActive() { UpdateActivationIndicator(); }
void AFighterPawn::OnRep_IsMoving() { RefreshMovementAudioComponent(); }

void AFighterPawn::BroadcastActionsRemaining() {
  OnActionsChanged.Broadcast(ActionsRemaining);
  UpdateActivationIndicator();
}

void AFighterPawn::EnsureActivationWidget() {
  auto PrepareWidget = [this](UWidgetComponent *WidgetComponent,
                              TWeakObjectPtr<UFighterActivationWidget>
                                  &CachedWidget) {
    if (!WidgetComponent) {
      CachedWidget = nullptr;
      return;
    }

    if (!WidgetComponent->GetWidgetClass()) {
      if (ActivationWidgetTemplate) {
        WidgetComponent->SetWidgetClass(ActivationWidgetTemplate);
      } else {
        WidgetComponent->SetWidgetClass(
            UFighterActivationWidget::StaticClass());
      }
    }

    if (!WidgetComponent->GetUserWidgetObject()) {
      WidgetComponent->InitWidget();
    }

    if (!CachedWidget.IsValid()) {
      CachedWidget = Cast<UFighterActivationWidget>(
          WidgetComponent->GetUserWidgetObject());
    }

    if (!CachedWidget.IsValid()) {
      return;
    }

    WidgetComponent->SetRelativeScale3D(FVector(ActivationWidgetScale));
    CachedWidget->SetIconTextures(
        ResolveActivationIcon(ActivationReadyIcon, ActivationReadyTexture),
        ResolveActivationIcon(ActivationSpentIcon, ActivationSpentTexture));
  };

  PrepareWidget(ActivationWidget, CachedActivationWidget);
  PrepareWidget(ActivationWidgetBack, CachedActivationWidgetBack);
}

void AFighterPawn::UpdateActivationIndicator() {
  EnsureActivationWidget();

  const bool bHasFrontWidget =
      ActivationWidget && CachedActivationWidget.IsValid();
  const bool bHasBackWidget =
      ActivationWidgetBack && CachedActivationWidgetBack.IsValid();

  if (!bHasFrontWidget && !bHasBackWidget) {
    return;
  }

  EFighterActivationIndicatorState DesiredState =
      EFighterActivationIndicatorState::Hidden;

  if (IsAlive()) {
    if (bIsCurrentlyActive) {
      DesiredState = EFighterActivationIndicatorState::Active;
    } else if (bHasActivatedThisRound && ActionsRemaining <= 0) {
      DesiredState = EFighterActivationIndicatorState::Spent;
    }
  }

  if (bHasFrontWidget) {
    CachedActivationWidget->SetActivationState(DesiredState);
  }
  if (bHasBackWidget) {
    CachedActivationWidgetBack->SetActivationState(DesiredState);
  }

  const bool bShouldShow =
      DesiredState != EFighterActivationIndicatorState::Hidden;
  if (ActivationWidget) {
    ActivationWidget->SetVisibility(bShouldShow);
    ActivationWidget->SetHiddenInGame(!bShouldShow);
  }
  if (ActivationWidgetBack) {
    ActivationWidgetBack->SetVisibility(bShouldShow);
    ActivationWidgetBack->SetHiddenInGame(!bShouldShow);
  }
}

UTexture2D *AFighterPawn::ResolveActivationIcon(
    TSoftObjectPtr<UTexture2D> &IconSource, UTexture2D *&CachedTexture) {
  if (CachedTexture) {
    return CachedTexture;
  }

  if (IconSource.IsValid()) {
    CachedTexture = IconSource.Get();
  } else if (!IconSource.IsNull()) {
    CachedTexture = IconSource.LoadSynchronous();
  }

  return CachedTexture;
}

bool AFighterPawn::ShouldOverrideSpawnFacingYaw() const {
  const AFighterPawn *NativeDefaults =
      AFighterPawn::StaticClass()->GetDefaultObject<AFighterPawn>();
  return bOverrideSpawnFacingYaw ||
         !FMath::IsNearlyEqual(SpawnFacingYaw, NativeDefaults->SpawnFacingYaw,
                               KINDA_SMALL_NUMBER);
}

float AFighterPawn::GetCurrentWorldFacingYaw() const {
  return FRotator::NormalizeAxis(GetActorRotation().Yaw + DisplayMeshYawOffset);
}

void AFighterPawn::ApplyFacingYaw(float TargetYaw) {
  const float AdjustedYaw =
      FRotator::NormalizeAxis(TargetYaw - DisplayMeshYawOffset);
  SetActorRotation(FRotator(0.f, AdjustedYaw, 0.f));
}

void AFighterPawn::FaceTowardsLocation(const FVector &TargetLocation) {
  FVector Direction = TargetLocation - GetActorLocation();
  Direction.Z = 0.f;
  if (!Direction.IsNearlyZero()) {
    const FRotator LookRotation = Direction.Rotation();
    ApplyFacingYaw(LookRotation.Yaw + SpawnFacingYawDelta);
  }
}

void AFighterPawn::FaceTowardsCells(const FIntPoint &FromCell,
                                    const FIntPoint &ToCell) {
  if (FromCell == ToCell) {
    return;
  }

  FVector FromLocation = GetAlignedWorldLocation(FromCell);
  FVector TargetLocation = GetAlignedWorldLocation(ToCell);

  FVector Direction = TargetLocation - FromLocation;
  Direction.Z = 0.f;
  if (!Direction.IsNearlyZero()) {
    const FRotator LookRotation = Direction.Rotation();
    ApplyFacingYaw(LookRotation.Yaw + SpawnFacingYawDelta);
  }
}

void AFighterPawn::RefreshMovementAudioComponent() {
  if (!MovementAudioComponent) {
    return;
  }

  MovementAudioComponent->SetSound(MovementSound);

  const bool bShouldPlay = bIsMoving && MovementSound;
  if (bShouldPlay) {
    if (!MovementAudioComponent->IsPlaying()) {
      MovementAudioComponent->Play();
    }
  } else {
    MovementAudioComponent->Stop();
  }
}

void AFighterPawn::SetIsMoving(bool bNewIsMoving) {
  if (bIsMoving == bNewIsMoving) {
    return;
  }

  bIsMoving = bNewIsMoving;
  RefreshMovementAudioComponent();
}
