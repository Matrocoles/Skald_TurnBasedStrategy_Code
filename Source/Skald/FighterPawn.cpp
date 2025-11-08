#include "FighterPawn.h"
#include "Skald_PlayerController.h"
#include "Abilities/SkaldAbilityComponent.h"
#include "Algo/Reverse.h"
#include "Blueprint/UserWidget.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/DecalComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/Queue.h"
#include "Containers/Set.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Curves/CurveFloat.h"
#include "GridBattleManager.h"
#include "GridOverlayComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Skald_GameInstance.h"
#include "Skald_PlayerState.h"
#include "SkaldDiceManager.h"
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
constexpr float VisualAvoidanceFloorNormalThreshold = 0.65f;
constexpr int32 DisengageMaxDistance = 3;
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

  AbilityComponent = CreateDefaultSubobject<USkaldAbilityComponent>(TEXT("AbilityComponent"));

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

  PassiveBuffDecal =
      CreateDefaultSubobject<UDecalComponent>(TEXT("PassiveBuffDecal"));
  PassiveBuffDecal->SetupAttachment(CollisionComponent);
  PassiveBuffDecal->SetRelativeRotation(FRotator(-90.f, 0.f, 0.f));
  PassiveBuffDecal->SetRelativeLocation(
      FVector(0.f, 0.f,
              -CollisionComponent->GetUnscaledCapsuleHalfHeight() +
                  PassiveBuffDecalFloorOffset));
  PassiveBuffDecal->DecalSize = PassiveBuffDecalSizeSingleCell;
  PassiveBuffDecal->SetHiddenInGame(true);
  PassiveBuffDecal->SetVisibility(false);
  PassiveBuffDecal->SetCanEverAffectNavigation(false);

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
  UpdatePassiveBuffDecalTransform();
  RefreshPassiveBuffDecalMaterial();
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
  DOREPLIFETIME(AFighterPawn, bIsEngaged);
  DOREPLIFETIME(AFighterPawn, GridFootprint);
  DOREPLIFETIME(AFighterPawn, CurrentCell);
  DOREPLIFETIME(AFighterPawn, MovementSourceCell);
  DOREPLIFETIME(AFighterPawn, SpawnFacingYawDelta);
  DOREPLIFETIME(AFighterPawn, MaxHealth);
  DOREPLIFETIME(AFighterPawn, AttackType);
  DOREPLIFETIME(AFighterPawn, AttackFX);
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
  UpdatePassiveBuffDecalSize();
  UpdatePassiveBuffDecalTransform();
  RefreshPassiveBuffDecalMaterial();
  SetPassiveBuffVisible(false);
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

  RefreshAbilityLoadout();

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

  MovementSourceCell = CurrentCell;

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

  RecalculateBattleEngagement();

  EnsureDiceManagerBinding();
}

