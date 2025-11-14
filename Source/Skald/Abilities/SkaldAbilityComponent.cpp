#include "Abilities/SkaldAbilityComponent.h"

#include "Algo/Sort.h"
#include "Abilities/SkaldAbilityTypes.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/DecalComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Set.h"
#include "Engine/DataTable.h"
#include "FighterPawn.h"
#include "GridBattleManager.h"
#include "GridOverlayComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"
#include "SkaldLogging.h"
#include "Skald_GameInstance.h"
#include "Sound/SoundBase.h"
#include "UObject/WeakObjectPtrTemplates.h"

namespace
{
FName BuildFactionRowName(ESkaldFaction Faction)
{
    if (Faction == ESkaldFaction::None)
    {
        return NAME_None;
    }

    if (const UEnum* FactionEnum = StaticEnum<ESkaldFaction>())
    {
        return FName(*FactionEnum->GetNameStringByValue(static_cast<int64>(Faction)));
    }

    return NAME_None;
}

bool ModifierHasExplicitDuration(const FSkaldActiveAbilityModifier& Modifier)
{
    return Modifier.bRemoveOnRoundStart || Modifier.bRemoveOnActivationStart
        || Modifier.bRemoveOnActivationEnd || Modifier.bRemoveWhenRoundsExpire;
}

bool AreCellSetsAdjacent(const TArray<FIntPoint>& A, const TArray<FIntPoint>& B)
{
    for (const FIntPoint& CellA : A)
    {
        for (const FIntPoint& CellB : B)
        {
            const int32 Distance = FMath::Max(FMath::Abs(CellA.X - CellB.X), FMath::Abs(CellA.Y - CellB.Y));
            if (Distance <= 1)
            {
                return true;
            }
        }
    }

    return false;
}

void ApplyDefaultModifierDuration(FSkaldActiveAbilityModifier& Modifier)
{
    if (ModifierHasExplicitDuration(Modifier))
    {
        return;
    }

    if (Modifier.RemainingRounds > 0)
    {
        Modifier.bRemoveWhenRoundsExpire = true;
        return;
    }

    Modifier.bRemoveOnRoundStart = true;
    Modifier.bRemoveWhenRoundsExpire = true;
    Modifier.RemainingRounds = 1;
}

const FName LowHealthPenaltyId(TEXT("Ability_Global_LowHealthPenalty"));
const FName TacticalReservesAttackBuffId(TEXT("Ability_Human_Elite_AttackBuff"));
const FName FrogLineAbilityId(TEXT("Ability_Frog_Line"));
const FName FrogfolkLineAbilityId(TEXT("Ability_Frogfolk_Line"));
const FName RaincallerAbilityId(TEXT("Ability_Frog_Elite"));
const FName RaincallerAltAbilityId(TEXT("Ability_Frogfolk_Elite"));
} // namespace

USkaldAbilityComponent::USkaldAbilityComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);
    ReactionsRemaining = ReactionsPerRound;
    bHasInitialisedLoadout = false;
    LoadedAbilityDataTable = nullptr;
    bApplyViralLashOnNextAttack = false;
    bApplyScrapperFeintOnNextMiss = false;
    bApplyRallyingShotOnNextAttack = false;
    RallyingShotDesignatedAlly.Reset();
    bBrutalChargeActive = false;
    BrutalChargeDistanceMoved = 0;
    bRuneRiposteReady = false;
    bVeilStepBonusActive = false;
    bDeathlessAdvanceReady = false;
    bShieldWallPivotActive = false;
    ShieldWallPivotProtectedAlly.Reset();
    TacticalReservesBuffedThisRound.Empty();
    bHasPendingTacticalReservesAttackBuff = false;
    bEmpirePassiveDefenceBonusActive = false;
    bSmashThroughActive = false;
    bForgeguardBraceReady = false;
    bDeepDelveMortarPending = false;
    DeepDelveMortarTargetCell = FIntPoint(INDEX_NONE, INDEX_NONE);
    bMoonlanceFlurryActive = false;
    MoonlanceFlurryAttacksRemaining = 0;
    bStarfallInvocationPending = false;
    bGraveGraspPending = false;
    bSoulHarvestActive = false;
    bSoulHarvestKillSecured = false;
    bHarrierDashActive = false;
    bHarrierDashDefencePenaltyConsumed = false;
    bSuppressingFireActive = false;
    bRendAndTearActive = false;
    bArtilleryStrikePending = false;
    ArtilleryStrikeTargetCell = FIntPoint(INDEX_NONE, INDEX_NONE);
    bGoblinFlashBombActive = false;
    bGoblinNetActive = false;
    bGoblinAmbushActive = false;
    bGoblinAmbushPenaltyPending = false;
    bIgnoreDifficultTerrainForNextMove = false;
    BubbleWardSourceCount = 0;
    BubbleWardProtectionStacks = 0;
    WaaghRoarBuffCount = 0;
    HowlOfTheAlphaBuffCount = 0;
    CriticalHitThresholdOverride = 0;
    ViralLashCarriers.Empty();
    ViralLashDebuffedFighters.Reset();
    AberrantBloomHazardCells.Empty();
    AberrantBloomMovementDebuffed.Reset();
    AberrantBloomDamage = 0;
    RaincallerTemplateCells.Empty();
    RaincallerOccupants.Empty();
    RaincallerTemplateExpireRound = INDEX_NONE;
    RaincallerTemplateSourceId = NAME_None;

    bElfEvasionActive = false;
    bLizardPenaltyConsumedThisRound = false;
    bRavpackMomentumPending = false;
    bLowHealthStrengthPenaltyActive = false;
    bUndeadResilienceActive = false;
    DwarfPassiveAdjacentCount = 0;
    LastKnownHealth = 0;
    PassiveVisualStackCounts.Empty();
    FactionsThatAttackedOwnerThisRound.Reset();
    RaincallerAmphibiousStackCount = 0;

    SlotOrder = {ESkaldAbilitySlot::Ability1, ESkaldAbilitySlot::Ability2, ESkaldAbilitySlot::Ability3};
}

void USkaldAbilityComponent::BeginPlay()
{
    Super::BeginPlay();

    CachedFighter = Cast<AFighterPawn>(GetOwner());
    if (AFighterPawn* Fighter = CachedFighter.Get())
    {
        Fighter->OnHealthChanged.AddDynamic(this, &USkaldAbilityComponent::HandleOwnerHealthChanged);
        LastKnownHealth = Fighter->Stats.Health;
        Fighter->ClearAllPassiveBuffIndicators();
    }
    TryRegisterBattleDelegates();
}

void USkaldAbilityComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    ClearAllTraps();
    ClearAberrantBloomHazards();
    ClearRaincallerTemplate();
    RemoveBattleDelegates();
    if (GetOwnerRole() == ROLE_Authority)
    {
        while (ActiveTargetModifiers.Num() > 0)
        {
            const int32 LastIndex = ActiveTargetModifiers.Num() - 1;
            const FSkaldExternalTargetModifier Modifier = ActiveTargetModifiers[LastIndex];
            if (!Modifier.SourceAbilityId.IsNone())
            {
                RemoveTargetModifiersByAbilityId(Modifier.SourceAbilityId);
                continue;
            }

            if (AFighterPawn* Target = Modifier.Target.Get())
            {
                const FText AbilityName = FText::FromName(Modifier.SourceAbilityId);
                ApplyTargetStatDelta(Target, Modifier.Delta, false);
                Target->NotifyStatusEffectRemoved(Modifier.SourceAbilityId, AbilityName, Modifier.Delta);
            }

            ActiveTargetModifiers.RemoveAtSwap(LastIndex);
        }
    }
    if (AFighterPawn* Fighter = CachedFighter.Get())
    {
        Fighter->ClearAllPassiveBuffIndicators();
        Fighter->ClearFloatingStatusEffects();
    }
    Super::EndPlay(EndPlayReason);
}

void USkaldAbilityComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(USkaldAbilityComponent, PassiveAbility);
    DOREPLIFETIME(USkaldAbilityComponent, ReplicatedAbilitySlots);
    DOREPLIFETIME(USkaldAbilityComponent, ReactionsRemaining);
    DOREPLIFETIME(USkaldAbilityComponent, bHasInitialisedLoadout);
}

void USkaldAbilityComponent::RefreshAbilityLoadout(const FFighterStats& InStats, ESkaldFaction InFaction)
{
    if (!CachedFighter.IsValid())
    {
        CachedFighter = Cast<AFighterPawn>(GetOwner());
    }

    ClearAllTraps();
    AbilitySlots.Empty();
    bApplyViralLashOnNextAttack = false;
    bApplyScrapperFeintOnNextMiss = false;
    bApplyRallyingShotOnNextAttack = false;
    RallyingShotDesignatedAlly.Reset();
    bBrutalChargeActive = false;
    BrutalChargeDistanceMoved = 0;
    bRuneRiposteReady = false;
    bVeilStepBonusActive = false;
    bDeathlessAdvanceReady = false;
    bGoblinFlashBombActive = false;
    bGoblinNetActive = false;
    bGoblinAmbushActive = false;
    bGoblinAmbushPenaltyPending = false;
    bHarrierDashActive = false;
    bHarrierDashDefencePenaltyConsumed = false;
    bElfEvasionActive = false;
    bLizardPenaltyConsumedThisRound = false;
    bRavpackMomentumPending = false;
    bLowHealthStrengthPenaltyActive = false;
    bUndeadResilienceActive = false;
    DwarfPassiveAdjacentCount = 0;
    PassiveVisualStackCounts.Empty();
    FactionsThatAttackedOwnerThisRound.Reset();
    bEmpirePassiveDefenceBonusActive = false;
    if (AFighterPawn* Fighter = CachedFighter.Get())
    {
        Fighter->ClearAllPassiveBuffIndicators();
        Fighter->ClearFloatingStatusEffects();
        LastKnownHealth = Fighter->Stats.Health;
    }

    RemoveModifiersByAbilityId(LowHealthPenaltyId);
    RemoveModifiersByAbilityId(TEXT("Ability_Empire_Passive"));
    RemoveModifiersByAbilityId(TacticalReservesAttackBuffId);

    FSkaldFactionAbilitySet AbilitySet;
    const bool bFoundAbilitySet = TryResolveFactionAbilitySet(InFaction, AbilitySet);

    PassiveAbility = bFoundAbilitySet ? AbilitySet.Passive : FSkaldAbilityDefinition();

    if (bFoundAbilitySet)
    {
        const FSkaldAbilityDefinition ActiveAbility = ResolveActiveAbilityForCost(AbilitySet, InStats.ArmyCost);
        if (ActiveAbility.IsValid())
        {
            FSkaldAbilityState& SlotState = AbilitySlots.Add(ESkaldAbilitySlot::Ability1);
            SlotState.Definition = ActiveAbility;
            SlotState.CooldownRemaining = 0;
            SlotState.bHasBeenUsed = false;
            SlotState.bIsOnCooldown = false;

            if (GetOwnerRole() == ROLE_Authority && ActiveAbility.AbilityId == TEXT("Ability_Undead_Elite"))
            {
                bDeathlessAdvanceReady = true;
            }
        }
    }

    bHasInitialisedLoadout = true;
    ReactionsRemaining = ReactionsPerRound;

    UpdateReplicatedAbilitySlots();

    RefreshPassiveState();

    BroadcastStateChanged();
}

void ApplyDamageToFighter(AFighterPawn* Target, int32 Damage)
{
    if (!Target || Damage <= 0)
    {
        return;
    }

    const int32 PreviousHealth = Target->Stats.Health;
    Target->Stats.Health = FMath::Max(0, Target->Stats.Health - Damage);
    if (Target->Stats.Health != PreviousHealth)
    {
        Target->OnHealthChanged.Broadcast(Target->Stats.Health);
    }
}

void HealFighter(AFighterPawn* Target, int32 Amount)
{
    if (!Target || Amount <= 0)
    {
        return;
    }

    const int32 PreviousHealth = Target->Stats.Health;
    const int32 MaxHealth = Target->GetMaxHealth();
    const int32 NewHealth = MaxHealth > 0 ? FMath::Min(PreviousHealth + Amount, MaxHealth) : PreviousHealth + Amount;
    Target->Stats.Health = FMath::Max(0, NewHealth);
    if (Target->Stats.Health != PreviousHealth)
    {
        Target->OnHealthChanged.Broadcast(Target->Stats.Health);
    }
}

void USkaldAbilityComponent::HandleRoundStarted()
{
    for (auto& Pair : AbilitySlots)
    {
        FSkaldAbilityState& State = Pair.Value;
        if (State.CooldownRemaining > 0)
        {
            State.CooldownRemaining = FMath::Max(0, State.CooldownRemaining - 1);
        }
        State.bIsOnCooldown = State.CooldownRemaining > 0;
    }

    ReactionsRemaining = ReactionsPerRound;
    bApplyRallyingShotOnNextAttack = false;
    RallyingShotDesignatedAlly.Reset();
    bBrutalChargeActive = false;
    BrutalChargeDistanceMoved = 0;
    bRuneRiposteReady = false;
    bVeilStepBonusActive = false;
    bShieldWallPivotActive = false;
    ShieldWallPivotProtectedAlly.Reset();
    TacticalReservesBuffedThisRound.Empty();
    bSmashThroughActive = false;
    bForgeguardBraceReady = false;
    bDeepDelveMortarPending = false;
    DeepDelveMortarTargetCell = FIntPoint(INDEX_NONE, INDEX_NONE);
    bMoonlanceFlurryActive = false;
    MoonlanceFlurryAttacksRemaining = 0;
    bStarfallInvocationPending = false;
    bGraveGraspPending = false;
    bSoulHarvestActive = false;
    bSoulHarvestKillSecured = false;
    bHarrierDashActive = false;
    bSuppressingFireActive = false;
    bRendAndTearActive = false;
    bArtilleryStrikePending = false;
    ArtilleryStrikeTargetCell = FIntPoint(INDEX_NONE, INDEX_NONE);
    bGoblinFlashBombActive = false;
    bGoblinNetActive = false;
    bGoblinAmbushActive = false;
    bGoblinAmbushPenaltyPending = false;
    bLizardPenaltyConsumedThisRound = false;
    FactionsThatAttackedOwnerThisRound.Reset();
    UpdateViralLashCarriers();
    UpdateRaincallerTemplateLifetime();
    RefreshRaincallerOccupants();

    for (auto It = AberrantBloomMovementDebuffed.CreateIterator(); It; ++It)
    {
        if (!It->IsValid())
        {
            It.RemoveCurrent();
        }
    }

    if (GetOwnerRole() == ROLE_Authority)
    {
        for (int32 TrapIndex = ActiveTraps.Num() - 1; TrapIndex >= 0; --TrapIndex)
        {
            FSkaldAbilityTrapState& Trap = ActiveTraps[TrapIndex];
            if (Trap.bPendingPlacement)
            {
                continue;
            }

            if (Trap.RoundsRemaining > 0)
            {
                --Trap.RoundsRemaining;
                if (Trap.RoundsRemaining <= 0)
                {
                    RemoveTrapAtIndex(TrapIndex);
                    continue;
                }
            }
        }

        RemoveExpiredModifiers(ESkaldAbilityModifierPhase::RoundStart);
        RefreshPassiveState();
    }
    UpdateReplicatedAbilitySlots();
    BroadcastStateChanged();
}

void USkaldAbilityComponent::HandleActivationStarted()
{
    ReactionsRemaining = ReactionsPerRound;
    bOwnerAttackedThisActivation = false;
    BrutalChargeDistanceMoved = 0;
    bShieldWallPivotActive = false;
    ShieldWallPivotProtectedAlly.Reset();
    bSmashThroughActive = false;
    bForgeguardBraceReady = false;
    bDeepDelveMortarPending = false;
    bMoonlanceFlurryActive = false;
    MoonlanceFlurryAttacksRemaining = 0;
    bStarfallInvocationPending = false;
    bGraveGraspPending = false;
    bSoulHarvestActive = false;
    bSoulHarvestKillSecured = false;
    bHarrierDashActive = false;
    bSuppressingFireActive = false;
    bRendAndTearActive = false;
    bArtilleryStrikePending = false;
    bGoblinFlashBombActive = false;
    bGoblinNetActive = false;
    bGoblinAmbushActive = false;
    bGoblinAmbushPenaltyPending = false;
    if (GetOwnerRole() == ROLE_Authority)
    {
        RemoveExpiredModifiers(ESkaldAbilityModifierPhase::ActivationStart);

        if (PassiveAbility.AbilityId == TEXT("Ability_Ravpack_Passive") && bRavpackMomentumPending)
        {
            FSkaldActiveAbilityModifier Modifier;
            Modifier.SourceAbilityId = PassiveAbility.AbilityId;
            Modifier.Delta.Movement = 2;
            Modifier.bRemoveOnActivationEnd = true;
            AddActiveModifier(MoveTemp(Modifier));
            bRavpackMomentumPending = false;
        }

        ApplyActivationPassiveEffects();
        RefreshPassiveState();

        if (PassiveAbility.AbilityId == TEXT("Ability_Goblin_Passive"))
        {
            if (!CachedBattleManager.IsValid())
            {
                TryRegisterBattleDelegates();
            }

            AFighterPawn* OwnerFighter = CachedFighter.Get();
            if (OwnerFighter)
            {
                bool bHasNearbyAlly = false;
                if (CachedBattleManager.IsValid())
                {
                    const TArray<AFighterPawn*> Fighters = CachedBattleManager->GetInitiativeOrderSnapshot();
                    for (AFighterPawn* Fighter : Fighters)
                    {
                        if (!Fighter || Fighter == OwnerFighter || Fighter->Faction != OwnerFighter->Faction)
                        {
                            continue;
                        }

                        const int32 Distance = OwnerFighter->GetFootprintDistanceToFighter(Fighter);
                        if (Distance <= 2)
                        {
                            bHasNearbyAlly = true;
                            break;
                        }
                    }
                }

                FSkaldActiveAbilityModifier Modifier;
                Modifier.SourceAbilityId = PassiveAbility.AbilityId;
                if (bHasNearbyAlly)
                {
                    Modifier.Delta.AttackDice = 1;
                }
                else
                {
                    Modifier.Delta.Movement = 1;
                }
                Modifier.bRemoveOnActivationEnd = true;
                AddActiveModifier(MoveTemp(Modifier));
            }
        }
    }
    BroadcastStateChanged();
}

