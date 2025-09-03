#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GridObstacleActor.generated.h"

class UStaticMeshComponent;
class UGridObstacleComponent;

/**
 * Actor wrapper for a static mesh that participates in grid obstacle logic.
 * Place in a level for walls or props that block movement or line of sight.
 */
UCLASS()
class SKALD_API AGridObstacleActor : public AActor
{
    GENERATED_BODY()

public:
    AGridObstacleActor();

    /** Mesh representing the obstacle. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Obstacle")
    UStaticMeshComponent* MeshComponent;

    /** Component controlling movement and vision blocking behaviour. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Obstacle")
    UGridObstacleComponent* GridObstacle;
};