void AFighterPawn::EndPlay(const EEndPlayReason::Type EndPlayReason) {
  CleanupDiceManagerBinding();
  Super::EndPlay(EndPlayReason);
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

void AFighterPawn::SetAttackType(EFighterAttackType InAttackType) {
  if (AttackType == InAttackType) {
    return;
  }

  AttackType = InAttackType;
  OnRep_AttackType();
}

void AFighterPawn::OnRep_AttackType() {}

void AFighterPawn::Tick(float DeltaSeconds) {
  Super::Tick(DeltaSeconds);

  UpdateHitFlash(DeltaSeconds);
  TickActiveProjectileFX(DeltaSeconds);

  const bool bShouldRunMovement =
      bIsMoving && (HasAuthority() || IsLocallyControlled());

  if (!bShouldRunMovement) {
    return;
  }

  const float EffectiveTolerance =
      FMath::Max(MovementStopTolerance, KINDA_SMALL_NUMBER);

  if (MovementStraightLineDistance <= KINDA_SMALL_NUMBER ||
      MovementSpeed <= KINDA_SMALL_NUMBER) {
    SetActorLocation(MovementTargetLocation);
    MovementProgress = 1.f;
    SetIsMoving(false);
    MovementTargetLocation = GetActorLocation();
    return;
  }

  const float DistanceStep = MovementSpeed * DeltaSeconds;
  const float ProgressStep =
      DistanceStep / FMath::Max(MovementStraightLineDistance, KINDA_SMALL_NUMBER);
  MovementProgress =
      FMath::Clamp(MovementProgress + ProgressStep, 0.f, 1.f);

  const FVector NextLocation = SampleVisualMovementPath(MovementProgress);
  SetActorLocation(NextLocation);

  const bool bArrived =
      MovementProgress >= 1.f - KINDA_SMALL_NUMBER ||
      FVector::DistSquared(NextLocation, MovementTargetLocation) <=
          FMath::Square(EffectiveTolerance);

  if (bArrived) {
    SetActorLocation(MovementTargetLocation);
    MovementProgress = 1.f;
    SetIsMoving(false);
    MovementTargetLocation = GetActorLocation();
  }
}

void AFighterPawn::RebuildVisualMovementPath(const FVector &Destination) {
  VisualMovementPathPoints.Reset();
  VisualMovementCumulativeDistances.Reset();
  VisualMovementPathLength = 0.f;

  const FVector StartLocation = MovementStartLocation;
  VisualMovementPathPoints.Add(StartLocation);

  bool bConstructedFromGridPath = false;

  if (UGridOverlayComponent *Grid = GetGrid()) {
    FIntPoint StartAnchor = MovementSourceCell;
    if (!Grid->IsCellInBounds(StartAnchor)) {
      StartAnchor = Grid->WorldToGrid(StartLocation);
    }

    FIntPoint TargetAnchor = CurrentCell;
    if (!Grid->IsCellInBounds(TargetAnchor)) {
      TargetAnchor = Grid->WorldToGrid(Destination);
    }

    if (Grid->IsCellInBounds(StartAnchor) && Grid->IsCellInBounds(TargetAnchor) &&
        StartAnchor != TargetAnchor) {
      const TArray<FIntPoint> StartFootprint = GetOccupiedCells(StartAnchor);
      TSet<FIntPoint> IgnoredCells;
      IgnoredCells.Reserve(StartFootprint.Num());
      for (const FIntPoint &Cell : StartFootprint) {
        IgnoredCells.Add(Cell);
      }

      auto CanOccupyAnchor = [&](const FIntPoint &Anchor) {
        const bool bIsDestination = Anchor == TargetAnchor;
        const TArray<FIntPoint> CandidateCells = GetOccupiedCells(Anchor);
        if (CandidateCells.Num() == 0) {
          return false;
        }

        for (const FIntPoint &Cell : CandidateCells) {
          if (!Grid->IsCellInBounds(Cell) || Grid->IsObscured(Cell)) {
            return false;
          }

          const bool bIgnored = IgnoredCells.Contains(Cell);
          if (!bIgnored && !bIsDestination && Grid->IsOccupied(Cell)) {
            return false;
          }
        }

        return true;
      };

      auto IsDiagonalStepClear = [&](const FIntPoint &From, const FIntPoint &To) {
        if (From.X == To.X || From.Y == To.Y) {
          return true;
        }

        const TArray<FIntPoint> FromCells = GetOccupiedCells(From);
        const TArray<FIntPoint> NextCells = GetOccupiedCells(To);

        TSet<FIntPoint> NextCellSet;
        NextCellSet.Reserve(NextCells.Num());
        for (const FIntPoint &NextCell : NextCells) {
          NextCellSet.Add(NextCell);
        }

        auto IsBlocked = [&](const FIntPoint &CheckCell) {
          if (!Grid->IsCellInBounds(CheckCell) || Grid->IsObscured(CheckCell)) {
            return true;
          }

          if (NextCellSet.Contains(CheckCell) || IgnoredCells.Contains(CheckCell)) {
            return false;
          }

          return Grid->IsOccupied(CheckCell);
        };

        const int32 StepX = FMath::Clamp(To.X - From.X, -1, 1);
        const int32 StepY = FMath::Clamp(To.Y - From.Y, -1, 1);

        for (const FIntPoint &FromCell : FromCells) {
          if (IsBlocked(FromCell + FIntPoint(StepX, 0)) ||
              IsBlocked(FromCell + FIntPoint(0, StepY))) {
            return false;
          }
        }

        return true;
      };

      struct FVisualPathNode {
        FIntPoint Cell;
        int32 Cost;
      };

      auto FrontierComparator = [](const FVisualPathNode &A,
                                   const FVisualPathNode &B) {
        return A.Cost < B.Cost;
      };

      const int32 EstimatedDistance =
          FMath::Max(FMath::Abs(TargetAnchor.X - StartAnchor.X),
                     FMath::Abs(TargetAnchor.Y - StartAnchor.Y));
      const int32 SearchCostLimit =
          FMath::Max3(Stats.Movement, EstimatedDistance * 4, 32);

      TArray<FVisualPathNode> Frontier;
      Frontier.Reserve(32);
      Frontier.Add({StartAnchor, 0});

      TMap<FIntPoint, int32> BestCost;
      BestCost.Reserve(32);
      BestCost.Add(StartAnchor, 0);

      TMap<FIntPoint, FIntPoint> CameFrom;
      CameFrom.Reserve(32);
      CameFrom.Add(StartAnchor, StartAnchor);

      static const FIntPoint Directions[] = {
          FIntPoint(1, 0),  FIntPoint(-1, 0), FIntPoint(0, 1),  FIntPoint(0, -1),
          FIntPoint(1, 1),  FIntPoint(1, -1), FIntPoint(-1, 1), FIntPoint(-1, -1)};

      bool bFoundPath = false;

      while (Frontier.Num() > 0) {
        Frontier.Sort(FrontierComparator);

        const FVisualPathNode Node = Frontier[0];
        Frontier.RemoveAt(0, 1, EAllowShrinking::No);

        const FIntPoint Cell = Node.Cell;
        const int32 DistanceFromStart = Node.Cost;

        const int32 *RecordedCost = BestCost.Find(Cell);
        if (!RecordedCost || DistanceFromStart > *RecordedCost) {
          continue;
        }

        if (Cell == TargetAnchor) {
          bFoundPath = true;
          break;
        }

        for (const FIntPoint &Dir : Directions) {
          const FIntPoint Next = Cell + Dir;

          if (!CanOccupyAnchor(Next)) {
            continue;
          }

          if (!IsDiagonalStepClear(Cell, Next)) {
            continue;
          }

          const int32 StepCost =
              DistanceFromStart +
              FMath::Max(1, GetMovementStepCost(Cell, Next, Grid));
          if (StepCost > SearchCostLimit) {
            continue;
          }

          const int32 *ExistingCost = BestCost.Find(Next);
          if (ExistingCost && StepCost >= *ExistingCost) {
            continue;
          }

          BestCost.Add(Next, StepCost);
          CameFrom.Add(Next, Cell);
          Frontier.Add({Next, StepCost});
        }
      }

      if (bFoundPath) {
        TArray<FIntPoint> PathAnchors;
        PathAnchors.Reserve(16);

        FIntPoint Step = TargetAnchor;
        PathAnchors.Add(Step);

        while (Step != StartAnchor) {
          const FIntPoint *Previous = CameFrom.Find(Step);
          if (!Previous || *Previous == Step) {
            bFoundPath = false;
            break;
          }

          Step = *Previous;
          PathAnchors.Add(Step);
        }

        if (bFoundPath && PathAnchors.Num() > 1) {
          Algo::Reverse(PathAnchors);

          for (int32 Index = 1; Index < PathAnchors.Num(); ++Index) {
            const bool bIsFinalAnchor = Index == PathAnchors.Num() - 1;
            const FVector AnchorLocation =
                GetAlignedWorldLocation(PathAnchors[Index]);

            if (bIsFinalAnchor) {
              if (!AnchorLocation.Equals(Destination, KINDA_SMALL_NUMBER)) {
                VisualMovementPathPoints.Add(AnchorLocation);
              }
            } else {
              VisualMovementPathPoints.Add(AnchorLocation);
            }
          }

          bConstructedFromGridPath = true;
        }
      }
    }
  }

  const bool bCanAttemptAvoidance = !bConstructedFromGridPath &&
                                    bUseVisualObstacleAvoidance &&
                                    VisualAvoidanceProbeRadius > KINDA_SMALL_NUMBER &&
                                    MovementStraightLineDistance > KINDA_SMALL_NUMBER;

  if (bCanAttemptAvoidance) {
    if (UWorld *World = GetWorld()) {
      const FCollisionShape ProbeShape =
          FCollisionShape::MakeSphere(VisualAvoidanceProbeRadius);
      FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(FighterVisualAvoidance),
                                        false, this);
      QueryParams.bTraceComplex = false;
      QueryParams.AddIgnoredActor(this);

      FHitResult BlockingHit;
      const bool bHit = World->SweepSingleByChannel(
          BlockingHit, StartLocation, Destination, FQuat::Identity,
          VisualAvoidanceTraceChannel, ProbeShape, QueryParams);

      if (bHit && ShouldUseAvoidanceHit(BlockingHit)) {
        const FVector TravelDirection =
            (Destination - StartLocation).GetSafeNormal();
        const FVector SurfaceNormal =
            BlockingHit.Normal.GetSafeNormal(KINDA_SMALL_NUMBER, FVector::UpVector);

        const FVector UpBasis =
            FMath::Abs(FVector::DotProduct(TravelDirection, FVector::UpVector)) >=
                    (1.f - KINDA_SMALL_NUMBER)
                ? FVector(0.f, 1.f, 0.f)
                : FVector::UpVector;

        FVector AvoidanceDirection =
            FVector::CrossProduct(SurfaceNormal, UpBasis);

        if (AvoidanceDirection.IsNearlyZero()) {
          AvoidanceDirection = FVector::CrossProduct(SurfaceNormal, TravelDirection);
        }

        if (AvoidanceDirection.IsNearlyZero()) {
          AvoidanceDirection = FVector::CrossProduct(TravelDirection, UpBasis);
        }

        const bool bHasAvoidanceDirection = !AvoidanceDirection.IsNearlyZero();
        if (bHasAvoidanceDirection) {
          AvoidanceDirection = AvoidanceDirection.GetSafeNormal();
          if (FVector::DotProduct(AvoidanceDirection, TravelDirection) < 0.f) {
            AvoidanceDirection *= -1.f;
          }
        } else {
          AvoidanceDirection = FVector::ZeroVector;
        }

        const float PathLength = MovementStraightLineDistance;
        const float FirstDistance =
            FMath::Clamp(BlockingHit.Distance, 0.f, PathLength);
        const float SecondDistance =
            FMath::Clamp(BlockingHit.Distance + VisualAvoidanceRejoinDistance, 0.f,
                         PathLength);

        const float FirstAlpha =
            PathLength > KINDA_SMALL_NUMBER ? FirstDistance / PathLength : 0.f;
        const float SecondAlpha = PathLength > KINDA_SMALL_NUMBER
                                       ? SecondDistance / PathLength
                                       : 1.f;

        FVector FirstWaypoint =
            FMath::Lerp(StartLocation, Destination, FirstAlpha) +
            SurfaceNormal * VisualAvoidanceSurfacePush;
        if (bHasAvoidanceDirection) {
          FirstWaypoint += AvoidanceDirection * VisualAvoidanceSideStep;
        }
        FirstWaypoint.Z =
            FMath::Lerp(StartLocation.Z, Destination.Z, FirstAlpha);

        VisualMovementPathPoints.Add(FirstWaypoint);

        if (SecondAlpha < 1.f - KINDA_SMALL_NUMBER) {
          FVector SecondWaypoint =
              FMath::Lerp(StartLocation, Destination, SecondAlpha) +
              SurfaceNormal * (VisualAvoidanceSurfacePush * 0.5f);
          if (bHasAvoidanceDirection) {
            SecondWaypoint +=
                AvoidanceDirection *
                (VisualAvoidanceSideStep * VisualAvoidanceReturnRatio);
          }
          SecondWaypoint.Z =
              FMath::Lerp(StartLocation.Z, Destination.Z, SecondAlpha);

          VisualMovementPathPoints.Add(SecondWaypoint);
        }
      }
    }
  }

  VisualMovementPathPoints.Add(Destination);

  for (int32 Index = 1; Index < VisualMovementPathPoints.Num(); ++Index) {
    ConformPathPointToGround(VisualMovementPathPoints[Index]);
  }

  VisualMovementCumulativeDistances.Reserve(VisualMovementPathPoints.Num());
  VisualMovementCumulativeDistances.Add(0.f);
  float AccumulatedDistance = 0.f;

  for (int32 Index = 1; Index < VisualMovementPathPoints.Num(); ++Index) {
    AccumulatedDistance += FVector::Dist(VisualMovementPathPoints[Index - 1],
                                         VisualMovementPathPoints[Index]);
    VisualMovementCumulativeDistances.Add(AccumulatedDistance);
  }

  VisualMovementPathLength = AccumulatedDistance;

  if (VisualMovementPathLength <= KINDA_SMALL_NUMBER) {
    VisualMovementPathLength = MovementStraightLineDistance;
    if (VisualMovementCumulativeDistances.Num() > 0) {
      VisualMovementCumulativeDistances.Last() = VisualMovementPathLength;
    }
  }
}

bool AFighterPawn::ShouldUseAvoidanceHit(const FHitResult &Hit) const {
  if (!Hit.bBlockingHit) {
    return false;
  }

  if (Hit.Distance <= KINDA_SMALL_NUMBER) {
    return false;
  }

  const FVector SurfaceNormal = Hit.Normal.GetSafeNormal();
  if (SurfaceNormal.IsNearlyZero()) {
    return false;
  }

  const float UpDot = FVector::DotProduct(SurfaceNormal, FVector::UpVector);
  if (UpDot >= VisualAvoidanceFloorNormalThreshold - KINDA_SMALL_NUMBER) {
    return false;
  }

  return true;
}

FVector AFighterPawn::SampleVisualMovementPath(float NormalisedDistance) const {
  if (VisualMovementPathPoints.Num() <= 1) {
    return MovementTargetLocation;
  }

  if (VisualMovementPathLength <= KINDA_SMALL_NUMBER) {
    return VisualMovementPathPoints.Last();
  }

  const float ClampedAlpha = FMath::Clamp(NormalisedDistance, 0.f, 1.f);
  const float TargetDistance = VisualMovementPathLength * ClampedAlpha;

  for (int32 Index = 1; Index < VisualMovementPathPoints.Num(); ++Index) {
    const float SegmentStart =
        VisualMovementCumulativeDistances.IsValidIndex(Index - 1)
            ? VisualMovementCumulativeDistances[Index - 1]
            : 0.f;
    const float SegmentEnd =
        VisualMovementCumulativeDistances.IsValidIndex(Index)
            ? VisualMovementCumulativeDistances[Index]
            : SegmentStart;

    const float SegmentLength = SegmentEnd - SegmentStart;
    if (SegmentLength <= KINDA_SMALL_NUMBER) {
      continue;
    }

    const bool bWithinSegment = TargetDistance <= SegmentEnd ||
                                Index == VisualMovementPathPoints.Num() - 1;

    if (bWithinSegment) {
      const float LocalAlpha =
          (TargetDistance - SegmentStart) / FMath::Max(SegmentLength, KINDA_SMALL_NUMBER);
      return FMath::Lerp(VisualMovementPathPoints[Index - 1],
                         VisualMovementPathPoints[Index],
                         FMath::Clamp(LocalAlpha, 0.f, 1.f));
    }
  }

  return VisualMovementPathPoints.Last();
}

