#include "GridObstacleActor.h"
#include "Components/StaticMeshComponent.h"
#include "GridObstacleComponent.h"

AGridObstacleActor::AGridObstacleActor()
{
    PrimaryActorTick.bCanEverTick = false;

    MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
    RootComponent = MeshComponent;

    GridObstacle = CreateDefaultSubobject<UGridObstacleComponent>(TEXT("GridObstacle"));
}

