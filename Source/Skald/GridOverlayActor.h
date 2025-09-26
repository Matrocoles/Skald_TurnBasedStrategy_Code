#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GridOverlayActor.generated.h"

class UGridOverlayComponent;
class USceneComponent;

/**
 * Actor wrapper that exposes the grid overlay component directly in the level.
 */
UCLASS()
class SKALD_API AGridOverlayActor : public AActor {
  GENERATED_BODY()

public:
  AGridOverlayActor();

protected:
  virtual void BeginPlay() override;

  /** Root component used to anchor the grid overlay. */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid")
  USceneComponent *SceneRoot = nullptr;

  /** Grid overlay component responsible for sampling and rendering the grid. */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid")
  UGridOverlayComponent *GridComponent = nullptr;
};

