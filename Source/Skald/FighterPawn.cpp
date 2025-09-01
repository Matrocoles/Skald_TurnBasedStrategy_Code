#include "FighterPawn.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "GridOverlayComponent.h"
#include "Skald_GameInstance.h"

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
