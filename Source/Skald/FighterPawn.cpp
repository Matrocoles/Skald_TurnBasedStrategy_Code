#include "FighterPawn.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextBlock.h"
#include "EngineUtils.h"
#include "GridOverlayComponent.h"
#include "Skald_GameInstance.h"
#include "Blueprint/UserWidget.h"


AFighterPawn::AFighterPawn() {
  PrimaryActorTick.bCanEverTick = false;

  DisplayMesh =
      CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DisplayMesh"));
  RootComponent = DisplayMesh;

  HealthWidget =
      CreateDefaultSubobject<UWidgetComponent>(TEXT("HealthWidget"));
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
}

void AFighterPawn::BeginActivation() { ActionsRemaining = Stats.Movement; }

void AFighterPawn::MoveToCell(FIntPoint TargetCell) {
  int32 OldHealth = Stats.Health;
  int32 Distance = FMath::Abs(TargetCell.X - CurrentCell.X) +
                   FMath::Abs(TargetCell.Y - CurrentCell.Y);
  if (Distance > ActionsRemaining) {
    return;
  }

  CurrentCell = TargetCell;
  FVector NewLocation = GetActorLocation();
  UGridOverlayComponent *Grid = nullptr;
  if (UWorld *World = GetWorld()) {
    for (TActorIterator<AActor> It(World); It; ++It) {
      if ((Grid = It->FindComponentByClass<UGridOverlayComponent>())) {
        NewLocation = Grid->GridToWorld(TargetCell);
        break;
      }
    }
  }
  SetActorLocation(NewLocation);
  ActionsRemaining -= Distance;

  if (Grid) {
    Grid->ClearHighlights();
  }

  if (Stats.Health != OldHealth) {
    OnHealthChanged.Broadcast(Stats.Health);
  }
}

void AFighterPawn::PerformAttack(AFighterPawn *Target) {
  if (!Target || ActionsRemaining <= 0 || !Target->IsAlive()) {
    return;
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
            if (UTextBlock *Text =
                    Cast<UTextBlock>(DamageWidget->GetWidgetFromName(
                        TEXT("DamageText")))) {
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

  if (UWorld *World = GetWorld()) {
    for (TActorIterator<AActor> It(World); It; ++It) {
      if (UGridOverlayComponent *Grid =
              It->FindComponentByClass<UGridOverlayComponent>()) {
        Grid->ClearHighlights();
        break;
      }
    }
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
