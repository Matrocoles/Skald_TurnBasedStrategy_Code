#include "FighterPawn.h"
#include "Blueprint/UserWidget.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/TextBlock.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "GridBattleManager.h"
#include "GridOverlayComponent.h"
#include "Net/UnrealNetwork.h"
#include "Skald_GameInstance.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "UI/FighterActivationWidget.h"
#include "UI/FighterHealthWidget.h"

namespace
{
constexpr int32 ActionsPerActivation = 2;
constexpr float ActivationWidgetScale = 0.1f;
constexpr float WidgetMirrorSeparation = 0.5f;
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

  HealthWidgetTemplate = UFighterHealthWidget::StaticClass();

  static ConstructorHelpers::FClassFinder<UUserWidget> DamageWidgetFinder(
      TEXT("/Game/Blueprints/UI/WBP_DamageFloat"));
  if (DamageWidgetFinder.Succeeded()) {
    DamageFloatWidgetTemplate = DamageWidgetFinder.Class;
  }

  static ConstructorHelpers::FClassFinder<UUserWidget> MissWidgetFinder(
      TEXT("/Game/Blueprints/UI/WBP_MissWidget"));
  if (MissWidgetFinder.Succeeded()) {
    MissWidgetTemplate = MissWidgetFinder.Class;
  }

  ActivationWidgetTemplate = UFighterActivationWidget::StaticClass();

  ActionsRemaining = 0;
  bHasActivatedThisRound = false;
  bIsCurrentlyActive = false;
  CurrentCell = FIntPoint::ZeroValue;
  FighterId = NAME_None;

  ApplyFootprintScale();
  UpdateMeshOffset();
}

void AFighterPawn::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty> &OutLifetimeProps) const {
  Super::GetLifetimeReplicatedProps(OutLifetimeProps);

  DOREPLIFETIME(AFighterPawn, Stats);
  DOREPLIFETIME(AFighterPawn, FighterId);
  DOREPLIFETIME(AFighterPawn, FighterPortrait);
  DOREPLIFETIME(AFighterPawn, bIsAttacker);
  DOREPLIFETIME(AFighterPawn, ActionsRemaining);
  DOREPLIFETIME(AFighterPawn, bHasActivatedThisRound);
  DOREPLIFETIME(AFighterPawn, bIsCurrentlyActive);
  DOREPLIFETIME(AFighterPawn, GridFootprint);
  DOREPLIFETIME(AFighterPawn, CurrentCell);
  DOREPLIFETIME(AFighterPawn, SpawnFacingYawDelta);
  DOREPLIFETIME(AFighterPawn, MaxHealth);
}

void AFighterPawn::OnConstruction(const FTransform &Transform) {
  Super::OnConstruction(Transform);
  ApplyFootprintScale();
  UpdateMeshOffset();
  RefreshDisplayMeshYawOffset();
  const float IncomingSpawnYaw = Transform.GetRotation().Rotator().Yaw;
  const bool bShouldOverride = ShouldOverrideSpawnFacingYaw();
  SpawnFacingYawDelta =
      bShouldOverride
          ? FRotator::NormalizeAxis(SpawnFacingYaw - IncomingSpawnYaw)
          : 0.f;
  const float DesiredYaw = bShouldOverride ? SpawnFacingYaw : IncomingSpawnYaw;
  ApplyFacingYaw(DesiredYaw);
}

