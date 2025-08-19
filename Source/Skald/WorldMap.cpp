#include "WorldMap.h"
#include "Territory.h"

AWorldMap::AWorldMap()
{
    PrimaryActorTick.bCanEverTick = false;
    SelectedTerritory = nullptr;
}

void AWorldMap::RegisterTerritory(ATerritory* Territory)
{
    if (Territory && !Territories.Contains(Territory))
    {
        Territories.Add(Territory);
        Territory->OnTerritorySelected.AddDynamic(this, &AWorldMap::SelectTerritory);
    }
}

ATerritory* AWorldMap::GetTerritoryById(int32 TerritoryId) const
{
    for (ATerritory* Territory : Territories)
    {
        if (Territory && Territory->TerritoryID == TerritoryId)
        {
            return Territory;
        }
    }
    return nullptr;
}

void AWorldMap::SelectTerritory(ATerritory* Territory)
{
    if (Territory)
    {
        SelectedTerritory = Territory;
    }
}

bool AWorldMap::MoveBetween(ATerritory* From, ATerritory* To)
{
    if (!From || !To)
    {
        return false;
    }

    if (From->MoveTo(To))
    {
        SelectedTerritory = To;
        return true;
    }

    return false;
}

