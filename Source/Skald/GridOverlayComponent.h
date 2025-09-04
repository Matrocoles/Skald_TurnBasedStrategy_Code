#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "GridOverlayComponent.generated.h"

class AFighterPawn;
class UGridObstacleComponent;
struct FHitResult;

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

  /** Query whether a grid cell is obscured. */
  UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Grid")
  bool IsObscured(const FIntPoint &GridCoord) const;

  /** Check if there is line of sight between two grid cells. */
  UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Grid")
  bool HasLineOfSight(const FIntPoint &Start, const FIntPoint &End) const;

  /** Get the cached height for a grid cell. */
  UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Grid")
  float GetCellHeight(const FIntPoint &GridCoord) const;

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

    /** Register an obstacle component so it can affect grid behaviour. */
    UFUNCTION(BlueprintCallable, Category = "Grid")
    void RegisterObstacle(UGridObstacleComponent *Obstacle);

  /** Whether landscape should be treated as an obstacle based on slope. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid|Landscape")
  bool bTreatLandscapeAsObstacle = true;

  /** Minimum slope angle for landscape to block movement or sight. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid|Landscape",
            meta = (EditCondition = "bTreatLandscapeAsObstacle"))
  float LandscapeSlopeThreshold = 45.f;

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

  /** Obscured array; true if the cell is blocked by an obstacle. */
  UPROPERTY()
  TArray<bool> ObscuredCells;

  /** Cached world-space Z for each grid cell. */
  UPROPERTY()
  TArray<float> CellHeights;

  /** Get linear array index for a grid coordinate. */
  int32 Index(const FIntPoint &GridCoord) const;

  /** Check whether grid coordinate is inside bounds. */
    bool IsValidGrid(const FIntPoint &GridCoord) const;

    /** Obstacles currently registered with this grid. */
    UPROPERTY()
    TArray<UGridObstacleComponent *> Obstacles;

    /** Process a landscape hit to potentially flag a cell as blocked. */
    void HandleLandscapeHit(const FHitResult &Hit, const FIntPoint &Cell,
                            int32 CellIndex);
};
