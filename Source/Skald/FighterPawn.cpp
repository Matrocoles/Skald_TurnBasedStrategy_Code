#include "FighterPawn.h"
#include "Blueprint/UserWidget.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextBlock.h"
#include "Engine/CollisionProfile.h"
#include "EngineUtils.h"
#include "GridBattleManager.h"
#include "GridOverlayComponent.h"
#include "Net/UnrealNetwork.h"
#include "Skald_GameInstance.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
constexpr int32 ActionsPerActivation = 2;
}

AFighterPawn::AFighterPawn() {
  PrimaryActorTick.bCanEverTick = false;

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
  HealthWidget->SetRelativeLocation(FVector(0.f, 0.f, 400.f));
  HealthWidget->SetRelativeRotation(FRotator(0.f, 90.f, 0.f));
  HealthWidget->SetRelativeScale3D(FVector(1.f));

  static ConstructorHelpers::FClassFinder<UUserWidget> HealthWidgetFinder(
      TEXT("/Game/Blueprints/UI/WBP_FighterHealth"));
  if (HealthWidgetFinder.Succeeded()) {
    HealthWidgetTemplate = HealthWidgetFinder.Class;
  }

  static ConstructorHelpers::FClassFinder<UUserWidget> DamageWidgetFinder(
      TEXT("/Game/Blueprints/UI/WBP_DamageFloat"));
  if (DamageWidgetFinder.Succeeded()) {
    DamageFloatWidgetTemplate = DamageWidgetFinder.Class;
  }

  ActionsRemaining = 0;
  bHasActivatedThisRound = false;
  bIsCurrentlyActive = false;
  CurrentCell = FIntPoint::ZeroValue;

  UpdateMeshOffset();
}

void AFighterPawn::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty> &OutLifetimeProps) const {
  Super::GetLifetimeReplicatedProps(OutLifetimeProps);

  DOREPLIFETIME(AFighterPawn, Stats);
  DOREPLIFETIME(AFighterPawn, bIsAttacker);
  DOREPLIFETIME(AFighterPawn, ActionsRemaining);
  DOREPLIFETIME(AFighterPawn, bHasActivatedThisRound);
  DOREPLIFETIME(AFighterPawn, bIsCurrentlyActive);
  DOREPLIFETIME(AFighterPawn, CurrentCell);
}

void AFighterPawn::OnConstruction(const FTransform &Transform) {
  Super::OnConstruction(Transform);
  UpdateMeshOffset();
}

