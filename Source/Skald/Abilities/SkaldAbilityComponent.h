#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Abilities/SkaldAbilityTypes.h"
#include "UObject/ScriptDelegates.h"
#include "Misc/Optional.h"
#include "Templates/SharedPointer.h"
#include "SkaldAbilityComponent.generated.h"

class AFighterPawn;
class UDataTable;
class UDecalComponent;
class UNiagaraSystem;
class UGridOverlayComponent;
struct FFighterStats;
struct FDiceRollResult;
class UGridBattleManager;

USTRUCT()
struct FSkaldAbilityContext
{
    GENERATED_BODY();

    UPROPERTY()
    FName AbilityId = NAME_None;

    UPROPERTY()
    TWeakObjectPtr<AFighterPawn> TargetFighter;

    UPROPERTY()
    FIntPoint TargetCell = FIntPoint(INDEX_NONE, INDEX_NONE);

    UPROPERTY()
    bool bHasTargetCell = false;
};

USTRUCT(BlueprintType)
struct FSkaldAbilityStatDelta
{
    GENERATED_BODY();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ability|Stats")
    int32 AttackDice = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ability|Stats")
    int32 AttackDamage = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ability|Stats")
    int32 MeleeAttackDamage = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ability|Stats")
    int32 RangedAttackDamage = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ability|Stats")
    int32 AttackRange = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ability|Stats")
    int32 Movement = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ability|Stats")
    int32 Defence = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ability|Stats")
    int32 Strength = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ability|Stats")
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
struct FSkaldExternalTargetModifier
{
    GENERATED_BODY();

    UPROPERTY()
    TWeakObjectPtr<AFighterPawn> Target;

    UPROPERTY()
    FName SourceAbilityId = NAME_None;

    UPROPERTY()
    FSkaldAbilityStatDelta Delta;
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

USTRUCT()
struct FSkaldViralLashCarrierState
{
    GENERATED_BODY();

    UPROPERTY()
    TWeakObjectPtr<AFighterPawn> Carrier;

    UPROPERTY()
    int32 RoundNumber = INDEX_NONE;

    UPROPERTY()
    bool bHasSpread = false;
};

USTRUCT()
struct FSkaldAberrantBloomHazardCell
{
    GENERATED_BODY();

    UPROPERTY()
    FIntPoint Cell = FIntPoint(INDEX_NONE, INDEX_NONE);

    UPROPERTY()
    TWeakObjectPtr<UDecalComponent> VisualComponent;
};

USTRUCT()
struct FSkaldRaincallerCell
{
    GENERATED_BODY();

    UPROPERTY()
    FIntPoint Cell = FIntPoint(INDEX_NONE, INDEX_NONE);

    UPROPERTY()
    TWeakObjectPtr<UDecalComponent> VisualComponent;
};

USTRUCT()
struct FSkaldRaincallerOccupantState
{
    GENERATED_BODY();

    UPROPERTY()
    TWeakObjectPtr<AFighterPawn> Fighter;

    UPROPERTY()
    bool bIsAlly = false;

    UPROPERTY()
    bool bAmphibiousApplied = false;

    UPROPERTY()
    FSkaldAbilityStatDelta AppliedDelta;

    UPROPERTY()
    FName SourceAbilityId = NAME_None;
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
    void NotifyOtherFighterMoved(AFighterPawn* Fighter, const TArray<FIntPoint>& PreviousCells, const TArray<FIntPoint>& NewCells);

    /** Attempt to trigger a slot. Returns true on success, populates OutFailureReason otherwise. */
    bool TryBeginAbility(ESkaldAbilitySlot Slot, FText& OutFailureReason);

    /** Query whether a slot can currently be triggered without mutating state. */
    bool CanActivateAbility(ESkaldAbilitySlot Slot, FText* OutFailureReason = nullptr) const;

    /** Provide the ability context associated with the next activation. */
    void SetPendingAbilityContext(const FSkaldAbilityContext& Context);

    /** Clear any pending ability context without consuming it. */
    void ClearPendingAbilityContext();

    /** Fetch the pending ability context if available. */
    const FSkaldAbilityContext* GetPendingAbilityContext() const;

    /** Restore one spent reaction if possible. */
    bool TryRefreshReaction();

