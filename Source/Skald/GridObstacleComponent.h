#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "GridObstacleComponent.generated.h"

class UGridOverlayComponent;

/**
 * Component that marks an actor as a grid obstacle.
 * Can be added to static mesh actors to control movement and line-of-sight blocking behaviour.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class SKALD_API UGridObstacleComponent : public UActorComponent {
  GENERATED_BODY()

public:
  UGridObstacleComponent();

  virtual void BeginPlay() override;

  /** Whether this obstacle blocks unit movement. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
  bool bBlocksMovement = true;

  /** Whether this obstacle blocks line of sight. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
  bool bBlocksLineOfSight = true;

  /** Whether units can climb over this obstacle. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
  bool bClimbable = false;

  /** Enable manual control over which grid cells are considered blocked by this obstacle. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid", meta = (InlineEditConditionToggle))
  bool bOverrideBlockedCells = false;

  /** Minimum cell offset from the actor location when overriding the blocked area. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid", meta = (EditCondition = "bOverrideBlockedCells"))
  FIntPoint CustomBlockedCellsMin = FIntPoint::ZeroValue;

  /** Maximum cell offset from the actor location when overriding the blocked area. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid", meta = (EditCondition = "bOverrideBlockedCells"))
  FIntPoint CustomBlockedCellsMax = FIntPoint::ZeroValue;

  /** Calculate a custom grid footprint when manual overrides are enabled. */
  bool GetCustomGridFootprint(const UGridOverlayComponent *Grid, FIntPoint &OutMin,
                              FIntPoint &OutMax) const;
};