void USkaldAbilityComponent::HandleActivationFinished()
{
    if (GetOwnerRole() == ROLE_Authority)
    {
        RemoveExpiredModifiers(ESkaldAbilityModifierPhase::ActivationEnd);
        const bool bAttackedThisActivation = bOwnerAttackedThisActivation;

        if (bGoblinAmbushActive && bGoblinAmbushPenaltyPending && bAttackedThisActivation)
        {
            FSkaldActiveAbilityModifier Modifier;
            Modifier.SourceAbilityId = TEXT("Ability_Goblin_Elite");
            Modifier.Delta.Defence = -1;
            Modifier.bRemoveOnRoundStart = true;
            AddActiveModifier(MoveTemp(Modifier));
        }

        bGoblinAmbushActive = false;
        bGoblinAmbushPenaltyPending = false;
        bGoblinFlashBombActive = false;
        bGoblinNetActive = false;
        bOwnerAttackedThisActivation = false;
        bApplyViralLashOnNextAttack = false;
        bApplyScrapperFeintOnNextMiss = false;
        bApplyRallyingShotOnNextAttack = false;
        RallyingShotDesignatedAlly.Reset();
        bBrutalChargeActive = false;
        BrutalChargeDistanceMoved = 0;
        bVeilStepBonusActive = false;
        bShieldWallPivotActive = false;
        ShieldWallPivotProtectedAlly.Reset();
        bSmashThroughActive = false;
        bForgeguardBraceReady = false;
        bDeepDelveMortarPending = false;
        DeepDelveMortarTargetCell = FIntPoint(INDEX_NONE, INDEX_NONE);
        bMoonlanceFlurryActive = false;
        MoonlanceFlurryAttacksRemaining = 0;
        bStarfallInvocationPending = false;
        bGraveGraspPending = false;
        bSoulHarvestActive = false;
        bSoulHarvestKillSecured = false;
        bHarrierDashActive = false;
        bSuppressingFireActive = false;
        bRendAndTearActive = false;
        bArtilleryStrikePending = false;
        ArtilleryStrikeTargetCell = FIntPoint(INDEX_NONE, INDEX_NONE);
    }
    BroadcastStateChanged();
}

bool USkaldAbilityComponent::TryBeginAbility(ESkaldAbilitySlot Slot, FText& OutFailureReason)
{
    if (!CanActivateAbility(Slot, &OutFailureReason))
    {
        return false;
    }

    TryRegisterBattleDelegates();

    FSkaldAbilityState* State = AbilitySlots.Find(Slot);
    if (!State)
    {
        OutFailureReason = NSLOCTEXT("SkaldAbilities", "AbilityUnavailable", "No ability is assigned to that slot.");
        return false;
    }

    if (!ConsumeCost(State->Definition))
    {
        OutFailureReason = NSLOCTEXT("SkaldAbilities", "AbilityCostFailed", "Unable to pay the ability cost.");
        return false;
    }

    State->bHasBeenUsed = true;
    if (State->Definition.CooldownRounds > 0)
    {
        State->CooldownRemaining = State->Definition.CooldownRounds;
        State->bIsOnCooldown = true;
    }

    UpdateReplicatedAbilitySlots();

    if (GetOwnerRole() == ROLE_Authority)
    {
        MulticastAbilityTriggered(State->Definition);
    }
    else
    {
        HandleAbilityTriggeredLocal(State->Definition);
    }

    BroadcastStateChanged();
    return true;
}

bool USkaldAbilityComponent::CanActivateAbility(ESkaldAbilitySlot Slot, FText* OutFailureReason) const
{
    FText LocalReason;
    FText& Failure = OutFailureReason ? *OutFailureReason : LocalReason;

    if (!bHasInitialisedLoadout)
    {
        Failure = NSLOCTEXT("SkaldAbilities", "AbilityNotInitialised", "Ability loadout not ready.");
        return false;
    }

    const FSkaldAbilityState* State = AbilitySlots.Find(Slot);
    if (!State || !State->Definition.IsValid())
    {
        Failure = NSLOCTEXT("SkaldAbilities", "AbilityUnavailable", "No ability is assigned to that slot.");
        return false;
    }

    if (!CanTriggerAbility(*State, Failure))
    {
        return false;
    }

    return CanPayCost(State->Definition, Failure);
}

bool USkaldAbilityComponent::TryRefreshReaction()
{
    if (GetOwnerRole() != ROLE_Authority)
    {
        return false;
    }

    if (ReactionsRemaining >= ReactionsPerRound)
    {
        return false;
    }

    ++ReactionsRemaining;
    BroadcastStateChanged();
    return true;
}

void USkaldAbilityComponent::ForceSpendAllReactions()
{
    if (GetOwnerRole() != ROLE_Authority)
    {
        return;
    }

    if (ReactionsRemaining > 0)
    {
        ReactionsRemaining = 0;
        BroadcastStateChanged();
    }
}

void USkaldAbilityComponent::MarkTacticalReservesAttackBuffPending()
{
    bHasPendingTacticalReservesAttackBuff = true;
}

void USkaldAbilityComponent::ClearTacticalReservesAttackBuff()
{
    bHasPendingTacticalReservesAttackBuff = false;
}

void USkaldAbilityComponent::GetAbilityStates(TArray<FSkaldAbilityState>& OutStates) const
{
    OutStates.Reset();
    for (ESkaldAbilitySlot Slot : SlotOrder)
    {
        if (const FSkaldAbilityState* State = AbilitySlots.Find(Slot))
        {
            OutStates.Add(*State);
        }
    }
}

const FSkaldAbilityState* USkaldAbilityComponent::FindAbilityState(ESkaldAbilitySlot Slot) const
{
    return AbilitySlots.Find(Slot);
}

void USkaldAbilityComponent::OnRep_AbilitySlots()
{
    AbilitySlots.Empty();
    for (const FSkaldReplicatedAbilitySlotState& Entry : ReplicatedAbilitySlots)
    {
        AbilitySlots.Add(Entry.Slot, Entry.State);
    }

    BroadcastStateChanged();
}

void USkaldAbilityComponent::BroadcastStateChanged()
{
    OnAbilityStateChanged.Broadcast(this);
}

bool USkaldAbilityComponent::CanPayCost(const FSkaldAbilityDefinition& Definition, FText& OutError) const
{
    const AFighterPawn* Fighter = CachedFighter.Get();
    if (!Fighter)
    {
        OutError = NSLOCTEXT("SkaldAbilities", "AbilityNoOwner", "Ability owner missing.");
        return false;
    }

    switch (Definition.CostType)
    {
    case ESkaldAbilityCostType::Action:
        if (!Fighter->IsCurrentlyActive() || Fighter->GetActionsRemaining() <= 0)
        {
            OutError = NSLOCTEXT("SkaldAbilities", "AbilityNeedsAction", "No actions remaining.");
            return false;
        }
        break;
    case ESkaldAbilityCostType::Reaction:
        if (ReactionsRemaining <= 0)
        {
            OutError = NSLOCTEXT("SkaldAbilities", "AbilityNeedsReaction", "Reaction already spent.");
            return false;
        }
        break;
    case ESkaldAbilityCostType::Free:
    default:
        break;
    }

    return true;
}

bool USkaldAbilityComponent::ConsumeCost(const FSkaldAbilityDefinition& Definition)
{
    AFighterPawn* Fighter = CachedFighter.Get();
    if (!Fighter)
    {
        return false;
    }

    switch (Definition.CostType)
    {
    case ESkaldAbilityCostType::Action:
        return Fighter->ConsumeAction();
    case ESkaldAbilityCostType::Reaction:
        if (ReactionsRemaining > 0)
        {
            --ReactionsRemaining;
            return true;
        }
        return false;
    case ESkaldAbilityCostType::Free:
    default:
        return true;
    }
}

bool USkaldAbilityComponent::CanTriggerAbility(const FSkaldAbilityState& State, FText& OutError) const
{
    if (State.Definition.bOncePerBattle && State.bHasBeenUsed)
    {
        OutError = NSLOCTEXT("SkaldAbilities", "AbilityOncePerBattle", "This ability can only be used once per battle.");
        return false;
    }

    if (State.bIsOnCooldown)
    {
        OutError = NSLOCTEXT("SkaldAbilities", "AbilityOnCooldown", "Ability is on cooldown.");
        return false;
    }

    return true;
}

void USkaldAbilityComponent::SetPendingAbilityContext(const FSkaldAbilityContext& Context)
{
    PendingAbilityContext = Context;
}

void USkaldAbilityComponent::ClearPendingAbilityContext()
{
    PendingAbilityContext.Reset();
}

const FSkaldAbilityContext* USkaldAbilityComponent::GetPendingAbilityContext() const
{
    return PendingAbilityContext.IsSet() ? &PendingAbilityContext.GetValue() : nullptr;
}

void USkaldAbilityComponent::HandleAbilityTriggeredLocal(const FSkaldAbilityDefinition& Definition)
{
    PlayAbilityFeedback(Definition);
    const bool bShouldApplyEffects = (GetOwnerRole() == ROLE_Authority) && !bSuppressAbilityEffectOnNextTrigger;
    if (bShouldApplyEffects)
    {
        ApplyAbilityEffects(Definition);
    }

    if (bSuppressAbilityEffectOnNextTrigger)
    {
        bSuppressAbilityEffectOnNextTrigger = false;
    }
    OnAbilityTriggered.Broadcast(this, Definition);
    ClearPendingAbilityContext();
}

void USkaldAbilityComponent::MulticastAbilityTriggered_Implementation(const FSkaldAbilityDefinition& Definition)
{
    HandleAbilityTriggeredLocal(Definition);
}

void USkaldAbilityComponent::MulticastTrapPlaced_Implementation(const FIntPoint& Cell)
{
    if (GetOwnerRole() == ROLE_Authority)
    {
        return;
    }

    SpawnTrapVisualAtCell(Cell);
}

void USkaldAbilityComponent::MulticastTrapRemoved_Implementation(const FIntPoint& Cell)
{
    if (GetOwnerRole() == ROLE_Authority)
    {
        return;
    }

    if (AFighterPawn* OwnerFighter = CachedFighter.Get())
    {
        if (UGridOverlayComponent* Grid = OwnerFighter->GetGrid())
        {
            Grid->RemoveTrapMarker(Cell);
        }
    }
}

void USkaldAbilityComponent::PlayAbilityFeedback(const FSkaldAbilityDefinition& Definition)
{
    AFighterPawn* Fighter = CachedFighter.Get();
    if (!Fighter)
    {
        return;
    }

    const FSkaldAbilityVisuals& Visuals = Definition.Visuals;
    if (!Visuals.HasAnyVisuals())
    {
        return;
    }

    if (!Visuals.Montage.IsNull())
    {
        if (UAnimMontage* Montage = Visuals.Montage.LoadSynchronous())
        {
            if (USkeletalMeshComponent* SkeletalMesh = Fighter->FindComponentByClass<USkeletalMeshComponent>())
            {
                if (UAnimInstance* AnimInstance = SkeletalMesh->GetAnimInstance())
                {
                    AnimInstance->Montage_Play(Montage);
                }
            }
        }
    }

    if (!Visuals.NiagaraEffect.IsNull())
    {
        UNiagaraSystem* NiagaraTemplate = Visuals.NiagaraEffect.LoadSynchronous();
        if (NiagaraTemplate)
        {
            UNiagaraFunctionLibrary::SpawnSystemAttached(
                NiagaraTemplate,
                Fighter->GetRootComponent(),
                NAME_None,
                FVector::ZeroVector,
                FRotator::ZeroRotator,
                EAttachLocation::KeepRelativeOffset,
                true);
        }
    }

    if (!Visuals.Sound.IsNull())
    {
        if (USoundBase* Cue = Visuals.Sound.LoadSynchronous())
        {
            UGameplayStatics::SpawnSoundAttached(
                Cue,
                Fighter->GetRootComponent());
        }
    }
}

