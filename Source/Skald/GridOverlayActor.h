#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GridOverlayActor.generated.h"

class UGridOverlayComponent;
class USceneComponent;
class AGridObstacleActor;

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

  /** Minimum number of random obstacles to spawn. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid|Obstacles", meta = (EditCondition = "bSpawnRandomObstacles", ClampMin = "0"))
  int32 MinObstacleCount = 0;

  /** Maximum number of random obstacles to spawn. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid|Obstacles", meta = (EditCondition = "bSpawnRandomObstacles", ClampMin = "0"))
  int32 MaxObstacleCount = 0;

  /** Candidate obstacle classes that can be spawned. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid|Obstacles", meta = (EditCondition = "bSpawnRandomObstacles"))
  TArray<TSubclassOf<AGridObstacleActor>> ObstacleCandidates;

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
  UPROPERTY(Transient, Category = "Grid|Obstacles", meta = (AllowPrivateAccess = "true"))
  TArray<TObjectPtr<AGridObstacleActor>> SpawnedObstacleActors;

  /** Grid cells that currently contain spawned obstacles. */
  TSet<FIntPoint> SpawnedObstacleCells;

  /** True once random obstacles have been spawned at runtime. */
  bool bObstaclesSpawned = false;
};

