#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InputCoreTypes.h"
#include "Territory.generated.h"

class ASkaldPlayerState;
class ATerritory;
class UStaticMesh;
class UStaticMeshComponent;
class UPrimitiveComponent;
class UMaterialInstanceDynamic;
class UTextRenderComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTerritorySelectedSignature, ATerritory*, Territory);

/**
 * Actor representing a single territory on the world map.
 */
UCLASS(Blueprintable, BlueprintType)
class SKALD_API ATerritory : public AActor
{
    GENERATED_BODY()

public:
    ATerritory();

    virtual void BeginPlay() override;

    virtual void GetLifetimeReplicatedProps(
        TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    /** Owning player of this territory (renamed to avoid AActor::Owner shadowing). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Territory", meta = (DisplayName = "Owner"), ReplicatedUsing = OnRep_OwningPlayer)
    ASkaldPlayerState* OwningPlayer = nullptr;

    /** Amount of resources produced by this territory. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Territory", Replicated)
    int32 Resources = 0;

    /** Unique identifier for this territory. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Territory", Replicated)
    int32 TerritoryID = 0;

    /** Display name for this territory. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Territory", Replicated)
    FString TerritoryName;

    /** Whether this territory is a capital. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Territory", Replicated)
    bool bIsCapital = false;

    /** Mesh asset used to mark this territory as a capital. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Territory")
    UStaticMesh* CapitalMeshAsset = nullptr;

    /** Optional identifier describing which continent this territory belongs to. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Territory", Replicated)
    int32 ContinentID = 0;

    /** Adjacent territories that units may move to. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Territory", Replicated)
    TArray<ATerritory*> AdjacentTerritories;

    /** Number of armies stationed in this territory. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Territory", ReplicatedUsing = OnRep_ArmyStrength)
    int32 ArmyStrength = 0;

    /** ID of siege equipment built in this territory, 0 if none. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Territory", Replicated)
    int32 BuiltSiegeID = 0;
    /** Whether this territory contains treasure. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Territory", Replicated)
    bool HasTreasure = false;


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
    bool MoveTo(ATerritory* TargetTerritory, int32 Troops);

    /** React to mouse entering the territory. */
    UFUNCTION()
    void HandleMouseEnter(UPrimitiveComponent* TouchedComponent);

    /** React to mouse leaving the territory. */
    UFUNCTION()
    void HandleMouseLeave(UPrimitiveComponent* TouchedComponent);

    /** React to the territory being clicked. */
    UFUNCTION()
    void HandleClicked(UPrimitiveComponent* TouchedComponent, FKey ButtonPressed);

    /** Refresh the visual appearance of this territory. */
    UFUNCTION(BlueprintCallable, Category = "Territory")
    void RefreshAppearance();

    UFUNCTION()
    void OnRep_OwningPlayer();

    UFUNCTION()
    void OnRep_ArmyStrength();

protected:
    /** Visual representation of the territory. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Territory")
    UStaticMeshComponent* MeshComponent = nullptr;

    /** Mesh indicating this territory is a capital. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Territory")
    UStaticMeshComponent* CapitalMesh = nullptr;

    /** Text label showing name, owner and army count. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Territory")
    UTextRenderComponent* LabelComponent = nullptr;

    /** Dynamic material used for highlighting. */
    UPROPERTY()
    UMaterialInstanceDynamic* DynamicMaterial = nullptr;

    /** Base color of the territory mesh. */
    FLinearColor DefaultColor;

    /** Whether the territory has been selected. */
    bool bIsSelected = false;

    void UpdateTerritoryColor();
    void UpdateLabel();
};