void USkaldAbilityComponent::ApplyAbilityEffects(const FSkaldAbilityDefinition& Definition)
{
    if (!Definition.IsValid())
    {
        return;
    }

    if (Definition.AbilityId == TEXT("Ability_Human_Skirmish"))
    {
        if (GetOwnerRole() == ROLE_Authority)
        {
            bApplyRallyingShotOnNextAttack = true;
            AFighterPawn* OwnerFighter = CachedFighter.Get();
            RallyingShotDesignatedAlly.Reset();
            if (OwnerFighter)
            {
                AFighterPawn* PreferredAlly = ResolveAbilityTargetFromContext(Definition.AbilityId);
                if (IsValidRallyingShotAlly(OwnerFighter, PreferredAlly))
                {
                    RallyingShotDesignatedAlly = PreferredAlly;
                }
            }
        }
    }
    else if (Definition.AbilityId == TEXT("Ability_Orc_Skirmish"))
    {
        if (GetOwnerRole() == ROLE_Authority)
        {
            bBrutalChargeActive = true;
            BrutalChargeDistanceMoved = 0;

            FSkaldActiveAbilityModifier Modifier;
            Modifier.SourceAbilityId = Definition.AbilityId;
            Modifier.Delta.Movement = 2;
            Modifier.bRemoveOnActivationEnd = true;
            AddActiveModifier(MoveTemp(Modifier));
        }
    }
    else if (Definition.AbilityId == TEXT("Ability_Dwarf_Line"))
    {
        if (GetOwnerRole() == ROLE_Authority)
        {
            bRuneRiposteReady = true;
        }
    }
    else if (Definition.AbilityId == TEXT("Ability_Elf_Skirmish"))
    {
        if (GetOwnerRole() == ROLE_Authority)
        {
            bVeilStepBonusActive = true;

            FSkaldActiveAbilityModifier Modifier;
            Modifier.SourceAbilityId = Definition.AbilityId;
            Modifier.Delta.AttackRange = 1;
            Modifier.bRemoveOnActivationEnd = true;
            AddActiveModifier(MoveTemp(Modifier));
        }
    }
    else if (Definition.AbilityId == TEXT("Ability_Undead_Elite"))
    {
        if (GetOwnerRole() == ROLE_Authority)
        {
            bDeathlessAdvanceReady = true;
        }
    }
    else if (Definition.AbilityId == TEXT("Ability_Inflicted_Skirmish"))
    {
        if (GetOwnerRole() == ROLE_Authority)
        {
            bApplyViralLashOnNextAttack = true;
        }
    }
    else if (Definition.AbilityId == TEXT("Ability_Inflicted_Line"))
    {
        FSkaldActiveAbilityModifier Modifier;
        Modifier.SourceAbilityId = Definition.AbilityId;
        Modifier.Delta.AttackDice = 2;
        Modifier.Delta.Defence = -1;
        Modifier.bRemoveOnActivationStart = true;
        AddActiveModifier(MoveTemp(Modifier));
    }
    else if (Definition.AbilityId == TEXT("Ability_Ravpack_Skirmish"))
    {
        if (GetOwnerRole() == ROLE_Authority)
        {
            bApplyScrapperFeintOnNextMiss = true;
        }
    }
    else if (Definition.AbilityId == TEXT("Ability_Ravpack_Elite"))
    {
        FSkaldActiveAbilityModifier Modifier;
        Modifier.SourceAbilityId = Definition.AbilityId;
        Modifier.Delta.AttackDice = 1;
        Modifier.Delta.Movement = 1;
        Modifier.bRemoveOnActivationEnd = true;
        Modifier.bDealSelfDamageOnActivationEndIfAttack = true;
        Modifier.SelfDamageAmount = 1;
        AddActiveModifier(MoveTemp(Modifier));
    }
    else if (Definition.AbilityId == TEXT("Ability_Human_Line"))
    {
        if (GetOwnerRole() == ROLE_Authority)
        {
            bShieldWallPivotActive = true;
            ShieldWallPivotProtectedAlly.Reset();

            AFighterPawn* OwnerFighter = CachedFighter.Get();
            if (OwnerFighter && CachedBattleManager.IsValid())
            {
                AFighterPawn* SelectedAlly = ResolveAbilityTargetFromContext(Definition.AbilityId);
                const auto IsValidShieldWallAlly = [OwnerFighter](AFighterPawn* Candidate)
                {
                    if (!Candidate || Candidate == OwnerFighter)
                    {
                        return false;
                    }

                    if (Candidate->Faction != OwnerFighter->Faction)
                    {
                        return false;
                    }

                    return OwnerFighter->GetFootprintDistanceToFighter(Candidate) <= 1;
                };

                if (!IsValidShieldWallAlly(SelectedAlly))
                {
                    const TArray<AFighterPawn*> Fighters = CachedBattleManager->GetInitiativeOrderSnapshot();
                    AFighterPawn* BestAlly = nullptr;
                    int32 BestDistance = TNumericLimits<int32>::Max();
                    for (AFighterPawn* Fighter : Fighters)
                    {
                        if (!IsValidShieldWallAlly(Fighter))
                        {
                            continue;
                        }

                        const int32 Distance = OwnerFighter->GetFootprintDistanceToFighter(Fighter);
                        if (Distance < BestDistance)
                        {
                            BestAlly = Fighter;
                            BestDistance = Distance;
                        }
                    }

                    SelectedAlly = BestAlly;
                }

                if (IsValidShieldWallAlly(SelectedAlly))
                {
                    ShieldWallPivotProtectedAlly = SelectedAlly;

                    FSkaldActiveAbilityModifier OwnerModifier;
                    OwnerModifier.SourceAbilityId = Definition.AbilityId;
                    OwnerModifier.Delta.Defence = 1;
                    OwnerModifier.bRemoveOnActivationStart = true;
                    AddActiveModifier(MoveTemp(OwnerModifier));

                    FSkaldActiveAbilityModifier AllyModifier;
                    AllyModifier.SourceAbilityId = Definition.AbilityId;
                    AllyModifier.Delta.Defence = 1;
                    AllyModifier.bRemoveOnActivationStart = true;
                    ApplyModifierToTarget(SelectedAlly, MoveTemp(AllyModifier));

                    if (!TrySwapAdjacentWithAlly(OwnerFighter, SelectedAlly))
                    {
                        TryShiftFighterOneCell(SelectedAlly, OwnerFighter);
                    }
                }
            }
        }
    }
    else if (Definition.AbilityId == TEXT("Ability_Human_Elite"))
    {
        if (GetOwnerRole() == ROLE_Authority)
        {
            AFighterPawn* OwnerFighter = CachedFighter.Get();
            if (OwnerFighter && CachedBattleManager.IsValid())
            {
                constexpr int32 TacticalRange = 5;
                const auto IsValidTacticalReservesTarget = [OwnerFighter](AFighterPawn* Candidate)
                {
                    if (!Candidate || Candidate == OwnerFighter)
                    {
                        return false;
                    }

                    if (Candidate->Faction != OwnerFighter->Faction || Candidate->Stats.Health <= 0)
                    {
                        return false;
                    }

                    return OwnerFighter->GetFootprintDistanceToFighter(Candidate) <= TacticalRange;
                };

                TArray<AFighterPawn*> Fighters = CachedBattleManager->GetInitiativeOrderSnapshot();
                Fighters.RemoveAll([&](AFighterPawn* Fighter)
                    { return !IsValidTacticalReservesTarget(Fighter); });

                Algo::SortBy(Fighters, [OwnerFighter](AFighterPawn* Fighter)
                    { return OwnerFighter->GetFootprintDistanceToFighter(Fighter); });

                TArray<AFighterPawn*> AlliesToBuff;
                AlliesToBuff.Reserve(2);

                auto TryAddAlly = [&](AFighterPawn* Candidate)
                {
                    if (!Candidate || AlliesToBuff.Num() >= 2)
                    {
                        return;
                    }

                    if (!IsValidTacticalReservesTarget(Candidate))
                    {
                        return;
                    }

                    if (TacticalReservesBuffedThisRound.Contains(Candidate))
                    {
                        return;
                    }

                    if (AlliesToBuff.Contains(Candidate))
                    {
                        return;
                    }

                    AlliesToBuff.Add(Candidate);
                };

                AFighterPawn* PreferredAlly = ResolveAbilityTargetFromContext(Definition.AbilityId);
                TryAddAlly(PreferredAlly);

                for (AFighterPawn* Fighter : Fighters)
                {
                    TryAddAlly(Fighter);
                    if (AlliesToBuff.Num() >= 2)
                    {
                        break;
                    }
                }

                for (AFighterPawn* Ally : AlliesToBuff)
                {
                    if (!Ally)
                    {
                        continue;
                    }

                    FSkaldActiveAbilityModifier AttackBuff;
                    AttackBuff.SourceAbilityId = TacticalReservesAttackBuffId;
                    AttackBuff.Delta.AttackDice = 1;
                    AttackBuff.bRemoveOnRoundStart = true;
                    ApplyModifierToTarget(Ally, MoveTemp(AttackBuff));

                    if (USkaldAbilityComponent* AllyAbility = Ally->GetAbilityComponent())
                    {
                        AllyAbility->MarkTacticalReservesAttackBuffPending();
                    }

                    TryShiftFighterMultipleCells(Ally, OwnerFighter, 2);
                    TacticalReservesBuffedThisRound.Add(Ally);
                }
            }
        }
    }
    else if (Definition.AbilityId == TEXT("Ability_Orc_Line"))
    {
        if (GetOwnerRole() == ROLE_Authority)
        {
            bSmashThroughActive = true;
        }
    }
    else if (Definition.AbilityId == TEXT("Ability_Orc_Elite"))
    {
        if (GetOwnerRole() == ROLE_Authority)
        {
            AFighterPawn* OwnerFighter = CachedFighter.Get();
            if (OwnerFighter && CachedBattleManager.IsValid())
            {
                const TArray<AFighterPawn*> Fighters = CachedBattleManager->GetInitiativeOrderSnapshot();
                for (AFighterPawn* Fighter : Fighters)
                {
                    if (!Fighter || Fighter->Faction != OwnerFighter->Faction)
                    {
                        continue;
                    }

                    FSkaldActiveAbilityModifier Modifier;
                    Modifier.SourceAbilityId = Definition.AbilityId;
                    Modifier.Delta.AttackDice = 1;
                    Modifier.Delta.Movement = 2;
                    Modifier.RemainingRounds = 1;
                    Modifier.bRemoveOnRoundStart = true;
                    ApplyModifierToTarget(Fighter, MoveTemp(Modifier));
                }
            }

            ConsumeOncePerBattleAbility(Definition.AbilityId);
        }
    }
    else if (Definition.AbilityId == TEXT("Ability_Dwarf_Skirmish"))
    {
        if (GetOwnerRole() == ROLE_Authority)
        {
            bForgeguardBraceReady = true;

            FSkaldActiveAbilityModifier Modifier;
            Modifier.SourceAbilityId = Definition.AbilityId;
            Modifier.Delta.Defence = 2;
            Modifier.bRemoveOnActivationEnd = true;
            AddActiveModifier(MoveTemp(Modifier));
        }
    }
    else if (Definition.AbilityId == TEXT("Ability_Dwarf_Elite"))
    {
        if (GetOwnerRole() == ROLE_Authority)
        {
            bDeepDelveMortarPending = true;
            FIntPoint TargetCell;
            if (ResolveAbilityTargetCellFromContext(Definition.AbilityId, TargetCell))
            {
                DeepDelveMortarTargetCell = TargetCell;
            }
            else if (AFighterPawn* OwnerFighter = CachedFighter.Get())
            {
                DeepDelveMortarTargetCell = OwnerFighter->GetCurrentCell();
            }

            FSkaldActiveAbilityModifier Modifier;
            Modifier.SourceAbilityId = Definition.AbilityId;
            Modifier.Delta.AttackDice = -1;
            Modifier.bRemoveOnActivationEnd = true;
            AddActiveModifier(MoveTemp(Modifier));
        }
    }
    else if (Definition.AbilityId == TEXT("Ability_Elf_Line"))
    {
        if (GetOwnerRole() == ROLE_Authority)
        {
            bMoonlanceFlurryActive = true;
            MoonlanceFlurryAttacksRemaining = 2;
            RemoveModifiersByAbilityId(Definition.AbilityId);

            FSkaldActiveAbilityModifier Modifier;
            Modifier.SourceAbilityId = Definition.AbilityId;
            Modifier.Delta.AttackDice = -1;
            Modifier.bRemoveOnActivationEnd = true;
            AddActiveModifier(MoveTemp(Modifier));
        }
    }
    else if (Definition.AbilityId == TEXT("Ability_Elf_Elite"))
    {
        if (GetOwnerRole() == ROLE_Authority)
        {
            bStarfallInvocationPending = true;

            FSkaldActiveAbilityModifier Modifier;
            Modifier.SourceAbilityId = Definition.AbilityId;
            Modifier.Delta.AttackDice = 1;
            if (AFighterPawn* OwnerFighter = CachedFighter.Get())
            {
                const int32 DesiredRange = 8;
                const int32 RangeDelta = FMath::Max(0, DesiredRange - OwnerFighter->Stats.AttackRange);
                if (RangeDelta > 0)
                {
                    Modifier.Delta.AttackRange = RangeDelta;
                }
            }
            Modifier.bRemoveOnActivationEnd = true;
            AddActiveModifier(MoveTemp(Modifier));
        }
    }
    else if (Definition.AbilityId == TEXT("Ability_Lizardfolk_Skirmish")
        || Definition.AbilityId == TEXT("Ability_Lizard_Skirmish"))
    {
        if (GetOwnerRole() == ROLE_Authority)
        {
            AFighterPawn* OwnerFighter = CachedFighter.Get();
            if (OwnerFighter && CachedBattleManager.IsValid())
            {
                const TArray<AFighterPawn*> Fighters = CachedBattleManager->GetInitiativeOrderSnapshot();
                for (AFighterPawn* Fighter : Fighters)
                {
                    if (!Fighter || Fighter->Faction == OwnerFighter->Faction)
                    {
                        continue;
                    }

                    const int32 Distance = OwnerFighter->GetFootprintDistanceToFighter(Fighter);
                    if (Distance <= 1)
                    {
                        FSkaldActiveAbilityModifier Modifier;
                        Modifier.SourceAbilityId = Definition.AbilityId;
                        Modifier.Delta.Movement = -1;
                        Modifier.bRemoveOnRoundStart = true;
                        ApplyModifierToTarget(Fighter, MoveTemp(Modifier));
                    }
                }
            }

            FSkaldActiveAbilityModifier OwnerModifier;
            OwnerModifier.SourceAbilityId = Definition.AbilityId;
            OwnerModifier.Delta.AttackDice = -1;
            OwnerModifier.bRemoveOnActivationEnd = true;
            AddActiveModifier(MoveTemp(OwnerModifier));
        }
    }
    else if (Definition.AbilityId == TEXT("Ability_Lizardfolk_Line")
        || Definition.AbilityId == TEXT("Ability_Lizard_Line"))
    {
        if (CanApplyAmphibiousRushBonus(Definition))
        {
            FSkaldActiveAbilityModifier Modifier;
            Modifier.SourceAbilityId = Definition.AbilityId;
            Modifier.Delta.Movement = 2;
            Modifier.Delta.AttackDice = 1;
            Modifier.bRemoveOnActivationEnd = true;
            AddActiveModifier(MoveTemp(Modifier));
        }
    }
    else if (Definition.AbilityId == TEXT("Ability_Lizardfolk_Elite")
        || Definition.AbilityId == TEXT("Ability_Lizard_Elite"))
    {
        if (GetOwnerRole() == ROLE_Authority)
        {
            if (AFighterPawn* Fighter = CachedFighter.Get())
            {
                const int32 HealAmount = FMath::RandRange(1, 3);
                HealFighter(Fighter, HealAmount);

                for (int32 Index = ActiveModifiers.Num() - 1; Index >= 0; --Index)
                {
                    const FSkaldAbilityStatDelta& Delta = ActiveModifiers[Index].Delta;
                    if (Delta.AttackDice < 0 || Delta.AttackDamage < 0 || Delta.Movement < 0 || Delta.Defence < 0
                        || Delta.Strength < 0 || Delta.CriticalBonusDamage < 0)
                    {
                        RemoveActiveModifier(Index);
                        break;
                    }
                }
            }
        }
    }
    else if (Definition.AbilityId == TEXT("Ability_Undead_Skirmish"))
    {
        if (GetOwnerRole() == ROLE_Authority)
        {
            bGraveGraspPending = true;
        }
    }
    else if (Definition.AbilityId == TEXT("Ability_Undead_Line"))
    {
        if (GetOwnerRole() == ROLE_Authority)
        {
            bSoulHarvestActive = true;
            bSoulHarvestKillSecured = false;
        }
    }
    else if (Definition.AbilityId == TEXT("Ability_Gnoll_Skirmish"))
    {
        if (GetOwnerRole() == ROLE_Authority)
        {
            bHarrierDashActive = true;
        }
    }
    else if (Definition.AbilityId == TEXT("Ability_Gnoll_Line"))
    {
        if (GetOwnerRole() == ROLE_Authority)
        {
            AFighterPawn* OwnerFighter = CachedFighter.Get();
            if (OwnerFighter && CachedBattleManager.IsValid())
            {
                const TArray<AFighterPawn*> Fighters = CachedBattleManager->GetInitiativeOrderSnapshot();
                for (AFighterPawn* Fighter : Fighters)
                {
                    if (!Fighter || Fighter->Faction != OwnerFighter->Faction)
                    {
                        continue;
                    }

                    const int32 Distance = OwnerFighter->GetFootprintDistanceToFighter(Fighter);
                    if (Distance <= 4)
                    {
                        FSkaldActiveAbilityModifier Modifier;
                        Modifier.SourceAbilityId = Definition.AbilityId;
                        Modifier.Delta.Movement = 1;
                        Modifier.RemainingRounds = 1;
                        Modifier.bRemoveOnRoundStart = true;
                        ApplyModifierToTarget(Fighter, MoveTemp(Modifier));
                    }
                }
            }
        }
    }
    else if (Definition.AbilityId == TEXT("Ability_Gnoll_Elite"))
    {
        if (GetOwnerRole() == ROLE_Authority)
        {
            bRendAndTearActive = true;
        }
    }
    else if (Definition.AbilityId == TEXT("Ability_Goblin_Skirmish"))
    {
        if (GetOwnerRole() == ROLE_Authority)
        {
            bGoblinFlashBombActive = true;
            RemoveModifiersByAbilityId(Definition.AbilityId);

            FSkaldActiveAbilityModifier Modifier;
            Modifier.SourceAbilityId = Definition.AbilityId;
            Modifier.Delta.AttackDamage = -1;
            Modifier.bRemoveOnActivationEnd = true;
            AddActiveModifier(MoveTemp(Modifier));
        }
    }
    else if (Definition.AbilityId == TEXT("Ability_Goblin_Line"))
    {
        if (GetOwnerRole() == ROLE_Authority)
        {
            bGoblinNetActive = true;
        }
    }
    else if (Definition.AbilityId == TEXT("Ability_Goblin_Elite"))
    {
        if (GetOwnerRole() == ROLE_Authority)
        {
            RemoveModifiersByAbilityId(Definition.AbilityId);

            FSkaldActiveAbilityModifier Modifier;
            Modifier.SourceAbilityId = Definition.AbilityId;
            Modifier.Delta.AttackDice = 1;
            Modifier.Delta.Movement = 2;
            Modifier.bRemoveOnActivationEnd = true;
            AddActiveModifier(MoveTemp(Modifier));

            bGoblinAmbushActive = true;
            bGoblinAmbushPenaltyPending = true;
        }
    }
    else if (Definition.AbilityId == TEXT("Ability_Empire_Skirmish"))
    {
        if (GetOwnerRole() == ROLE_Authority)
        {
            bSuppressingFireActive = true;

            FSkaldActiveAbilityModifier Modifier;
            Modifier.SourceAbilityId = Definition.AbilityId;
            Modifier.Delta.AttackDamage = -1;
            Modifier.bRemoveOnActivationEnd = true;
            AddActiveModifier(MoveTemp(Modifier));
        }
    }
    else if (Definition.AbilityId == TEXT("Ability_Empire_Line"))
    {
        if (GetOwnerRole() == ROLE_Authority)
        {
            AFighterPawn* OwnerFighter = CachedFighter.Get();
            if (OwnerFighter && CachedBattleManager.IsValid())
            {
                const TArray<AFighterPawn*> Fighters = CachedBattleManager->GetInitiativeOrderSnapshot();
                AFighterPawn* BestAlly = nullptr;
                int32 BestDistance = TNumericLimits<int32>::Max();
                for (AFighterPawn* Fighter : Fighters)
                {
                    if (!Fighter || Fighter == OwnerFighter || Fighter->Faction != OwnerFighter->Faction)
                    {
                        continue;
                    }

                    const int32 Distance = OwnerFighter->GetFootprintDistanceToFighter(Fighter);
                    if (Distance <= 1 && Distance < BestDistance)
                    {
                        BestAlly = Fighter;
                        BestDistance = Distance;
                    }
                }

                if (BestAlly)
                {
                    FSkaldActiveAbilityModifier Modifier;
                    Modifier.SourceAbilityId = Definition.AbilityId;
                    Modifier.Delta.AttackDice = 1;
                    Modifier.bRemoveOnActivationEnd = true;
                    ApplyModifierToTarget(BestAlly, MoveTemp(Modifier));
                }
            }
        }
    }
    else if (Definition.AbilityId == TEXT("Ability_Empire_Elite"))
    {
        if (GetOwnerRole() == ROLE_Authority)
        {
            bArtilleryStrikePending = true;
            FIntPoint TargetCell;
            if (ResolveAbilityTargetCellFromContext(Definition.AbilityId, TargetCell))
            {
                ArtilleryStrikeTargetCell = TargetCell;
            }
            else if (AFighterPawn* OwnerFighter = CachedFighter.Get())
            {
                ArtilleryStrikeTargetCell = OwnerFighter->GetCurrentCell();
            }

            FSkaldActiveAbilityModifier Modifier;
            Modifier.SourceAbilityId = Definition.AbilityId;
            Modifier.Delta.AttackDamage = 2;
            Modifier.bRemoveOnActivationEnd = true;
            AddActiveModifier(MoveTemp(Modifier));
        }
    }
    else if (Definition.AbilityId == TEXT("Ability_Inflicted_Elite"))
    {
        if (GetOwnerRole() == ROLE_Authority)
        {
            SpawnAberrantBloomHazard(Definition);
            ConsumeOncePerBattleAbility(Definition.AbilityId);
        }
    }
    else if (Definition.AbilityId == TEXT("Ability_Frogfolk_Skirmish")
        || Definition.AbilityId == TEXT("Ability_Frog_Skirmish"))
    {
        if (GetOwnerRole() == ROLE_Authority)
        {
            AFighterPawn* OwnerFighter = CachedFighter.Get();
            if (OwnerFighter && CachedBattleManager.IsValid())
            {
                const auto IsValidTongueSnareTarget = [OwnerFighter](AFighterPawn* Candidate)
                {
                    if (!Candidate || Candidate->Faction == OwnerFighter->Faction || Candidate->Stats.Health <= 0)
                    {
                        return false;
                    }

                    return OwnerFighter->GetFootprintDistanceToFighter(Candidate) <= 4;
                };

                AFighterPawn* Target = ResolveAbilityTargetFromContext(Definition.AbilityId);
                if (!IsValidTongueSnareTarget(Target))
                {
                    const TArray<AFighterPawn*> Fighters = CachedBattleManager->GetInitiativeOrderSnapshot();
                    AFighterPawn* BestEnemy = nullptr;
                    int32 BestDistance = TNumericLimits<int32>::Max();
                    for (AFighterPawn* Fighter : Fighters)
                    {
                        if (!IsValidTongueSnareTarget(Fighter))
                        {
                            continue;
                        }

                        const int32 Distance = OwnerFighter->GetFootprintDistanceToFighter(Fighter);
                        if (Distance < BestDistance)
                        {
                            BestEnemy = Fighter;
                            BestDistance = Distance;
                        }
                    }

                    Target = BestEnemy;
                }

                if (IsValidTongueSnareTarget(Target))
                {
                    FSkaldActiveAbilityModifier Modifier;
                    Modifier.SourceAbilityId = Definition.AbilityId;
                    Modifier.Delta.AttackDice = -1;
                    Modifier.bRemoveOnRoundStart = true;
                    ApplyModifierToTarget(Target, MoveTemp(Modifier));

                    TryPullFighterOneCellTowards(OwnerFighter, Target);
                }
            }
        }
    }
    else if (Definition.AbilityId == FrogfolkLineAbilityId
        || Definition.AbilityId == FrogLineAbilityId)
    {
        if (GetOwnerRole() == ROLE_Authority)
        {
            AFighterPawn* OwnerFighter = CachedFighter.Get();
            if (OwnerFighter && CachedBattleManager.IsValid())
            {
                const TArray<AFighterPawn*> Fighters = CachedBattleManager->GetInitiativeOrderSnapshot();
                AFighterPawn* BestAlly = nullptr;
                int32 BestDistance = TNumericLimits<int32>::Max();
                for (AFighterPawn* Fighter : Fighters)
                {
                    if (!Fighter || Fighter->Faction != OwnerFighter->Faction || Fighter == OwnerFighter)
                    {
                        continue;
                    }

                    const int32 Distance = OwnerFighter->GetFootprintDistanceToFighter(Fighter);
                    if (Distance <= 3 && Distance < BestDistance)
                    {
                        BestAlly = Fighter;
                        BestDistance = Distance;
                    }
                }

                if (BestAlly)
                {
                    FSkaldActiveAbilityModifier Modifier;
                    Modifier.SourceAbilityId = Definition.AbilityId;
                    Modifier.Delta.Defence = 1;
                    Modifier.RemainingRounds = 1;
                    Modifier.bRemoveOnRoundStart = true;
                    ApplyModifierToTarget(BestAlly, MoveTemp(Modifier));

                    HealFighter(BestAlly, 1);
                }
            }
        }
    }
    else if (Definition.AbilityId == RaincallerAltAbilityId
        || Definition.AbilityId == RaincallerAbilityId)
    {
        if (GetOwnerRole() == ROLE_Authority)
        {
            SpawnRaincallerTemplate(Definition);
        }
    }
    else if (Definition.AbilityId == TEXT("Ability_Ravpack_Line"))
    {
        if (GetOwnerRole() == ROLE_Authority)
        {
            AFighterPawn* OwnerFighter = CachedFighter.Get();
            if (OwnerFighter)
            {
                FSkaldAbilityTrapState TrapState;
                TrapState.SourceAbilityId = Definition.AbilityId;
                TrapState.AbilityDefinition = Definition;
                TrapState.RoundsRemaining = 2;
                TrapState.Damage = OwnerFighter->Stats.AttackDamage + OwnerFighter->Stats.CriticalBonusDamage;
                TrapState.bPendingPlacement = true;
                ActiveTraps.Add(MoveTemp(TrapState));
            }
        }
    }
}


