#include "FighterPawn.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "GridOverlayComponent.h"

namespace {
/** Size of a grid cell in world units. */
constexpr float CellSize = 100.f;
} // namespace

AFighterPawn::AFighterPawn() {
  PrimaryActorTick.bCanEverTick = false;

  DisplayMesh =
      CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DisplayMesh"));
  RootComponent = DisplayMesh;

  ActionsRemaining = 0;
  CurrentCell = FIntPoint::ZeroValue;
}

void AFighterPawn::BeginActivation() { ActionsRemaining = Stats.Movement; }

void AFighterPawn::MoveToCell(FIntPoint TargetCell) {
  int32 Distance = FMath::Abs(TargetCell.X - CurrentCell.X) +
                   FMath::Abs(TargetCell.Y - CurrentCell.Y);
  if (Distance > ActionsRemaining) {
    return;
  }

  CurrentCell = TargetCell;
  FVector NewLocation(TargetCell.X * CellSize, TargetCell.Y * CellSize,
                      GetActorLocation().Z);
  SetActorLocation(NewLocation);
  ActionsRemaining -= Distance;

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

void AFighterPawn::PerformAttack(AFighterPawn *Target) {
  if (!Target || ActionsRemaining <= 0 || !Target->IsAlive()) {
    return;
  }

  Target->Stats.Health =
      FMath::Max(0, Target->Stats.Health - Stats.AttackDamage);
  Target->OnHealthChanged.Broadcast(Target->Stats.Health);
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