void AFighterPawn::BeginPlay() {
  Super::BeginPlay();

  if (HealthWidget && HealthWidgetTemplate) {
    HealthWidget->SetWidgetClass(HealthWidgetTemplate);
  }

  OnHealthChanged.AddDynamic(this, &AFighterPawn::UpdateHealthDisplay);
  OnHealthChanged.Broadcast(Stats.Health);

  BroadcastActionsRemaining();

  UpdateMeshOffset();

  if (UGridOverlayComponent *Grid = GetGrid()) {
    CurrentCell = Grid->WorldToGrid(GetActorLocation());
    FVector AlignedLocation = Grid->GridToWorld(CurrentCell);
    AlignedLocation.Z += GetSimpleCollisionHalfHeight();
    SetActorLocation(AlignedLocation);
    Grid->SetOccupied(CurrentCell, true);
  }

  if (UWorld *World = GetWorld()) {
    if (USkaldGameInstance *GI =
            Cast<USkaldGameInstance>(World->GetGameInstance())) {
      if (GI->GridBattleManager) {
        GI->GridBattleManager->RegisterFighter(this, bIsAttacker);
      }
    }
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
}

void AFighterPawn::ResetActivationState() {
  CleanupPendingAttack();

  ActionsRemaining = 0;
  bHasActivatedThisRound = false;
  bIsCurrentlyActive = false;

  BroadcastActionsRemaining();
}

void AFighterPawn::FinishActivation() {
  CleanupPendingAttack();

  ActionsRemaining = 0;
  bIsCurrentlyActive = false;

  BroadcastActionsRemaining();
}

UGridOverlayComponent *AFighterPawn::GetGrid() {
  if (!CachedGrid) {
    if (UWorld *World = GetWorld()) {
      for (TActorIterator<AActor> It(World); It; ++It) {
        CachedGrid = It->FindComponentByClass<UGridOverlayComponent>();
        if (CachedGrid) {
          break;
        }
      }
    }
  }
  return CachedGrid;
}

FIntPoint AFighterPawn::GetCurrentCell() const { return CurrentCell; }

UUserWidget *AFighterPawn::GetDamageWidgetFromPool() {
  for (UUserWidget *Widget : DamageWidgetPool) {
    if (Widget && !Widget->IsInViewport()) {
      return Widget;
    }
  }
  if (DamageFloatWidgetTemplate) {
    if (UWorld *World = GetWorld()) {
      if (UUserWidget *NewWidget =
              CreateWidget<UUserWidget>(World, DamageFloatWidgetTemplate)) {
        DamageWidgetPool.Add(NewWidget);
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
  if (Grid && Grid->IsObscured(TargetCell)) {
    return;
  }

  if (Grid) {
    Grid->SetOccupied(CurrentCell, false);
  }

  CurrentCell = TargetCell;
  FVector NewLocation = GetActorLocation();
  if (Grid) {
    NewLocation = Grid->GridToWorld(TargetCell);
    NewLocation.Z += GetSimpleCollisionHalfHeight();
  }
  SetActorLocation(NewLocation);
  ActionsRemaining = FMath::Max(0, ActionsRemaining - 1);

  BroadcastActionsRemaining();

  if (Grid) {
    Grid->SetOccupied(TargetCell, true);
    Grid->ClearHighlights();
  }
}

void AFighterPawn::PerformAttack(AFighterPawn *Target) {
  if (bAttackSequenceInProgress || !bIsCurrentlyActive || ActionsRemaining <= 0 ||
      !Target || !Target->IsAlive()) {
    return;
  }

  const int32 Distance = FMath::Abs(Target->CurrentCell.X - CurrentCell.X) +
                         FMath::Abs(Target->CurrentCell.Y - CurrentCell.Y);
  if (Distance > Stats.AttackRange) {
    return;
  }

  UGridOverlayComponent *Grid = GetGrid();
  if (Grid && !Grid->HasLineOfSight(CurrentCell, Target->CurrentCell)) {
    return;
  }

  const int32 RequiredRoll =
      Stats.Strength > Target->Stats.Defence
          ? 3
          : (Stats.Strength < Target->Stats.Defence ? 5 : 4);

  PendingAttackTarget = Target;
  PendingAttackRequiredRoll = RequiredRoll;
  PendingAttackDiceRemaining = FMath::Max(Stats.AttackDice, 0);
  bAttackSequenceInProgress = PendingAttackDiceRemaining > 0;

  if (!bAttackSequenceInProgress) {
    FinalizePendingAttack(Target);
    return;
  }

  ResolvePendingAttackRoll();
}

void AFighterPawn::ResolvePendingAttackRoll() {
  UWorld *World = GetWorld();
  if (!World) {
    CleanupPendingAttack();
    return;
  }

  AFighterPawn *Target = PendingAttackTarget.Get();
  if (!Target || !Target->IsAlive()) {
    FinalizePendingAttack(Target);
    return;
  }

  USkaldGameInstance *GameInstance =
      Cast<USkaldGameInstance>(World->GetGameInstance());
  if (!GameInstance) {
    CleanupPendingAttack();
    return;
  }

  const int32 Roll = GameInstance->CombatRandomStream.RandRange(1, 6);
  int32 DamageThisDie = 0;

  if (Roll == 6) {
    DamageThisDie = Stats.AttackDamage + 3; // crit
  } else if (Roll >= PendingAttackRequiredRoll) {
    DamageThisDie = Stats.AttackDamage;
  }

  const bool bHit = DamageThisDie > 0;
  if (UGridBattleManager *BattleManager = GameInstance->GridBattleManager) {
    BattleManager->ReportAttackRoll(this, Target, Roll, bHit, DamageThisDie);
  }

  if (DamageThisDie > 0) {
    Target->Stats.Health =
        FMath::Max(0, Target->Stats.Health - DamageThisDie);

    if (UUserWidget *DamageWidget = GetDamageWidgetFromPool()) {
      if (UTextBlock *Text = Cast<UTextBlock>(
              DamageWidget->GetWidgetFromName(TEXT("DamageText")))) {
        Text->SetText(FText::AsNumber(DamageThisDie));
      }
      DamageWidget->AddToViewport();
      FTimerHandle LocalTimer;
      World->GetTimerManager().SetTimer(
          LocalTimer, FTimerDelegate::CreateLambda([DamageWidget]() {
            DamageWidget->RemoveFromParent();
          }),
          1.f, false);
    }
  }

  --PendingAttackDiceRemaining;

  if (PendingAttackDiceRemaining > 0 && Target->IsAlive()) {
    World->GetTimerManager().SetTimer(AttackRollTimerHandle, this,
                                      &AFighterPawn::ResolvePendingAttackRoll,
                                      1.f, false);
  } else {
    FinalizePendingAttack(Target);
  }
}

void AFighterPawn::FinalizePendingAttack(AFighterPawn *Target) {
  if (Target) {
    Target->OnHealthChanged.Broadcast(Target->Stats.Health);
    if (!Target->IsAlive()) {
      Target->Destroy();
    }
  }

  ActionsRemaining = FMath::Max(0, ActionsRemaining - 1);

  BroadcastActionsRemaining();

  if (UGridOverlayComponent *Grid = GetGrid()) {
    Grid->ClearHighlights();
  }

  CleanupPendingAttack();
}

void AFighterPawn::CleanupPendingAttack() {
  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(AttackRollTimerHandle);
  }

  PendingAttackTarget = nullptr;
  PendingAttackDiceRemaining = 0;
  PendingAttackRequiredRoll = 0;
  bAttackSequenceInProgress = false;
}

void AFighterPawn::UpdateMeshOffset() {
  if (DisplayMesh && CollisionComponent) {
    const float HalfHeight = CollisionComponent->GetUnscaledCapsuleHalfHeight();
    DisplayMesh->SetRelativeLocation(FVector(0.f, 0.f, -HalfHeight));
  }
}

bool AFighterPawn::IsAlive() const { return Stats.Health > 0; }

void AFighterPawn::UpdateHealthDisplay(int32 NewHealth) {
  if (!HealthWidget) {
    return;
  }
  if (UUserWidget *Widget = HealthWidget->GetUserWidgetObject()) {
    if (UTextBlock *Text =
            Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("HealthText")))) {
      Text->SetText(FText::AsNumber(NewHealth));
    }
  }
}

void AFighterPawn::Destroyed() {
  FinishActivation();
  if (UGridOverlayComponent *Grid = GetGrid()) {
    Grid->SetOccupied(CurrentCell, false);
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

void AFighterPawn::OnRep_ActionsRemaining() { BroadcastActionsRemaining(); }

void AFighterPawn::BroadcastActionsRemaining() {
  OnActionsChanged.Broadcast(ActionsRemaining);
}