void USkaldAbilityComponent::AddActiveModifier(FSkaldActiveAbilityModifier&& Modifier)
{
    if (GetOwnerRole() != ROLE_Authority)
    {
        return;
    }

    if (!CachedFighter.IsValid())
    {
        return;
    }

    if (PassiveAbility.AbilityId == TEXT("Ability_Lizard_Passive") && !bLizardPenaltyConsumedThisRound)
    {
        bool bAdjusted = false;
        if (Modifier.Delta.AttackDice < 0)
        {
            const int32 Adjustment = FMath::Min(1, -Modifier.Delta.AttackDice);
            Modifier.Delta.AttackDice += Adjustment;
            bAdjusted = Adjustment > 0;
        }

        if (!bAdjusted && Modifier.Delta.Strength < 0)
        {
            const int32 Adjustment = FMath::Min(1, -Modifier.Delta.Strength);
            Modifier.Delta.Strength += Adjustment;
            bAdjusted = Adjustment > 0;
        }

        if (bAdjusted)
        {
            bLizardPenaltyConsumedThisRound = true;
        }
    }

    const bool bBubbleWardModifier = (Modifier.SourceAbilityId == FrogLineAbilityId || Modifier.SourceAbilityId == FrogfolkLineAbilityId);
    if (bBubbleWardModifier)
    {
        ++BubbleWardSourceCount;
        ++BubbleWardProtectionStacks;
    }

    ApplyDefaultModifierDuration(Modifier);
    ApplyStatDeltaToOwner(Modifier.Delta, true);
    const FName SourceAbility = Modifier.SourceAbilityId;
    const FSkaldAbilityStatDelta ModifierDelta = Modifier.Delta;
    ActiveModifiers.Add(MoveTemp(Modifier));
    HandleModifierApplied(SourceAbility);

    if (AFighterPawn* Fighter = CachedFighter.Get())
    {
        const FSkaldAbilityDefinition Definition = GetAbilityDefinitionById(SourceAbility);
        const FText AbilityName = Definition.IsValid() ? Definition.AbilityName : FText::FromName(SourceAbility);
        Fighter->NotifyStatusEffectApplied(SourceAbility, AbilityName, ModifierDelta);
    }

    if (IsPassiveAbilityId(SourceAbility))
    {
        HandlePassiveEffectApplied(GetAbilityDefinitionById(SourceAbility));
    }
}

void USkaldAbilityComponent::RemoveActiveModifier(int32 Index)
{
    if (GetOwnerRole() != ROLE_Authority)
    {
        return;
    }

    if (!ActiveModifiers.IsValidIndex(Index))
    {
        return;
    }

    const FSkaldActiveAbilityModifier RemovedModifier = ActiveModifiers[Index];
    ApplyStatDeltaToOwner(RemovedModifier.Delta, false);
    ActiveModifiers.RemoveAtSwap(Index);
    HandleModifierRemoved(RemovedModifier.SourceAbilityId);

    if (AFighterPawn* Fighter = CachedFighter.Get())
    {
        const FSkaldAbilityDefinition Definition = GetAbilityDefinitionById(RemovedModifier.SourceAbilityId);
        const FText AbilityName = Definition.IsValid() ? Definition.AbilityName : FText::FromName(RemovedModifier.SourceAbilityId);
        Fighter->NotifyStatusEffectRemoved(RemovedModifier.SourceAbilityId, AbilityName, RemovedModifier.Delta);
    }

    if (RemovedModifier.SourceAbilityId == LowHealthPenaltyId)
    {
        bLowHealthStrengthPenaltyActive = false;
    }

    if (IsPassiveAbilityId(RemovedModifier.SourceAbilityId))
    {
        HandlePassiveEffectRemoved(RemovedModifier.SourceAbilityId);
    }
}

void USkaldAbilityComponent::RemoveTargetModifiersByAbilityId(FName AbilityId)
{
    if (GetOwnerRole() != ROLE_Authority || AbilityId.IsNone())
    {
        return;
    }

    for (int32 Index = ActiveTargetModifiers.Num() - 1; Index >= 0; --Index)
    {
        const FSkaldExternalTargetModifier& ExternalModifier = ActiveTargetModifiers[Index];
        if (ExternalModifier.SourceAbilityId != AbilityId)
        {
            continue;
        }

        if (AFighterPawn* Target = ExternalModifier.Target.Get())
        {
            ApplyTargetStatDelta(Target, ExternalModifier.Delta, false);

            const FSkaldAbilityDefinition Definition = GetAbilityDefinitionById(AbilityId);
            const FText AbilityName = Definition.IsValid() ? Definition.AbilityName : FText::FromName(AbilityId);
            Target->NotifyStatusEffectRemoved(AbilityId, AbilityName, ExternalModifier.Delta);
        }

        ActiveTargetModifiers.RemoveAtSwap(Index);
    }
}

int32 USkaldAbilityComponent::FindPendingTrapIndex(FName AbilityId) const
{
    for (int32 Index = ActiveTraps.Num() - 1; Index >= 0; --Index)
    {
        const FSkaldAbilityTrapState& Trap = ActiveTraps[Index];
        if (!Trap.bPendingPlacement)
        {
            continue;
        }

        if (!AbilityId.IsNone() && Trap.SourceAbilityId != AbilityId)
        {
            continue;
        }

        return Index;
    }

    return INDEX_NONE;
}

void USkaldAbilityComponent::RemoveTrapAtIndex(int32 Index)
{
    if (GetOwnerRole() != ROLE_Authority)
    {
        return;
    }

    if (!ActiveTraps.IsValidIndex(Index))
    {
        return;
    }

    const FSkaldAbilityTrapState TrapCopy = ActiveTraps[Index];

    if (AFighterPawn* OwnerFighter = CachedFighter.Get())
    {
        if (UGridOverlayComponent* Grid = OwnerFighter->GetGrid())
        {
            if (TrapCopy.Cell != FIntPoint(INDEX_NONE, INDEX_NONE))
            {
                Grid->RemoveTrapMarker(TrapCopy.Cell);
            }
        }
    }

    ActiveTraps.RemoveAtSwap(Index);

    if (TrapCopy.Cell != FIntPoint(INDEX_NONE, INDEX_NONE))
    {
        MulticastTrapRemoved(TrapCopy.Cell);
    }
}

void USkaldAbilityComponent::ClearAllTraps()
{
    if (GetOwnerRole() != ROLE_Authority)
    {
        ActiveTraps.Empty();
        return;
    }

    for (int32 Index = ActiveTraps.Num() - 1; Index >= 0; --Index)
    {
        RemoveTrapAtIndex(Index);
    }

    ActiveTraps.Empty();
}

UDecalComponent* USkaldAbilityComponent::SpawnTrapVisualAtCell(const FIntPoint& Cell)
{
    AFighterPawn* OwnerFighter = CachedFighter.Get();
    if (!OwnerFighter)
    {
        return nullptr;
    }

    if (UGridOverlayComponent* Grid = OwnerFighter->GetGrid())
    {
        return Grid->AddTrapMarker(Cell);
    }

    return nullptr;
}

void USkaldAbilityComponent::RemoveExpiredModifiers(ESkaldAbilityModifierPhase Phase)
{
    if (GetOwnerRole() != ROLE_Authority)
    {
        return;
    }

    for (int32 Index = ActiveModifiers.Num() - 1; Index >= 0; --Index)
    {
        FSkaldActiveAbilityModifier& Modifier = ActiveModifiers[Index];
        bool bShouldRemove = false;

        if (Phase == ESkaldAbilityModifierPhase::RoundStart)
        {
            if (Modifier.bRemoveWhenRoundsExpire && Modifier.RemainingRounds > 0)
            {
                --Modifier.RemainingRounds;
                if (Modifier.RemainingRounds <= 0)
                {
                    bShouldRemove = true;
                }
            }

            if (Modifier.bRemoveOnRoundStart)
            {
                bShouldRemove = true;
            }
        }
        else if (Phase == ESkaldAbilityModifierPhase::ActivationStart)
        {
            if (Modifier.bRemoveOnActivationStart)
            {
                bShouldRemove = true;
            }
        }
        else if (Phase == ESkaldAbilityModifierPhase::ActivationEnd)
        {
            if (Modifier.bRemoveOnActivationEnd)
            {
                if (Modifier.bDealSelfDamageOnActivationEndIfAttack && bOwnerAttackedThisActivation && Modifier.SelfDamageAmount > 0)
                {
                    if (AFighterPawn* Fighter = CachedFighter.Get())
                    {
                        const int32 PreviousHealth = Fighter->Stats.Health;
                        Fighter->Stats.Health = FMath::Max(0, Fighter->Stats.Health - Modifier.SelfDamageAmount);
                        if (Fighter->Stats.Health != PreviousHealth)
                        {
                            Fighter->OnHealthChanged.Broadcast(Fighter->Stats.Health);
                        }
                    }
                }

                bShouldRemove = true;
            }
        }

        if (bShouldRemove)
        {
            RemoveActiveModifier(Index);
        }
    }
}

void USkaldAbilityComponent::NotifyAttackCommitted()
{
    if (GetOwnerRole() == ROLE_Authority)
    {
        bOwnerAttackedThisActivation = true;
    }
}

void USkaldAbilityComponent::NotifyOtherFighterMoved(AFighterPawn* Fighter, const TArray<FIntPoint>& PreviousCells, const TArray<FIntPoint>& NewCells)
{
    if (GetOwnerRole() != ROLE_Authority || !Fighter)
    {
        return;
    }

    AFighterPawn* OwnerFighter = CachedFighter.Get();
    if (!OwnerFighter || !OwnerFighter->IsAlive())
    {
        return;
    }

    HandleAberrantBloomOnMovement(Fighter, NewCells);
    HandleRaincallerOnMovement(Fighter);

    if (!bForgeguardBraceReady)
    {
        return;
    }

    if (Fighter == OwnerFighter || Fighter->Faction == OwnerFighter->Faction)
    {
        return;
    }

    const TArray<FIntPoint> OwnerCells = OwnerFighter->GetOccupiedCells();
    if (OwnerCells.Num() == 0 || PreviousCells.Num() == 0 || NewCells.Num() == 0)
    {
        return;
    }

    const bool bWasAdjacent = AreCellSetsAdjacent(OwnerCells, PreviousCells);
    const bool bIsAdjacent = AreCellSetsAdjacent(OwnerCells, NewCells);
    if (!bWasAdjacent && bIsAdjacent)
    {
        ApplyDamageToFighter(Fighter, 1);

        FSkaldActiveAbilityModifier Modifier;
        Modifier.SourceAbilityId = TEXT("Ability_Dwarf_Skirmish");
        Modifier.Delta.Movement = -1;
        Modifier.bRemoveOnRoundStart = true;
        ApplyModifierToTarget(Fighter, MoveTemp(Modifier));

        bForgeguardBraceReady = false;
        RemoveModifiersByAbilityId(TEXT("Ability_Dwarf_Skirmish"));
    }
}

void USkaldAbilityComponent::NotifyOwnerMoved(int32 DistanceMoved)
{
    if (GetOwnerRole() != ROLE_Authority)
    {
        return;
    }

    if (DistanceMoved <= 0)
    {
        return;
    }

    if (bBrutalChargeActive)
    {
        BrutalChargeDistanceMoved += DistanceMoved;
    }

    if (bIgnoreDifficultTerrainForNextMove)
    {
        bIgnoreDifficultTerrainForNextMove = false;
    }

    if (PassiveAbility.AbilityId == TEXT("Ability_Elf_Passive") && DistanceMoved > 0)
    {
        if (!bElfEvasionActive)
        {
            bElfEvasionActive = true;
            HandlePassiveEffectApplied(PassiveAbility);
        }
    }

    RefreshPassiveState();
    RefreshAllPassiveStates();

    HandleRaincallerOnMovement(CachedFighter.Get());
}

bool USkaldAbilityComponent::TreatsDifficultTerrainAsNormal() const
{
    if (PassiveAbility.AbilityId == TEXT("Ability_Frog_Passive"))
    {
        return true;
    }

    if (RaincallerAmphibiousStackCount > 0)
    {
        return true;
    }

    return bIgnoreDifficultTerrainForNextMove;
}

bool USkaldAbilityComponent::CanIgnoreEngagementRestrictions() const
{
    return HowlOfTheAlphaBuffCount > 0;
}

void USkaldAbilityComponent::AddRaincallerAmphibiousStack()
{
    ++RaincallerAmphibiousStackCount;
}

void USkaldAbilityComponent::RemoveRaincallerAmphibiousStack()
{
    RaincallerAmphibiousStackCount = FMath::Max(0, RaincallerAmphibiousStackCount - 1);
}

int32 USkaldAbilityComponent::GetCriticalHitThreshold() const
{
    return CriticalHitThresholdOverride > 0 ? CriticalHitThresholdOverride : 6;
}

void USkaldAbilityComponent::ModifyOutgoingAttackStats(
    AFighterPawn* Target, FFighterStats& InOutStats)
{
    if (GetOwnerRole() != ROLE_Authority || !CachedFighter.IsValid())
    {
        return;
    }

    if (PassiveAbility.AbilityId == TEXT("Ability_Gnoll_Passive") && Target)
    {
        if (USkaldAbilityComponent* TargetAbility = Target->FindComponentByClass<USkaldAbilityComponent>())
        {
            if (TargetAbility->HasFactionAttackedOwnerThisRound(CachedFighter->Faction))
            {
                InOutStats.AttackDice = FMath::Max(0, InOutStats.AttackDice + 1);
            }
        }
    }
}

void USkaldAbilityComponent::ModifyIncomingAttackStats(
    AFighterPawn* Attacker, FFighterStats& InOutAttackerStats)
{
    if (GetOwnerRole() != ROLE_Authority)
    {
        return;
    }

    (void)Attacker;

    if (PassiveAbility.AbilityId == TEXT("Ability_Elf_Passive") && bElfEvasionActive)
    {
        InOutAttackerStats.AttackDice = FMath::Max(0, InOutAttackerStats.AttackDice - 1);
    }
}

void USkaldAbilityComponent::HandleIncomingAttackStarted()
{
    if (GetOwnerRole() != ROLE_Authority)
    {
        return;
    }

    if (PassiveAbility.AbilityId == TEXT("Ability_Inflicted_Passive"))
    {
        int32 Roll = 0;
        if (UWorld* World = GetWorld())
        {
            if (USkaldGameInstance* GameInstance = World->GetGameInstance<USkaldGameInstance>())
            {
                Roll = GameInstance->CombatRandomStream.RandRange(1, 6);
            }
        }

        if (Roll <= 0)
        {
            Roll = FMath::RandRange(1, 6);
        }

        if (Roll >= 4)
        {
            FSkaldActiveAbilityModifier Modifier;
            Modifier.SourceAbilityId = PassiveAbility.AbilityId;
            Modifier.bRemoveOnRoundStart = true;
            Modifier.Delta.Defence = 1;

            AddActiveModifier(MoveTemp(Modifier));
        }
    }
}

void USkaldAbilityComponent::HandleIncomingAttackFinished()
{
}

bool USkaldAbilityComponent::HasHarrierDashDefencePenalty() const
{
    return bHarrierDashDefencePenaltyConsumed;
}

void USkaldAbilityComponent::MarkHarrierDashDefencePenaltyConsumed()
{
    bHarrierDashDefencePenaltyConsumed = true;
}

void USkaldAbilityComponent::RefreshPassiveState()
{
    if (GetOwnerRole() != ROLE_Authority || !PassiveAbility.IsValid())
    {
        return;
    }

    if (PassiveAbility.AbilityId == TEXT("Ability_Dwarf_Passive"))
    {
        RefreshDwarfPassive();
    }
    else if (PassiveAbility.AbilityId == TEXT("Ability_Empire_Passive"))
    {
        RefreshEmpirePassive();
    }
}

void USkaldAbilityComponent::RefreshAllPassiveStates()
{
    if (GetOwnerRole() != ROLE_Authority)
    {
        return;
    }

    if (!CachedBattleManager.IsValid())
    {
        TryRegisterBattleDelegates();
    }

    if (!CachedBattleManager.IsValid())
    {
        return;
    }

    const TArray<AFighterPawn*> Fighters = CachedBattleManager->GetInitiativeOrderSnapshot();
    for (AFighterPawn* Fighter : Fighters)
    {
        if (!Fighter)
        {
            continue;
        }

        if (USkaldAbilityComponent* Ability = Fighter->FindComponentByClass<USkaldAbilityComponent>())
        {
            Ability->RefreshPassiveState();
        }
    }
}

void USkaldAbilityComponent::RefreshDwarfPassive()
{
    if (!CachedFighter.IsValid())
    {
        return;
    }

    if (!CachedBattleManager.IsValid())
    {
        TryRegisterBattleDelegates();
    }

    if (!CachedBattleManager.IsValid())
    {
        return;
    }

    const int32 AdjacentCount = CountAdjacentFactionAllies(CachedFighter.Get(), CachedFighter->Faction);
    if (AdjacentCount == DwarfPassiveAdjacentCount)
    {
        return;
    }

    RemoveModifiersByAbilityId(PassiveAbility.AbilityId);
    DwarfPassiveAdjacentCount = AdjacentCount;

    if (AdjacentCount > 0)
    {
        FSkaldActiveAbilityModifier Modifier;
        Modifier.SourceAbilityId = PassiveAbility.AbilityId;
        Modifier.Delta.Defence = AdjacentCount;
        AddActiveModifier(MoveTemp(Modifier));
    }
}

