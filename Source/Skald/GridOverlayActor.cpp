#include "GridOverlayActor.h"
#include "Components/SceneComponent.h"
#include "GridOverlayComponent.h"

AGridOverlayActor::AGridOverlayActor() {
  PrimaryActorTick.bCanEverTick = false;

  SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
  SetRootComponent(SceneRoot);

  GridComponent = CreateDefaultSubobject<UGridOverlayComponent>(TEXT("GridOverlay"));
}

void AGridOverlayActor::BeginPlay() {
  if (GridComponent) {
    GridComponent->ApplyRandomizedOrigin();
  }

  Super::BeginPlay();
}

