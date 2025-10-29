#pragma once

#include "Components/ActorComponent.h"
#include "Abilities/SkaldAbilityTypes.h"
#include "SkaldAbilityComponent.generated.h"

class AFighterPawn;
class UDataTable;
class UDecalComponent;
class UNiagaraSystem;
struct FFighterStats;
struct FDiceRollResult;
class UGridBattleManager;

USTRUCT()
struct FSkaldAbilityStatDelta
{
    GENERATED_BODY();

    UPROPERTY()
    int32 AttackDice = 0;

    UPROPERTY()
    int32 AttackDamage = 0;

    UPROPERTY()
    int32 AttackRange = 0;

    UPROPERTY()
    int32 Movement = 0;

    UPROPERTY()
    int32 Defence = 0;

    UPROPERTY()
    int32 Strength = 0;

    UPROPERTY()
    int32 CriticalBonusDamage = 0;
};

USTRUCT()
struct FSkaldActiveAbilityModifier
{
    GENERATED_BODY();

    UPROPERTY()
    FName SourceAbilityId = NAME_None;

    UPROPERTY()
    FSkaldAbilityStatDelta Delta;

    UPROPERTY()
    int32 RemainingRounds = 0;

    UPROPERTY()
    bool bRemoveWhenRoundsExpire = false;

    UPROPERTY()
    bool bRemoveOnRoundStart = false;

    UPROPERTY()
    bool bRemoveOnActivationStart = false;

    UPROPERTY()
    bool bRemoveOnActivationEnd = false;

    UPROPERTY()
    bool bDealSelfDamageOnActivationEndIfAttack = false;

    UPROPERTY()
    int32 SelfDamageAmount = 0;
};

USTRUCT()
struct FSkaldAbilityTrapState
{
    GENERATED_BODY();

    UPROPERTY()
    FIntPoint Cell = FIntPoint(INDEX_NONE, INDEX_NONE);

    UPROPERTY()
    int32 RoundsRemaining = 0;

    UPROPERTY()
    int32 Damage = 0;

    UPROPERTY()
    FName SourceAbilityId = NAME_None;

    UPROPERTY()
    FSkaldAbilityDefinition AbilityDefinition;

    UPROPERTY(Transient)
    TWeakObjectPtr<UDecalComponent> VisualComponent;

    UPROPERTY()
    bool bPendingPlacement = false;
};

enum class ESkaldAbilityModifierPhase : uint8
{
    RoundStart,
    ActivationStart,
    ActivationEnd
};

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
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    /** Assign passives/actives for the owning fighter. Safe to call multiple times. */
    void RefreshAbilityLoadout(const FFighterStats& InStats, ESkaldFaction InFaction);

    /** Reduce cooldowns and refresh per-round counters. Call from the owning pawn when a new round begins. */
    void HandleRoundStarted();

    /** Reset reaction usage and other activation state. Call when the fighter begins an activation. */
    void HandleActivationStarted();

    /** Cleanup for modifiers that end when the activation finishes. */
    void HandleActivationFinished();

    /** Record that the owning fighter performed an attack during its activation. */
    void NotifyAttackCommitted();

    /** Attempt to trigger a slot. Returns true on success, populates OutFailureReason otherwise. */
    bool TryBeginAbility(ESkaldAbilitySlot Slot, FText& OutFailureReason);

    /** Query whether a slot can currently be triggered without mutating state. */
    bool CanActivateAbility(ESkaldAbilitySlot Slot, FText* OutFailureReason = nullptr) const;

    /** Restore one spent reaction if possible. */
    bool TryRefreshReaction();

    /** Force the owning fighter to lose all remaining reactions for the round. */
    void ForceSpendAllReactions();

    /** Place a ground-targeted trap at the supplied grid cell. */
    bool DeployTrapAtCell(const FIntPoint& Cell, FName AbilityId, FText& OutError);

    /** Query whether this component is waiting for a trap placement for the given ability. */
    bool HasPendingTrapForAbility(FName AbilityId) const;

    /** Resolve any trap owned by this component at the supplied cell. */
    bool TryResolveTrapAtCell(const FIntPoint& Cell, AFighterPawn* TriggeringFighter);

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

    /** Apply a modifier originating from another fighter (e.g. enemy debuffs). */
    void ReceiveExternalModifier(FSkaldActiveAbilityModifier&& Modifier);
    void NotifyOwnerMoved(int32 DistanceMoved);

