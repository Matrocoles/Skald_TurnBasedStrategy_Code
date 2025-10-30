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
    bBrutalChargeActive = false;
    BrutalChargeDistanceMoved = 0;
    bRuneRiposteReady = false;
    bVeilStepBonusActive = false;
    bDeathlessAdvanceReady = false;
    bShieldWallPivotActive = false;
    ShieldWallPivotProtectedAlly.Reset();
    TacticalReservesRefreshedThisRound.Empty();
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

    SlotOrder = {ESkaldAbilitySlot::Ability1, ESkaldAbilitySlot::Ability2, ESkaldAbilitySlot::Ability3};
}

void USkaldAbilityComponent::BeginPlay()
{
    Super::BeginPlay();

    CachedFighter = Cast<AFighterPawn>(GetOwner());
    if (AFighterPawn* Fighter = CachedFighter.Get())
    {
        Fighter->OnHealthChanged.AddDynamic(this, &USkaldAbilityComponent::HandleOwnerHealthChanged);
    }
    TryRegisterBattleDelegates();
}

void USkaldAbilityComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    ClearAllTraps();
    RemoveBattleDelegates();
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
    bBrutalChargeActive = false;
    BrutalChargeDistanceMoved = 0;
    bRuneRiposteReady = false;
    bVeilStepBonusActive = false;
    bDeathlessAdvanceReady = false;
    bGoblinFlashBombActive = false;
    bGoblinNetActive = false;
    bGoblinAmbushActive = false;
    bGoblinAmbushPenaltyPending = false;

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
        }
    }

    bHasInitialisedLoadout = true;
    ReactionsRemaining = ReactionsPerRound;

    UpdateReplicatedAbilitySlots();

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
    bBrutalChargeActive = false;
    BrutalChargeDistanceMoved = 0;
    bRuneRiposteReady = false;
    bVeilStepBonusActive = false;
    bShieldWallPivotActive = false;
    ShieldWallPivotProtectedAlly.Reset();
    TacticalReservesRefreshedThisRound.Empty();
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
        bBrutalChargeActive = false;
        BrutalChargeDistanceMoved = 0;
        bVeilStepBonusActive = false;
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
                    ShieldWallPivotProtectedAlly = BestAlly;
                    FSkaldActiveAbilityModifier Modifier;
                    Modifier.SourceAbilityId = Definition.AbilityId;
                    Modifier.Delta.Defence = 1;
                    Modifier.bRemoveOnActivationStart = true;
                    ApplyModifierToTarget(BestAlly, MoveTemp(Modifier));
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
                TArray<AFighterPawn*> Fighters = CachedBattleManager->GetInitiativeOrderSnapshot();
                Fighters.RemoveAll([OwnerFighter](AFighterPawn* Fighter)
                    { return !Fighter || Fighter->Faction != OwnerFighter->Faction || Fighter == OwnerFighter; });

                Algo::SortBy(Fighters, [OwnerFighter](AFighterPawn* Fighter)
                    { return OwnerFighter->GetFootprintDistanceToFighter(Fighter); });

                int32 RefreshedCount = 0;
                for (AFighterPawn* Fighter : Fighters)
                {
                    if (!Fighter || RefreshedCount >= 2)
                    {
                        break;
                    }

                    const int32 Distance = OwnerFighter->GetFootprintDistanceToFighter(Fighter);
                    if (Distance > 3)
                    {
                        continue;
                    }

                    if (TacticalReservesRefreshedThisRound.Contains(Fighter))
                    {
                        continue;
                    }

                    bool bRefreshed = Fighter->TryRestoreAction();
                    if (!bRefreshed)
                    {
                        bRefreshed = Fighter->TryRestoreReaction();
                    }

                    if (bRefreshed)
                    {
                        TacticalReservesRefreshedThisRound.Add(Fighter);
                        ++RefreshedCount;
                    }
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
                    Modifier.Delta.Movement = 1;
                    Modifier.bRemoveOnActivationStart = true;
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
    else if (Definition.AbilityId == TEXT("Ability_Lizardfolk_Skirmish"))
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
    else if (Definition.AbilityId == TEXT("Ability_Lizardfolk_Line"))
    {
        FSkaldActiveAbilityModifier Modifier;
        Modifier.SourceAbilityId = Definition.AbilityId;
        Modifier.Delta.Movement = 2;
        Modifier.Delta.AttackDice = 1;
        Modifier.bRemoveOnActivationEnd = true;
        AddActiveModifier(MoveTemp(Modifier));
    }
    else if (Definition.AbilityId == TEXT("Ability_Lizardfolk_Elite"))
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

            FSkaldActiveAbilityModifier Modifier;
            Modifier.SourceAbilityId = Definition.AbilityId;
            Modifier.Delta.AttackDamage = -1;
            Modifier.bRemoveOnActivationEnd = true;
            AddActiveModifier(MoveTemp(Modifier));
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
                        ApplyDamageToFighter(Fighter, OwnerFighter->Stats.AttackDamage);

                        FSkaldActiveAbilityModifier Modifier;
                        Modifier.SourceAbilityId = Definition.AbilityId;
                        Modifier.Delta.Movement = -1;
                        Modifier.bRemoveOnRoundStart = true;
                        ApplyModifierToTarget(Fighter, MoveTemp(Modifier));
                    }
                }
            }

            ConsumeOncePerBattleAbility(Definition.AbilityId);
        }
    }
    else if (Definition.AbilityId == TEXT("Ability_Frogfolk_Skirmish"))
    {
        if (GetOwnerRole() == ROLE_Authority)
        {
            AFighterPawn* OwnerFighter = CachedFighter.Get();
            if (OwnerFighter && CachedBattleManager.IsValid())
            {
                const TArray<AFighterPawn*> Fighters = CachedBattleManager->GetInitiativeOrderSnapshot();
                AFighterPawn* BestEnemy = nullptr;
                int32 BestDistance = TNumericLimits<int32>::Max();
                for (AFighterPawn* Fighter : Fighters)
                {
                    if (!Fighter || Fighter->Faction == OwnerFighter->Faction)
                    {
                        continue;
                    }

                    const int32 Distance = OwnerFighter->GetFootprintDistanceToFighter(Fighter);
                    if (Distance <= 4 && Distance < BestDistance)
                    {
                        BestEnemy = Fighter;
                        BestDistance = Distance;
                    }
                }

                if (BestEnemy)
                {
                    FSkaldActiveAbilityModifier Modifier;
                    Modifier.SourceAbilityId = Definition.AbilityId;
                    Modifier.Delta.AttackDice = -1;
                    Modifier.bRemoveOnRoundStart = true;
                    ApplyModifierToTarget(BestEnemy, MoveTemp(Modifier));
                }
            }
        }
    }
    else if (Definition.AbilityId == TEXT("Ability_Frogfolk_Line"))
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
    else if (Definition.AbilityId == TEXT("Ability_Frogfolk_Elite"))
    {
        if (GetOwnerRole() == ROLE_Authority)
        {
            AFighterPawn* OwnerFighter = CachedFighter.Get();
            if (OwnerFighter && CachedBattleManager.IsValid())
            {
                const TArray<AFighterPawn*> Fighters = CachedBattleManager->GetInitiativeOrderSnapshot();
                for (AFighterPawn* Fighter : Fighters)
                {
                    if (!Fighter)
                    {
                        continue;
                    }

                    const int32 Distance = OwnerFighter->GetFootprintDistanceToFighter(Fighter);
                    if (Distance <= 2)
                    {
                        FSkaldActiveAbilityModifier Modifier;
                        Modifier.SourceAbilityId = Definition.AbilityId;
                        if (Fighter->Faction == OwnerFighter->Faction)
                        {
                            Modifier.Delta.Defence = 1;
                        }
                        else
                        {
                            Modifier.Delta.AttackDice = -1;
                        }
                        Modifier.RemainingRounds = 1;
                        Modifier.bRemoveOnRoundStart = true;
                        ApplyModifierToTarget(Fighter, MoveTemp(Modifier));
                    }
                }
            }
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

    ApplyDefaultModifierDuration(Modifier);
    ApplyStatDeltaToOwner(Modifier.Delta, true);
    ActiveModifiers.Add(MoveTemp(Modifier));
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

    ApplyStatDeltaToOwner(ActiveModifiers[Index].Delta, false);
    ActiveModifiers.RemoveAtSwap(Index);
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
        }
    }
}