void AFighterPawn::ResetVisualMovementPath() {
  MovementProgress = 0.f;
  MovementStraightLineDistance = 0.f;
  VisualMovementPathPoints.Reset();
  VisualMovementCumulativeDistances.Reset();
  VisualMovementPathLength = 0.f;
}

void AFighterPawn::ConformPathPointToGround(FVector &Location) const {
  if (VisualGroundConformTraceHeight <= KINDA_SMALL_NUMBER) {
    return;
  }

  UWorld *World = GetWorld();
  if (!World) {
    return;
  }

  const FVector TraceStart =
      Location + FVector(0.f, 0.f, VisualGroundConformTraceHeight);
  const FVector TraceEnd =
      Location - FVector(0.f, 0.f, VisualGroundConformTraceHeight);

  FHitResult Hit;
  FCollisionQueryParams QueryParams(
      SCENE_QUERY_STAT(FighterMovementGroundConform), false, this);
  QueryParams.bTraceComplex = false;
  QueryParams.AddIgnoredActor(this);

  if (World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd,
                                      VisualGroundConformTraceChannel,
                                      QueryParams) &&
      Hit.bBlockingHit) {
    Location = Hit.Location;
    Location.Z += GetSimpleCollisionHalfHeight();
  }
}

void AFighterPawn::TickActiveProjectileFX(float DeltaSeconds) {
  if (ActiveProjectileFX.Num() <= 0) {
    return;
  }

  for (int32 Index = ActiveProjectileFX.Num() - 1; Index >= 0; --Index) {
    FActiveProjectileFX &Projectile = ActiveProjectileFX[Index];
    UNiagaraComponent *Component = Projectile.Component.Get();
    if (!Component) {
      ActiveProjectileFX.RemoveAtSwap(Index);
      continue;
    }

    const float TravelTime = Projectile.TravelTime;
    Projectile.ElapsedTime += DeltaSeconds;

    const float EffectiveTime =
        TravelTime > KINDA_SMALL_NUMBER ? TravelTime : KINDA_SMALL_NUMBER;
    const float Alpha =
        TravelTime > KINDA_SMALL_NUMBER
            ? FMath::Clamp(Projectile.ElapsedTime / EffectiveTime, 0.f, 1.f)
            : 1.f;
    const FVector NewLocation = FMath::Lerp(Projectile.StartLocation,
                                            Projectile.EndLocation, Alpha);
    Component->SetWorldLocation(NewLocation);
    Component->SetWorldRotation(
        (Projectile.EndLocation - Projectile.StartLocation).Rotation());

    if (Alpha >= 1.f) {
      Component->Deactivate();
      Component->SetAutoDestroy(true);
      ActiveProjectileFX.RemoveAtSwap(Index);
    }
  }
}

FVector AFighterPawn::ResolveFXOrigin(const FName &SocketName,
                                      const FVector &LocalOffset,
                                      FRotator *OutSocketRotation) const {
  const USceneComponent *SourceComponent =
      DisplayMesh ? static_cast<const USceneComponent *>(DisplayMesh)
                  : GetRootComponent();
  if (!SourceComponent) {
    if (OutSocketRotation) {
      *OutSocketRotation = GetActorRotation();
    }
    const FTransform ActorTransform = GetActorTransform();
    return ActorTransform.TransformPosition(LocalOffset);
  }

  FTransform SocketTransform = SourceComponent->GetComponentTransform();
  if (!SocketName.IsNone() && SourceComponent->DoesSocketExist(SocketName)) {
    SocketTransform = SourceComponent->GetSocketTransform(SocketName);
  }

  if (OutSocketRotation) {
    *OutSocketRotation = SocketTransform.Rotator();
  }

  return SocketTransform.TransformPosition(LocalOffset);
}

void AFighterPawn::SpawnPreAttackSoundAtLocation(
    const FVector &Location) const {
  if (AttackFX.PreAttackSound.IsNull()) {
    return;
  }

  if (UWorld *World = GetWorld()) {
    if (USoundBase *SoundCue = AttackFX.PreAttackSound.LoadSynchronous()) {
      UGameplayStatics::SpawnSoundAtLocation(World, SoundCue, Location);
    }
  }
}

void AFighterPawn::PlayPreAttackFX(AFighterPawn *Target) {
  PlayMeleePreAttackFX(Target);

  if (AttackType == EFighterAttackType::Ranged) {
    PlayRangedPreAttackFX(Target);
  }
}

void AFighterPawn::TriggerAttackPresentationFX(AFighterPawn *Target) {
  if (HasAuthority()) {
    MulticastPlayPreAttackFX(Target);
    return;
  }

  PlayPreAttackFX(Target);
}

void AFighterPawn::PlayMeleePreAttackFX(AFighterPawn *Target) {
  if (AttackFX.PreAttackEffect.IsNull() && AttackFX.PreAttackSound.IsNull()) {
    return;
  }

  const float TargetHalfHeight =
      Target ? Target->GetSimpleCollisionHalfHeight() : GetSimpleCollisionHalfHeight();
  const FVector TargetLocation = (Target ? Target->GetActorLocation()
                                         : GetActorLocation()) +
                                 FVector(0.f, 0.f, TargetHalfHeight);
  FRotator SocketRotation;
  const FVector SpawnLocation =
      ResolveFXOrigin(AttackFX.PreAttackSocket, AttackFX.PreAttackOffset,
                      &SocketRotation);

  FRotator SpawnRotation = SocketRotation;
  if (AttackFX.PreAttackSocket.IsNone()) {
    FVector Direction = (TargetLocation - SpawnLocation).GetSafeNormal();
    if (Direction.IsNearlyZero()) {
      Direction = SocketRotation.Vector();
    }
    SpawnRotation = Direction.Rotation();
  }

  if (!AttackFX.PreAttackEffect.IsNull()) {
    if (UWorld *World = GetWorld()) {
      if (UNiagaraSystem *Effect =
              AttackFX.PreAttackEffect.LoadSynchronous()) {
        UNiagaraComponent *NiagaraComponent =
            UNiagaraFunctionLibrary::SpawnSystemAtLocation(
                World, Effect, SpawnLocation, SpawnRotation);
        if (NiagaraComponent) {
          NiagaraComponent->SetAutoDestroy(true);
        }
      }
    }
  }

  SpawnPreAttackSoundAtLocation(SpawnLocation);
}

void AFighterPawn::PlayRangedPreAttackFX(AFighterPawn *Target) {
  if (AttackType != EFighterAttackType::Ranged) {
    return;
  }

  if (!Target) {
    return;
  }

  const float TargetHalfHeight = Target->GetSimpleCollisionHalfHeight();
  const FVector TargetLocation =
      Target->GetActorLocation() + FVector(0.f, 0.f, TargetHalfHeight);
  FRotator SocketRotation;
  const FVector SpawnLocation =
      ResolveFXOrigin(AttackFX.ProjectileSocket, AttackFX.ProjectileOffset,
                      &SocketRotation);

  FVector Direction = (TargetLocation - SpawnLocation).GetSafeNormal();
  if (Direction.IsNearlyZero()) {
    Direction = SocketRotation.Vector();
  }
  const FRotator SpawnRotation = Direction.Rotation();

  SpawnProjectileFX(SpawnLocation, TargetLocation, SpawnRotation);

  if (!AttackFX.ProjectileSound.IsNull()) {
    if (UWorld *World = GetWorld()) {
      if (USoundBase *SoundCue = AttackFX.ProjectileSound.LoadSynchronous()) {
        UGameplayStatics::SpawnSoundAtLocation(World, SoundCue, SpawnLocation);
      }
    }
  }
}

void AFighterPawn::SpawnProjectileFX(const FVector &SpawnLocation,
                                     const FVector &TargetLocation,
                                     const FRotator &SpawnRotation) {
  if (AttackFX.ProjectileEffect.IsNull()) {
    return;
  }

  if (UWorld *World = GetWorld()) {
    if (UNiagaraSystem *Effect =
            AttackFX.ProjectileEffect.LoadSynchronous()) {
      UNiagaraComponent *ProjectileComponent =
          UNiagaraFunctionLibrary::SpawnSystemAtLocation(
              World, Effect, SpawnLocation, SpawnRotation);
      if (!ProjectileComponent) {
        return;
      }

      ProjectileComponent->SetAutoDestroy(false);

      const float Distance = FVector::Dist(SpawnLocation, TargetLocation);
      if (Distance > KINDA_SMALL_NUMBER &&
          !AttackFX.ProjectileDistanceParameter.IsNone()) {
        ProjectileComponent->SetVariableFloat(
            AttackFX.ProjectileDistanceParameter, Distance);
      }

      if (!AttackFX.ProjectileColorParameter.IsNone()) {
        ProjectileComponent->SetVariableLinearColor(
            AttackFX.ProjectileColorParameter, AttackFX.ProjectileColor);
      }

      const float Speed = FMath::Max(KINDA_SMALL_NUMBER, AttackFX.ProjectileSpeed);
      const float TravelTime =
          Distance > KINDA_SMALL_NUMBER ? Distance / Speed : 0.f;

      if (TravelTime <= 0.f) {
        ProjectileComponent->SetWorldLocation(TargetLocation);
        ProjectileComponent->Deactivate();
        ProjectileComponent->SetAutoDestroy(true);
        return;
      }

      FActiveProjectileFX Projectile;
      Projectile.Component = ProjectileComponent;
      Projectile.StartLocation = SpawnLocation;
      Projectile.EndLocation = TargetLocation;
      Projectile.TravelTime = TravelTime;
      Projectile.ElapsedTime = 0.f;
      ActiveProjectileFX.Add(MoveTemp(Projectile));
    }
  }
}

