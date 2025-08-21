#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InputCoreTypes.h"
#include "Territory.generated.h"

class ASkaldPlayerState;
class ATerritory;
class UStaticMeshComponent;
class UPrimitiveComponent;
class UMaterialInstanceDynamic;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTerritorySelectedSignature, ATerritory*, Territory);

/**
 * Actor representing a single territory on the world map.
 */
UCLASS()
class SKALD_API ATerritory : public AActor
{
    GENERATED_BODY()

public:
    ATerritory();

    virtual void BeginPlay() override;

    /** Owning player of this territory (renamed to avoid AActor::Owner shadowing). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Territory", meta = (DisplayName = "Owner"))
    ASkaldPlayerState* OwningPlayer = nullptr;

    /** Amount of resources produced by this territory. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Territory")
    int32 Resources = 0;

    /** Unique identifier for this territory. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Territory")
    int32 TerritoryID = 0;

    /** Adjacent territories that units may move to. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Territory")
    TArray<ATerritory*> AdjacentTerritories;

    /** Number of armies stationed in this territory. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Territory")
    int32 ArmyStrength = 0;

    /** Called when the territory is selected. */
    UPROPERTY(BlueprintAssignable, Category = "Territory")
    FTerritorySelectedSignature OnTerritorySelected;

    /** Mark this territory as selected. */
    UFUNCTION(BlueprintCallable, Category = "Territory")
    void Select();

    /** Remove selection state from this territory. */
    UFUNCTION(BlueprintCallable, Category = "Territory")
    void Deselect();

    /** Check if another territory is adjacent to this one. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Territory")
    bool IsAdjacentTo(const ATerritory* Other) const;

    /** Attempt to move units to the target territory. */
    UFUNCTION(BlueprintCallable, Category = "Territory")
    bool MoveTo(ATerritory* TargetTerritory);

    /** React to mouse entering the territory. */
    UFUNCTION()
    void HandleMouseEnter(UPrimitiveComponent* TouchedComponent);

    /** React to mouse leaving the territory. */
    UFUNCTION()
    void HandleMouseLeave(UPrimitiveComponent* TouchedComponent);

    /** React to the territory being clicked. */
    UFUNCTION()
    void HandleClicked(UPrimitiveComponent* TouchedComponent, FKey ButtonPressed);

protected:
    /** Visual representation of the territory. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Territory")
    UStaticMeshComponent* MeshComponent = nullptr;

    /** Dynamic material used for highlighting. */
    UPROPERTY()
    UMaterialInstanceDynamic* DynamicMaterial = nullptr;

    /** Base color of the territory mesh. */
    FLinearColor DefaultColor;

    /** Whether the territory has been selected. */
    bool bIsSelected = false;
};