void USkaldAbilityComponent::RemoveBattleDelegates()
{
    if (UGridBattleManager* BattleManager = CachedBattleManager.Get())
    {
        BattleManager->OnAttackResolved.RemoveDynamic(this, &USkaldAbilityComponent::HandleBattleAttackResolved);
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
            }

            bSmashThroughActive = false;
        }

        if (bDeepDelveMortarPending)
        {
            if (Defender && Result.HitCount > 0)
            {
                if (CachedBattleManager.IsValid())
                {
                    const TArray<AFighterPawn*> Fighters = CachedBattleManager->GetInitiativeOrderSnapshot();
                    const int32 SplashDamage = FMath::Max(1, OwnerFighter->Stats.AttackDamage / 2);
                    for (AFighterPawn* Fighter : Fighters)
                    {
                        if (!Fighter || Fighter == Defender)
                        {
                            continue;
                        }

                        if (Defender->GetFootprintDistanceToFighter(Fighter) <= 1)
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
                FSkaldActiveAbilityModifier Modifier;
                Modifier.SourceAbilityId = TEXT("Ability_Gnoll_Skirmish");
                Modifier.Delta.Defence = -1;
                Modifier.bRemoveOnRoundStart = true;
                ApplyModifierToTarget(Defender, MoveTemp(Modifier));
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
                for (AFighterPawn* Fighter : Fighters)
                {
                    if (!Fighter || Fighter == Defender)
                    {
                        continue;
                    }

                    if (Defender->GetFootprintDistanceToFighter(Fighter) <= 2)
                    {
                        const bool bAlly = Fighter->Faction == OwnerFighter->Faction;
                        const int32 DamageToApply = bAlly ? FMath::Max(1, BaseDamage / 2) : BaseDamage;
                        ApplyDamageToFighter(Fighter, DamageToApply);
                    }
                }
            }

            bArtilleryStrikePending = false;
        }

    }
    else if (Defender == OwnerFighter)
    {
        if (bRuneRiposteReady && Result.HitCount > 0)
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
}

void USkaldAbilityComponent::HandleViralLashResolved(AFighterPawn* Defender, const FDiceRollResult& Result)
{
    if (!Defender || Result.HitCount <= 0)
    {
        return;
    }

    FSkaldActiveAbilityModifier Modifier;
    Modifier.SourceAbilityId = TEXT("Ability_Inflicted_Skirmish");
    Modifier.Delta.Defence = -1;
    Modifier.bRemoveOnRoundStart = true;

    ApplyModifierToTarget(Defender, MoveTemp(Modifier));
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
        return;
    }

    AFighterPawn* OwnerFighter = CachedFighter.Get();
    if (!OwnerFighter || !CachedBattleManager.IsValid())
    {
        return;
    }

    const int32 Range = OwnerFighter->Stats.AttackRange;
    AFighterPawn* BestAlly = nullptr;
    int32 BestDistance = TNumericLimits<int32>::Max();

    const TArray<AFighterPawn*> Fighters = CachedBattleManager->GetInitiativeOrderSnapshot();
    for (AFighterPawn* Fighter : Fighters)
    {
        if (!Fighter || Fighter == OwnerFighter || Fighter->Faction != OwnerFighter->Faction)
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

    if (!BestAlly)
    {
        return;
    }

    FSkaldActiveAbilityModifier Modifier;
    Modifier.SourceAbilityId = TEXT("Ability_Human_Skirmish");
    Modifier.Delta.Movement = 1;
    Modifier.RemainingRounds = 1;
    Modifier.bRemoveOnRoundStart = true;
    ApplyModifierToTarget(BestAlly, MoveTemp(Modifier));
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

    if (OwnerFighter->GetAttackType() != EFighterAttackType::Melee)
    {
        return;
    }

    const int32 DamageToApply = FMath::Max(0, OwnerFighter->Stats.AttackDamage);
    if (DamageToApply <= 0)
    {
        return;
    }

    const int32 PreviousHealth = Attacker->Stats.Health;
    Attacker->Stats.Health = FMath::Max(0, Attacker->Stats.Health - DamageToApply);
    if (Attacker->Stats.Health != PreviousHealth)
    {
        Attacker->OnHealthChanged.Broadcast(Attacker->Stats.Health);
    }
}

void USkaldAbilityComponent::HandleOwnerHealthChanged(int32 NewHealth)
{
    if (GetOwnerRole() != ROLE_Authority)
    {
        return;
    }

    if (!bDeathlessAdvanceReady || NewHealth > 0)
    {
        return;
    }

    bDeathlessAdvanceReady = false;

    AFighterPawn* Fighter = CachedFighter.Get();
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

    auto ApplyIntDelta = [](int32& Stat, int32 Amount)
    {
        if (Amount == 0)
        {
            return;
        }

        Stat = FMath::Max(0, Stat + Amount);
    };

    ApplyIntDelta(Target->Stats.AttackDice, Modifier.Delta.AttackDice);
    ApplyIntDelta(Target->Stats.AttackDamage, Modifier.Delta.AttackDamage);

    auto ApplyTypedAttackDamage = [&](int32 Amount, EFighterAttackType RequiredType)
    {
        if (Amount == 0)
        {
            return;
        }

        if (Target->GetAttackType() != RequiredType)
        {
            return;
        }

        Target->Stats.AttackDamage = FMath::Max(0, Target->Stats.AttackDamage + Amount);
    };

    ApplyTypedAttackDamage(Modifier.Delta.MeleeAttackDamage, EFighterAttackType::Melee);
    ApplyTypedAttackDamage(Modifier.Delta.RangedAttackDamage, EFighterAttackType::Ranged);
    ApplyIntDelta(Target->Stats.Movement, Modifier.Delta.Movement);
    ApplyIntDelta(Target->Stats.Defence, Modifier.Delta.Defence);
    ApplyIntDelta(Target->Stats.Strength, Modifier.Delta.Strength);
    ApplyIntDelta(Target->Stats.CriticalBonusDamage, Modifier.Delta.CriticalBonusDamage);
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