void USkaldAbilityComponent::RefreshEmpirePassive()
{
    if (!CachedFighter.IsValid())
    {
        return;
    }

    if (!CachedBattleManager.IsValid())
    {
        TryRegisterBattleDelegates();
    }

    if (!CachedBattleManager.IsValid())
    {
        return;
    }

    AFighterPawn* Fighter = CachedFighter.Get();
    const bool bShouldBeActive = CountAdjacentEnemies(Fighter) >= 2;

    if (bShouldBeActive == bEmpirePassiveDefenceBonusActive)
    {
        return;
    }

    RemoveModifiersByAbilityId(PassiveAbility.AbilityId);
    bEmpirePassiveDefenceBonusActive = bShouldBeActive;

    if (bEmpirePassiveDefenceBonusActive)
    {
        FSkaldActiveAbilityModifier Modifier;
        Modifier.SourceAbilityId = PassiveAbility.AbilityId;
        Modifier.Delta.Defence = 1;
        AddActiveModifier(MoveTemp(Modifier));
    }
}

void USkaldAbilityComponent::ApplyActivationPassiveEffects()
{
    if (!CachedFighter.IsValid())
    {
        return;
    }

    if (!CachedBattleManager.IsValid())
    {
        TryRegisterBattleDelegates();
    }

    if (!CachedBattleManager.IsValid())
    {
        return;
    }

    AFighterPawn* OwnerFighter = CachedFighter.Get();
    const TArray<AFighterPawn*> Fighters = CachedBattleManager->GetInitiativeOrderSnapshot();

    int32 AdjacentFrogCount = 0;
    for (AFighterPawn* Fighter : Fighters)
    {
        if (!Fighter || Fighter == OwnerFighter)
        {
            continue;
        }

        if (Fighter->Faction == OwnerFighter->Faction)
        {
            continue;
        }

        if (Fighter->Stats.Health <= 0)
        {
            continue;
        }

        if (USkaldAbilityComponent* Ability = Fighter->FindComponentByClass<USkaldAbilityComponent>())
        {
            if (Ability->GetPassiveAbility().AbilityId == TEXT("Ability_Frog_Passive"))
            {
                if (OwnerFighter->GetFootprintDistanceToFighter(Fighter) <= 1)
                {
                    ++AdjacentFrogCount;
                }
            }
        }
    }

    if (AdjacentFrogCount > 0)
    {
        FSkaldActiveAbilityModifier Modifier;
        Modifier.SourceAbilityId = TEXT("Ability_Frog_Passive");
        Modifier.Delta.Movement = -AdjacentFrogCount;
        Modifier.bRemoveOnActivationEnd = true;
        AddActiveModifier(MoveTemp(Modifier));
    }
}

int32 USkaldAbilityComponent::CountAdjacentFactionAllies(
    AFighterPawn* Fighter, ESkaldFaction Faction) const
{
    if (!Fighter || !CachedBattleManager.IsValid())
    {
        return 0;
    }

    int32 Count = 0;
    const TArray<AFighterPawn*> Fighters = CachedBattleManager->GetInitiativeOrderSnapshot();
    for (AFighterPawn* Other : Fighters)
    {
        if (!Other || Other == Fighter)
        {
            continue;
        }

        if (Other->Faction != Faction || Other->Stats.Health <= 0)
        {
            continue;
        }

        if (Fighter->GetFootprintDistanceToFighter(Other) <= 1)
        {
            ++Count;
        }
    }

    return Count;
}

int32 USkaldAbilityComponent::CountAdjacentEnemies(AFighterPawn* Fighter) const
{
    if (!Fighter || !CachedBattleManager.IsValid())
    {
        return 0;
    }

    int32 Count = 0;
    const TArray<AFighterPawn*> Fighters = CachedBattleManager->GetInitiativeOrderSnapshot();
    for (AFighterPawn* Other : Fighters)
    {
        if (!Other || Other == Fighter)
        {
            continue;
        }

        if (Other->Faction == Fighter->Faction || Other->Stats.Health <= 0)
        {
            continue;
        }

        if (Fighter->GetFootprintDistanceToFighter(Other) <= 1)
        {
            ++Count;
        }
    }

    return Count;
}

void USkaldAbilityComponent::HandlePassiveEffectApplied(const FSkaldAbilityDefinition& Definition)
{
    if (!Definition.IsValid())
    {
        return;
    }

    AFighterPawn* Fighter = CachedFighter.Get();
    if (!Fighter)
    {
        return;
    }

    const FSkaldAbilityDefinition FactionPassive = GetFactionPassive(Fighter->Faction);
    if (Definition.AbilityId != PassiveAbility.AbilityId && Definition.AbilityId != FactionPassive.AbilityId)
    {
        return;
    }

    int32& Count = PassiveVisualStackCounts.FindOrAdd(Definition.AbilityId);
    ++Count;
    if (Count == 1)
    {
        Fighter->NotifyPassiveBuffApplied(Definition);
        PlayAbilityFeedback(Definition);
    }
}

void USkaldAbilityComponent::HandlePassiveEffectRemoved(FName AbilityId)
{
    if (AbilityId.IsNone())
    {
        return;
    }

    AFighterPawn* Fighter = CachedFighter.Get();
    if (!Fighter)
    {
        return;
    }

    if (int32* Count = PassiveVisualStackCounts.Find(AbilityId))
    {
        *Count = FMath::Max(0, *Count - 1);
        if (*Count == 0)
        {
            PassiveVisualStackCounts.Remove(AbilityId);
            Fighter->NotifyPassiveBuffRemoved(AbilityId);
        }
    }
}

bool USkaldAbilityComponent::HasFactionAttackedOwnerThisRound(ESkaldFaction Faction) const
{
    return FactionsThatAttackedOwnerThisRound.Contains(Faction);
}

void USkaldAbilityComponent::HandleModifierApplied(FName AbilityId)
{
    if (AbilityId == TEXT("Ability_Orc_Elite"))
    {
        ++WaaghRoarBuffCount;
        CriticalHitThresholdOverride = 5;
        bIgnoreDifficultTerrainForNextMove = true;
    }
    else if (AbilityId == TEXT("Ability_Gnoll_Line"))
    {
        ++HowlOfTheAlphaBuffCount;
    }
    else if (AbilityId == TacticalReservesAttackBuffId)
    {
        MarkTacticalReservesAttackBuffPending();
    }
}

void USkaldAbilityComponent::HandleModifierRemoved(FName AbilityId)
{
    if (AbilityId == TEXT("Ability_Orc_Elite"))
    {
        WaaghRoarBuffCount = FMath::Max(0, WaaghRoarBuffCount - 1);
        if (WaaghRoarBuffCount <= 0)
        {
            CriticalHitThresholdOverride = 0;
            bIgnoreDifficultTerrainForNextMove = false;
        }
    }
    else if (AbilityId == TEXT("Ability_Gnoll_Line"))
    {
        HowlOfTheAlphaBuffCount = FMath::Max(0, HowlOfTheAlphaBuffCount - 1);
    }
    else if (AbilityId == TacticalReservesAttackBuffId)
    {
        ClearTacticalReservesAttackBuff();
    }
    else if (AbilityId == FrogLineAbilityId || AbilityId == FrogfolkLineAbilityId)
    {
        BubbleWardSourceCount = FMath::Max(0, BubbleWardSourceCount - 1);
        BubbleWardProtectionStacks = FMath::Min(BubbleWardProtectionStacks, BubbleWardSourceCount);
    }
}

bool USkaldAbilityComponent::DeployTrapAtCell(const FIntPoint& Cell, FName AbilityId, FText& OutError)
{
    OutError = FText::GetEmpty();

    if (GetOwnerRole() != ROLE_Authority)
    {
        return true;
    }

    AFighterPawn* OwnerFighter = CachedFighter.Get();
    if (!OwnerFighter)
    {
        OutError = NSLOCTEXT("SkaldAbilities", "AbilityNoSelection", "Select a fighter before using abilities.");
        return false;
    }

    UGridOverlayComponent* Grid = OwnerFighter->GetGrid();
    if (!Grid || !Grid->IsCellInBounds(Cell))
    {
        OutError = NSLOCTEXT("SkaldAbilities", "AbilityCellInvalid", "Select a valid cell on the grid.");
        return false;
    }

    if (Grid->IsOccupied(Cell))
    {
        OutError = NSLOCTEXT("SkaldAbilities", "AbilityTrapCellOccupied", "That tile is already occupied.");
        return false;
    }

    if (Grid->HasTrapMarker(Cell))
    {
        OutError = NSLOCTEXT("SkaldAbilities", "AbilityTrapCellBlocked", "A trap already covers that tile.");
        return false;
    }

    const int32 PendingIndex = FindPendingTrapIndex(AbilityId);

    if (PendingIndex == INDEX_NONE)
    {
        OutError = NSLOCTEXT("SkaldAbilities", "AbilityTrapUnavailable", "No trap is ready to deploy.");
        return false;
    }

    FSkaldAbilityTrapState& Trap = ActiveTraps[PendingIndex];
    Trap.Cell = Cell;
    Trap.bPendingPlacement = false;
    Trap.VisualComponent = MakeWeakObjectPtr(SpawnTrapVisualAtCell(Cell));

    MulticastTrapPlaced(Cell);
    return true;
}

bool USkaldAbilityComponent::TryResolveTrapAtCell(const FIntPoint& Cell, AFighterPawn* TriggeringFighter)
{
    if (GetOwnerRole() != ROLE_Authority)
    {
        return false;
    }

    AFighterPawn* OwnerFighter = CachedFighter.Get();
    if (!OwnerFighter || !TriggeringFighter || TriggeringFighter->Faction == OwnerFighter->Faction)
    {
        return false;
    }

    for (int32 Index = 0; Index < ActiveTraps.Num(); ++Index)
    {
        FSkaldAbilityTrapState& Trap = ActiveTraps[Index];
        if (Trap.bPendingPlacement || Trap.Cell != Cell)
        {
            continue;
        }

        ApplyDamageToFighter(TriggeringFighter, Trap.Damage);

        const FSkaldAbilityTrapState TrapCopy = Trap;
        RemoveTrapAtIndex(Index);

        bSuppressAbilityEffectOnNextTrigger = true;
        MulticastAbilityTriggered(TrapCopy.AbilityDefinition);

        return true;
    }

    return false;
}

bool USkaldAbilityComponent::HasPendingTrapForAbility(FName AbilityId) const
{
    return FindPendingTrapIndex(AbilityId) != INDEX_NONE;
}

void USkaldAbilityComponent::ReceiveExternalModifier(FSkaldActiveAbilityModifier&& Modifier)
{
    AddActiveModifier(MoveTemp(Modifier));
    BroadcastStateChanged();
}

void USkaldAbilityComponent::ApplyStatDeltaToOwner(const FSkaldAbilityStatDelta& Delta, bool bApply)
{
    if (GetOwnerRole() != ROLE_Authority)
    {
        return;
    }

    AFighterPawn* Fighter = CachedFighter.Get();
    if (!Fighter)
    {
        return;
    }

    auto ApplyIntDelta = [bApply](int32& Stat, int32 Amount)
    {
        if (Amount == 0)
        {
            return;
        }

        const int32 DeltaValue = bApply ? Amount : -Amount;
        Stat = FMath::Max(0, Stat + DeltaValue);
    };

    ApplyIntDelta(Fighter->Stats.AttackDice, Delta.AttackDice);
    ApplyIntDelta(Fighter->Stats.AttackDamage, Delta.AttackDamage);

    auto ApplyTypedAttackDamage = [&](int32 Amount, EFighterAttackType RequiredType)
    {
        if (Amount == 0)
        {
            return;
        }

        if (Fighter->GetAttackType() != RequiredType)
        {
            return;
        }

        const int32 DeltaValue = bApply ? Amount : -Amount;
        Fighter->Stats.AttackDamage = FMath::Max(0, Fighter->Stats.AttackDamage + DeltaValue);
    };

    ApplyTypedAttackDamage(Delta.MeleeAttackDamage, EFighterAttackType::Melee);
    ApplyTypedAttackDamage(Delta.RangedAttackDamage, EFighterAttackType::Ranged);
    ApplyIntDelta(Fighter->Stats.AttackRange, Delta.AttackRange);
    ApplyIntDelta(Fighter->Stats.Movement, Delta.Movement);
    ApplyIntDelta(Fighter->Stats.Defence, Delta.Defence);
    ApplyIntDelta(Fighter->Stats.Strength, Delta.Strength);
    ApplyIntDelta(Fighter->Stats.CriticalBonusDamage, Delta.CriticalBonusDamage);
}