    /** Force the owning fighter to lose all remaining reactions for the round. */
    void ForceSpendAllReactions();

    /** Mark that the owner has an outstanding Tactical Reserves attack bonus. */
    void MarkTacticalReservesAttackBuffPending();

    /** Clear the pending Tactical Reserves attack bonus, if any. */
    void ClearTacticalReservesAttackBuff();

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
    bool TreatsDifficultTerrainAsNormal() const;
    bool CanIgnoreEngagementRestrictions() const;
    int32 GetCriticalHitThreshold() const;
    void ModifyOutgoingAttackStats(AFighterPawn* Target, FFighterStats& InOutStats);
    void ModifyIncomingAttackStats(AFighterPawn* Attacker, FFighterStats& InOutAttackerStats);
    void HandleIncomingAttackStarted();
    void HandleIncomingAttackFinished();

    bool HasHarrierDashDefencePenalty() const;
    void MarkHarrierDashDefencePenaltyConsumed();
    bool TryPerformHarrierDashAdvance(AFighterPawn* Target);

protected:
    virtual void BeginPlay() override;

    UFUNCTION()
    void OnRep_AbilitySlots();

    UFUNCTION(NetMulticast, Reliable)
    void MulticastAbilityTriggered(const FSkaldAbilityDefinition& Definition, AFighterPawn* TargetFighter, bool bHasTargetCell, FIntPoint TargetCell);

    void BroadcastStateChanged();

    bool CanPayCost(const FSkaldAbilityDefinition& Definition, FText& OutError) const;
    bool ConsumeCost(const FSkaldAbilityDefinition& Definition);
    bool CanTriggerAbility(const FSkaldAbilityState& State, FText& OutError) const;