void AFighterPawn::MulticastPlayPreAttackFX_Implementation(AFighterPawn *Target) {
  PlayPreAttackFX(Target);
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

  if (AbilityComponent) {
    AbilityComponent->HandleActivationStarted();
  }

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

  if (AbilityComponent) {
    AbilityComponent->HandleRoundStarted();
  }
}

void AFighterPawn::FinishActivation() {
  ActionsRemaining = 0;
  bIsCurrentlyActive = false;

  BroadcastActionsRemaining();
  UpdateActivationIndicator();

  if (AbilityComponent) {
    AbilityComponent->HandleActivationFinished();
  }

  if (UGridOverlayComponent *Grid = GetGrid()) {
    Grid->ClearSelectionHighlight();
  }
}

bool AFighterPawn::ConsumeAction() {
  if (!bIsCurrentlyActive || ActionsRemaining <= 0) {
    return false;
  }

  ActionsRemaining = FMath::Max(0, ActionsRemaining - 1);
  BroadcastActionsRemaining();
  return true;
}

bool AFighterPawn::TryRestoreAction() {
  if (!bIsCurrentlyActive || ActionsRemaining >= ActionsPerActivation) {
    return false;
  }

  ActionsRemaining = FMath::Clamp(ActionsRemaining + 1, 0, ActionsPerActivation);
  BroadcastActionsRemaining();
  return true;
}

