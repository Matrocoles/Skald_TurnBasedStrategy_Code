#pragma once

#include "Components/ActorComponent.h"
#include "Abilities/SkaldAbilityTypes.h"
#include "SkaldAbilityComponent.generated.h"

class AFighterPawn;
class UNiagaraSystem;
struct FFighterStats;

USTRUCT(BlueprintType)
struct FSkaldAbilityState
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Ability")
    FSkaldAbilityDefinition Definition;

    /** Remaining cooldown rounds; zero means ready. */
    UPROPERTY(BlueprintReadOnly, Category = "Ability")
    int32 CooldownRemaining = 0;

    /** True once the ability has been used at least once. */
    UPROPERTY(BlueprintReadOnly, Category = "Ability")
    bool bHasBeenUsed = false;

    /** True if the ability is currently considered on cooldown. */
    UPROPERTY(BlueprintReadOnly, Category = "Ability")
    bool bIsOnCooldown = false;
};

USTRUCT()
struct FSkaldReplicatedAbilitySlotState
{
    GENERATED_BODY()

    UPROPERTY()
    ESkaldAbilitySlot Slot = ESkaldAbilitySlot::Ability1;

    UPROPERTY()
    FSkaldAbilityState State;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSkaldAbilityComponentUpdated, USkaldAbilityComponent*, AbilityComponent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSkaldAbilityTriggered, USkaldAbilityComponent*, AbilityComponent, const FSkaldAbilityDefinition&, AbilityDefinition);

/**
 * Lightweight ability manager that maps faction/unit cost to passives and actives.
 * The component owns cooldown bookkeeping and broadcasts updates for the HUD.
 */
UCLASS(ClassGroup = (Skald), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class SKALD_API USkaldAbilityComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    USkaldAbilityComponent();

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    /** Assign passives/actives for the owning fighter. Safe to call multiple times. */
    void RefreshAbilityLoadout(const FFighterStats& InStats, ESkaldFaction InFaction);

    /** Reduce cooldowns and refresh per-round counters. Call from the owning pawn when a new round begins. */
    void HandleRoundStarted();

    /** Reset reaction usage and other activation state. Call when the fighter begins an activation. */
    void HandleActivationStarted();

    /** Attempt to trigger a slot. Returns true on success, populates OutFailureReason otherwise. */
    bool TryBeginAbility(ESkaldAbilitySlot Slot, FText& OutFailureReason);

    /** Passive shared with every fighter in the faction. */
    FSkaldAbilityDefinition GetPassiveAbility() const { return PassiveAbility; }

    /** Copy of the active slot states in display order. */
    void GetAbilityStates(TArray<FSkaldAbilityState>& OutStates) const;

    /** Lookup a specific slot state. */
    const FSkaldAbilityState* FindAbilityState(ESkaldAbilitySlot Slot) const;

    /** Broadcast when loadout or cooldowns change. */
    UPROPERTY(BlueprintAssignable, Category = "Abilities|Events")
    FSkaldAbilityComponentUpdated OnAbilityStateChanged;

    /** Broadcast when an ability successfully fires (allows blueprints to play bespoke FX). */
    UPROPERTY(BlueprintAssignable, Category = "Abilities|Events")
    FSkaldAbilityTriggered OnAbilityTriggered;

protected:
    virtual void BeginPlay() override;

    UFUNCTION()
    void OnRep_AbilitySlots();

    UFUNCTION(NetMulticast, Reliable)
    void MulticastAbilityTriggered(const FSkaldAbilityDefinition& Definition);

    void BroadcastStateChanged();

    bool CanPayCost(const FSkaldAbilityDefinition& Definition, FText& OutError) const;
    bool ConsumeCost(const FSkaldAbilityDefinition& Definition);

    void HandleAbilityTriggeredLocal(const FSkaldAbilityDefinition& Definition);
    void PlayAbilityFeedback(const FSkaldAbilityDefinition& Definition);

    /** Owning fighter cached for convenience. */
    TWeakObjectPtr<AFighterPawn> CachedFighter;

    /** Passive shown on HUD and roster screens. */
    UPROPERTY(Replicated)
    FSkaldAbilityDefinition PassiveAbility;

    /** Mapping from input slots to their ability state. */
    UPROPERTY()
    TMap<ESkaldAbilitySlot, FSkaldAbilityState> AbilitySlots;

    /** Lightweight replicated view of slot -> state pairs. */
    UPROPERTY(ReplicatedUsing = OnRep_AbilitySlots)
    TArray<FSkaldReplicatedAbilitySlotState> ReplicatedAbilitySlots;

    /** Default reaction availability refreshed every round. */
    UPROPERTY(EditDefaultsOnly, Category = "Ability")
    int32 ReactionsPerRound = 1;

    /** Remaining reactions for the current round. */
    UPROPERTY(Replicated)
    int32 ReactionsRemaining;

    /** True once loadout has been initialised. */
    UPROPERTY(Replicated)
    bool bHasInitialisedLoadout;

    /** Slots stored to maintain deterministic iteration order. */
    UPROPERTY()
    TArray<ESkaldAbilitySlot> SlotOrder;

    void UpdateReplicatedAbilitySlots();
};