protected:
    virtual void BeginPlay() override;

    UFUNCTION()
    void OnRep_AbilitySlots();

    UFUNCTION(NetMulticast, Reliable)
    void MulticastAbilityTriggered(const FSkaldAbilityDefinition& Definition);

    void BroadcastStateChanged();

    bool CanPayCost(const FSkaldAbilityDefinition& Definition, FText& OutError) const;
    bool ConsumeCost(const FSkaldAbilityDefinition& Definition);
    bool CanTriggerAbility(const FSkaldAbilityState& State, FText& OutError) const;

    void HandleAbilityTriggeredLocal(const FSkaldAbilityDefinition& Definition);
    void PlayAbilityFeedback(const FSkaldAbilityDefinition& Definition);
    void ApplyAbilityEffects(const FSkaldAbilityDefinition& Definition);
    void AddActiveModifier(FSkaldActiveAbilityModifier&& Modifier);
    void RemoveActiveModifier(int32 Index);
    int32 FindPendingTrapIndex(FName AbilityId) const;
    void RemoveTrapAtIndex(int32 Index);
    void ClearAllTraps();
    UDecalComponent* SpawnTrapVisualAtCell(const FIntPoint& Cell);
    void RemoveExpiredModifiers(ESkaldAbilityModifierPhase Phase);
    void ApplyStatDeltaToOwner(const FSkaldAbilityStatDelta& Delta, bool bApply);
    bool TryResolveFactionAbilitySet(ESkaldFaction InFaction, FSkaldFactionAbilitySet& OutSet);
    FSkaldAbilityDefinition ResolveActiveAbilityForCost(const FSkaldFactionAbilitySet& AbilitySet, int32 ArmyCost) const;
    UDataTable* GetFactionAbilityDataTable();
    void TryRegisterBattleDelegates();
    void RemoveBattleDelegates();
    void HandleViralLashResolved(AFighterPawn* Defender, const FDiceRollResult& Result);
    void HandleScrapperFeintResolved(const FDiceRollResult& Result);
    void HandleRallyingShotResolved(const FDiceRollResult& Result);
    void HandleBrutalChargeResolved(AFighterPawn* Defender, const FDiceRollResult& Result);
    void HandleRuneRiposteTriggered(AFighterPawn* Attacker, const FDiceRollResult& Result);
    void ApplyModifierToTarget(AFighterPawn* Target, FSkaldActiveAbilityModifier&& Modifier);
    void RemoveModifiersByAbilityId(FName AbilityId);
    void ConsumeOncePerBattleAbility(FName AbilityId);

    UFUNCTION()
    void HandleBattleAttackResolved(AFighterPawn* Attacker, AFighterPawn* Defender, const FDiceRollResult& Result);

    UFUNCTION()
    void HandleOwnerHealthChanged(int32 NewHealth);

    UFUNCTION(NetMulticast, Reliable)
    void MulticastTrapPlaced(const FIntPoint& Cell);

    UFUNCTION(NetMulticast, Reliable)
    void MulticastTrapRemoved(const FIntPoint& Cell);

    /** Owning fighter cached for convenience. */
    TWeakObjectPtr<AFighterPawn> CachedFighter;

    /** Cached pointer to the active battle manager for event hooks. */
    TWeakObjectPtr<UGridBattleManager> CachedBattleManager;

    /** Passive shown on HUD and roster screens. */
    UPROPERTY(Replicated)
    FSkaldAbilityDefinition PassiveAbility;

    /** Mapping from input slots to their ability state. */
    UPROPERTY()
    TMap<ESkaldAbilitySlot, FSkaldAbilityState> AbilitySlots;

    /** Lightweight replicated view of slot -> state pairs. */
    UPROPERTY(ReplicatedUsing = OnRep_AbilitySlots)
    TArray<FSkaldReplicatedAbilitySlotState> ReplicatedAbilitySlots;

    /** Optional data table providing faction ability definitions at runtime. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability", meta = (AllowedClasses = "DataTable"))
    TSoftObjectPtr<UDataTable> FactionAbilityTable;

    /** Cached pointer to the loaded data table asset, if any. */
    UPROPERTY(Transient)
    UDataTable* LoadedAbilityDataTable = nullptr;

    /** True when Viral Lash should apply its contagion on the next attack resolution. */
    bool bApplyViralLashOnNextAttack = false;

    /** True when Scrapper Feint should grant its reposition bonus on the next miss. */
    bool bApplyScrapperFeintOnNextMiss = false;
    bool bApplyRallyingShotOnNextAttack = false;
    bool bBrutalChargeActive = false;
    int32 BrutalChargeDistanceMoved = 0;
    bool bRuneRiposteReady = false;
    bool bVeilStepBonusActive = false;
    bool bDeathlessAdvanceReady = false;
    bool bShieldWallPivotActive = false;
    TWeakObjectPtr<AFighterPawn> ShieldWallPivotProtectedAlly;
    TSet<TWeakObjectPtr<AFighterPawn>> TacticalReservesRefreshedThisRound;
    bool bSmashThroughActive = false;
    bool bForgeguardBraceReady = false;
    bool bDeepDelveMortarPending = false;
    bool bMoonlanceFlurryActive = false;
    int32 MoonlanceFlurryAttacksRemaining = 0;
    bool bStarfallInvocationPending = false;
    bool bGraveGraspPending = false;
    bool bSoulHarvestActive = false;
    bool bSoulHarvestKillSecured = false;
    bool bHarrierDashActive = false;
    bool bSuppressingFireActive = false;
    bool bRendAndTearActive = false;
    bool bArtilleryStrikePending = false;
    bool bGoblinFlashBombActive = false;
    bool bGoblinNetActive = false;
    bool bGoblinAmbushActive = false;
    bool bGoblinAmbushPenaltyPending = false;
    bool bSuppressAbilityEffectOnNextTrigger = false;

    /** Active traps armed by this ability component. */
    UPROPERTY()
    TArray<FSkaldAbilityTrapState> ActiveTraps;

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

    /** Transient list of active stat modifiers granted by abilities. */
    UPROPERTY()
    TArray<FSkaldActiveAbilityModifier> ActiveModifiers;

    /** Tracks whether the owning fighter has made an attack during its current activation. */
    UPROPERTY()
    bool bOwnerAttackedThisActivation = false;
};

