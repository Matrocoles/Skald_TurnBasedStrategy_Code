#include "Territory.h"
#include "Skald_PlayerState.h"
#include "Components/StaticMeshComponent.h"

ATerritory::ATerritory()
{
    PrimaryActorTick.bCanEverTick = false;
    MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
    RootComponent = MeshComponent;

    Owner = nullptr;
    Resources = 0;
    TerritoryID = 0;
}

void ATerritory::BeginPlay()
{
    Super::BeginPlay();
}

void ATerritory::Select()
{
    OnTerritorySelected.Broadcast(this);
}

bool ATerritory::IsAdjacentTo(const ATerritory* Other) const
{
    for (const ATerritory* Adjacent : AdjacentTerritories)
    {
        if (Adjacent == Other)
        {
            return true;
        }
    }
    return false;
}

bool ATerritory::MoveTo(ATerritory* TargetTerritory)
{
    if (!TargetTerritory || !IsAdjacentTo(TargetTerritory))
    {
        return false;
    }

    // Movement logic would be handled here. For now we simply select the target.
    TargetTerritory->Select();
    return true;
}

