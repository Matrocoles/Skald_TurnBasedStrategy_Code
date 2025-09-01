#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "GridOverlayComponent.generated.h"

class AFighterPawn;

/**
 * Component that tracks grid cell occupancy and provides world/grid conversion.
 * Designed to be attached to the battle map floor actor.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class SKALD_API UGridOverlayComponent : public UActorComponent {
  GENERATED_BODY()

public:
  UGridOverlayComponent();

  virtual void BeginPlay() override;

  /** Convert a world location to grid coordinates. */
  UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Grid")
  FIntPoint WorldToGrid(const FVector &WorldLocation) const;

  /** Convert grid coordinates to the centre world location of the cell. */
  UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Grid")
  FVector GridToWorld(const FIntPoint &GridCoord) const;

  /** Query whether a grid cell is occupied. */
  UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Grid")
  bool IsOccupied(const FIntPoint &GridCoord) const;

  /** Mark a grid cell as occupied or free. */
  UFUNCTION(BlueprintCallable, Category = "Grid")
  void SetOccupied(const FIntPoint &GridCoord, bool bOccupied);

  /** Draw a debug highlight box for a grid cell. */
  UFUNCTION(BlueprintCallable, Category = "Grid")
  void HighlightCell(const FIntPoint &GridCoord, const FColor &Color,
                     float Duration = 0.f, bool bPersistent = false) const;

  /** Highlight all reachable movement cells for the fighter. */
  UFUNCTION(BlueprintCallable, Category = "Grid")
  void HighlightMovement(AFighterPawn *Fighter);

  /** Highlight attack range for the fighter. */
  UFUNCTION(BlueprintCallable, Category = "Grid")
  void HighlightAttack(AFighterPawn *Fighter);

  /** Remove any persistent highlights. */
  UFUNCTION(BlueprintCallable, Category = "Grid")
  void ClearHighlights() const;

protected:
  /** Width of the grid in cells. */
  int32 Width = 0;

  /** Height of the grid in cells. */
  int32 Height = 0;

  /** Size of one cell in world units. */
  float CellSize = 100.f;

  /** World origin of the grid (cell 0,0). */
  FVector Origin = FVector::ZeroVector;

  /** Occupancy array; true if the cell is occupied. */
  UPROPERTY()
  TArray<bool> Cells;

  /** Get linear array index for a grid coordinate. */
  int32 Index(const FIntPoint &GridCoord) const;

  /** Check whether grid coordinate is inside bounds. */
  bool IsValidGrid(const FIntPoint &GridCoord) const;
};