    void HandleAbilityTriggeredLocal(const FSkaldAbilityDefinition& Definition);
    void PlayAbilityFeedback(const FSkaldAbilityDefinition& Definition);
    void ApplyAbilityEffects(const FSkaldAbilityDefinition& Definition);
    void AddActiveModifier(FSkaldActiveAbilityModifier&& Modifier);
    void RemoveActiveModifier(int32 Index);
    void RemoveTargetModifiersByAbilityId(FName AbilityId);
    int32 FindPendingTrapIndex(FName AbilityId) const;
    void RemoveTrapAtIndex(int32 Index);
    void ClearAllTraps();
    UDecalComponent* SpawnTrapVisualAtCell(const FIntPoint& Cell, const FSkaldAbilityDefinition* SourceAbility = nullptr);
    void HandleModifierApplied(FName AbilityId);
    void HandleModifierRemoved(FName AbilityId);
    void RemoveExpiredModifiers(ESkaldAbilityModifierPhase Phase);
    void ApplyStatDeltaToOwner(const FSkaldAbilityStatDelta& Delta, bool bApply);
    FSkaldAbilityStatDelta ApplyTargetStatDelta(AFighterPawn* Target, const FSkaldAbilityStatDelta& Delta, bool bApply);
    bool TryResolveFactionAbilitySet(ESkaldFaction InFaction, FSkaldFactionAbilitySet& OutSet);
    FSkaldAbilityDefinition ResolveActiveAbilityForCost(const FSkaldFactionAbilitySet& AbilitySet, int32 ArmyCost) const;
    UDataTable* GetFactionAbilityDataTable();
    void TryRegisterBattleDelegates();
    void RemoveBattleDelegates();
    void HandleViralLashResolved(AFighterPawn* Defender, const FDiceRollResult& Result);
    void HandleScrapperFeintResolved(const FDiceRollResult& Result);
    void HandleRallyingShotResolved(const FDiceRollResult& Result);
    bool IsValidRallyingShotAlly(AFighterPawn* Owner, AFighterPawn* Candidate) const;
    AFighterPawn* ResolveAbilityTargetFromContext(const FName& AbilityId) const;
    bool ResolveAbilityTargetCellFromContext(const FName& AbilityId, FIntPoint& OutCell) const;
    bool TryPushFighterOneCellAway(const AFighterPawn* Source, AFighterPawn* Target) const;
    bool TryPullFighterOneCellTowards(const AFighterPawn* Source, AFighterPawn* Target) const;
    bool TrySwapAdjacentWithAlly(AFighterPawn* Owner, AFighterPawn* Ally) const;
    bool TryShiftFighterOneCell(AFighterPawn* Fighter, const AFighterPawn* Reference) const;
    void TryShiftFighterMultipleCells(AFighterPawn* Fighter, const AFighterPawn* Reference, int32 MaxSteps) const;
    void ApplyAbilityContextFromReplication(const FName& AbilityId, AFighterPawn* TargetFighter, bool bHasTargetCell, const FIntPoint& TargetCell);
    void SpawnTargetCellVisuals(const FSkaldAbilityDefinition& Definition);
    void ApplyTerrainVisualOverrides(const FSkaldAbilityDefinition& Definition, const FIntPoint& Cell, UDecalComponent* Decal, UGridOverlayComponent* Grid);
    void HandleBrutalChargeResolved(AFighterPawn* Defender, const FDiceRollResult& Result);
    void HandleRuneRiposteTriggered(AFighterPawn* Attacker, const FDiceRollResult& Result);
    void ApplyModifierToTarget(AFighterPawn* Target, FSkaldActiveAbilityModifier&& Modifier);
    void RemoveModifiersByAbilityId(FName AbilityId);
    void ConsumeOncePerBattleAbility(FName AbilityId);
    void RefreshPassiveState();
    void RefreshAllPassiveStates();
    void RefreshDwarfPassive();
    void RefreshEmpirePassive();
    void ApplyActivationPassiveEffects();
    int32 CountAdjacentFactionAllies(AFighterPawn* Fighter, ESkaldFaction Faction) const;
    int32 CountAdjacentEnemies(AFighterPawn* Fighter) const;
    void HandlePassiveEffectApplied(const FSkaldAbilityDefinition& Definition);
    void HandlePassiveEffectRemoved(FName AbilityId);
    bool HasFactionAttackedOwnerThisRound(ESkaldFaction Faction) const;
    bool CanApplyAmphibiousRushBonus(const FSkaldAbilityDefinition& Definition) const;
    bool IsAnyCellDifficult(const TArray<FIntPoint>& Cells, UGridOverlayComponent* Grid) const;
    void SpawnAberrantBloomHazard(const FSkaldAbilityDefinition& Definition);
    void ClearAberrantBloomHazards();
    bool DoesAnyCellMatchAberrantBloom(const TArray<FIntPoint>& Cells) const;
    void HandleAberrantBloomTriggered(AFighterPawn* Victim);
    void HandleAberrantBloomOnMovement(AFighterPawn* Fighter, const TArray<FIntPoint>& NewCells);
    void SpawnRaincallerTemplate(const FSkaldAbilityDefinition& Definition);
    void ClearRaincallerTemplate();
    void RefreshRaincallerOccupants();
    void HandleRaincallerOnMovement(AFighterPawn* Fighter);
    void UpdateRaincallerEffectForFighter(AFighterPawn* Fighter);
    bool DoesAnyCellMatchRaincaller(const TArray<FIntPoint>& Cells) const;
    int32 FindRaincallerOccupantIndex(AFighterPawn* Fighter) const;
    void RemoveRaincallerOccupantAtIndex(int32 Index);
    void UpdateRaincallerTemplateLifetime();
    void UpdateViralLashCarriers();
    bool ApplyViralLashDebuffToFighter(AFighterPawn* Fighter);
    void HandleViralLashCarrierDamaged(AFighterPawn* Defender, const FDiceRollResult& Result);
    void AddRaincallerAmphibiousStack();
    void RemoveRaincallerAmphibiousStack();

    UFUNCTION()
    void HandleBattleAttackResolved(AFighterPawn* Attacker, AFighterPawn* Defender, const FDiceRollResult& Result);

    UFUNCTION()
    void HandleActiveFighterChanged(AFighterPawn* NewFighter);

    UFUNCTION()
    void HandleOwnerHealthChanged(int32 NewHealth);

