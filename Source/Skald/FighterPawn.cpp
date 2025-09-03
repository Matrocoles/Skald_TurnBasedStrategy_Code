#include "FighterPawn.h"
#include "Blueprint/UserWidget.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextBlock.h"
#include "EngineUtils.h"
#include "GridOverlayComponent.h"
#include "Skald_GameInstance.h"

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

  UGridOverlayComponent *Grid = nullptr;
  if (UWorld *World = GetWorld()) {
    for (TActorIterator<AActor> It(World); It; ++It) {
      Grid = It->FindComponentByClass<UGridOverlayComponent>();
      if (Grid != nullptr) {
        break;
      }
    }
  }
  if (Grid) {
    FIntPoint StartCell = CurrentCell;
    FIntPoint TargetCell = Target->CurrentCell;
    int32 x0 = StartCell.X;
    int32 y0 = StartCell.Y;
    int32 x1 = TargetCell.X;
    int32 y1 = TargetCell.Y;
    int32 dx = FMath::Abs(x1 - x0);
    int32 sx = x0 < x1 ? 1 : -1;
    int32 dy = -FMath::Abs(y1 - y0);
    int32 sy = y0 < y1 ? 1 : -1;
    int32 err = dx + dy;
    FIntPoint Current(x0, y0);

    while (true) {
      if (Current != StartCell && Grid->IsObscured(Current)) {
        return;
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
  }

  FRandomStream *RandomStream = nullptr;
  if (UWorld *World = GetWorld()) {
    if (USkaldGameInstance *GameInstance =
            Cast<USkaldGameInstance>(World->GetGameInstance())) {
      RandomStream = &GameInstance->CombatRandomStream;
    }
  }
  static FRandomStream FallbackStream;
  if (!RandomStream) {
    FallbackStream.Initialize(FMath::Rand());
    RandomStream = &FallbackStream;
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

      if (DamageFloatWidgetTemplate && Target) {
        if (UWorld *World = GetWorld()) {
          if (UUserWidget *DamageWidget =
                  CreateWidget<UUserWidget>(World, DamageFloatWidgetTemplate)) {
            if (UTextBlock *Text = Cast<UTextBlock>(
                    DamageWidget->GetWidgetFromName(TEXT("DamageText")))) {
              Text->SetText(FText::AsNumber(Stats.AttackDamage));
            }
            DamageWidget->AddToViewport();
          }
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