void USkaldAbilityComponent::TryRegisterBattleDelegates()
{
    if (GetOwnerRole() != ROLE_Authority)
    {
        return;
    }

    if (UGridBattleManager* ExistingManager = CachedBattleManager.Get())
    {
        ExistingManager->OnAttackResolved.RemoveDynamic(this, &USkaldAbilityComponent::HandleBattleAttackResolved);
        ExistingManager->OnAttackResolved.AddDynamic(this, &USkaldAbilityComponent::HandleBattleAttackResolved);
        ExistingManager->OnActiveFighterChanged.RemoveDynamic(this, &USkaldAbilityComponent::HandleActiveFighterChanged);
        ExistingManager->OnActiveFighterChanged.AddDynamic(this, &USkaldAbilityComponent::HandleActiveFighterChanged);
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    if (USkaldGameInstance* GameInstance = World->GetGameInstance<USkaldGameInstance>())
    {
        if (UGridBattleManager* BattleManager = GameInstance->GridBattleManager)
        {
            CachedBattleManager = BattleManager;
            BattleManager->OnAttackResolved.RemoveDynamic(this, &USkaldAbilityComponent::HandleBattleAttackResolved);
            BattleManager->OnAttackResolved.AddDynamic(this, &USkaldAbilityComponent::HandleBattleAttackResolved);
            BattleManager->OnActiveFighterChanged.RemoveDynamic(this, &USkaldAbilityComponent::HandleActiveFighterChanged);
            BattleManager->OnActiveFighterChanged.AddDynamic(this, &USkaldAbilityComponent::HandleActiveFighterChanged);
        }
    }
}

void USkaldAbilityComponent::RemoveBattleDelegates()
{
    if (UGridBattleManager* BattleManager = CachedBattleManager.Get())
    {
        BattleManager->OnAttackResolved.RemoveDynamic(this, &USkaldAbilityComponent::HandleBattleAttackResolved);
        BattleManager->OnActiveFighterChanged.RemoveDynamic(this, &USkaldAbilityComponent::HandleActiveFighterChanged);
    }
    CachedBattleManager.Reset();
}

void USkaldAbilityComponent::HandleBattleAttackResolved(AFighterPawn* Attacker, AFighterPawn* Defender, const FDiceRollResult& Result)
{
    if (GetOwnerRole() != ROLE_Authority)
    {
        return;
    }

    AFighterPawn* OwnerFighter = CachedFighter.Get();
    if (!OwnerFighter)
    {
        return;
    }

    if (RaincallerTemplateCells.Num() > 0)
    {
        HandleRaincallerOnMovement(Attacker);
        HandleRaincallerOnMovement(Defender);
    }

    const bool bDefenderDied = Defender && Result.StartingHealth > 0 && Result.EndingHealth <= 0;

    if (bShieldWallPivotActive && ShieldWallPivotProtectedAlly.IsValid() && Defender == ShieldWallPivotProtectedAlly.Get())
    {
        if (Result.HitCount > 0)
        {
            HealFighter(ShieldWallPivotProtectedAlly.Get(), 1);
        }

        bShieldWallPivotActive = false;
        ShieldWallPivotProtectedAlly.Reset();
    }

    if (Attacker == OwnerFighter)
    {
        if (bHasPendingTacticalReservesAttackBuff)
        {
            RemoveModifiersByAbilityId(TacticalReservesAttackBuffId);
            ClearTacticalReservesAttackBuff();
        }

        if (PassiveAbility.AbilityId == TEXT("Ability_Human_Passive"))
        {
            if (!CachedBattleManager.IsValid())
            {
                TryRegisterBattleDelegates();
            }

            if (CachedBattleManager.IsValid())
            {
                const TArray<AFighterPawn*> Fighters = CachedBattleManager->GetInitiativeOrderSnapshot();
                for (AFighterPawn* Fighter : Fighters)
                {
                    if (!Fighter || Fighter == OwnerFighter || Fighter->Faction != OwnerFighter->Faction || Fighter->Stats.Health <= 0)
                    {
                        continue;
                    }

                    if (OwnerFighter->GetFootprintDistanceToFighter(Fighter) <= 1)
                    {
                        FSkaldActiveAbilityModifier Modifier;
                        Modifier.SourceAbilityId = PassiveAbility.AbilityId;
                        Modifier.Delta.AttackDice = 1;
                        Modifier.bRemoveOnRoundStart = true;
                        ApplyModifierToTarget(Fighter, MoveTemp(Modifier));
                    }
                }
            }

            RemoveModifiersByAbilityId(PassiveAbility.AbilityId);
        }

        if (PassiveAbility.AbilityId == TEXT("Ability_Orc_Passive"))
        {
            if (Result.CriticalHitCount > 0)
            {
                FSkaldActiveAbilityModifier Modifier;
                Modifier.SourceAbilityId = PassiveAbility.AbilityId;
                Modifier.Delta.Strength = 1;
                AddActiveModifier(MoveTemp(Modifier));
            }
            else if (Result.HitCount <= 0)
            {
                RemoveModifiersByAbilityId(PassiveAbility.AbilityId);
            }
        }

        if (PassiveAbility.AbilityId == TEXT("Ability_Ravpack_Passive") && bDefenderDied)
        {
            bRavpackMomentumPending = true;
        }

        if (bApplyViralLashOnNextAttack)
        {
            HandleViralLashResolved(Defender, Result);
            bApplyViralLashOnNextAttack = false;
        }

        if (bApplyScrapperFeintOnNextMiss)
        {
            HandleScrapperFeintResolved(Result);
        }

        if (bApplyRallyingShotOnNextAttack)
        {
            HandleRallyingShotResolved(Result);
            bApplyRallyingShotOnNextAttack = false;
        }

        if (bBrutalChargeActive)
        {
            HandleBrutalChargeResolved(Defender, Result);
            bBrutalChargeActive = false;
            BrutalChargeDistanceMoved = 0;
        }

        if (bVeilStepBonusActive)
        {
            bVeilStepBonusActive = false;
            RemoveModifiersByAbilityId(TEXT("Ability_Elf_Skirmish"));
        }

        if (bSmashThroughActive)
        {
            if (Defender && Result.HitCount > 0)
            {
                FSkaldActiveAbilityModifier Modifier;
                Modifier.SourceAbilityId = TEXT("Ability_Orc_Line");
                Modifier.Delta.Defence = -1;
                Modifier.bRemoveOnRoundStart = true;
                ApplyModifierToTarget(Defender, MoveTemp(Modifier));
                TryPushFighterOneCellAway(OwnerFighter, Defender);
            }

            bSmashThroughActive = false;
        }

        if (bDeepDelveMortarPending)
        {
            if (Defender && Result.HitCount > 0)
            {
                const FIntPoint OriginCell = (DeepDelveMortarTargetCell.X != INDEX_NONE)
                    ? DeepDelveMortarTargetCell
                    : Defender->GetCurrentCell();
                if (OriginCell.X != INDEX_NONE && CachedBattleManager.IsValid())
                {
                    const TArray<AFighterPawn*> Fighters = CachedBattleManager->GetInitiativeOrderSnapshot();
                    const int32 SplashDamage = FMath::Max(1, OwnerFighter->Stats.AttackDamage / 2);
                    for (AFighterPawn* Fighter : Fighters)
                    {
                        if (!Fighter || Fighter == Defender)
                        {
                            continue;
                        }

                        if (Fighter->GetFootprintDistanceToCell(OriginCell) <= 1)
                        {
                            ApplyDamageToFighter(Fighter, SplashDamage);
                        }
                    }
                }

                if (Result.CriticalHitCount > 0)
                {
                    FSkaldActiveAbilityModifier Modifier;
                    Modifier.SourceAbilityId = TEXT("Ability_Dwarf_Elite");
                    Modifier.Delta.Movement = -2;
                    Modifier.bRemoveOnActivationStart = true;
                    ApplyModifierToTarget(Defender, MoveTemp(Modifier));
                }
            }

            bDeepDelveMortarPending = false;
            DeepDelveMortarTargetCell = FIntPoint(INDEX_NONE, INDEX_NONE);
        }

        if (bMoonlanceFlurryActive)
        {
            if (Result.CriticalHitCount > 0)
            {
                OwnerFighter->TryRestoreReaction();
            }

            if (MoonlanceFlurryAttacksRemaining > 0)
            {
                --MoonlanceFlurryAttacksRemaining;
            }

            if (MoonlanceFlurryAttacksRemaining <= 0)
            {
                bMoonlanceFlurryActive = false;
                RemoveModifiersByAbilityId(TEXT("Ability_Elf_Line"));
            }
        }

        if (bStarfallInvocationPending)
        {
            if (Defender && Result.HitCount > 0)
            {
                FSkaldActiveAbilityModifier Modifier;
                Modifier.SourceAbilityId = TEXT("Ability_Elf_Elite");
                Modifier.Delta.AttackDice = -1;
                Modifier.bRemoveOnActivationStart = true;
                ApplyModifierToTarget(Defender, MoveTemp(Modifier));
            }

            bStarfallInvocationPending = false;
            ConsumeOncePerBattleAbility(TEXT("Ability_Elf_Elite"));
        }

        if (bGraveGraspPending)
        {
            if (Defender && Result.HitCount > 0)
            {
                FSkaldActiveAbilityModifier Modifier;
                Modifier.SourceAbilityId = TEXT("Ability_Undead_Skirmish");
                Modifier.Delta.Movement = -Defender->Stats.Movement;
                Modifier.bRemoveOnActivationStart = true;
                ApplyModifierToTarget(Defender, MoveTemp(Modifier));
            }

            bGraveGraspPending = false;
        }

        if (bSoulHarvestActive)
        {
            if (Defender && Result.HitCount > 0 && Result.EndingHealth <= 0)
            {
                FSkaldActiveAbilityModifier Modifier;
                Modifier.SourceAbilityId = TEXT("Ability_Undead_Line");
                Modifier.Delta.AttackDice = 1;
                Modifier.RemainingRounds = 1;
                Modifier.bRemoveOnRoundStart = true;
                AddActiveModifier(MoveTemp(Modifier));
                HealFighter(OwnerFighter, 1);
                bSoulHarvestKillSecured = true;
            }
            else if (!bSoulHarvestKillSecured)
            {
                OwnerFighter->TryRestoreAction();
            }

            bSoulHarvestActive = false;
        }

        if (bHarrierDashActive)
        {
            if (Defender && Result.HitCount > 0)
            {
                if (USkaldAbilityComponent* DefenderAbility = Defender->GetAbilityComponent())
                {
                    if (!DefenderAbility->HasHarrierDashDefencePenalty())
                    {
                        FSkaldActiveAbilityModifier Modifier;
                        Modifier.SourceAbilityId = TEXT("Ability_Gnoll_Skirmish");
                        Modifier.Delta.Defence = -1;
                        Modifier.bRemoveOnRoundStart = true;
                        ApplyModifierToTarget(Defender, MoveTemp(Modifier));
                        DefenderAbility->MarkHarrierDashDefencePenaltyConsumed();
                    }
                }
            }

            bHarrierDashActive = false;
        }

        if (bGoblinFlashBombActive)
        {
            if (Defender && Result.HitCount > 0)
            {
                FSkaldActiveAbilityModifier Modifier;
                Modifier.SourceAbilityId = TEXT("Ability_Goblin_Skirmish");
                Modifier.Delta.Defence = -1;
                Modifier.bRemoveOnActivationStart = true;
                ApplyModifierToTarget(Defender, MoveTemp(Modifier));

                if (USkaldAbilityComponent* DefenderAbility = Defender->GetAbilityComponent())
                {
                    DefenderAbility->ForceSpendAllReactions();
                }
            }

            bGoblinFlashBombActive = false;
            RemoveModifiersByAbilityId(TEXT("Ability_Goblin_Skirmish"));
        }

        if (bGoblinNetActive)
        {
            if (Defender && Result.HitCount > 0)
            {
                FSkaldActiveAbilityModifier Modifier;
                Modifier.SourceAbilityId = TEXT("Ability_Goblin_Line");
                Modifier.Delta.Movement = -2;
                Modifier.Delta.AttackDamage = -1;
                Modifier.bRemoveOnActivationStart = true;
                ApplyModifierToTarget(Defender, MoveTemp(Modifier));
            }

            bGoblinNetActive = false;
        }

        if (bSuppressingFireActive)
        {
            if (Defender && Result.HitCount > 0)
            {
                FSkaldActiveAbilityModifier Modifier;
                Modifier.SourceAbilityId = TEXT("Ability_Empire_Skirmish");
                Modifier.Delta.Movement = -2;
                Modifier.bRemoveOnActivationStart = true;
                ApplyModifierToTarget(Defender, MoveTemp(Modifier));

                if (USkaldAbilityComponent* DefenderAbility = Defender->GetAbilityComponent())
                {
                    DefenderAbility->ForceSpendAllReactions();
                }
            }

            bSuppressingFireActive = false;
        }

        if (bRendAndTearActive)
        {
            if (Defender && Result.HitCount > 0)
            {
                const int32 AdditionalDamage = FMath::Clamp(Result.HitCount, 0, 3);
                ApplyDamageToFighter(Defender, AdditionalDamage);
            }

            bRendAndTearActive = false;
        }

        if (bArtilleryStrikePending)
        {
            if (Defender && Result.HitCount > 0 && CachedBattleManager.IsValid())
            {
                const TArray<AFighterPawn*> Fighters = CachedBattleManager->GetInitiativeOrderSnapshot();
                const int32 BaseDamage = OwnerFighter->Stats.AttackDamage + 2;
                const FIntPoint OriginCell = (ArtilleryStrikeTargetCell.X != INDEX_NONE)
                    ? ArtilleryStrikeTargetCell
                    : Defender->GetCurrentCell();
                for (AFighterPawn* Fighter : Fighters)
                {
                    if (!Fighter || Fighter == Defender)
                    {
                        continue;
                    }

                    if (OriginCell.X != INDEX_NONE && Fighter->GetFootprintDistanceToCell(OriginCell) <= 2)
                    {
                        const bool bAlly = Fighter->Faction == OwnerFighter->Faction;
                        const int32 DamageToApply = bAlly ? FMath::Max(1, BaseDamage / 2) : BaseDamage;
                        ApplyDamageToFighter(Fighter, DamageToApply);
                    }
                }
            }

            bArtilleryStrikePending = false;
            ArtilleryStrikeTargetCell = FIntPoint(INDEX_NONE, INDEX_NONE);
        }

    }
    else if (Defender == OwnerFighter)
    {
        if (Attacker)
        {
            FactionsThatAttackedOwnerThisRound.Add(Attacker->Faction);
        }

        if (BubbleWardProtectionStacks > 0 && Attacker && Attacker->GetAttackType() == EFighterAttackType::Ranged)
        {
            int32 HighestDamage = 0;
            for (const FDiceRollOutcome& Outcome : Result.DiceOutcomes)
            {
                if (!Outcome.bHit)
                {
                    continue;
                }

                HighestDamage = FMath::Max(HighestDamage, Outcome.Damage);
            }

            const int32 DamageTaken = FMath::Max(0, Result.StartingHealth - Result.EndingHealth);
            const int32 HealAmount = FMath::Min(DamageTaken, HighestDamage);
            if (HealAmount > 0)
            {
                HealFighter(OwnerFighter, HealAmount);
            }

            BubbleWardProtectionStacks = FMath::Max(0, BubbleWardProtectionStacks - 1);
        }

        if (PassiveAbility.AbilityId == TEXT("Ability_Elf_Passive") && bElfEvasionActive && Result.HitCount > 0)
        {
            bElfEvasionActive = false;
            HandlePassiveEffectRemoved(PassiveAbility.AbilityId);
        }

        const bool bMeleeAttack = Attacker && Attacker->GetAttackType() == EFighterAttackType::Melee;
        if (bRuneRiposteReady && bMeleeAttack)
        {
            HandleRuneRiposteTriggered(Attacker, Result);
            bRuneRiposteReady = false;
        }

        if (bForgeguardBraceReady)
        {
            ApplyDamageToFighter(Attacker, 1);
            bForgeguardBraceReady = false;
            RemoveModifiersByAbilityId(TEXT("Ability_Dwarf_Skirmish"));
        }
    }

    if (bDefenderDied && Attacker == OwnerFighter)
    {
        RefreshAllPassiveStates();
    }

    if (GetOwnerRole() == ROLE_Authority && ViralLashCarriers.Num() > 0 && Defender && Result.TotalDamage > 0)
    {
        HandleViralLashCarrierDamaged(Defender, Result);
    }
}

void USkaldAbilityComponent::HandleViralLashResolved(AFighterPawn* Defender, const FDiceRollResult& Result)
{
    if (!Defender || Result.HitCount <= 0)
    {
        return;
    }

    ApplyViralLashDebuffToFighter(Defender);

    if (GetOwnerRole() == ROLE_Authority)
    {
        UpdateViralLashCarriers();

        const int32 CurrentRound = CachedBattleManager.IsValid() ? CachedBattleManager->GetCurrentRound() : INDEX_NONE;
        FSkaldViralLashCarrierState* Existing = ViralLashCarriers.FindByPredicate([
            Defender
        ](const FSkaldViralLashCarrierState& Entry)
        {
            return Entry.Carrier.IsValid() && Entry.Carrier.Get() == Defender;
        });

        if (Existing)
        {
            Existing->RoundNumber = CurrentRound;
            Existing->bHasSpread = false;
        }
        else
        {
            FSkaldViralLashCarrierState& State = ViralLashCarriers.Emplace_GetRef();
            State.Carrier = Defender;
            State.RoundNumber = CurrentRound;
            State.bHasSpread = false;
        }
    }
}

void USkaldAbilityComponent::HandleScrapperFeintResolved(const FDiceRollResult& Result)
{
    if (Result.HitCount > 0)
    {
        bApplyScrapperFeintOnNextMiss = false;
        return;
    }

    FSkaldActiveAbilityModifier Modifier;
    Modifier.SourceAbilityId = TEXT("Ability_Ravpack_Skirmish");
    Modifier.Delta.Movement = 2;
    Modifier.bRemoveOnActivationEnd = true;

    AddActiveModifier(MoveTemp(Modifier));
    bApplyScrapperFeintOnNextMiss = false;
    BroadcastStateChanged();
}

void USkaldAbilityComponent::HandleRallyingShotResolved(const FDiceRollResult& Result)
{
    if (Result.HitCount <= 0)
    {
        RallyingShotDesignatedAlly.Reset();
        return;
    }

    AFighterPawn* OwnerFighter = CachedFighter.Get();
    if (!OwnerFighter || !CachedBattleManager.IsValid())
    {
        RallyingShotDesignatedAlly.Reset();
        return;
    }

    AFighterPawn* SelectedAlly = RallyingShotDesignatedAlly.Get();
    if (!IsValidRallyingShotAlly(OwnerFighter, SelectedAlly))
    {
        const int32 Range = OwnerFighter->Stats.AttackRange;
        AFighterPawn* BestAlly = nullptr;
        int32 BestDistance = TNumericLimits<int32>::Max();

        const TArray<AFighterPawn*> Fighters = CachedBattleManager->GetInitiativeOrderSnapshot();
        for (AFighterPawn* Fighter : Fighters)
        {
            if (!IsValidRallyingShotAlly(OwnerFighter, Fighter))
            {
                continue;
            }

            const int32 Distance = OwnerFighter->GetFootprintDistanceToFighter(Fighter);
            if (Distance <= Range && Distance < BestDistance)
            {
                BestAlly = Fighter;
                BestDistance = Distance;
            }
        }

        SelectedAlly = BestAlly;
    }

    if (!SelectedAlly)
    {
        RallyingShotDesignatedAlly.Reset();
        return;
    }

    FSkaldActiveAbilityModifier Modifier;
    Modifier.SourceAbilityId = TEXT("Ability_Human_Skirmish");
    Modifier.Delta.Movement = 1;
    Modifier.RemainingRounds = 1;
    Modifier.bRemoveOnRoundStart = true;
    ApplyModifierToTarget(SelectedAlly, MoveTemp(Modifier));

    TryShiftFighterOneCell(SelectedAlly, OwnerFighter);
    RallyingShotDesignatedAlly.Reset();
}

bool USkaldAbilityComponent::IsValidRallyingShotAlly(AFighterPawn* Owner, AFighterPawn* Candidate) const
{
    if (!Owner || !Candidate || Candidate == Owner)
    {
        return false;
    }

    if (Candidate->Faction != Owner->Faction || Candidate->Stats.Health <= 0)
    {
        return false;
    }

    const int32 Range = Owner->Stats.AttackRange;
    if (Range >= 0 && Owner->GetFootprintDistanceToFighter(Candidate) > Range)
    {
        return false;
    }

    return true;
}

bool USkaldAbilityComponent::CanApplyAmphibiousRushBonus(const FSkaldAbilityDefinition& Definition) const
{
    (void)Definition;

    AFighterPawn* Owner = CachedFighter.Get();
    if (!Owner)
    {
        return false;
    }

    UGridOverlayComponent* Grid = Owner->GetGrid();
    if (!Grid)
    {
        return false;
    }

    if (IsAnyCellDifficult(Owner->GetOccupiedCells(), Grid))
    {
        return true;
    }

    FIntPoint TargetCell;
    if (ResolveAbilityTargetCellFromContext(Definition.AbilityId, TargetCell) && Grid->IsCellInBounds(TargetCell))
    {
        const TArray<FIntPoint> TargetCells = Owner->GetOccupiedCells(TargetCell);
        if (IsAnyCellDifficult(TargetCells, Grid))
        {
            return true;
        }
    }

    return false;
}

bool USkaldAbilityComponent::IsAnyCellDifficult(const TArray<FIntPoint>& Cells, UGridOverlayComponent* Grid) const
{
    if (!Grid)
    {
        return false;
    }

    for (const FIntPoint& Cell : Cells)
    {
        if (Grid->IsDifficultTerrain(Cell))
        {
            return true;
        }
    }

    return false;
}

void USkaldAbilityComponent::SpawnAberrantBloomHazard(const FSkaldAbilityDefinition& Definition)
{
    (void)Definition;

    ClearAberrantBloomHazards();

    AFighterPawn* Owner = CachedFighter.Get();
    if (!Owner)
    {
        return;
    }

    UGridOverlayComponent* Grid = Owner->GetGrid();
    if (!Grid)
    {
        return;
    }

    const TArray<FIntPoint> OwnerCells = Owner->GetOccupiedCells();
    AberrantBloomDamage = FMath::Max(1, Owner->Stats.AttackDamage);
    AberrantBloomMovementDebuffed.Reset();

    TSet<FIntPoint> ProcessedCells;
    for (const FIntPoint& OwnerCell : OwnerCells)
    {
        for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
        {
            for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
            {
                if (OffsetX == 0 && OffsetY == 0)
                {
                    continue;
                }

                const FIntPoint Candidate = OwnerCell + FIntPoint(OffsetX, OffsetY);
                if (ProcessedCells.Contains(Candidate))
                {
                    continue;
                }

                ProcessedCells.Add(Candidate);

                if (!Grid->IsCellInBounds(Candidate) || Grid->IsObscured(Candidate) || Grid->IsOccupied(Candidate))
                {
                    continue;
                }

                FSkaldAberrantBloomHazardCell& Cell = AberrantBloomHazardCells.Emplace_GetRef();
                Cell.Cell = Candidate;
                Cell.VisualComponent = SpawnTrapVisualAtCell(Candidate);
            }
        }
    }

    TryRegisterBattleDelegates();
}

void USkaldAbilityComponent::ClearAberrantBloomHazards()
{
    if (AberrantBloomHazardCells.Num() > 0)
    {
        AFighterPawn* Owner = CachedFighter.Get();
        UGridOverlayComponent* Grid = Owner ? Owner->GetGrid() : nullptr;
        if (Grid)
        {
            for (const FSkaldAberrantBloomHazardCell& HazardCell : AberrantBloomHazardCells)
            {
                Grid->RemoveTrapMarker(HazardCell.Cell);
            }
        }
    }

    AberrantBloomHazardCells.Reset();
    AberrantBloomMovementDebuffed.Reset();
    AberrantBloomDamage = 0;
}

bool USkaldAbilityComponent::DoesAnyCellMatchAberrantBloom(const TArray<FIntPoint>& Cells) const
{
    if (AberrantBloomHazardCells.Num() == 0)
    {
        return false;
    }

    for (const FIntPoint& Cell : Cells)
    {
        for (const FSkaldAberrantBloomHazardCell& HazardCell : AberrantBloomHazardCells)
        {
            if (HazardCell.Cell == Cell)
            {
                return true;
            }
        }
    }

    return false;
}

void USkaldAbilityComponent::HandleAberrantBloomTriggered(AFighterPawn* Victim)
{
    if (!Victim || AberrantBloomHazardCells.Num() == 0 || AberrantBloomDamage <= 0)
    {
        return;
    }

    ApplyDamageToFighter(Victim, AberrantBloomDamage);

    for (auto It = AberrantBloomMovementDebuffed.CreateIterator(); It; ++It)
    {
        if (!It->IsValid())
        {
            It.RemoveCurrent();
        }
    }

    if (!AberrantBloomMovementDebuffed.Contains(Victim))
    {
        FSkaldActiveAbilityModifier Modifier;
        Modifier.SourceAbilityId = TEXT("Ability_Inflicted_Elite");
        Modifier.Delta.Movement = -1;
        Modifier.bRemoveOnRoundStart = true;
        ApplyModifierToTarget(Victim, MoveTemp(Modifier));
        AberrantBloomMovementDebuffed.Add(Victim);
    }
}

void USkaldAbilityComponent::HandleAberrantBloomOnMovement(AFighterPawn* Fighter, const TArray<FIntPoint>& NewCells)
{
    if (GetOwnerRole() != ROLE_Authority || AberrantBloomHazardCells.Num() == 0 || !Fighter)
    {
        return;
    }

    AFighterPawn* Owner = CachedFighter.Get();
    if (!Owner || Fighter == Owner || Fighter->Faction == Owner->Faction)
    {
        return;
    }

    if (DoesAnyCellMatchAberrantBloom(NewCells))
    {
        HandleAberrantBloomTriggered(Fighter);
    }
}

void USkaldAbilityComponent::SpawnRaincallerTemplate(const FSkaldAbilityDefinition& Definition)
{
    (void)Definition;

    ClearRaincallerTemplate();

    AFighterPawn* Owner = CachedFighter.Get();
    if (!Owner)
    {
        return;
    }

    UGridOverlayComponent* Grid = Owner->GetGrid();
    if (!Grid)
    {
        return;
    }

    const TArray<FIntPoint> OwnerCells = Owner->GetOccupiedCells();
    if (OwnerCells.Num() == 0)
    {
        return;
    }

    TSet<FIntPoint> ProcessedCells;
    for (const FIntPoint& Origin : OwnerCells)
    {
        for (int32 OffsetY = -2; OffsetY <= 2; ++OffsetY)
        {
            for (int32 OffsetX = -2; OffsetX <= 2; ++OffsetX)
            {
                const int32 Distance = FMath::Max(FMath::Abs(OffsetX), FMath::Abs(OffsetY));
                if (Distance > 2)
                {
                    continue;
                }

                const FIntPoint Candidate = Origin + FIntPoint(OffsetX, OffsetY);
                if (!Grid->IsCellInBounds(Candidate))
                {
                    continue;
                }

                if (!ProcessedCells.Add(Candidate))
                {
                    continue;
                }

                FSkaldRaincallerCell& Cell = RaincallerTemplateCells.Emplace_GetRef();
                Cell.Cell = Candidate;
                Cell.VisualComponent = SpawnTrapVisualAtCell(Candidate);
            }
        }
    }

    RaincallerTemplateSourceId = Definition.AbilityId;

    if (CachedBattleManager.IsValid())
    {
        const int32 CurrentRound = CachedBattleManager->GetCurrentRound();
        RaincallerTemplateExpireRound = (CurrentRound == INDEX_NONE) ? INDEX_NONE : CurrentRound + 1;
    }
    else
    {
        RaincallerTemplateExpireRound = INDEX_NONE;
    }

    RefreshRaincallerOccupants();
    TryRegisterBattleDelegates();
}

void USkaldAbilityComponent::ClearRaincallerTemplate()
{
    if (RaincallerTemplateCells.Num() > 0)
    {
        AFighterPawn* Owner = CachedFighter.Get();
        UGridOverlayComponent* Grid = Owner ? Owner->GetGrid() : nullptr;
        if (Grid)
        {
            for (const FSkaldRaincallerCell& Cell : RaincallerTemplateCells)
            {
                Grid->RemoveTrapMarker(Cell.Cell);
            }
        }
    }

    for (int32 Index = RaincallerOccupants.Num() - 1; Index >= 0; --Index)
    {
        RemoveRaincallerOccupantAtIndex(Index);
    }

    RaincallerTemplateCells.Reset();
    RaincallerTemplateExpireRound = INDEX_NONE;
    RaincallerTemplateSourceId = NAME_None;
}

bool USkaldAbilityComponent::DoesAnyCellMatchRaincaller(const TArray<FIntPoint>& Cells) const
{
    if (RaincallerTemplateCells.Num() == 0)
    {
        return false;
    }

    for (const FIntPoint& Cell : Cells)
    {
        for (const FSkaldRaincallerCell& RainCell : RaincallerTemplateCells)
        {
            if (RainCell.Cell == Cell)
            {
                return true;
            }
        }
    }

    return false;
}

int32 USkaldAbilityComponent::FindRaincallerOccupantIndex(AFighterPawn* Fighter) const
{
    if (!Fighter)
    {
        return INDEX_NONE;
    }

    for (int32 Index = RaincallerOccupants.Num() - 1; Index >= 0; --Index)
    {
        const FRaincallerOccupantState& Entry = RaincallerOccupants[Index];
        if (Entry.Fighter.IsValid() && Entry.Fighter.Get() == Fighter)
        {
            return Index;
        }
    }

    return INDEX_NONE;
}

void USkaldAbilityComponent::RemoveRaincallerOccupantAtIndex(int32 Index)
{
    if (!RaincallerOccupants.IsValidIndex(Index))
    {
        return;
    }

    const FRaincallerOccupantState Entry = RaincallerOccupants[Index];
    if (AFighterPawn* Fighter = Entry.Fighter.Get())
    {
        ApplyTargetStatDelta(Fighter, Entry.AppliedDelta, false);

        const FName SourceAbilityId = !Entry.SourceAbilityId.IsNone() ? Entry.SourceAbilityId : RaincallerAbilityId;
        const FSkaldAbilityDefinition Definition = GetAbilityDefinitionById(SourceAbilityId);
        const FText AbilityName = Definition.IsValid() ? Definition.AbilityName : FText::FromName(SourceAbilityId);
        Fighter->NotifyStatusEffectRemoved(SourceAbilityId, AbilityName, Entry.AppliedDelta);

        if (Entry.bAmphibiousApplied)
        {
            if (USkaldAbilityComponent* Ability = Fighter->FindComponentByClass<USkaldAbilityComponent>())
            {
                Ability->RemoveRaincallerAmphibiousStack();
            }
        }
    }

    RaincallerOccupants.RemoveAtSwap(Index);
}

void USkaldAbilityComponent::RefreshRaincallerOccupants()
{
    if (GetOwnerRole() != ROLE_Authority)
    {
        return;
    }

    if (RaincallerTemplateCells.Num() == 0)
    {
        for (int32 Index = RaincallerOccupants.Num() - 1; Index >= 0; --Index)
        {
            RemoveRaincallerOccupantAtIndex(Index);
        }
        return;
    }

    if (!CachedBattleManager.IsValid())
    {
        TryRegisterBattleDelegates();
    }

    if (!CachedBattleManager.IsValid())
    {
        return;
    }

    const TArray<AFighterPawn*> Fighters = CachedBattleManager->GetInitiativeOrderSnapshot();
    for (AFighterPawn* Fighter : Fighters)
    {
        UpdateRaincallerEffectForFighter(Fighter);
    }
}

void USkaldAbilityComponent::HandleRaincallerOnMovement(AFighterPawn* Fighter)
{
    if (GetOwnerRole() != ROLE_Authority || RaincallerTemplateCells.Num() == 0)
    {
        return;
    }

    UpdateRaincallerEffectForFighter(Fighter);
}

void USkaldAbilityComponent::UpdateRaincallerEffectForFighter(AFighterPawn* Fighter)
{
    if (GetOwnerRole() != ROLE_Authority || RaincallerTemplateCells.Num() == 0 || !Fighter)
    {
        return;
    }

    for (int32 Index = RaincallerOccupants.Num() - 1; Index >= 0; --Index)
    {
        if (!RaincallerOccupants[Index].Fighter.IsValid())
        {
            RemoveRaincallerOccupantAtIndex(Index);
        }
    }

    const int32 ExistingIndex = FindRaincallerOccupantIndex(Fighter);
    const bool bInsideTemplate = Fighter->IsAlive() && DoesAnyCellMatchRaincaller(Fighter->GetOccupiedCells());

    if (bInsideTemplate && ExistingIndex == INDEX_NONE)
    {
        AFighterPawn* Owner = CachedFighter.Get();
        const bool bIsAlly = Owner && Fighter->Faction == Owner->Faction;

        FSkaldAbilityStatDelta Delta;
        if (bIsAlly)
        {
            Delta.Defence = 1;
        }
        else
        {
            Delta.AttackDice = -1;
        }

        const FName SourceAbilityId = !RaincallerTemplateSourceId.IsNone() ? RaincallerTemplateSourceId : RaincallerAbilityId;
        const FSkaldAbilityDefinition Definition = GetAbilityDefinitionById(SourceAbilityId);
        const FText AbilityName = Definition.IsValid() ? Definition.AbilityName : FText::FromName(SourceAbilityId);

        ApplyTargetStatDelta(Fighter, Delta, true);
        Fighter->NotifyStatusEffectApplied(SourceAbilityId, AbilityName, Delta);

        bool bAmphibiousApplied = false;
        if (bIsAlly)
        {
            if (USkaldAbilityComponent* Ability = Fighter->FindComponentByClass<USkaldAbilityComponent>())
            {
                Ability->AddRaincallerAmphibiousStack();
                bAmphibiousApplied = true;
            }
        }

        FRaincallerOccupantState& Entry = RaincallerOccupants.Emplace_GetRef();
        Entry.Fighter = Fighter;
        Entry.bIsAlly = bIsAlly;
        Entry.bAmphibiousApplied = bAmphibiousApplied;
        Entry.AppliedDelta = Delta;
        Entry.SourceAbilityId = SourceAbilityId;
    }
    else if (!bInsideTemplate && ExistingIndex != INDEX_NONE)
    {
        RemoveRaincallerOccupantAtIndex(ExistingIndex);
    }
}

void USkaldAbilityComponent::UpdateRaincallerTemplateLifetime()
{
    if (GetOwnerRole() != ROLE_Authority || RaincallerTemplateCells.Num() == 0)
    {
        return;
    }

    if (RaincallerTemplateExpireRound == INDEX_NONE || !CachedBattleManager.IsValid())
    {
        return;
    }

    const int32 CurrentRound = CachedBattleManager->GetCurrentRound();
    if (CurrentRound != INDEX_NONE && CurrentRound >= RaincallerTemplateExpireRound)
    {
        ClearRaincallerTemplate();
    }
}

void USkaldAbilityComponent::UpdateViralLashCarriers()
{
    if (GetOwnerRole() != ROLE_Authority)
    {
        return;
    }

    const int32 CurrentRound = CachedBattleManager.IsValid() ? CachedBattleManager->GetCurrentRound() : INDEX_NONE;
    for (int32 Index = ViralLashCarriers.Num() - 1; Index >= 0; --Index)
    {
        const bool bInvalidRound = (CurrentRound != INDEX_NONE && ViralLashCarriers[Index].RoundNumber != INDEX_NONE
            && ViralLashCarriers[Index].RoundNumber < CurrentRound);
        if (!ViralLashCarriers[Index].Carrier.IsValid() || bInvalidRound)
        {
            ViralLashCarriers.RemoveAtSwap(Index);
        }
    }

    for (auto It = ViralLashDebuffedFighters.CreateIterator(); It; ++It)
    {
        if (!It->IsValid())
        {
            It.RemoveCurrent();
        }
    }
}

bool USkaldAbilityComponent::ApplyViralLashDebuffToFighter(AFighterPawn* Fighter)
{
    if (!Fighter || GetOwnerRole() != ROLE_Authority)
    {
        return false;
    }

    for (auto It = ViralLashDebuffedFighters.CreateIterator(); It; ++It)
    {
        if (!It->IsValid())
        {
            It.RemoveCurrent();
        }
    }

    if (ViralLashDebuffedFighters.Contains(Fighter))
    {
        return false;
    }

    FSkaldActiveAbilityModifier Modifier;
    Modifier.SourceAbilityId = TEXT("Ability_Inflicted_Skirmish");
    Modifier.Delta.Defence = -1;
    Modifier.bRemoveOnRoundStart = true;
    ApplyModifierToTarget(Fighter, MoveTemp(Modifier));
    ViralLashDebuffedFighters.Add(Fighter);
    return true;
}

void USkaldAbilityComponent::HandleViralLashCarrierDamaged(AFighterPawn* Defender, const FDiceRollResult& Result)
{
    if (GetOwnerRole() != ROLE_Authority || !Defender || ViralLashCarriers.Num() == 0)
    {
        return;
    }

    if (Result.TotalDamage <= 0 && Result.EndingHealth >= Result.StartingHealth)
    {
        return;
    }

    UpdateViralLashCarriers();

    const int32 CurrentRound = CachedBattleManager.IsValid() ? CachedBattleManager->GetCurrentRound() : INDEX_NONE;
    FSkaldViralLashCarrierState* State = ViralLashCarriers.FindByPredicate([
        Defender
    ](const FSkaldViralLashCarrierState& Entry)
    {
        return Entry.Carrier.IsValid() && Entry.Carrier.Get() == Defender;
    });

    if (!State || State->bHasSpread)
    {
        return;
    }

    if (State->RoundNumber != INDEX_NONE && CurrentRound != INDEX_NONE && State->RoundNumber != CurrentRound)
    {
        State->bHasSpread = true;
        return;
    }

    if (!CachedBattleManager.IsValid())
    {
        return;
    }

    const TArray<AFighterPawn*> Fighters = CachedBattleManager->GetInitiativeOrderSnapshot();
    for (AFighterPawn* Fighter : Fighters)
    {
        if (!Fighter || Fighter == Defender || Fighter->Faction != Defender->Faction)
        {
            continue;
        }

        if (Defender->GetFootprintDistanceToFighter(Fighter) <= 1)
        {
            ApplyViralLashDebuffToFighter(Fighter);
        }
    }

    State->bHasSpread = true;
}

void USkaldAbilityComponent::HandleActiveFighterChanged(AFighterPawn* NewFighter)
{
    if (GetOwnerRole() != ROLE_Authority || AberrantBloomHazardCells.Num() == 0 || !NewFighter)
    {
        return;
    }

    AFighterPawn* Owner = CachedFighter.Get();
    if (!Owner || NewFighter->Faction == Owner->Faction)
    {
        return;
    }

    if (DoesAnyCellMatchAberrantBloom(NewFighter->GetOccupiedCells()))
    {
        HandleAberrantBloomTriggered(NewFighter);
    }
}

bool USkaldAbilityComponent::TryPerformHarrierDashAdvance(AFighterPawn* Target)
{
    if (GetOwnerRole() != ROLE_Authority || !Target)
    {
        return false;
    }

    AFighterPawn* Owner = CachedFighter.Get();
    if (!Owner)
    {
        return false;
    }

    UGridOverlayComponent* Grid = Owner->GetGrid();
    if (!Grid)
    {
        return false;
    }

    const int32 MaxDistance = Owner->Stats.Movement;
    if (MaxDistance <= 0)
    {
        return false;
    }

    const TArray<FIntPoint> TargetCells = Target->GetOccupiedCells();
    if (TargetCells.Num() == 0)
    {
        return false;
    }

    const FIntPoint OwnerCell = Owner->GetCurrentCell();
    FIntPoint BestCell = FIntPoint(INDEX_NONE, INDEX_NONE);
    int32 BestDistance = TNumericLimits<int32>::Max();

    auto IsCandidateValid = [&](const FIntPoint& Candidate)
    {
        if (!Grid->IsCellInBounds(Candidate) || Grid->IsObscured(Candidate) || Grid->IsOccupied(Candidate))
        {
            return false;
        }

        const int32 DistanceFromOwner = FMath::Max(
            FMath::Abs(Candidate.X - OwnerCell.X),
            FMath::Abs(Candidate.Y - OwnerCell.Y));
        return DistanceFromOwner <= MaxDistance;
    };

    for (const FIntPoint& TargetCell : TargetCells)
    {
        for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
        {
            for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
            {
                if (OffsetX == 0 && OffsetY == 0)
                {
                    continue;
                }

                const FIntPoint Candidate = TargetCell + FIntPoint(OffsetX, OffsetY);
                if (!IsCandidateValid(Candidate))
                {
                    continue;
                }

                const int32 DistanceFromOwner = FMath::Max(
                    FMath::Abs(Candidate.X - OwnerCell.X),
                    FMath::Abs(Candidate.Y - OwnerCell.Y));
                if (DistanceFromOwner < BestDistance)
                {
                    BestDistance = DistanceFromOwner;
                    BestCell = Candidate;
                }
            }
        }
    }

    if (BestCell.X == INDEX_NONE || BestCell.Y == INDEX_NONE)
    {
        return false;
    }

    return Owner->TryTeleportToCell(BestCell, MaxDistance, false);
}

AFighterPawn* USkaldAbilityComponent::ResolveAbilityTargetFromContext(const FName& AbilityId) const
{
    const FSkaldAbilityContext* Context = GetPendingAbilityContext();
    if (!Context || Context->AbilityId != AbilityId)
    {
        return nullptr;
    }

    return Context->TargetFighter.Get();
}

bool USkaldAbilityComponent::ResolveAbilityTargetCellFromContext(const FName& AbilityId, FIntPoint& OutCell) const
{
    const FSkaldAbilityContext* Context = GetPendingAbilityContext();
    if (!Context || Context->AbilityId != AbilityId || !Context->bHasTargetCell)
    {
        return false;
    }

    OutCell = Context->TargetCell;
    return true;
}

bool USkaldAbilityComponent::TryPushFighterOneCellAway(const AFighterPawn* Source, AFighterPawn* Target) const
{
    if (!Source || !Target || Source == Target)
    {
        return false;
    }

    const FIntPoint SourceCell = Source->GetCurrentCell();
    const FIntPoint TargetCell = Target->GetCurrentCell();
    const FIntPoint Step(
        FMath::Clamp(TargetCell.X - SourceCell.X, -1, 1),
        FMath::Clamp(TargetCell.Y - SourceCell.Y, -1, 1));
    if (Step == FIntPoint::ZeroValue)
    {
        return false;
    }

    const FIntPoint Destination = TargetCell + Step;
    return Target->TryForceMoveToCell(Destination, TArray<FIntPoint>(), false, true);
}

bool USkaldAbilityComponent::TryPullFighterOneCellTowards(const AFighterPawn* Source, AFighterPawn* Target) const
{
    if (!Source || !Target || Source == Target)
    {
        return false;
    }

    const FIntPoint SourceCell = Source->GetCurrentCell();
    const FIntPoint TargetCell = Target->GetCurrentCell();
    const FIntPoint Step(
        FMath::Clamp(SourceCell.X - TargetCell.X, -1, 1),
        FMath::Clamp(SourceCell.Y - TargetCell.Y, -1, 1));
    if (Step == FIntPoint::ZeroValue)
    {
        return false;
    }

    const FIntPoint Destination = TargetCell + Step;
    if (Destination == TargetCell)
    {
        return false;
    }

    return Target->TryForceMoveToCell(Destination, TArray<FIntPoint>(), false, true);
}

bool USkaldAbilityComponent::TrySwapAdjacentWithAlly(AFighterPawn* Owner, AFighterPawn* Ally) const
{
    if (!Owner || !Ally)
    {
        return false;
    }

    if (Owner->GetFootprintDistanceToFighter(Ally) > 1)
    {
        return false;
    }

    const FIntPoint OwnerCell = Owner->GetCurrentCell();
    const FIntPoint AllyCell = Ally->GetCurrentCell();
    TArray<FIntPoint> OwnerCells = Owner->GetOccupiedCells(OwnerCell);
    TArray<FIntPoint> AllyCells = Ally->GetOccupiedCells(AllyCell);

    if (!Ally->TryForceMoveToCell(OwnerCell, OwnerCells, false, false))
    {
        return false;
    }

    if (Owner->TryForceMoveToCell(AllyCell, AllyCells, false, false))
    {
        return true;
    }

    // Owner failed to move, restore the ally.
    Ally->TryForceMoveToCell(AllyCell, TArray<FIntPoint>(), false, false);
    return false;
}

bool USkaldAbilityComponent::TryShiftFighterOneCell(AFighterPawn* Fighter, const AFighterPawn* Reference) const
{
    if (!Fighter)
    {
        return false;
    }

    const FIntPoint StartCell = Fighter->GetCurrentCell();
    TArray<FIntPoint> CandidateAnchors;
    CandidateAnchors.Reserve(8);

    for (int32 Y = -1; Y <= 1; ++Y)
    {
        for (int32 X = -1; X <= 1; ++X)
        {
            if (X == 0 && Y == 0)
            {
                continue;
            }

            CandidateAnchors.Add(StartCell + FIntPoint(X, Y));
        }
    }

    if (Reference)
    {
        const FIntPoint ReferenceCell = Reference->GetCurrentCell();
        Algo::SortBy(CandidateAnchors, [ReferenceCell](const FIntPoint& Cell)
            {
                return FMath::Max(FMath::Abs(Cell.X - ReferenceCell.X),
                    FMath::Abs(Cell.Y - ReferenceCell.Y));
            });
    }

    TSet<FIntPoint> TriedAnchors;
    for (const FIntPoint& Anchor : CandidateAnchors)
    {
        if (TriedAnchors.Contains(Anchor))
        {
            continue;
        }

        TriedAnchors.Add(Anchor);

        if (Fighter->TryForceMoveToCell(Anchor, TArray<FIntPoint>(), false, false))
        {
            return true;
        }
    }

    return false;
}

void USkaldAbilityComponent::TryShiftFighterMultipleCells(AFighterPawn* Fighter, const AFighterPawn* Reference, int32 MaxSteps) const
{
    if (!Fighter || MaxSteps <= 0)
    {
        return;
    }

    for (int32 Step = 0; Step < MaxSteps; ++Step)
    {
        if (!TryShiftFighterOneCell(Fighter, Reference))
        {
            break;
        }
    }
}

void USkaldAbilityComponent::HandleBrutalChargeResolved(AFighterPawn* Defender, const FDiceRollResult& Result)
{
    if (!Defender || Result.HitCount <= 0 || BrutalChargeDistanceMoved < 4)
    {
        return;
    }

    const int32 ExtraDamage = Result.HitCount;
    if (ExtraDamage <= 0)
    {
        return;
    }

    const int32 PreviousHealth = Defender->Stats.Health;
    Defender->Stats.Health = FMath::Max(0, Defender->Stats.Health - ExtraDamage);
    if (Defender->Stats.Health != PreviousHealth)
    {
        Defender->OnHealthChanged.Broadcast(Defender->Stats.Health);
    }
}

void USkaldAbilityComponent::HandleRuneRiposteTriggered(AFighterPawn* Attacker, const FDiceRollResult& Result)
{
    if (!Attacker)
    {
        return;
    }

    AFighterPawn* OwnerFighter = CachedFighter.Get();
    if (!OwnerFighter)
    {
        return;
    }

    if (Attacker->GetAttackType() != EFighterAttackType::Melee)
    {
        return;
    }

    const int32 BaseDamage = FMath::Max(0, OwnerFighter->Stats.AttackDamage);
    if (BaseDamage <= 0)
    {
        return;
    }

    const int32 MissCount = FMath::Max(0, Result.MissCount);
    if (MissCount <= 0)
    {
        return;
    }

    const int32 DamageToApply = BaseDamage * MissCount;
    ApplyDamageToFighter(Attacker, DamageToApply);
}

void USkaldAbilityComponent::HandleOwnerHealthChanged(int32 NewHealth)
{
    if (GetOwnerRole() != ROLE_Authority)
    {
        return;
    }

    const int32 PreviousHealth = LastKnownHealth;

    AFighterPawn* Fighter = CachedFighter.Get();

    if (Fighter)
    {
        if (PassiveAbility.AbilityId == TEXT("Ability_Elf_Passive") && bElfEvasionActive && NewHealth < PreviousHealth)
        {
            bElfEvasionActive = false;
            HandlePassiveEffectRemoved(PassiveAbility.AbilityId);
        }

        LastKnownHealth = NewHealth;

        if (NewHealth <= 0 && RaincallerTemplateCells.Num() > 0)
        {
            ClearRaincallerTemplate();
        }
    }

    const bool bUndeadPassive = PassiveAbility.AbilityId == TEXT("Ability_Undead_Passive");
    const bool bDroppedToCritical = NewHealth <= 1 && PreviousHealth > 1;
    const bool bRecoveredFromCritical = NewHealth > 1 && PreviousHealth <= 1;

    if (bUndeadPassive && PassiveAbility.IsValid())
    {
        if (bDroppedToCritical && !bUndeadResilienceActive)
        {
            bUndeadResilienceActive = true;

            if (bLowHealthStrengthPenaltyActive)
            {
                RemoveModifiersByAbilityId(LowHealthPenaltyId);
                bLowHealthStrengthPenaltyActive = false;
            }

            constexpr int32 UndeadCriticalDefenceBonus = 1;
            constexpr int32 UndeadCriticalAttackDiceBonus = 1;

            FSkaldActiveAbilityModifier ResilienceModifier;
            ResilienceModifier.SourceAbilityId = PassiveAbility.AbilityId;
            ResilienceModifier.Delta.Defence = UndeadCriticalDefenceBonus;
            ResilienceModifier.Delta.AttackDice = UndeadCriticalAttackDiceBonus;
            AddActiveModifier(MoveTemp(ResilienceModifier));
        }
        else if (bRecoveredFromCritical && bUndeadResilienceActive)
        {
            bUndeadResilienceActive = false;
            RemoveModifiersByAbilityId(PassiveAbility.AbilityId);
        }
    }
    else
    {
        if (bDroppedToCritical && !bLowHealthStrengthPenaltyActive && Fighter)
        {
            FSkaldActiveAbilityModifier Modifier;
            Modifier.SourceAbilityId = LowHealthPenaltyId;
            Modifier.Delta.Strength = -1;
            Modifier.bRemoveWhenRoundsExpire = true;
            Modifier.RemainingRounds = 0;
            AddActiveModifier(MoveTemp(Modifier));
            bLowHealthStrengthPenaltyActive = true;
        }
        else if (bRecoveredFromCritical && bLowHealthStrengthPenaltyActive)
        {
            RemoveModifiersByAbilityId(LowHealthPenaltyId);
            bLowHealthStrengthPenaltyActive = false;
        }
    }

    if (NewHealth <= 0 && bUndeadResilienceActive && PassiveAbility.IsValid())
    {
        bUndeadResilienceActive = false;
        RemoveModifiersByAbilityId(PassiveAbility.AbilityId);
    }

    if (!bDeathlessAdvanceReady || NewHealth > 0)
    {
        return;
    }

    bDeathlessAdvanceReady = false;

    if (!Fighter)
    {
        return;
    }

    Fighter->Stats.Health = FMath::Max(1, Fighter->Stats.Health);
    Fighter->OnHealthChanged.Broadcast(Fighter->Stats.Health);

    FSkaldActiveAbilityModifier Modifier;
    Modifier.SourceAbilityId = TEXT("Ability_Undead_Elite");
    Modifier.Delta.AttackDice = 2;
    Modifier.RemainingRounds = 1;
    Modifier.bRemoveOnRoundStart = true;
    AddActiveModifier(MoveTemp(Modifier));

    for (auto It = AbilitySlots.CreateIterator(); It; ++It)
    {
        if (It->Value.Definition.AbilityId == TEXT("Ability_Undead_Elite"))
        {
            It.RemoveCurrent();
            break;
        }
    }

    UpdateReplicatedAbilitySlots();
    BroadcastStateChanged();
}

FSkaldAbilityStatDelta USkaldAbilityComponent::ApplyTargetStatDelta(AFighterPawn* Target, const FSkaldAbilityStatDelta& Delta, bool bApply)
{
    if (!Target)
    {
        return FSkaldAbilityStatDelta();
    }

    FSkaldAbilityStatDelta AppliedDelta;

    auto ApplyIntDelta = [bApply](int32& Stat, int32 Amount)
    {
        if (Amount == 0)
        {
            return 0;
        }

        const int32 DeltaValue = bApply ? Amount : -Amount;
        const int32 PreviousValue = Stat;
        Stat = FMath::Max(0, Stat + DeltaValue);

        return Stat - PreviousValue;
    };

    auto ApplyIntDeltaAndRecord = [&](int32& Stat, int32 Amount, int32& OutRecorded)
    {
        if (Amount == 0)
        {
            return;
        }

        const int32 AppliedAmount = ApplyIntDelta(Stat, Amount);
        if (bApply)
        {
            OutRecorded = AppliedAmount;
        }
    };

    ApplyIntDeltaAndRecord(Target->Stats.AttackDice, Delta.AttackDice, AppliedDelta.AttackDice);
    ApplyIntDeltaAndRecord(Target->Stats.AttackDamage, Delta.AttackDamage, AppliedDelta.AttackDamage);

    auto ApplyTypedAttackDamage = [&](int32 Amount, EFighterAttackType RequiredType, int32& OutRecorded)
    {
        if (Amount == 0)
        {
            return;
        }

        const bool bShouldApply = bApply ? Target->GetAttackType() == RequiredType : true;
        if (!bShouldApply)
        {
            return;
        }

        const int32 DeltaValue = bApply ? Amount : -Amount;
        const int32 PreviousValue = Target->Stats.AttackDamage;
        Target->Stats.AttackDamage = FMath::Max(0, Target->Stats.AttackDamage + DeltaValue);

        if (bApply)
        {
            OutRecorded = Target->Stats.AttackDamage - PreviousValue;
        }
    };

    ApplyTypedAttackDamage(Delta.MeleeAttackDamage, EFighterAttackType::Melee, AppliedDelta.MeleeAttackDamage);
    ApplyTypedAttackDamage(Delta.RangedAttackDamage, EFighterAttackType::Ranged, AppliedDelta.RangedAttackDamage);
    ApplyIntDeltaAndRecord(Target->Stats.Movement, Delta.Movement, AppliedDelta.Movement);
    ApplyIntDeltaAndRecord(Target->Stats.Defence, Delta.Defence, AppliedDelta.Defence);
    ApplyIntDeltaAndRecord(Target->Stats.Strength, Delta.Strength, AppliedDelta.Strength);
    ApplyIntDeltaAndRecord(Target->Stats.CriticalBonusDamage, Delta.CriticalBonusDamage, AppliedDelta.CriticalBonusDamage);

    return AppliedDelta;
}

void USkaldAbilityComponent::ApplyModifierToTarget(AFighterPawn* Target, FSkaldActiveAbilityModifier&& Modifier)
{
    if (!Target)
    {
        return;
    }

    if (USkaldAbilityComponent* TargetAbility = Target->FindComponentByClass<USkaldAbilityComponent>())
    {
        TargetAbility->ReceiveExternalModifier(MoveTemp(Modifier));
        return;
    }

    if (GetOwnerRole() != ROLE_Authority)
    {
        return;
    }

    const FSkaldAbilityStatDelta AppliedDelta = ApplyTargetStatDelta(Target, Modifier.Delta, true);

    const FSkaldAbilityDefinition Definition = GetAbilityDefinitionById(Modifier.SourceAbilityId);
    const FText AbilityName = Definition.IsValid() ? Definition.AbilityName : FText::FromName(Modifier.SourceAbilityId);
    Target->NotifyStatusEffectApplied(Modifier.SourceAbilityId, AbilityName, AppliedDelta);

    FSkaldExternalTargetModifier& ExternalModifier = ActiveTargetModifiers.Emplace_GetRef();
    ExternalModifier.Target = Target;
    ExternalModifier.SourceAbilityId = Modifier.SourceAbilityId;
    ExternalModifier.Delta = AppliedDelta;
}

void USkaldAbilityComponent::RemoveModifiersByAbilityId(FName AbilityId)
{
    if (GetOwnerRole() != ROLE_Authority || AbilityId.IsNone())
    {
        return;
    }

    for (int32 Index = ActiveModifiers.Num() - 1; Index >= 0; --Index)
    {
        if (ActiveModifiers[Index].SourceAbilityId == AbilityId)
        {
            RemoveActiveModifier(Index);
        }
    }

    RemoveTargetModifiersByAbilityId(AbilityId);
}

void USkaldAbilityComponent::ConsumeOncePerBattleAbility(FName AbilityId)
{
    if (GetOwnerRole() != ROLE_Authority || AbilityId.IsNone())
    {
        return;
    }

    for (auto It = AbilitySlots.CreateIterator(); It; ++It)
    {
        if (It->Value.Definition.AbilityId == AbilityId)
        {
            It.RemoveCurrent();
            break;
        }
    }

    UpdateReplicatedAbilitySlots();
    BroadcastStateChanged();
}

bool USkaldAbilityComponent::TryResolveFactionAbilitySet(ESkaldFaction InFaction, FSkaldFactionAbilitySet& OutSet)
{
    const UEnum* FactionEnum = StaticEnum<ESkaldFaction>();
    const FString FactionName = FactionEnum ? FactionEnum->GetNameStringByValue(static_cast<int64>(InFaction)) : FString(TEXT("Unknown"));

    bool bConsultedDataTable = false;
    if (UDataTable* Table = GetFactionAbilityDataTable())
    {
        bConsultedDataTable = true;
        const FString Context = TEXT("USkaldAbilityComponent::TryResolveFactionAbilitySet");
        const FName RowName = BuildFactionRowName(InFaction);
        if (!RowName.IsNone())
        {
            if (const FSkaldFactionAbilityTableRow* TableRow = Table->FindRow<FSkaldFactionAbilityTableRow>(RowName, Context, false))
            {
                OutSet = TableRow->AbilitySet;
                return true;
            }
        }

        const UScriptStruct* const ExpectedRowStruct = FSkaldFactionAbilityTableRow::StaticStruct();
        const UScriptStruct* const TableRowStruct = Table->GetRowStruct();
        if (TableRowStruct == ExpectedRowStruct)
        {
            for (const TPair<FName, uint8*>& RowPair : Table->GetRowMap())
            {
                if (const FSkaldFactionAbilityTableRow* TableRow = reinterpret_cast<const FSkaldFactionAbilityTableRow*>(RowPair.Value))
                {
                    if (TableRow->Faction == InFaction)
                    {
                        OutSet = TableRow->AbilitySet;
                        return true;
                    }
                }
            }
        }
        else
        {
            UE_LOG(
                LogSkald,
                Warning,
                TEXT(
                    "Faction ability table '%s' uses row struct '%s' but '%s' is required; skipping fallback iteration."),
                *FactionAbilityTable.ToSoftObjectPath().ToString(),
                TableRowStruct ? *TableRowStruct->GetName() : TEXT("<null>"),
                *ExpectedRowStruct->GetName());
        }
    }

    if (const FSkaldFactionAbilitySet* StaticSet = FindFactionAbilitySet(InFaction))
    {
        if (bConsultedDataTable)
        {
            UE_LOG(
                LogSkald,
                Warning,
                TEXT("Faction ability table '%s' lacks an entry for faction '%s'; falling back to built-in defaults."),
                *FactionAbilityTable.ToSoftObjectPath().ToString(),
                *FactionName);
        }
        OutSet = *StaticSet;
        return true;
    }

    if (bConsultedDataTable)
    {
        UE_LOG(
            LogSkald,
            Warning,
            TEXT("Faction ability table '%s' missing entry for faction '%s' and no built-in default is available."),
            *FactionAbilityTable.ToSoftObjectPath().ToString(),
            *FactionName);
    }

    return false;
}

FSkaldAbilityDefinition USkaldAbilityComponent::ResolveActiveAbilityForCost(const FSkaldFactionAbilitySet& AbilitySet, int32 ArmyCost) const
{
    const ESkaldAbilityTier Tier = ResolveAbilityTierForCost(ArmyCost);
    switch (Tier)
    {
    case ESkaldAbilityTier::Skirmish:
        return AbilitySet.SkirmishAbility;
    case ESkaldAbilityTier::Line:
        return AbilitySet.LineAbility;
    case ESkaldAbilityTier::Elite:
        return AbilitySet.EliteAbility;
    default:
        break;
    }

    return FSkaldAbilityDefinition();
}

UDataTable* USkaldAbilityComponent::GetFactionAbilityDataTable()
{
    if (FactionAbilityTable.IsNull())
    {
        LoadedAbilityDataTable = nullptr;
        return nullptr;
    }

    UDataTable* Table = FactionAbilityTable.Get();
    if (!Table)
    {
        Table = FactionAbilityTable.LoadSynchronous();
    }

    LoadedAbilityDataTable = Table;
    return LoadedAbilityDataTable;
}

void USkaldAbilityComponent::UpdateReplicatedAbilitySlots()
{
    if (GetOwnerRole() != ROLE_Authority)
    {
        return;
    }

    ReplicatedAbilitySlots.Reset();
    ReplicatedAbilitySlots.Reserve(AbilitySlots.Num());

    TSet<ESkaldAbilitySlot> ProcessedSlots;
    for (ESkaldAbilitySlot Slot : SlotOrder)
    {
        if (const FSkaldAbilityState* State = AbilitySlots.Find(Slot))
        {
            FSkaldReplicatedAbilitySlotState& Entry = ReplicatedAbilitySlots.AddDefaulted_GetRef();
            Entry.Slot = Slot;
            Entry.State = *State;
            ProcessedSlots.Add(Slot);
        }
    }

    for (const TPair<ESkaldAbilitySlot, FSkaldAbilityState>& Pair : AbilitySlots)
    {
        if (!ProcessedSlots.Contains(Pair.Key))
        {
            FSkaldReplicatedAbilitySlotState& Entry = ReplicatedAbilitySlots.AddDefaulted_GetRef();
            Entry.Slot = Pair.Key;
            Entry.State = Pair.Value;
        }
    }
}

