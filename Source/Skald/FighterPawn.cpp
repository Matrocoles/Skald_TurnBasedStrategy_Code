#include "FighterPawn.h"
#include "Blueprint/UserWidget.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextBlock.h"
#include "EngineUtils.h"
#include "GridOverlayComponent.h"
#include "Skald_GameInstance.h"
#include "TimerManager.h"

AFighterPawn::AFighterPawn() {
  PrimaryActorTick.bCanEverTick = false;

  DisplayMesh =
      CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DisplayMesh"));
  RootComponent = DisplayMesh;

  HealthWidget = CreateDefaultSubobject<UWidgetComponent>(TEXT("HealthWidget"));
  HealthWidget->SetupAttachment(DisplayMesh);

  ActionsRemaining = 0;
  CurrentCell = FIntPoint::ZeroValue;
}

void AFighterPawn::BeginPlay() {
  Super::BeginPlay();

  if (HealthWidget && HealthWidgetTemplate) {
    HealthWidget->SetWidgetClass(HealthWidgetTemplate);
  }

  OnHealthChanged.AddDynamic(this, &AFighterPawn::UpdateHealthDisplay);
  OnHealthChanged.Broadcast(Stats.Health);

  if (UGridOverlayComponent *Grid = GetGrid()) {
    CurrentCell = Grid->WorldToGrid(GetActorLocation());
    Grid->SetOccupied(CurrentCell, true);
  }
}

void AFighterPawn::BeginActivation() { ActionsRemaining = Stats.Movement; }

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
  int32 Distance = FMath::Abs(TargetCell.X - CurrentCell.X) +
                   FMath::Abs(TargetCell.Y - CurrentCell.Y);
  if (Distance > ActionsRemaining) {
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
    NewLocation.Z = Grid->GetCellHeight(TargetCell);
  }
  SetActorLocation(NewLocation);
  ActionsRemaining -= Distance;

  if (Grid) {
    Grid->SetOccupied(TargetCell, true);
    Grid->ClearHighlights();
  }
}

void AFighterPawn::PerformAttack(AFighterPawn *Target) {
  if (!Target || ActionsRemaining <= 0 || !Target->IsAlive()) {
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
    int32 Roll = RandomStream->RandRange(1, 6);
    if (Roll >= RequiredRoll) {
      Target->Stats.Health =
          FMath::Max(0, Target->Stats.Health - Stats.AttackDamage);

      if (UUserWidget *DamageWidget = GetDamageWidgetFromPool()) {
        if (UTextBlock *Text = Cast<UTextBlock>(
                DamageWidget->GetWidgetFromName(TEXT("DamageText")))) {
          Text->SetText(FText::AsNumber(Stats.AttackDamage));
        }
        DamageWidget->AddToViewport();
        if (UWorld *World = GetWorld()) {
          FTimerHandle Timer;
          World->GetTimerManager().SetTimer(
              Timer,
              FTimerDelegate::CreateLambda([DamageWidget]() {
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
  --ActionsRemaining;

  if (Grid) {
    Grid->ClearHighlights();
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
  if (UGridOverlayComponent *Grid = GetGrid()) {
    Grid->SetOccupied(CurrentCell, false);
  }
  CurrentCell = FIntPoint::ZeroValue;
  Super::Destroyed();
}