void AFighterPawn::BeginPlay() {
  Super::BeginPlay();

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

  OnHealthChanged.AddDynamic(this, &AFighterPawn::UpdateHealthDisplay);
  OnHealthChanged.Broadcast(Stats.Health);

  BroadcastActionsRemaining();
  UpdateActivationIndicator();

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

  if (!bIsMoving) {
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
    bIsMoving = false;
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
    const int32 Distance = FMath::Abs(SelfCell.X - Cell.X) +
                           FMath::Abs(SelfCell.Y - Cell.Y);
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
      const int32 Distance = FMath::Abs(SelfCell.X - OtherCell.X) +
                             FMath::Abs(SelfCell.Y - OtherCell.Y);
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
      const int32 Distance = FMath::Abs(SelfCell.X - OtherCell.X) +
                             FMath::Abs(SelfCell.Y - OtherCell.Y);
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

UUserWidget *AFighterPawn::GetDamageWidgetFromPool() {
  for (UUserWidget *Widget : DamageWidgetPool) {
    if (Widget && !Widget->IsInViewport()) {
      Widget->SetVisibility(ESlateVisibility::HitTestInvisible);
      return Widget;
    }
  }
  if (DamageFloatWidgetTemplate) {
    if (UWorld *World = GetWorld()) {
      if (UUserWidget *NewWidget =
              CreateWidget<UUserWidget>(World, DamageFloatWidgetTemplate)) {
        NewWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
        DamageWidgetPool.Add(NewWidget);
        return NewWidget;
      }
    }
  }
  return nullptr;
}

UUserWidget *AFighterPawn::GetMissWidgetFromPool() {
  for (UUserWidget *Widget : MissWidgetPool) {
    if (Widget && !Widget->IsInViewport()) {
      Widget->SetVisibility(ESlateVisibility::HitTestInvisible);
      return Widget;
    }
  }
  if (MissWidgetTemplate) {
    if (UWorld *World = GetWorld()) {
      if (UUserWidget *NewWidget =
              CreateWidget<UUserWidget>(World, MissWidgetTemplate)) {
        NewWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
        MissWidgetPool.Add(NewWidget);
        return NewWidget;
      }
    }
  }
  return nullptr;
}

void AFighterPawn::MoveToCell(FIntPoint TargetCell) {
  if (!bIsCurrentlyActive || ActionsRemaining <= 0) {
    return;
  }
  const int32 Distance = FMath::Abs(TargetCell.X - CurrentCell.X) +
                         FMath::Abs(TargetCell.Y - CurrentCell.Y);
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
    bIsMoving = false;
    MovementTargetLocation = GetActorLocation();
  } else {
    bIsMoving = true;
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

  if (PendingAttackTarget.IsValid() || PendingAttackRolls.Num() > 0) {
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

  const int32 RequiredRoll =
      Stats.Strength > Target->Stats.Defence
          ? 3
          : (Stats.Strength < Target->Stats.Defence ? 5 : 4);

  TArray<FQueuedAttackRoll> QueuedRolls;
  QueuedRolls.Reserve(Stats.AttackDice);

  int32 SimulatedHealth = Target->Stats.Health;
  for (int32 i = 0; i < Stats.AttackDice && SimulatedHealth > 0; ++i) {
    const int32 Roll = RandomStream->RandRange(1, 6);
    int32 DamageThisDie = 0;

    if (Roll == 6) {
      DamageThisDie = Stats.AttackDamage + Stats.CriticalBonusDamage; // crit
    } else if (Roll >= RequiredRoll) {
      DamageThisDie = Stats.AttackDamage;
    }

    if (DamageThisDie > 0) {
      SimulatedHealth = FMath::Max(0, SimulatedHealth - DamageThisDie);
    }

    FQueuedAttackRoll &NewRoll = QueuedRolls.Emplace_GetRef();
    NewRoll.RollValue = Roll;
    NewRoll.Damage = DamageThisDie;
    NewRoll.bHit = DamageThisDie > 0;
  }

  StartQueuedAttack(Target, MoveTemp(QueuedRolls));

  ActionsRemaining = FMath::Max(0, ActionsRemaining - 1);

  BroadcastActionsRemaining();

  RefreshDisplayMeshYawOffset();
  FaceTowardsCells(CurrentCell, Target->CurrentCell);
  FaceTowardsLocation(Target->GetActorLocation());

  if (Grid) {
    Grid->ClearHighlights();
  }
}

void AFighterPawn::StartQueuedAttack(AFighterPawn *Target,
                                     TArray<FQueuedAttackRoll> &&Rolls) {
  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(AttackRollTimerHandle);
  }

  PendingAttackRolls = MoveTemp(Rolls);
  PendingAttackRollIndex = 0;
  PendingAttackTarget = Target;
  bPendingAttackTargetDied = false;
  bHasProcessedPendingRoll = false;

  if (PendingAttackRolls.Num() == 0) {
    FinalizeQueuedAttack();
    return;
  }

  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().SetTimer(AttackRollTimerHandle, this,
                                      &AFighterPawn::ResolveNextAttackRoll, 1.f,
                                      false);
  } else {
    ResolveNextAttackRoll();
  }
}

void AFighterPawn::ResolveNextAttackRoll() {
  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(AttackRollTimerHandle);
  }

  if (!PendingAttackRolls.IsValidIndex(PendingAttackRollIndex)) {
    FinalizeQueuedAttack();
    return;
  }

  AFighterPawn *Target = PendingAttackTarget.Get();
  if (!Target) {
    FinalizeQueuedAttack();
    return;
  }

  const FQueuedAttackRoll Roll = PendingAttackRolls[PendingAttackRollIndex];
  bHasProcessedPendingRoll = true;

  if (USkaldGameInstance *GI = Cast<USkaldGameInstance>(GetGameInstance())) {
    if (UGridBattleManager *BattleManager = GI->GridBattleManager) {
      BattleManager->ReportAttackRoll(this, Target, Roll.RollValue, Roll.bHit,
                                      Roll.Damage);
    }
  }

  if (Roll.bHit) {
    Target->Stats.Health =
        FMath::Max(0, Target->Stats.Health - Roll.Damage);
    if (Target->Stats.Health <= 0) {
      bPendingAttackTargetDied = true;
    }

    if (UUserWidget *DamageWidget = GetDamageWidgetFromPool()) {
      if (UTextBlock *Text = Cast<UTextBlock>(
              DamageWidget->GetWidgetFromName(TEXT("DamageText")))) {
        Text->SetText(FText::AsNumber(Roll.Damage));
      }
      DamageWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
      DamageWidget->AddToViewport();
      if (UWorld *WorldPtr = GetWorld()) {
        FTimerHandle Timer;
        const TWeakObjectPtr<UUserWidget> DamageWidgetWeak = DamageWidget;
        WorldPtr->GetTimerManager().SetTimer(
            Timer, FTimerDelegate::CreateLambda([DamageWidgetWeak]() {
              if (UUserWidget *ResolvedWidget = DamageWidgetWeak.Get()) {
                ResolvedWidget->RemoveFromParent();
              }
            }),
            1.f, false);
      }
    }
  } else {
    if (UUserWidget *MissWidget = GetMissWidgetFromPool()) {
      if (UTextBlock *MissText =
              Cast<UTextBlock>(MissWidget->GetWidgetFromName(TEXT("Missed")))) {
        MissText->SetText(NSLOCTEXT("Skald", "BattleAttackMiss", "Missed"));
      }
      MissWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
      MissWidget->AddToViewport();
      if (UWorld *WorldPtr = GetWorld()) {
        FTimerHandle Timer;
        const TWeakObjectPtr<UUserWidget> MissWidgetWeak = MissWidget;
        WorldPtr->GetTimerManager().SetTimer(
            Timer, FTimerDelegate::CreateLambda([MissWidgetWeak]() {
              if (UUserWidget *ResolvedWidget = MissWidgetWeak.Get()) {
                ResolvedWidget->RemoveFromParent();
              }
            }),
            1.f, false);
      }
    }
  }

  Target->OnHealthChanged.Broadcast(Target->Stats.Health);

  ++PendingAttackRollIndex;

  const bool bHasMoreRolls =
      PendingAttackRolls.IsValidIndex(PendingAttackRollIndex);
  const bool bTargetAlive = Target->IsAlive();

  if (bHasMoreRolls && bTargetAlive) {
    if (UWorld *WorldPtr = GetWorld()) {
      WorldPtr->GetTimerManager().SetTimer(
          AttackRollTimerHandle, this, &AFighterPawn::ResolveNextAttackRoll,
          1.f, false);
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
    if (!Target->IsAlive() && !Target->IsActorBeingDestroyed()) {
      Target->Destroy();
    }
  }

  PendingAttackRolls.Reset();
  PendingAttackRollIndex = 0;
  PendingAttackTarget = nullptr;
  bPendingAttackTargetDied = false;
  bHasProcessedPendingRoll = false;

  OnQueuedAttackFinalized.Broadcast();
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
}

void AFighterPawn::Destroyed() {
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
  AlignToCurrentCell();
}

void AFighterPawn::OnRep_HasActivatedThisRound() { UpdateActivationIndicator(); }

void AFighterPawn::OnRep_IsCurrentlyActive() { UpdateActivationIndicator(); }

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