    UFUNCTION(NetMulticast, Reliable)
    void MulticastTrapPlaced(const FIntPoint& Cell, FName SourceAbilityId);

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
    TWeakObjectPtr<AFighterPawn> RallyingShotDesignatedAlly;
    bool bBrutalChargeActive = false;
    int32 BrutalChargeDistanceMoved = 0;
    bool bRuneRiposteReady = false;
    bool bVeilStepBonusActive = false;
    bool bDeathlessAdvanceReady = false;
    bool bShieldWallPivotActive = false;
    TWeakObjectPtr<AFighterPawn> ShieldWallPivotProtectedAlly;
    TSet<TWeakObjectPtr<AFighterPawn>> TacticalReservesBuffedThisRound;
    bool bHasPendingTacticalReservesAttackBuff = false;
    bool bEmpirePassiveDefenceBonusActive = false;
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
    bool bHarrierDashDefencePenaltyConsumed = false;
    bool bSuppressingFireActive = false;
    bool bRendAndTearActive = false;
    bool bArtilleryStrikePending = false;
    FIntPoint DeepDelveMortarTargetCell = FIntPoint(INDEX_NONE, INDEX_NONE);
    FIntPoint ArtilleryStrikeTargetCell = FIntPoint(INDEX_NONE, INDEX_NONE);
    bool bGoblinFlashBombActive = false;
    bool bGoblinNetActive = false;
    bool bGoblinAmbushActive = false;
    bool bGoblinAmbushPenaltyPending = false;
    bool bIgnoreDifficultTerrainForNextMove = false;
    int32 BubbleWardSourceCount = 0;
    int32 BubbleWardProtectionStacks = 0;
    int32 WaaghRoarBuffCount = 0;
    int32 HowlOfTheAlphaBuffCount = 0;
    int32 CriticalHitThresholdOverride = 0;
    bool bSuppressAbilityEffectOnNextTrigger = false;
    bool bElfEvasionActive = false;
    bool bLizardPenaltyConsumedThisRound = false;
    bool bRavpackMomentumPending = false;
    bool bLowHealthStrengthPenaltyActive = false;
    bool bUndeadResilienceActive = false;
    int32 DwarfPassiveAdjacentCount = 0;
    int32 LastKnownHealth = 0;
    TMap<FName, int32> PassiveVisualStackCounts;
    TSet<ESkaldFaction> FactionsThatAttackedOwnerThisRound;
    int32 RaincallerAmphibiousStackCount = 0;

    /** Active traps armed by this ability component. */
    UPROPERTY()
    TArray<FSkaldAbilityTrapState> ActiveTraps;

    /** Optional context describing the command that triggered the next ability. */
    TOptional<FSkaldAbilityContext> PendingAbilityContext;

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

    /** Modifiers applied directly to external targets without their own ability component. */
    UPROPERTY()
    TArray<FSkaldExternalTargetModifier> ActiveTargetModifiers;

    /** Tracks whether the owning fighter has made an attack during its current activation. */
    UPROPERTY()
    bool bOwnerAttackedThisActivation = false;

    /** Viral Lash contagion carriers tracked for the current round. */
    UPROPERTY()
    TArray<FSkaldViralLashCarrierState> ViralLashCarriers;

    /** Fighters that have already suffered the Viral Lash defence penalty. */
    UPROPERTY()
    TSet<TWeakObjectPtr<AFighterPawn>> ViralLashDebuffedFighters;

    /** Persistent Aberrant Bloom hazard cells and visuals. */
    UPROPERTY()
    TArray<FSkaldAberrantBloomHazardCell> AberrantBloomHazardCells;

    /** Fighters that have already received the Aberrant Bloom movement penalty. */
    UPROPERTY()
    TSet<TWeakObjectPtr<AFighterPawn>> AberrantBloomMovementDebuffed;

    /** Cached damage dealt when Aberrant Bloom triggers. */
    int32 AberrantBloomDamage = 0;

    /** Persistent Raincaller Deluge template cells and visuals. */
    UPROPERTY()
    TArray<FSkaldRaincallerCell> RaincallerTemplateCells;

    /** Fighters currently affected by Raincaller Deluge. */
    UPROPERTY()
    TArray<FSkaldRaincallerOccupantState> RaincallerOccupants;

    /** Round when the current Raincaller template should expire. */
    int32 RaincallerTemplateExpireRound = INDEX_NONE;

    /** Ability identifier used for the active Raincaller template. */
    FName RaincallerTemplateSourceId = NAME_None;
};
