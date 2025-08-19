#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WorldMap.generated.h"

class ATerritory;

/**
 * Actor owning and managing all territories in the map.
 */
UCLASS()
class SKALD_API AWorldMap : public AActor
{
    GENERATED_BODY()

public:
    AWorldMap();

    /** All territories contained in this world map. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldMap")
    TArray<ATerritory*> Territories;

    /** Currently selected territory. */
    UPROPERTY(BlueprintReadOnly, Category = "WorldMap")
    ATerritory* SelectedTerritory;

    /** Register a territory with the world map. */
    UFUNCTION(BlueprintCallable, Category = "WorldMap")
    void RegisterTerritory(ATerritory* Territory);

    /** Get a territory by its identifier. */
    UFUNCTION(BlueprintCallable, Category = "WorldMap")
    ATerritory* GetTerritoryById(int32 TerritoryId) const;

    /** Handle territory selection. */
    UFUNCTION(BlueprintCallable, Category = "WorldMap")
    void SelectTerritory(ATerritory* Territory);

    /** Move units between territories. */
    UFUNCTION(BlueprintCallable, Category = "WorldMap")
    bool MoveBetween(ATerritory* From, ATerritory* To);
};