bool AFighterPawn::TryRestoreReaction() {
  if (USkaldAbilityComponent *Ability = GetAbilityComponent()) {
    return Ability->TryRefreshReaction();
  }

  return false;
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

void AFighterPawn::NotifyPassiveBuffApplied(
    const FSkaldAbilityDefinition &Definition) {
  if (!Definition.IsValid()) {
    return;
  }

  int32 &Count = ActivePassiveBuffSources.FindOrAdd(Definition.AbilityId);
  ++Count;

  UpdatePassiveBuffDecalSize();
  UpdatePassiveBuffDecalTransform();
  RefreshPassiveBuffDecalMaterial();
  SetPassiveBuffVisible(true);
}

void AFighterPawn::NotifyPassiveBuffRemoved(FName AbilityId) {
  if (AbilityId.IsNone()) {
    return;
  }

  if (int32 *Count = ActivePassiveBuffSources.Find(AbilityId)) {
    *Count = FMath::Max(0, *Count - 1);
    if (*Count == 0) {
      ActivePassiveBuffSources.Remove(AbilityId);
    }
  }

  if (ActivePassiveBuffSources.Num() == 0) {
    SetPassiveBuffVisible(false);
  }
}

void AFighterPawn::ClearAllPassiveBuffIndicators() {
  ActivePassiveBuffSources.Empty();
  SetPassiveBuffVisible(false);
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

bool AFighterPawn::TreatsDifficultTerrainAsNormal() const {
  if (!AbilityComponent) {
    return false;
  }

  const FSkaldAbilityDefinition Passive = AbilityComponent->GetPassiveAbility();
  return Passive.AbilityId == TEXT("Ability_Frog_Passive");
}

int32 AFighterPawn::GetMovementStepCost(const FIntPoint &From, const FIntPoint &To,
                                        const UGridOverlayComponent *Grid) const {
  if (From == To) {
    return 0;
  }

  const bool bTreatsTerrainAsNormal = TreatsDifficultTerrainAsNormal();
  const bool bDifficult = Grid && (Grid->IsDifficultTerrain(From) ||
                                   Grid->IsDifficultTerrain(To));
  if (!bDifficult || bTreatsTerrainAsNormal) {
    return 1;
  }

  return 2;
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
  UpdatePassiveBuffDecalSize();
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

void AFighterPawn::SetPassiveBuffVisible(bool bVisible) {
  if (!PassiveBuffDecal) {
    return;
  }

  PassiveBuffDecal->SetVisibility(bVisible);
  PassiveBuffDecal->SetHiddenInGame(!bVisible);
}

void AFighterPawn::UpdatePassiveBuffDecalSize() {
  if (!PassiveBuffDecal) {
    return;
  }

  const FVector DesiredSize =
      GridFootprint == EFighterPawnFootprint::FourCells
          ? PassiveBuffDecalSizeFourCells
          : PassiveBuffDecalSizeSingleCell;
  PassiveBuffDecal->DecalSize = DesiredSize;
}

void AFighterPawn::UpdatePassiveBuffDecalTransform() {
  if (!PassiveBuffDecal || !CollisionComponent) {
    return;
  }

  const float CapsuleHalfHeight = CollisionComponent->GetScaledCapsuleHalfHeight();
  const float VerticalOffset =
      -CapsuleHalfHeight + PassiveBuffDecalFloorOffset;
  PassiveBuffDecal->SetRelativeLocation(FVector(0.f, 0.f, VerticalOffset));
}

void AFighterPawn::RefreshPassiveBuffDecalMaterial() {
  if (!PassiveBuffDecal) {
    return;
  }

  PassiveBuffDecal->SetDecalMaterial(PassiveBuffDecalMaterial);
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

int32 AFighterPawn::GetDisengageRange() const {
  return FMath::Max(0, FMath::Min(Stats.Movement, DisengageMaxDistance));
}

bool AFighterPawn::ValidateMovementDestination(
    const FIntPoint &TargetCell, int32 MaxDistance,
    TArray<FIntPoint> &OutPreviousCells, TArray<FIntPoint> &OutTargetCells,
    UGridOverlayComponent *&OutGrid) const {
  if (MaxDistance <= 0) {
    return false;
  }

  OutGrid = GetGrid();
  if (!OutGrid) {
    return false;
  }

  const FIntPoint StartCell = CurrentCell;
  const int32 Distance = FMath::Max(
      FMath::Abs(TargetCell.X - StartCell.X),
      FMath::Abs(TargetCell.Y - StartCell.Y));
  if (Distance > MaxDistance) {
    return false;
  }

  OutPreviousCells = GetOccupiedCells();
  OutTargetCells = GetOccupiedCells(TargetCell);
  if (OutTargetCells.Num() == 0) {
    return false;
  }

  bool bCanOccupyTarget = true;
  for (const FIntPoint &Cell : OutTargetCells) {
    if (!OutGrid->IsCellInBounds(Cell) || OutGrid->IsObscured(Cell)) {
      bCanOccupyTarget = false;
      break;
    }
    const bool bCellPreviouslyOccupied = OutPreviousCells.Contains(Cell);
    if (!bCellPreviouslyOccupied && OutGrid->IsOccupied(Cell)) {
      bCanOccupyTarget = false;
      break;
    }
  }

  if (!bCanOccupyTarget) {
    return false;
  }

  bool bTargetReachable = (StartCell == TargetCell);
  if (bTargetReachable) {
    return true;
  }

  TSet<FIntPoint> IgnoredCells;
  for (const FIntPoint &Cell : OutPreviousCells) {
    IgnoredCells.Add(Cell);
  }

  auto CanOccupyAnchor = [&](const FIntPoint &Anchor) {
    const TArray<FIntPoint> CandidateCells = GetOccupiedCells(Anchor);
    for (const FIntPoint &Cell : CandidateCells) {
      if (!OutGrid->IsCellInBounds(Cell) || OutGrid->IsObscured(Cell)) {
        return false;
      }
      if (OutGrid->IsOccupied(Cell) && !IgnoredCells.Contains(Cell)) {
        return false;
      }
    }
    return true;
  };

  static const FIntPoint Directions[8] = {
      FIntPoint(1, 0),  FIntPoint(-1, 0), FIntPoint(0, 1),  FIntPoint(0, -1),
      FIntPoint(1, 1),  FIntPoint(1, -1), FIntPoint(-1, 1), FIntPoint(-1, -1)};

  TMap<FIntPoint, int32> BestCost;
  TQueue<TPair<FIntPoint, int32>> Frontier;
  BestCost.Add(StartCell, 0);
  Frontier.Enqueue(TPair<FIntPoint, int32>(StartCell, 0));

  while (!Frontier.IsEmpty()) {
    TPair<FIntPoint, int32> Node;
    Frontier.Dequeue(Node);

    const FIntPoint Cell = Node.Key;
    const int32 DistanceFromStart = Node.Value;

    if (DistanceFromStart > MaxDistance) {
      continue;
    }

    if (Cell == TargetCell) {
      bTargetReachable = true;
      break;
    }

    for (const FIntPoint &Dir : Directions) {
      const FIntPoint Next = Cell + Dir;
      if (!OutGrid->CanTraverseVertical(Cell, Next)) {
        continue;
      }

      if (!CanOccupyAnchor(Next)) {
        continue;
      }

      if (Dir.X != 0 && Dir.Y != 0) {
        const FIntPoint StepX(Dir.X, 0);
        const FIntPoint StepY(0, Dir.Y);
        const TArray<FIntPoint> FromCells = GetOccupiedCells(Cell);
        const TArray<FIntPoint> NextCells = GetOccupiedCells(Next);
        TSet<FIntPoint> NextCellSet;
        NextCellSet.Reserve(NextCells.Num());
        for (const FIntPoint &NextCell : NextCells) {
          NextCellSet.Add(NextCell);
        }

        auto IsBlocked = [&](const FIntPoint &CheckCell) {
          if (!OutGrid->IsCellInBounds(CheckCell) ||
              OutGrid->IsObscured(CheckCell)) {
            return true;
          }
          if (OutGrid->IsOccupied(CheckCell) && !IgnoredCells.Contains(CheckCell) &&
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

      const int32 StepCost =
          FMath::Max(1, GetMovementStepCost(Cell, Next, OutGrid));
      const int32 NewCost = DistanceFromStart + StepCost;
      if (NewCost > MaxDistance) {
        continue;
      }

      const int32 *ExistingCost = BestCost.Find(Next);
      if (ExistingCost && *ExistingCost <= NewCost) {
        continue;
      }

      BestCost.Add(Next, NewCost);
      Frontier.Enqueue(TPair<FIntPoint, int32>(Next, NewCost));
    }
  }

  return bTargetReachable;
}

bool AFighterPawn::CommitMovementToCell(
    const FIntPoint &PreviousCell, const FIntPoint &TargetCell,
    UGridOverlayComponent *Grid, const TArray<FIntPoint> &PreviousCells,
    const TArray<FIntPoint> &TargetCells, int32 Distance, bool bSpendAction) {
  if (Grid) {
    for (const FIntPoint &Cell : PreviousCells) {
      Grid->SetOccupied(Cell, false);
    }
  }

  MovementSourceCell = PreviousCell;
  CurrentCell = TargetCell;

  const FVector NewLocation =
      Grid ? GetAlignedWorldLocation(TargetCell) : GetActorLocation();
  MovementTargetLocation = NewLocation;
  RefreshDisplayMeshYawOffset();
  FaceTowardsCells(PreviousCell, TargetCell);
  FaceTowardsLocation(NewLocation);

  ResolveTrapsAtDestination(TargetCells);

  if (!IsAlive()) {
    if (Grid) {
      Grid->ClearHighlights();
    }
    SetIsMoving(false);
    return false;
  }

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

  if (bSpendAction) {
    ConsumeAction();
  }

  if (AbilityComponent) {
    AbilityComponent->NotifyOwnerMoved(Distance);
  }

  if (Grid) {
    for (const FIntPoint &Cell : TargetCells) {
      Grid->SetOccupied(Cell, true);
    }
    Grid->ClearHighlights();
  }

  return true;
}

bool AFighterPawn::HasAdjacentEnemyAtAnchor(
    const FIntPoint &Anchor,
    const TArray<AFighterPawn *> &FighterSnapshot) const {
  if (!IsAlive()) {
    return false;
  }

  const TArray<FIntPoint> CandidateCells = GetOccupiedCells(Anchor);
  if (CandidateCells.Num() == 0) {
    return false;
  }

  for (AFighterPawn *Fighter : FighterSnapshot) {
    if (!Fighter || Fighter == this) {
      continue;
    }
    if (!Fighter->IsAlive() || Fighter->Faction == Faction ||
        Fighter->IsActorBeingDestroyed()) {
      continue;
    }

    const TArray<FIntPoint> OtherCells = Fighter->GetOccupiedCells();
    for (const FIntPoint &SelfCell : CandidateCells) {
      for (const FIntPoint &OtherCell : OtherCells) {
        const int32 Distance = FMath::Max(
            FMath::Abs(SelfCell.X - OtherCell.X),
            FMath::Abs(SelfCell.Y - OtherCell.Y));
        if (Distance <= 1) {
          return true;
        }
      }
    }
  }

  return false;
}

bool AFighterPawn::HasAdjacentEnemyAtAnchor(const FIntPoint &Anchor) const {
  if (const USkaldGameInstance *GI =
          Cast<USkaldGameInstance>(GetGameInstance())) {
    if (UGridBattleManager *BattleManager = GI->GridBattleManager) {
      const TArray<AFighterPawn *> Fighters =
          BattleManager->GetInitiativeOrderSnapshot();
      return HasAdjacentEnemyAtAnchor(Anchor, Fighters);
    }
  }
  return false;
}

void AFighterPawn::MoveToCell(FIntPoint TargetCell) {
  if (!bIsCurrentlyActive || ActionsRemaining <= 0) {
    return;
  }
  if (bIsEngaged) {
    return;
  }

  const int32 MaxDistance = Stats.Movement;
  if (MaxDistance <= 0) {
    return;
  }

  UGridOverlayComponent *Grid = nullptr;
  TArray<FIntPoint> PreviousCells;
  TArray<FIntPoint> TargetCells;
  if (!ValidateMovementDestination(TargetCell, MaxDistance, PreviousCells,
                                   TargetCells, Grid)) {
    return;
  }

  const FIntPoint PreviousCell = CurrentCell;
  const int32 Distance = FMath::Max(
      FMath::Abs(TargetCell.X - PreviousCell.X),
      FMath::Abs(TargetCell.Y - PreviousCell.Y));

  CommitMovementToCell(PreviousCell, TargetCell, Grid, PreviousCells,
                       TargetCells, Distance, true);
  RecalculateBattleEngagement();
}

bool AFighterPawn::TryDisengageToCell(FIntPoint TargetCell) {
  if (!bIsCurrentlyActive || ActionsRemaining <= 0) {
    return false;
  }
  if (!bIsEngaged) {
    return false;
  }

  const int32 DisengageRange = GetDisengageRange();
  if (DisengageRange <= 0) {
    return false;
  }

  if (TargetCell == CurrentCell) {
    return false;
  }

  UGridOverlayComponent *Grid = nullptr;
  TArray<FIntPoint> PreviousCells;
  TArray<FIntPoint> TargetCells;
  if (!ValidateMovementDestination(TargetCell, DisengageRange, PreviousCells,
                                   TargetCells, Grid)) {
    return false;
  }

  TArray<AFighterPawn *> FighterSnapshot;
  if (USkaldGameInstance *GI = Cast<USkaldGameInstance>(GetGameInstance())) {
    if (UGridBattleManager *BattleManager = GI->GridBattleManager) {
      FighterSnapshot = BattleManager->GetInitiativeOrderSnapshot();
    }
  }

  if (HasAdjacentEnemyAtAnchor(TargetCell, FighterSnapshot)) {
    return false;
  }

  const FIntPoint PreviousCell = CurrentCell;
  const int32 Distance = FMath::Max(
      FMath::Abs(TargetCell.X - PreviousCell.X),
      FMath::Abs(TargetCell.Y - PreviousCell.Y));

  const bool bCompletedMovement =
      CommitMovementToCell(PreviousCell, TargetCell, Grid, PreviousCells,
                           TargetCells, Distance, true);
  RecalculateBattleEngagement();
  return bCompletedMovement || !IsAlive();
}

bool AFighterPawn::TryTeleportToCell(FIntPoint TargetCell, int32 MaxDistance,
                                     bool bRequireLineOfSight) {
  UGridOverlayComponent *Grid = GetGrid();
  if (!Grid) {
    return false;
  }

  const FIntPoint StartCell = CurrentCell;
  const int32 Distance = FMath::Max(FMath::Abs(TargetCell.X - StartCell.X),
                                    FMath::Abs(TargetCell.Y - StartCell.Y));
  if (MaxDistance > 0 && Distance > MaxDistance) {
    return false;
  }

  const TArray<FIntPoint> PreviousCells = GetOccupiedCells(StartCell);
  const TArray<FIntPoint> TargetCells = GetOccupiedCells(TargetCell);
  if (TargetCells.Num() == 0) {
    return false;
  }

  bool bHasLineOfSight = !bRequireLineOfSight;

  for (const FIntPoint &Cell : TargetCells) {
    if (!Grid->IsCellInBounds(Cell) || Grid->IsObscured(Cell)) {
      return false;
    }

    const bool bPreviouslyOccupied = PreviousCells.Contains(Cell);
    if (!bPreviouslyOccupied && Grid->IsOccupied(Cell)) {
      return false;
    }
  }

  if (bRequireLineOfSight) {
    for (const FIntPoint &FromCell : PreviousCells) {
      for (const FIntPoint &ToCell : TargetCells) {
        if (Grid->HasLineOfSight(FromCell, ToCell)) {
          bHasLineOfSight = true;
          break;
        }
      }

      if (bHasLineOfSight) {
        break;
      }
    }

    if (!bHasLineOfSight) {
      return false;
    }
  }

  for (const FIntPoint &Cell : PreviousCells) {
    if (!TargetCells.Contains(Cell)) {
      Grid->SetOccupied(Cell, false);
    }
  }

  CurrentCell = TargetCell;
  MovementSourceCell = TargetCell;
  const FVector NewLocation = GetAlignedWorldLocation(TargetCell);
  MovementTargetLocation = NewLocation;
  SetActorLocation(NewLocation);
  SetIsMoving(false);
  RefreshDisplayMeshYawOffset();
  FaceTowardsCells(StartCell, TargetCell);
  FaceTowardsLocation(NewLocation);

  ResolveTrapsAtDestination(TargetCells);

  if (!IsAlive()) {
    for (const FIntPoint &Cell : PreviousCells) {
      Grid->SetOccupied(Cell, false);
    }
    for (const FIntPoint &Cell : TargetCells) {
      Grid->SetOccupied(Cell, false);
    }
    return true;
  }

  for (const FIntPoint &Cell : TargetCells) {
    Grid->SetOccupied(Cell, true);
  }

  const int32 NotifiedDistance = MaxDistance > 0 ? FMath::Min(Distance, MaxDistance)
                                                 : Distance;
  if (AbilityComponent) {
    AbilityComponent->NotifyOwnerMoved(NotifiedDistance);
  }

  RecalculateBattleEngagement();

  return true;
}

void AFighterPawn::RecalculateBattleEngagement() const {
  if (!HasAuthority()) {
    return;
  }

  const USkaldGameInstance *GI = Cast<USkaldGameInstance>(GetGameInstance());
  if (!GI) {
    return;
  }

  if (UGridBattleManager *BattleManager = GI->GridBattleManager) {
    const TArray<AFighterPawn *> Fighters =
        BattleManager->GetInitiativeOrderSnapshot();
    for (AFighterPawn *Fighter : Fighters) {
      if (Fighter && Fighter->HasAuthority()) {
        Fighter->RefreshEngagementStatusFromSnapshot(Fighters);
      }
    }
  }
}

void AFighterPawn::RefreshEngagementStatusFromSnapshot(
    const TArray<AFighterPawn *> &Fighters) {
  if (!HasAuthority()) {
    return;
  }

  if (!IsAlive()) {
    SetEngaged(false);
    return;
  }

  const bool bShouldEngage = HasAdjacentEnemyAtAnchor(CurrentCell, Fighters);
  SetEngaged(bShouldEngage);
}

void AFighterPawn::SetEngaged(bool bNewEngaged) {
  if (bIsEngaged == bNewEngaged) {
    return;
  }

  bIsEngaged = bNewEngaged;
  OnEngagementChanged.Broadcast(bIsEngaged);
}

void AFighterPawn::OnRep_IsEngaged() {
  OnEngagementChanged.Broadcast(bIsEngaged);
}

USkaldDiceManager *AFighterPawn::GetDiceManager() const {
  if (CachedDiceManager.IsValid()) {
    return CachedDiceManager.Get();
  }

  if (UWorld *World = GetWorld()) {
    if (USkaldGameInstance *GameInstance =
            Cast<USkaldGameInstance>(World->GetGameInstance())) {
      if (USkaldDiceManager *Manager =
              GameInstance->GetSubsystem<USkaldDiceManager>()) {
        return Manager;
      }
    }
  }

  return nullptr;
}

void AFighterPawn::EnsureDiceManagerBinding() {
  if (USkaldDiceManager *Manager = GetDiceManager()) {
    CachedDiceManager = Manager;
    if (!Manager->OnDiceRollCompleted.IsAlreadyBound(
            this, &AFighterPawn::HandleDiceRollCompleted)) {
      Manager->OnDiceRollCompleted.AddDynamic(
          this, &AFighterPawn::HandleDiceRollCompleted);
    }
  }
}

void AFighterPawn::CleanupDiceManagerBinding() {
  if (USkaldDiceManager *Manager = CachedDiceManager.Get()) {
    Manager->OnDiceRollCompleted.RemoveDynamic(
        this, &AFighterPawn::HandleDiceRollCompleted);
  }
  CachedDiceManager.Reset();
}

bool AFighterPawn::AttemptPhysicalAttackRoll(AFighterPawn* Target)
{
    if (!Target)
    {
        return false;
    }

    // All fighters now leverage the manual dice presentation so the same
    // cinematic attack camera and dice flow are used for both human players and
    // AI-controlled units.
    ASkaldPlayerController* OwningController = Cast<ASkaldPlayerController>(GetController());
    if (!OwningController)
    {
        return false;
    }

    // Only use the manual physical roll flow if we actually have dice.
    if (Stats.AttackDice <= 0)
    {
        return false; // fall back to normal auto-resolution path
    }

    // Store context so we know what to resolve when the player hits Roll.
    PendingPhysicalAttackTarget = Target;
    PendingAttackAttackerSnapshot = Stats;
    PendingAttackDefenderSnapshot = Target->Stats;
    bHasPendingAttackSnapshot = true;
    bAwaitingPhysicalAttackRoll = true;

    //  Debug: confirm we actually get here
    UE_LOG(LogTemp, Warning, TEXT("AttemptPhysicalAttackRoll: attacker %s vs target %s, Dice=%d"),
        *GetName(), *Target->GetName(), Stats.AttackDice);

    // Ask the battle manager / HUD to show the Roll button for this pawn.
    if (USkaldGameInstance* GI = Cast<USkaldGameInstance>(GetGameInstance()))
    {
        if (UGridBattleManager* BattleManager = GI->GridBattleManager)
        {
            UE_LOG(LogTemp, Warning, TEXT("AttemptPhysicalAttackRoll: calling ShowAttackRollButtonForPlayer"));

            if (HasAuthority())
            {
                ShowAttackRollButtonForPlayer();
            }
            else
            {
                ServerShowAttackRollButtonForPlayer();
            }
        }
    }

    return true;
}

void AFighterPawn::TriggerManualAttackRoll()
{
    // Safety: ensure a pending attack exists.
    if (!bAwaitingPhysicalAttackRoll || !PendingPhysicalAttackTarget.IsValid())
    {
        return;
    }

    AFighterPawn* Target = PendingPhysicalAttackTarget.Get();
    if (!Target || Stats.AttackDice <= 0)
    {
        return;
    }

    EnsureDiceManagerBinding();
    USkaldDiceManager* DiceManager = CachedDiceManager.Get();
    if (!DiceManager)
    {
        return;
    }

    const int32 DiceToRoll = FMath::Max(Stats.AttackDice, 0);
    const FGuid RollId = DiceManager->RollDice_D6(DiceToRoll, 0, false);
    if (!RollId.IsValid())
    {
        return;
    }

    PendingAttackRollId = RollId;
    // keep bAwaitingPhysicalAttackRoll = true; cleared when result arrives

    // Hide the button now that rolling has started
    if (USkaldGameInstance* GI = Cast<USkaldGameInstance>(GetGameInstance()))
    {
        if (UGridBattleManager* BattleManager = GI->GridBattleManager)
        {
            BattleManager->HideAttackRollButtonForFighter(this);
        }
    }
} // nice clean closing brace

// Server RPC Implementation  runs on the server
void AFighterPawn::ServerShowAttackRollButtonForPlayer_Implementation()
{
    ShowAttackRollButtonForPlayer();
}

AFighterPawn *AFighterPawn::GetPendingPhysicalAttackTarget() const {
  return PendingPhysicalAttackTarget.Get();
}

// Core function  actually shows the roll button
void AFighterPawn::ShowAttackRollButtonForPlayer()
{
    // Ensure this runs on the server only
    if (!HasAuthority())
    {
        UE_LOG(LogTemp, Warning, TEXT("ShowAttackRollButtonForPlayer called on client, skipping (no authority)."));
        return;
    }

    // Attempt to forward to the GridBattleManager via GameInstance
    if (UWorld* World = GetWorld())
    {
        if (USkaldGameInstance* GI = Cast<USkaldGameInstance>(World->GetGameInstance()))
        {
            if (UGridBattleManager* Manager = GI->GridBattleManager)
            {
                UE_LOG(LogTemp, Warning, TEXT("AFighterPawn forwarding to GridBattleManager->ShowAttackRollButtonForPlayer (%s)"), *GetName());
                Manager->ShowAttackRollButtonForPlayer(this);
                return;
            }
        }
    }

    // Fallback path: try directly via PlayerController RPC
    ASkaldPlayerController* PC = Cast<ASkaldPlayerController>(GetController());
    if (!PC)
    {
        UE_LOG(LogTemp, Warning, TEXT("ShowAttackRollButtonForPlayer: No valid controller for %s"), *GetName());
        return;
    }

    UE_LOG(LogTemp, Warning, TEXT("ShowAttackRollButtonForPlayer: Fallback path calling ClientShowAttackRollButton for %s"), *PC->GetName());
    PC->ClientShowAttackRollButton(this);
}

void AFighterPawn::HandleDiceRollCompleted(const FGuid &RollId,
                                           const TArray<int32> &Results) {
  if (!HasAuthority()) {
    ServerSubmitPhysicalDiceRoll(RollId, Results);
    return;
  }

  ProcessPhysicalDiceRollResults(RollId, Results);
}

void AFighterPawn::ProcessPhysicalDiceRollResults(
    const FGuid &RollId, const TArray<int32> &Results) {
  if (!bAwaitingPhysicalAttackRoll || RollId != PendingAttackRollId) {
    return;
  }

  FString ResultValuesString;
  for (int32 Index = 0; Index < Results.Num(); ++Index) {
    if (Index > 0) {
      ResultValuesString.Append(TEXT(","));
    }
    ResultValuesString.AppendInt(Results[Index]);
  }
  if (ResultValuesString.IsEmpty()) {
    ResultValuesString = TEXT("<empty>");
  }

  UE_LOG(LogTemp, Warning,
         TEXT("HandleDiceRollCompleted: RollId=%s Values=[%s]"),
         *RollId.ToString(), *ResultValuesString);

  bAwaitingPhysicalAttackRoll = false;
  PendingAttackRollId.Invalidate();

  AFighterPawn *Target = PendingPhysicalAttackTarget.Get();
  PendingPhysicalAttackTarget.Reset();

  if (!Target) {
    PendingAttackTarget.Reset();
    return;
  }

  USkaldGameInstance *GameInstance = nullptr;
  if (UWorld *World = GetWorld()) {
    GameInstance = Cast<USkaldGameInstance>(World->GetGameInstance());
  }

  FRandomStream *RandomStream = GameInstance ? &GameInstance->CombatRandomStream
                                             : nullptr;
  FRandomStream TempStream;
  if (!RandomStream) {
    TempStream.Initialize(FMath::Rand());
    RandomStream = &TempStream;
  }

  const FFighterStats &AttackerStats =
      bHasPendingAttackSnapshot ? PendingAttackAttackerSnapshot : Stats;

  FFighterStats DefenderStatsSnapshot =
      bHasPendingAttackSnapshot
          ? PendingAttackDefenderSnapshot
          : (Target ? Target->Stats : FFighterStats());

  FDiceRollResult DiceResult =
      UGridBattleManager::ResolveAttackDice(AttackerStats, DefenderStatsSnapshot,
                                            *RandomStream, Results);

  TArray<int32> SanitizedResults = Results;
  if (SanitizedResults.Num() > DiceResult.DiceOutcomes.Num()) {
    SanitizedResults.SetNum(DiceResult.DiceOutcomes.Num());
  }
  for (int32 &Value : SanitizedResults) {
    Value = FMath::Clamp(Value, 1, 6);
  }
  if (SanitizedResults.Num() < DiceResult.DiceOutcomes.Num()) {
    const int32 ExistingCount = SanitizedResults.Num();
    SanitizedResults.Reserve(DiceResult.DiceOutcomes.Num());
    for (int32 Index = ExistingCount; Index < DiceResult.DiceOutcomes.Num(); ++Index) {
      SanitizedResults.Add(DiceResult.DiceOutcomes[Index].RollValue);
    }
  }

  const bool bHasExistingQueuedAttack =
      bHasPendingDiceResult && PendingAttackTarget.IsValid() &&
      PendingAttackTarget.Get() == Target;

  if (bHasExistingQueuedAttack) {
    PendingPhysicalRollValues = SanitizedResults;

    const FFighterStats &AttackerStatsSnapshot =
        bHasPendingAttackSnapshot ? PendingAttackAttackerSnapshot : Stats;

    FFighterStats DefenderStatsSnapshot = bHasPendingAttackSnapshot
                                              ? PendingAttackDefenderSnapshot
                                              : (Target ? Target->Stats
                                                        : FFighterStats());

    PendingAttackDiceResult.HighStakesFaction = Faction;

    ApplyPhysicalRollResults(PendingAttackDiceResult, PendingPhysicalRollValues,
                             AttackerStatsSnapshot, DefenderStatsSnapshot);

    const int32 StartingHealth =
        PendingAttackDiceResult.StartingHealth > 0
            ? PendingAttackDiceResult.StartingHealth
            : FMath::Max(0, DefenderStatsSnapshot.Health);
    const int32 ClampedEndingHealth =
        FMath::Clamp(PendingAttackDiceResult.EndingHealth, 0, StartingHealth);

    PendingAttackDiceResult.EndingHealth = ClampedEndingHealth;
    PendingAttackDiceResult.TotalDamage =
        FMath::Clamp(StartingHealth - ClampedEndingHealth, 0, StartingHealth);

    if (Target) {
      const bool bHealthChanged = Target->Stats.Health != ClampedEndingHealth;
      Target->Stats.Health = ClampedEndingHealth;
      if (bHealthChanged) {
        Target->OnHealthChanged.Broadcast(Target->Stats.Health);
      }
      bPendingAttackTargetDied = Target->Stats.Health <= 0;
    }

    RefreshPendingAttackResultStats(Target);
    return;
  }

  PendingPhysicalRollValues = MoveTemp(SanitizedResults);
  DiceResult.HighStakesFaction = Faction;

  StartQueuedAttack(Target, MoveTemp(DiceResult));
}

void AFighterPawn::ServerSubmitPhysicalDiceRoll_Implementation(
    const FGuid &RollId, const TArray<int32> &Results) {
  ProcessPhysicalDiceRollResults(RollId, Results);
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

  USkaldGameInstance *GameInstance = nullptr;
  if (UWorld *World = GetWorld()) {
    GameInstance = Cast<USkaldGameInstance>(World->GetGameInstance());
  }

  if (GameInstance) {
    if (UGridBattleManager *BattleManager = GameInstance->GridBattleManager) {
      if (BattleManager->IsAwaitingAttackPresentation()) {
        BattleManager->ReportAttackRejected(
            this, Target,
            NSLOCTEXT("SkaldBattle", "AttackPresentationPending",
                      "Attack results are still playing."));
        return;
      }
    }
  }

  FRandomStream *RandomStream = nullptr;
  if (GameInstance) {
    RandomStream = &GameInstance->CombatRandomStream;
  }

  if (!RandomStream) {
    return;
  }

  if (AbilityComponent) {
    AbilityComponent->NotifyAttackCommitted();
  }

  ConsumeAction();

  RefreshDisplayMeshYawOffset();
  FaceTowardsCells(CurrentCell, Target->CurrentCell);
  FaceTowardsLocation(Target->GetActorLocation());

  if (Grid) {
    Grid->ClearHighlights();
  }

  if (AttemptPhysicalAttackRoll(Target)) {
    return;
  }

  FDiceRollResult DiceResult =
      UGridBattleManager::ResolveAttackDice(Stats, Target->Stats, *RandomStream);
  DiceResult.HighStakesFaction = Faction;

  StartQueuedAttack(Target, MoveTemp(DiceResult));
}

bool AFighterPawn::IsResolvingQueuedAttack() const {
  return PendingAttackTarget.IsValid() || bHasPendingDiceResult ||
         bAwaitingPhysicalAttackRoll;
}

void AFighterPawn::StartQueuedAttack(AFighterPawn *Target,
                                     FDiceRollResult &&DiceResult) {
  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(AttackRollTimerHandle);
  }

  if (Target) {
    PendingAttackDefenderSnapshot = Target->Stats;
  }
  PendingAttackAttackerSnapshot = Stats;
  bHasPendingAttackSnapshot = true;

  PendingAttackDiceResult = MoveTemp(DiceResult);
  PendingAttackOutcomeIndex = 0;
  PendingAttackTarget = Target;
  bPendingAttackTargetDied = false;
  bHasProcessedPendingRoll = false;
  bHasPendingDiceResult = true;

  ApplyPendingPhysicalRollValues();

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

  FDiceRollOutcome &Outcome =
      PendingAttackDiceResult.DiceOutcomes[PendingAttackOutcomeIndex];
  bHasProcessedPendingRoll = true;

  if (Outcome.bHit) {
    const FFighterStats &DamageStats =
        bHasPendingAttackSnapshot ? PendingAttackAttackerSnapshot : Stats;
    const int32 BaseDamage = FMath::Max(0, DamageStats.AttackDamage);
    const int32 CriticalBonus =
        FMath::Max(0, DamageStats.CriticalBonusDamage);
    const bool bCriticalRoll = Outcome.RollValue == 6;

    int32 CalculatedDamage = Outcome.bHit ? BaseDamage : 0;
    if (Outcome.bHit && bCriticalRoll) {
      CalculatedDamage += CriticalBonus;
    }

    Outcome.bCritical =
        Outcome.bHit && bCriticalRoll && CalculatedDamage > BaseDamage;
    Outcome.Damage = CalculatedDamage;

    const int32 HealthBefore = Target->Stats.Health;
    const int32 DamageToApply =
        FMath::Min(CalculatedDamage, HealthBefore);
    Target->Stats.Health =
        FMath::Max(0, HealthBefore - DamageToApply);
    if (Target->Stats.Health <= 0) {
      bPendingAttackTargetDied = true;
    }
  } else {
    Outcome.Damage = 0;
    Outcome.bCritical = false;
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

void AFighterPawn::ApplyPendingPhysicalRollValues() {
  if (!bHasPendingDiceResult || PendingPhysicalRollValues.Num() == 0) {
    return;
  }

  const FFighterStats &AttackerStats =
      bHasPendingAttackSnapshot ? PendingAttackAttackerSnapshot : Stats;

  FFighterStats DefenderStatsSnapshot = bHasPendingAttackSnapshot
                                            ? PendingAttackDefenderSnapshot
                                            : (PendingAttackTarget.IsValid()
                                                   ? PendingAttackTarget->Stats
                                                   : FFighterStats());

  ApplyPhysicalRollResults(PendingAttackDiceResult, PendingPhysicalRollValues,
                           AttackerStats, DefenderStatsSnapshot);
}

void AFighterPawn::ApplyPhysicalRollResults(
    FDiceRollResult &Result, const TArray<int32> &RollValues,
    const FFighterStats &AttackerStats,
    const FFighterStats &DefenderStats) {
  FString RollValuesString;
  for (int32 Index = 0; Index < RollValues.Num(); ++Index) {
    if (Index > 0) {
      RollValuesString.Append(TEXT(","));
    }
    RollValuesString.AppendInt(RollValues[Index]);
  }
  if (RollValuesString.IsEmpty()) {
    RollValuesString = TEXT("<empty>");
  }

  UE_LOG(LogTemp, Warning,
         TEXT("ApplyPhysicalRollResults: IncomingRolls=[%s] RollCount=%d OutcomeCount=%d"),
         *RollValuesString, RollValues.Num(), Result.DiceOutcomes.Num());

  if (RollValues.Num() == 0 || Result.DiceOutcomes.Num() == 0) {
    return;
  }

  const int32 ExpectedDice = FMath::Max(0, Result.DiceOutcomes.Num());
  if (ExpectedDice <= 0) {
    return;
  }

  TArray<int32> SanitizedValues = RollValues;
  if (SanitizedValues.Num() > ExpectedDice) {
    SanitizedValues.SetNum(ExpectedDice);
  }

  const int32 StartingHealth = Result.StartingHealth > 0
                                   ? Result.StartingHealth
                                   : FMath::Max(0, DefenderStats.Health);

  const int32 RequiredRoll = AttackerStats.Strength > DefenderStats.Defence
                                 ? 3
                                 : (AttackerStats.Strength < DefenderStats.Defence
                                        ? 5
                                        : 4);

  const int32 DiceCount = Result.DiceOutcomes.Num();
  const int32 ValuesToApply =
      FMath::Min(DiceCount, SanitizedValues.Num());

  const int32 BaseDamage = FMath::Max(0, AttackerStats.AttackDamage);
  const int32 CriticalBonus =
      FMath::Max(0, AttackerStats.CriticalBonusDamage);

  for (int32 Index = 0; Index < ValuesToApply; ++Index) {
    FDiceRollOutcome &Outcome = Result.DiceOutcomes[Index];
    const int32 RollValue = FMath::Clamp(SanitizedValues[Index], 1, 6);

    const bool bCriticalRoll = RollValue == 6;
    const bool bHit = bCriticalRoll || RollValue >= RequiredRoll;

    Outcome.RollValue = RollValue;

    if (!bHit) {
      Outcome.bHit = false;
      Outcome.bCritical = false;
      Outcome.Damage = 0;
      continue;
    }

    Outcome.bHit = true;

    int32 NewDamage = BaseDamage;
    if (bCriticalRoll) {
      NewDamage += CriticalBonus;
    }

    Outcome.Damage = FMath::Max(0, NewDamage);
    Outcome.bCritical = bCriticalRoll && Outcome.Damage > BaseDamage;
  }

  int32 HitCount = 0;
  int32 MissCount = 0;
  int32 CriticalCount = 0;
  int32 HighestCriticalDamage = 0;
  int32 SimulatedHealth = StartingHealth;

  for (FDiceRollOutcome &Outcome : Result.DiceOutcomes) {
    if (Outcome.bHit) {
      ++HitCount;

      const int32 AppliedDamage =
          FMath::Min(FMath::Max(0, Outcome.Damage), SimulatedHealth);
      SimulatedHealth = FMath::Max(0, SimulatedHealth - AppliedDamage);

      if (Outcome.bCritical) {
        ++CriticalCount;
        HighestCriticalDamage =
            FMath::Max(HighestCriticalDamage, Outcome.Damage);
      }
    } else {
      ++MissCount;
      Outcome.Damage = 0;
      Outcome.bCritical = false;
    }
  }

  const int32 ClampedEndingHealth =
      FMath::Clamp(SimulatedHealth, 0, StartingHealth);

  Result.HitCount = HitCount;
  Result.MissCount = MissCount;
  Result.CriticalHitCount = CriticalCount;
  Result.HighestCriticalDamage = HighestCriticalDamage;
  Result.EndingHealth = ClampedEndingHealth;
  Result.TotalDamage =
      FMath::Clamp(StartingHealth - ClampedEndingHealth, 0, StartingHealth);
  Result.bHighStakesCritical =
      CriticalCount > 0 && ClampedEndingHealth <= 0 && StartingHealth > 0;
}

void AFighterPawn::RefreshPendingAttackResultStats(AFighterPawn *Target) {
  if (!bHasPendingDiceResult) {
    return;
  }

  const int32 StartingHealth = PendingAttackDiceResult.StartingHealth;

  int32 HitCount = 0;
  int32 MissCount = 0;
  int32 CriticalCount = 0;
  int32 HighestCriticalDamage = 0;
  int32 SimulatedHealth = StartingHealth;

  for (const FDiceRollOutcome &Outcome : PendingAttackDiceResult.DiceOutcomes) {
    if (Outcome.bHit) {
      ++HitCount;

      const int32 ClampedDamage = FMath::Max(0, Outcome.Damage);
      if (SimulatedHealth > 0) {
        const int32 AppliedDamage = FMath::Min(ClampedDamage, SimulatedHealth);
        SimulatedHealth = FMath::Max(0, SimulatedHealth - AppliedDamage);
      }

      if (Outcome.bCritical) {
        ++CriticalCount;
        HighestCriticalDamage = FMath::Max(HighestCriticalDamage, ClampedDamage);
      }
    } else {
      ++MissCount;
    }
  }

  int32 ActualEndingHealth = SimulatedHealth;
  if (Target) {
    ActualEndingHealth = Target->Stats.Health;
  } else if (bPendingAttackTargetDied) {
    ActualEndingHealth = 0;
  }

  const int32 ClampedEndingHealth =
      FMath::Clamp(ActualEndingHealth, 0, StartingHealth);

  PendingAttackDiceResult.HitCount = HitCount;
  PendingAttackDiceResult.MissCount = MissCount;
  PendingAttackDiceResult.CriticalHitCount = CriticalCount;
  PendingAttackDiceResult.HighestCriticalDamage = HighestCriticalDamage;
  PendingAttackDiceResult.EndingHealth = ClampedEndingHealth;
  PendingAttackDiceResult.TotalDamage =
      FMath::Clamp(StartingHealth - ClampedEndingHealth, 0, StartingHealth);
  PendingAttackDiceResult.bHighStakesCritical =
      CriticalCount > 0 && ClampedEndingHealth <= 0 && StartingHealth > 0;
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
  }

  if (bHasPendingDiceResult) {
    ApplyPendingPhysicalRollValues();
    RefreshPendingAttackResultStats(Target);

    if (USkaldGameInstance *GI =
            Cast<USkaldGameInstance>(GetGameInstance())) {
      if (UGridBattleManager *BattleManager = GI->GridBattleManager) {
        BattleManager->ReportAttackResolution(this, Target,
                                             PendingAttackDiceResult);
      }
    }
  }

  const bool bTargetShouldBeDestroyed =
      Target && !Target->IsAlive() && !Target->IsActorBeingDestroyed();

  ClearQueuedAttackState(true);

  if (bTargetShouldBeDestroyed) {
    bool bHandledByBattleManager = false;

    if (USkaldGameInstance *GI = Cast<USkaldGameInstance>(GetGameInstance())) {
      if (UGridBattleManager *BattleManager = GI->GridBattleManager) {
        bHandledByBattleManager =
            BattleManager->RegisterPendingFighterDeath(Target);
      }
    }

    if (!bHandledByBattleManager) {
      if (UWorld *WorldPtr = GetWorld()) {
        const TWeakObjectPtr<AFighterPawn> TargetPtr = Target;
        WorldPtr->GetTimerManager().SetTimerForNextTick(
            FTimerDelegate::CreateLambda([TargetPtr]() {
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
  PendingPhysicalAttackTarget.Reset();
  bPendingAttackTargetDied = false;
  bHasProcessedPendingRoll = false;
  bAwaitingPhysicalAttackRoll = false;
  PendingAttackRollId.Invalidate();
  PendingAttackAttackerSnapshot = FFighterStats();
  PendingAttackDefenderSnapshot = FFighterStats();
  bHasPendingAttackSnapshot = false;
  PendingPhysicalRollValues.Reset();

  if (bBroadcastFinalized) {
    OnQueuedAttackFinalized.Broadcast();
  }
}

void AFighterPawn::HandleIncomingAttackStarted() {
  ++ActiveIncomingAttackCount;
  SetTargetedIndicatorVisible(true);
  if (AbilityComponent) {
    AbilityComponent->HandleIncomingAttackStarted();
  }
}

void AFighterPawn::HandleIncomingAttackFinished() {
  if (ActiveIncomingAttackCount > 0) {
    --ActiveIncomingAttackCount;
  }

  if (ActiveIncomingAttackCount <= 0) {
    ActiveIncomingAttackCount = 0;
    SetTargetedIndicatorVisible(false);
  }
  if (AbilityComponent) {
    AbilityComponent->HandleIncomingAttackFinished();
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

bool AFighterPawn::IsMoving() const { return bIsMoving; }

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
  for (FActiveProjectileFX &Projectile : ActiveProjectileFX) {
    if (UNiagaraComponent *Component = Projectile.Component.Get()) {
      Component->Deactivate();
      Component->SetAutoDestroy(true);
    }
  }
  ActiveProjectileFX.Empty();

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

  RecalculateBattleEngagement();
  CurrentCell = FIntPoint::ZeroValue;
  MovementSourceCell = FIntPoint::ZeroValue;
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

void AFighterPawn::OnRep_Faction() {
  // Ability state is replicated directly; clients avoid rebuilding their loadout.
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
void AFighterPawn::OnRep_IsMoving() {
  if (bIsMoving) {
    MovementTargetLocation = GetAlignedWorldLocation(CurrentCell);
    MovementStartLocation = GetActorLocation();
    MovementStraightLineDistance =
        FVector::Dist(MovementStartLocation, MovementTargetLocation);
    MovementProgress = 0.f;
    RebuildVisualMovementPath(MovementTargetLocation);
  } else {
    ResetVisualMovementPath();
  }

  RefreshMovementAudioComponent();
}

void AFighterPawn::BroadcastActionsRemaining() {
  OnActionsChanged.Broadcast(ActionsRemaining);
  UpdateActivationIndicator();
}

void AFighterPawn::UpdateAbilityLoadout() { RefreshAbilityLoadout(); }

void AFighterPawn::RefreshAbilityLoadout() {
  if (!AbilityComponent || !HasAuthority()) {
    return;
  }

  AbilityComponent->RefreshAbilityLoadout(Stats, Faction);
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

void AFighterPawn::ResolveTrapsAtDestination(
    const TArray<FIntPoint> &DestinationCells) {
  if (GetLocalRole() != ROLE_Authority || DestinationCells.Num() == 0) {
    return;
  }

  UWorld *World = GetWorld();
  if (!World) {
    return;
  }

  USkaldGameInstance *GameInstance =
      Cast<USkaldGameInstance>(World->GetGameInstance());
  if (!GameInstance) {
    return;
  }

  UGridBattleManager *BattleManager = GameInstance->GridBattleManager;
  if (!BattleManager) {
    return;
  }

  const TArray<AFighterPawn *> Fighters =
      BattleManager->GetInitiativeOrderSnapshot();

  for (const FIntPoint &Cell : DestinationCells) {
    for (AFighterPawn *Fighter : Fighters) {
      if (!Fighter || Fighter == this || Fighter->Faction == Faction) {
        continue;
      }

      if (USkaldAbilityComponent *OtherAbilityComponent =
              Fighter->GetAbilityComponent()) {
        while (OtherAbilityComponent->TryResolveTrapAtCell(Cell, this)) {
          // Continue triggering traps anchored to this cell.
        }
      }
    }
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
    if (bIsMoving && bNewIsMoving) {
      MovementStartLocation = GetActorLocation();
      MovementStraightLineDistance =
          FVector::Dist(MovementStartLocation, MovementTargetLocation);
      MovementProgress = 0.f;
      RebuildVisualMovementPath(MovementTargetLocation);
      RefreshMovementAudioComponent();
    }
    return;
  }

  bIsMoving = bNewIsMoving;

  if (bIsMoving) {
    MovementStartLocation = GetActorLocation();
    MovementStraightLineDistance =
        FVector::Dist(MovementStartLocation, MovementTargetLocation);
    MovementProgress = 0.f;
    RebuildVisualMovementPath(MovementTargetLocation);
  } else {
    ResetVisualMovementPath();
  }

  RefreshMovementAudioComponent();
}
