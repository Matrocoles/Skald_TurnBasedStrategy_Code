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
class UDecalComponent;
class UMaterialInterface;
class UTextRenderComponent;
class USoundBase;

/**
 * Actor representing a single territory on the world map.
 */
UCLASS(Blueprintable, BlueprintType)
class SKALD_API ATerritory : public AActor
{
    GENERATED_BODY()

public:
    ATerritory();

    virtual void OnConstruction(const FTransform& Transform) override;

    virtual void BeginPlay() override;

    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

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
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Territory", ReplicatedUsing = OnRep_IsCapital)
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

    /** Number of units stationed in this territory. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Territory", ReplicatedUsing = OnRep_ArmyUnits)
    int32 ArmyUnits = 0;

    /** ID of siege equipment built in this territory, 0 if none. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Territory", Replicated)
    int32 BuiltSiegeID = 0;
    /** Whether this territory contains treasure. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Territory", Replicated)
    bool bHasTreasure = false;


    /**
     * Mark this territory as selected. Pass -1 to indicate no specific
     * selecting player (equivalent to INDEX_NONE).
     */
    UFUNCTION(BlueprintCallable, Category = "Territory")
    void Select(int32 SelectingPlayerId = -1);

    /** Remove selection state from this territory. */
    UFUNCTION(BlueprintCallable, Category = "Territory")
    void Deselect();

    /** True if the local player should currently see selection visuals. */
    bool IsSelectionVisibleToLocalPlayer() const;

    /** Sound to play when this territory becomes selected locally, if set. */
    USoundBase *GetSelectionSound() const;

    /** Volume multiplier applied when playing the selection sound. */
    float GetSelectionSoundVolumeMultiplier() const;

    FORCEINLINE int32 GetTerritoryId() const { return TerritoryID; }

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

    /** Set an additional height offset applied to the selection decal. */
    UFUNCTION(BlueprintCallable, Category = "Territory|Selection")
    void SetSelectionDecalAdditionalHeightOffset(float AdditionalOffset);

    /** Retrieve the extra height offset applied to the selection decal. */
    UFUNCTION(BlueprintPure, Category = "Territory|Selection")
    float GetSelectionDecalAdditionalHeightOffset() const { return SelectionDecalAdditionalHeightOffset; }

    /** Effective offset used when positioning the selection decal. */
    UFUNCTION(BlueprintPure, Category = "Territory|Selection")
    float GetSelectionDecalEffectiveHeightOffset() const;

    /** Refresh the visual appearance of this territory. */
    UFUNCTION(BlueprintCallable, Category = "Territory")
    void RefreshAppearance();

    UFUNCTION()
    void OnRep_OwningPlayer();

    UFUNCTION()
    void OnRep_ArmyUnits();

    UFUNCTION()
    void OnRep_IsCapital();

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

    /** Decal used to visualize local selections. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Territory|Selection")
    UDecalComponent* SelectionDecal = nullptr;

    /** Material applied to the territory selection decal. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Territory|Selection")
    TObjectPtr<UMaterialInterface> SelectionDecalMaterial = nullptr;

    /** Decal dimensions used when highlighting this territory. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Territory|Selection")
    FVector SelectionDecalSize = FVector(64.f, 256.f, 256.f);

    /** Vertical offset applied to the selection decal relative to the mesh. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Territory|Selection")
    float SelectionDecalVerticalOffset = 0.f;

    /** Additional offset supplied externally (e.g. by the world map). */
    float SelectionDecalAdditionalHeightOffset = 0.f;

    /** Optional sound played when this territory becomes selected. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Territory|Selection")
    TObjectPtr<USoundBase> SelectionSound = nullptr;

    /** Volume multiplier for the selection sound. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Territory|Selection", meta = (ClampMin = "0.0"))
    float SelectionSoundVolumeMultiplier = 1.f;

    /** Dynamic material used for highlighting. */
    UPROPERTY()
    UMaterialInstanceDynamic* DynamicMaterial = nullptr;

    /** Base color of the territory mesh. */
    FLinearColor DefaultColor;

    /** Whether the territory has been selected. */
    bool bIsSelected = false;

    /** Cached identifier of the player who last selected this territory. */
    int32 LastSelectingPlayerId = INDEX_NONE;

    void UpdateTerritoryColor();
    void UpdateLabel();

    void UpdateSelectionDecalTransform();
    void ApplySelectionDecalMaterial();
    void SetSelectionDecalVisible(bool bVisible);
    void UpdateSelectionVisuals(bool bVisible);
    bool ShouldShowSelectionVisuals(int32 SelectingPlayerId) const;
    void EnsureDynamicMaterial();
};
