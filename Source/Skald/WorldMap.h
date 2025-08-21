#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WorldMap.generated.h"

class ATerritory;

/** Data describing how a territory should be spawned at runtime. */
USTRUCT(BlueprintType)
struct FTerritorySpawnData
{
    GENERATED_BODY();

    FTerritorySpawnData()
        : TerritoryID(0)
        , TerritoryName(TEXT(""))
        , bIsCapital(false)
        , ContinentID(0)
    {
    }

    FTerritorySpawnData(int32 InID, const FString& InName, bool bCapital = false, int32 InContinent = 0)
        : TerritoryID(InID)
        , TerritoryName(InName)
        , bIsCapital(bCapital)
        , ContinentID(InContinent)
    {
    }

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    int32 TerritoryID;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FString TerritoryName;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    bool bIsCapital;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    int32 ContinentID;
};

/**
 * Actor owning and managing all territories in the map.
 */
UCLASS()
class SKALD_API AWorldMap : public AActor
{
    GENERATED_BODY()

public:
    AWorldMap();

    virtual void BeginPlay() override;

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

    /** Actor class used when spawning territory instances. */
    UPROPERTY(EditAnywhere, Category = "WorldMap")
    TSubclassOf<ATerritory> TerritoryClass;

    /** Minimum XY (in local space) for random spawn positions. */
    UPROPERTY(EditAnywhere, Category = "WorldMap")
    FVector2D SpawnAreaMin = FVector2D(-500.f, -500.f);

    /** Maximum XY (in local space) for random spawn positions. */
    UPROPERTY(EditAnywhere, Category = "WorldMap")
    FVector2D SpawnAreaMax = FVector2D(500.f, 500.f);

protected:
    /** Predefined territory table used for spawning at begin play. */
    static const TArray<FTerritorySpawnData> DefaultTerritories;
};

