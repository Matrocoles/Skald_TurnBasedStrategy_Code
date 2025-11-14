#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "SkaldTypes.h"
#include "SkaldAbilityTypes.generated.h"

class UNiagaraSystem;
class USoundBase;
class UAnimMontage;
class UTexture2D;
class UMaterialInterface;

/** Slots exposed through the existing Ability1/2/3 input bindings. */
UENUM(BlueprintType)
enum class ESkaldAbilitySlot : uint8
{
    Ability1 UMETA(DisplayName = "Ability Slot 1"),
    Ability2 UMETA(DisplayName = "Ability Slot 2"),
    Ability3 UMETA(DisplayName = "Ability Slot 3")
};

/** Simple action economy descriptor for active abilities. */
UENUM(BlueprintType)
enum class ESkaldAbilityCostType : uint8
{
    Action UMETA(DisplayName = "Action"),
    Reaction UMETA(DisplayName = "Reaction"),
    Free UMETA(DisplayName = "Free")
};

/** Buckets used to map fighter cost to their ability tier. */
UENUM(BlueprintType)
enum class ESkaldAbilityTier : uint8
{
    Skirmish UMETA(DisplayName = "Skirmish"),
    Line UMETA(DisplayName = "Line"),
    Elite UMETA(DisplayName = "Elite")
};

/** Optional Niagara payload spawned directly on target grid cells. */
USTRUCT(BlueprintType)
struct FSkaldAbilityTargetCellFX
{
    GENERATED_BODY();

    /** Niagara system played at every cell targeted by the ability. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visuals|TargetCell")
    TSoftObjectPtr<UNiagaraSystem> NiagaraEffect;

    /** Offset applied relative to the centre of the grid cell. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visuals|TargetCell")
    FVector LocationOffset = FVector::ZeroVector;

    bool HasEffect() const
    {
        return !NiagaraEffect.IsNull();
    }
};

/** Optional payloads spawned for ability-authored terrain cells. */
USTRUCT(BlueprintType)
struct FSkaldAbilityTerrainCellVisuals
{
    GENERATED_BODY();

    /** Material override applied to the trap marker decal spawned for the cell. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visuals|Terrain")
    TSoftObjectPtr<UMaterialInterface> DecalMaterial;

    /** Niagara system played at the centre of the generated terrain cell. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visuals|Terrain")
    TSoftObjectPtr<UNiagaraSystem> NiagaraEffect;

    /** Offset applied to the Niagara effect relative to the cell centre. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visuals|Terrain")
    FVector NiagaraOffset = FVector::ZeroVector;

    bool HasAnyVisuals() const
    {
        return !DecalMaterial.IsNull() || !NiagaraEffect.IsNull();
    }
};

/** Optional audiovisual payloads referenced when executing an ability. */
USTRUCT(BlueprintType)
struct FSkaldAbilityVisuals
{
    GENERATED_BODY()

    /** Niagara system spawned when the ability starts. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visuals")
    TSoftObjectPtr<UNiagaraSystem> NiagaraEffect;

    /** Sound cue played when the ability fires. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visuals")
    TSoftObjectPtr<USoundBase> Sound;

    /** Optional animation montage triggered on the owning fighter. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visuals")
    TSoftObjectPtr<UAnimMontage> Montage;

    /** Optional Niagara payload to fire on the targeted grid cell(s). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visuals")
    FSkaldAbilityTargetCellFX TargetCellFX;

    /** Optional decal/Niagara overrides for generated terrain cells. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visuals")
    FSkaldAbilityTerrainCellVisuals TerrainCellFX;

    bool HasAnyVisuals() const
    {
        return !NiagaraEffect.IsNull() || !Sound.IsNull() || !Montage.IsNull()
            || TargetCellFX.HasEffect() || TerrainCellFX.HasAnyVisuals();
    }
};

/** Shared definition for both passives and active abilities. */
USTRUCT(BlueprintType)
struct FSkaldAbilityDefinition
{
    GENERATED_BODY()

    FSkaldAbilityDefinition()
        : AbilityId(NAME_None)
        , AbilityName(FText::GetEmpty())
        , AbilityDescription(FText::GetEmpty())
        , CostType(ESkaldAbilityCostType::Action)
        , CooldownRounds(0)
        , bOncePerBattle(false)
        , bIsPassive(false)
    {
    }

    /** Stable identifier used for logging, analytics and localisation lookups. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability")
    FName AbilityId;

    /** Display name exposed to UI. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability")
    FText AbilityName;

    /** Localised description used in tooltips and fighter lists. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability")
    FText AbilityDescription;

    /** Type of economy consumed when this ability is executed. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability")
    ESkaldAbilityCostType CostType;

    /** Cooldown expressed in rounds. Zero indicates no cooldown. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability", meta = (ClampMin = "0"))
    int32 CooldownRounds;

    /** True if the ability can only be used a single time per battle. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability")
    bool bOncePerBattle;

    /** True when the definition represents a passive rather than an active. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability")
    bool bIsPassive;

    /** Optional audio/visual payload spawned when the ability resolves. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability")
    FSkaldAbilityVisuals Visuals;

    /** Optional icon used when presenting the ability in HUD widgets. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability")
    TSoftObjectPtr<UTexture2D> AbilityIcon;

    /** Human readable shorthand of the cost, e.g. "Action" or "Free". */
    FText BuildCostLabel() const;

    /** True when the ability struct holds meaningful data. */
    bool IsValid() const
    {
        return AbilityId != NAME_None;
    }
};

/** Bundled passive/active abilities available to a faction. */
USTRUCT(BlueprintType)
struct FSkaldFactionAbilitySet
{
    GENERATED_BODY()

    /** Passive granted to every fighter in the faction. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability")
    FSkaldAbilityDefinition Passive;

    /** Active for low-cost (skirmish) fighters. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability")
    FSkaldAbilityDefinition SkirmishAbility;

    /** Active for mid-cost (line) fighters. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability")
    FSkaldAbilityDefinition LineAbility;

    /** Active for high-cost (elite) fighters. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability")
    FSkaldAbilityDefinition EliteAbility;
};

/** Row wrapper used when authoring faction ability sets in a data table. */
USTRUCT(BlueprintType)
struct FSkaldFactionAbilityTableRow : public FTableRowBase
{
    GENERATED_BODY();

    /** Faction that the ability set applies to. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability")
    ESkaldFaction Faction = ESkaldFaction::None;

    /** Ability bundle defining passives and actives for the faction. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability")
    FSkaldFactionAbilitySet AbilitySet;
};

/** Return the ability tier associated with the supplied army cost. */
SKALD_API ESkaldAbilityTier ResolveAbilityTierForCost(int32 ArmyCost);

/** Fetch the ability bundle registered for a faction, if any. */
SKALD_API const FSkaldFactionAbilitySet* FindFactionAbilitySet(ESkaldFaction Faction);

/** Fetch the passive assigned to a faction, if available. */
SKALD_API FSkaldAbilityDefinition GetFactionPassive(ESkaldFaction Faction);

/** Fetch the active ability for a fighter based on faction and cost. */
SKALD_API FSkaldAbilityDefinition GetFactionActiveAbility(ESkaldFaction Faction, int32 ArmyCost);

/** Lookup an ability definition by its identifier if available. */
SKALD_API FSkaldAbilityDefinition GetAbilityDefinitionById(FName AbilityId);

/** True if the provided identifier maps to a registered passive ability. */
SKALD_API bool IsPassiveAbilityId(FName AbilityId);

