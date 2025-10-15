#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "GridBattleManager.h"
#include "GridOverlayComponent.generated.h"

class AFighterPawn;
class UGridObstacleComponent;
struct FHitResult;
class UInstancedStaticMeshComponent;
class UDecalComponent;
class UMaterialInterface;
class UStaticMesh;
class UMaterialInstanceDynamic;
struct FTimerHandle;
class UWorld;

USTRUCT()
struct FPendingGridOccupancyUpdate {
  GENERATED_BODY()

  FPendingGridOccupancyUpdate() = default;

  FPendingGridOccupancyUpdate(const FIntPoint &InGridCoord, bool bInOccupied)
      : GridCoord(InGridCoord), bOccupied(bInOccupied) {}

  UPROPERTY()
  FIntPoint GridCoord = FIntPoint::ZeroValue;

  UPROPERTY()
  bool bOccupied = false;
};

USTRUCT(BlueprintType)
struct FGridPlacementBounds {
  GENERATED_BODY()

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid|Placement")
  FVector2D Min = FVector2D::ZeroVector;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid|Placement")
  FVector2D Max = FVector2D::ZeroVector;

  bool HasArea() const {
    return (FMath::Abs(Max.X - Min.X) > KINDA_SMALL_NUMBER) ||
           (FMath::Abs(Max.Y - Min.Y) > KINDA_SMALL_NUMBER);
  }
};

