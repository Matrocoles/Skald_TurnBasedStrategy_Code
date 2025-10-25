#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GridOverlayActor.generated.h"

class UGridOverlayComponent;
class USceneComponent;
class AGridObstacleActor;

USTRUCT(BlueprintType)
struct FGridObstacleSpawnList {
  GENERATED_BODY()

  /** Minimum number of obstacles to spawn from this list. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid|Obstacles", meta = (ClampMin = "0"))
  int32 MinObstacleCount = 0;

  /** Maximum number of obstacles to spawn from this list. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid|Obstacles", meta = (ClampMin = "0"))
  int32 MaxObstacleCount = 0;

  /** Candidate obstacle classes that can be spawned from this list. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid|Obstacles")
  TArray<TSubclassOf<AGridObstacleActor>> ObstacleCandidates;

  bool HasCandidates() const { return ObstacleCandidates.Num() > 0; }
};

/**
 * Actor wrapper that exposes the grid overlay component directly in the level.
 */
UCLASS()
class SKALD_API AGridOverlayActor : public AActor {
  GENERATED_BODY()

public:
  AGridOverlayActor();

  /** Spawn random obstacles across the grid using the configured settings. */
  UFUNCTION(BlueprintCallable, CallInEditor, Category = "Grid|Obstacles")
  void SpawnRandomObstacles();

  /** Destroy and clear any spawned obstacle actors. */
  UFUNCTION(BlueprintCallable, CallInEditor, Category = "Grid|Obstacles")
  void ClearSpawnedObstacles();

protected:
  virtual void OnConstruction(const FTransform &Transform) override;
  virtual void BeginPlay() override;

  virtual void OnRep_ReplicatedMovement() override;

  /** Root component used to anchor the grid overlay. */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid")
  USceneComponent *SceneRoot = nullptr;

  /** Grid overlay component responsible for sampling and rendering the grid. */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid")
  UGridOverlayComponent *GridComponent = nullptr;

  /** Whether random obstacles should be spawned across the grid. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid|Obstacles")
  bool bSpawnRandomObstacles = false;

  /** Prevent random obstacles from appearing inside fighter spawn lanes. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid|Obstacles",
            meta = (EditCondition = "bSpawnRandomObstacles"))
  bool bRespectFighterSpawnLanes = true;

  /** Number of edge columns reserved for fighter spawns on each side of the grid. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid|Obstacles",
            meta = (EditCondition = "bSpawnRandomObstacles && bRespectFighterSpawnLanes",
                    ClampMin = "0"))
  int32 ReservedSpawnColumnWidth = 3;

  /** Large obstacle definitions. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid|Obstacles", meta = (EditCondition = "bSpawnRandomObstacles"), DisplayName = "Large Obstacles")
  FGridObstacleSpawnList LargeObstacles;

  /** Medium obstacle definitions. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid|Obstacles", meta = (EditCondition = "bSpawnRandomObstacles"), DisplayName = "Medium Obstacles")
  FGridObstacleSpawnList MediumObstacles;

  /** Small obstacle definitions. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid|Obstacles", meta = (EditCondition = "bSpawnRandomObstacles"), DisplayName = "Small Obstacles")
  FGridObstacleSpawnList SmallObstacles;

  /** Additional obstacle definitions for miscellaneous props. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid|Obstacles", meta = (EditCondition = "bSpawnRandomObstacles"), DisplayName = "Misc Obstacles")
  FGridObstacleSpawnList MiscObstacles;

  /** Tree themed environment props. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid|Obstacles", meta = (EditCondition = "bSpawnRandomObstacles"), DisplayName = "Environment Trees")
  FGridObstacleSpawnList EnvironmentTrees;

  /** Cliff themed environment props. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid|Obstacles", meta = (EditCondition = "bSpawnRandomObstacles"), DisplayName = "Environment Cliffs")
  FGridObstacleSpawnList EnvironmentCliffs;

  /** Optional deterministic seed override for obstacle placement. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid|Obstacles", meta = (EditCondition = "bSpawnRandomObstacles"))
  bool bOverrideObstacleSpawnSeed = false;

  /** Seed used when deterministic obstacle placement is desired. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid|Obstacles", meta = (EditCondition = "bSpawnRandomObstacles && bOverrideObstacleSpawnSeed"))
  int32 ObstacleSpawnSeed = 1337;

  /** Height used when tracing down to find a ground placement for an obstacle. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid|Obstacles", meta = (EditCondition = "bSpawnRandomObstacles", ClampMin = "0.0"))
  float ObstacleTraceHeight = 2000.f;

  /** Minimum vertical offset applied to spawned obstacles. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid|Obstacles", meta = (EditCondition = "bSpawnRandomObstacles"))
  float MinObstacleHeightOffset = 0.f;

  /** Maximum vertical offset applied to spawned obstacles. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid|Obstacles", meta = (EditCondition = "bSpawnRandomObstacles"))
  float MaxObstacleHeightOffset = 0.f;

private:
  /** Track obstacle actors spawned by this overlay. */
  UPROPERTY(Transient, meta = (AllowPrivateAccess = "true"))
  TArray<TObjectPtr<AGridObstacleActor>> SpawnedObstacleActors;

  /** Grid cells that currently contain spawned obstacles. */
  TSet<FIntPoint> SpawnedObstacleCells;

  /** True once random obstacles have been spawned at runtime. */
  bool bObstaclesSpawned = false;
};

