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
  ActionsRemaining = 0;
  bHasActivatedThisRound = false;
  bIsCurrentlyActive = false;

  BroadcastActionsRemaining();
}

void AFighterPawn::FinishActivation() {
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
  if (!bIsCurrentlyActive || ActionsRemaining <= 0 || !Target ||
      !Target->IsAlive()) {
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

  for (int32 i = 0; i < Stats.AttackDice && Target->IsAlive(); ++i) {
    const int32 Roll = RandomStream->RandRange(1, 6);
    int32 DamageThisDie = 0;

    if (Roll == 6) {
      DamageThisDie = Stats.AttackDamage + 3; // crit
    } else if (Roll >= RequiredRoll) {
      DamageThisDie = Stats.AttackDamage;
    }

    const bool bHit = DamageThisDie > 0;
    if (USkaldGameInstance *GI = Cast<USkaldGameInstance>(GetGameInstance())) {
      if (UGridBattleManager *BattleManager = GI->GridBattleManager) {
        BattleManager->ReportAttackRoll(this, Target, Roll, bHit, DamageThisDie);
      }
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
        if (UWorld *World = GetWorld()) {
          FTimerHandle Timer;
          World->GetTimerManager().SetTimer(
              Timer, FTimerDelegate::CreateLambda([DamageWidget]() {
                DamageWidget->RemoveFromParent();
              }),
              1.f, false);
        }
      }
    }
  }

  Target->OnHealthChanged.Broadcast(Target->Stats.Health);
  if (!Target->IsAlive()) {
    Target->Destroy();
  }
  ActionsRemaining = FMath::Max(0, ActionsRemaining - 1);

  BroadcastActionsRemaining();

  if (Grid) {
    Grid->ClearHighlights();
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