/**
 * Component that tracks grid cell occupancy and provides world/grid conversion.
 * Designed to be attached to the battle map floor actor.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class SKALD_API UGridOverlayComponent : public UActorComponent {
  GENERATED_BODY()

public:
  UGridOverlayComponent();

  virtual void OnRegister() override;
  virtual void BeginPlay() override;

  UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Grid")
  int32 GetWidth() const;

  UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Grid")
  int32 GetHeight() const;

  UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Grid")
  float GetCellSize() const;

  UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Grid")
  FVector GetOrigin() const;

  UFUNCTION(BlueprintCallable, Category = "Grid")
  void ApplyRandomizedOrigin();

  /** Update the cached origin from the owning actor's transform. */
  UFUNCTION(BlueprintCallable, Category = "Grid")
  void RefreshOriginFromOwner(bool bMarkPlacementRandomized = false);

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

  /** Check whether a grid coordinate is within the valid bounds of the grid. */
  UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Grid")
  bool IsCellInBounds(const FIntPoint &GridCoord) const;

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
                     float Duration = 0.f, bool bPersistent = false);

  /** Highlight all reachable movement cells for the fighter. */
  UFUNCTION(BlueprintCallable, Category = "Grid")
  void HighlightMovement(AFighterPawn *Fighter);

  /** Highlight attack range for the fighter. */
  UFUNCTION(BlueprintCallable, Category = "Grid")
  void HighlightAttack(AFighterPawn *Fighter);

  /** Highlight only the cell currently occupied by the fighter. */
  UFUNCTION(BlueprintCallable, Category = "Grid")
  void HighlightSelection(AFighterPawn *Fighter);

  /** Clear any selection highlight that should persist across commands. */
  UFUNCTION(BlueprintCallable, Category = "Grid")
  void ClearSelectionHighlight();

  /** Remove any persistent highlights. */
  UFUNCTION(BlueprintCallable, Category = "Grid")
  void ClearHighlights(bool bMaintainPersistentSelection = true);

  /** Rebuild the persistent grid overlay instances. */
  UFUNCTION(BlueprintCallable, Category = "Grid")
  void RebuildGridVisuals();

  /** Register an obstacle component so it can affect grid behaviour. */
  UFUNCTION(BlueprintCallable, Category = "Grid")
  void RegisterObstacle(UGridObstacleComponent *Obstacle);

  /** Clear any static obstacle state associated with the given cell. */
  void ClearStaticObstacleAtCell(const FIntPoint &GridCoord);

  /** If true, ClearHighlights() will FlushPersistentDebugLines for the entire world. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Grid|Debug")
  bool bFlushAllPersistentOnClear = true;

  /** Whether landscape should be treated as an obstacle based on slope. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid|Landscape")
  bool bTreatLandscapeAsObstacle = true;

  /** Minimum slope angle for landscape to block movement or sight. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid|Landscape",
            meta = (EditCondition = "bTreatLandscapeAsObstacle"))
  float LandscapeSlopeThreshold = 45.f;

  /** Mesh used for highlighting grid cells. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Grid|Highlight")
  UStaticMesh *HighlightMesh = nullptr;

  /** Material applied to the highlight mesh instances. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Grid|Highlight")
  UMaterialInterface *HighlightMaterial = nullptr;

  /** If true, highlights will spawn decals instead of instanced meshes. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid|Highlight|Decal")
  bool bUseDecalHighlights = false;

  /** Material applied to highlight decals (must use the Deferred Decal domain). */
  UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Grid|Highlight|Decal")
  UMaterialInterface *HighlightDecalMaterial = nullptr;

  /** Name of the vector parameter driven by highlight colours on the decal material. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Grid|Highlight|Decal")
  FName HighlightDecalColorParameter = TEXT("TintColor");

  /** Depth of the decal projection along the surface normal. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid|Highlight|Decal",
            meta = (ClampMin = "0.0"))
  float HighlightDecalProjectionDepth = 32.f;

  /** Multiplier applied to the XY footprint of the decal relative to the cell size. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid|Highlight|Decal",
            meta = (ClampMin = "0.01"))
  float HighlightDecalSizeMultiplier = 1.0f;

  /** Screen size threshold at which decals begin to fade out. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid|Highlight|Decal",
            meta = (ClampMin = "0.0"))
  float HighlightDecalFadeScreenSize = 0.01f;

  /** Delay before decals automatically fade out (0 to disable automatic fade). */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid|Highlight|Decal",
            meta = (ClampMin = "0.0"))
  float HighlightDecalLifeSpan = 0.f;

  /** Duration of the fade once it begins (used when LifeSpan is greater than zero). */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid|Highlight|Decal",
            meta = (ClampMin = "0.0"))
  float HighlightDecalFadeDuration = 0.25f;

  /** Vertical offset applied to highlight instances. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid|Highlight")
  float HighlightHeightOffset = 2.f;

  /** Mesh used for the persistent grid overlay. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Grid|Display")
  UStaticMesh *GridMesh = nullptr;

  /** Material applied to the persistent grid overlay. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Grid|Display")
  UMaterialInterface *GridMaterial = nullptr;

  /** Whether the base grid should be rendered at runtime. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid|Display")
  bool bDrawBaseGrid = true;

  /** If true, the persistent grid will spawn decals instead of instanced meshes. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid|Display|Decal")
  bool bUseDecalBaseGrid = false;

  /** Material applied to base grid decals (must use the Deferred Decal domain). */
  UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Grid|Display|Decal")
  UMaterialInterface *BaseGridDecalMaterial = nullptr;

  /** Name of the vector parameter driven by base grid colours on the decal material. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Grid|Display|Decal")
  FName BaseGridDecalColorParameter = TEXT("TintColor");

  /** Depth of the decal projection along the surface normal for the base grid. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid|Display|Decal",
            meta = (ClampMin = "0.0"))
  float BaseGridDecalProjectionDepth = 32.f;

  /** Multiplier applied to the XY footprint of base grid decals relative to the cell size. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid|Display|Decal",
            meta = (ClampMin = "0.01"))
  float BaseGridDecalSizeMultiplier = 1.0f;

  /** Screen size threshold at which base grid decals begin to fade out. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid|Display|Decal",
            meta = (ClampMin = "0.0"))
  float BaseGridDecalFadeScreenSize = 0.01f;

  /** If true, sampled cell rotations will be ignored when rendering the grid. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid|Display",
            meta = (DisplayName = "NoCellRotation"))
  bool bNoCellRotation = false;

  /** Vertical offset applied to the persistent grid overlay. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid|Display")
  float GridHeightOffset = 0.f;

  /** Default tint applied to traversable cells. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid|Display")
  FLinearColor DefaultCellColor = FLinearColor(0.1f, 0.1f, 0.1f, 0.35f);

  /** Tint applied to cells blocked by obstacles. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid|Display")
  FLinearColor BlockedCellColor = FLinearColor(0.6f, 0.0f, 0.0f, 0.55f);

  /** Tint applied to cells that block line of sight. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid|Display")
  FLinearColor ObscuredCellColor = FLinearColor(0.3f, 0.3f, 0.3f, 0.5f);

  /** Tint applied to cells currently occupied by a fighter. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid|Display")
  FLinearColor OccupiedCellColor = FLinearColor(0.1f, 0.3f, 0.8f, 0.65f);

  /** Tint applied when a fighter is selected. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid|Highlight")
  FLinearColor SelectionHighlightColor = FLinearColor(1.f, 1.f, 0.25f, 0.85f);

  /** Tint applied to reachable movement cells. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid|Highlight")
  FLinearColor MovementHighlightColor = FLinearColor(0.0f, 1.f, 0.3f, 0.85f);

  /** Tint applied to reachable attack cells. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid|Highlight")
  FLinearColor AttackHighlightColor = FLinearColor(1.f, 0.1f, 0.1f, 0.85f);

protected:
  /** Randomise the placement of the grid around the actor's starting point. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid|Placement")
  bool bRandomizePlacement = false;

  /** Bounds in local XY around the actor used for placement randomisation. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid|Placement",
            meta = (EditCondition = "bRandomizePlacement"))
  FGridPlacementBounds RandomPlacementBounds;

  /** Fallback placement radius when bounds are not provided. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid|Placement",
            meta = (EditCondition = "bRandomizePlacement", ClampMin = "0.0"))
  float RandomPlacementRadius = 0.f;

  /** Height of the placement trace used to find the ground. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid|Placement",
            meta = (EditCondition = "bRandomizePlacement", ClampMin = "0.0"))
  float PlacementTraceHeight = 0.f;

  /** Width of the grid in cells. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
  int32 Width = UGridBattleManager::GridSize;

  /** Height of the grid in cells. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
  int32 Height = UGridBattleManager::GridSize;

  /** Size of one cell in world units. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
  float CellSize = 100.f;

  /** World origin of the grid (cell 0,0). */
  FVector Origin = FVector::ZeroVector;

  /** Cached transform of the grid in world space (matches owning actor). */
  FTransform CachedGridTransform = FTransform::Identity;

  /** Cached inverse transform used for world/local grid conversions. */
  FTransform CachedGridInverseTransform = FTransform::Identity;

  /** Occupancy array; true if the cell is occupied. */
  UPROPERTY()
  TArray<bool> Cells;

  /** Obscured array; true if the cell is blocked by an obstacle. */
  UPROPERTY()
  TArray<bool> ObscuredCells;

  /** Cached world-space Z for each grid cell. */
  UPROPERTY()
  TArray<float> CellHeights;

  /** Cached world-space rotation for each grid cell. */
  UPROPERTY()
  TArray<FQuat> CellRotations;

  /** Guard to ensure placement randomisation is only applied once. */
  bool bHasRandomizedPlacement = false;

  /** Instanced mesh component used to render highlight quads. */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid|Highlight")
  UInstancedStaticMeshComponent *HighlightMeshComponent = nullptr;

  /** Instanced mesh component used to render the persistent grid. */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid|Display")
  UInstancedStaticMeshComponent *BaseGridMeshComponent = nullptr;

  /** Spawned decal components used when the base grid renders as decals. */
  UPROPERTY(Transient)
  TArray<TWeakObjectPtr<UDecalComponent>> BaseGridDecalComponents;

  /** Dynamic materials created for base grid decals to support colour changes. */
  UPROPERTY(Transient)
  TArray<TWeakObjectPtr<UMaterialInstanceDynamic>> BaseGridDecalMaterials;

  /** Map grid coordinates to highlight instance indices for updates. */
  UPROPERTY(Transient)
  TMap<FIntPoint, int32> HighlightedInstances;

  /** Map grid coordinates to spawned highlight decals when decal mode is active. */
  UPROPERTY(Transient)
  TMap<FIntPoint, TWeakObjectPtr<UDecalComponent>> HighlightedDecals;

  /** Dynamic materials created for active decal highlights. */
  UPROPERTY(Transient)
  TMap<FIntPoint, TWeakObjectPtr<UMaterialInstanceDynamic>> HighlightedDecalMaterials;

  /** Fighter whose current cell should remain highlighted until cleared. */
  UPROPERTY(Transient)
  TWeakObjectPtr<AFighterPawn> PersistentlyHighlightedFighter;

  /** Timers that remove decal components once their fade completes. */
  TMap<FIntPoint, FTimerHandle> HighlightedDecalRemovalTimers;

  /** Mapping of grid cell index to persistent grid instance index. */
  UPROPERTY(Transient)
  TArray<int32> BaseGridInstanceIndices;

  /** Tracks cells currently occupied by dynamic actors. */
  UPROPERTY(Transient)
  TArray<bool> DynamicOccupiedCells;

  /** Obstacles awaiting registration until the grid has initialised. */
  UPROPERTY(Transient)
  TArray<TWeakObjectPtr<UGridObstacleComponent>> PendingObstacles;

  /** Occupancy updates received before the grid initialises. */
  UPROPERTY(Transient)
  TArray<FPendingGridOccupancyUpdate> PendingOccupancyUpdates;

  /** Whether the grid has completed its initial world sampling. */
  bool bHasInitializedGrid = false;

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

  /** Resample height/occupancy information around the current origin. */
  void RefreshGridDataFromOrigin();

  /** Trace the world to populate per-cell height and slope information. */
  void SampleEnvironmentAtOrigin();

  /** Apply a registered obstacle's footprint to the cached grid data. */
  void ApplyObstacleToGrid(class UGridObstacleComponent *Obstacle);

  /** Ensure the instanced highlight component is ready for use. */
  bool EnsureHighlightComponentSetup();

  /** Apply highlight color as per-instance custom data. */
  void ApplyHighlightColor(int32 InstanceIndex, const FLinearColor &Color);

  /** Reapply the persistent selection highlight if one is set. */
  void RefreshPersistentSelectionHighlight();

  /** Highlight a cell using a spawned decal component. */
  void HighlightCellWithDecal(const FIntPoint &GridCoord, const FColor &Color);

  /** Resolve the material that should be used for base grid decals. */
  UMaterialInterface *GetBaseGridDecalMaterial() const;

  /** Destroy any existing base grid decals and clear cached references. */
  void ClearBaseGridDecals();

  /** Apply colour data to a base grid decal material instance. */
  void ApplyBaseGridDecalColor(int32 CellIndex, const FLinearColor &Color);

  /** Resolve the material that should be used for decal highlights. */
  UMaterialInterface *GetHighlightDecalMaterial() const;

  /** Schedule destruction of a decal component after it finishes fading. */
  void ScheduleDecalRemoval(const FIntPoint &GridCoord, UDecalComponent *Decal);

  /** Cancel any pending decal removal timer for the provided grid cell. */
  void ClearDecalRemovalTimer(const FIntPoint &GridCoord);

  /** Handle the completion of a decal fade-out to destroy the component. */
  void OnHighlightDecalFadeFinished(FIntPoint GridCoord,
                                    TWeakObjectPtr<UDecalComponent> DecalWeak);

  /** Ensure the instanced highlight component exists and belongs to the owner. */
  bool EnsureHighlightMeshComponentExists();

  /** Ensure the persistent grid component exists and belongs to the owner. */
  bool EnsureBaseGridMeshComponentExists();

  /** Shared setup for dynamically created instanced mesh components. */
  bool EnsureInstancedMeshComponent(UInstancedStaticMeshComponent *&Component,
                                    FName ComponentName);

  /** Apply common configuration to an instanced mesh component. */
  void ConfigureInstancedComponent(UInstancedStaticMeshComponent *Component,
                                   UStaticMesh *Mesh,
                                   UMaterialInterface *Material);

  /** Ensure the persistent grid component is configured for rendering. */
  bool EnsureBaseGridComponentSetup();

  /** Build or rebuild the persistent grid instances. */
  void RebuildBaseGridInstances();

  /** Apply colour data to the persistent grid instance for a cell. */
  void UpdateBaseGridVisual(const FIntPoint &GridCoord);

  /** Apply colour data to an instance of the persistent grid overlay. */
  void ApplyBaseGridColor(int32 InstanceIndex, const FLinearColor &Color);

  /** Resolve which colour should be used for a given grid cell. */
  FLinearColor GetBaseGridColor(int32 CellIndex) const;

  /** Resolve the effective rotation for a grid cell, respecting editor settings. */
  FQuat GetEffectiveCellRotation(int32 ArrayIndex) const;
};

namespace Skald
{
namespace GridOverlay
{
SKALD_API bool IsComponentFromVisibleLevel(const UGridOverlayComponent* GridComponent);
SKALD_API UGridOverlayComponent* FindActiveGridOverlay(UWorld* World, bool bPreferVisibleLevel = true);
} // namespace GridOverlay
} // namespace Skald
