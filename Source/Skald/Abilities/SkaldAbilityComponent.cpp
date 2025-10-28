#include "Abilities/SkaldAbilityComponent.h"

#include "Abilities/SkaldAbilityTypes.h"
#include "FighterPawn.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"
#include "Sound/SoundBase.h"

USkaldAbilityComponent::USkaldAbilityComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);
    ReactionsRemaining = ReactionsPerRound;
    bHasInitialisedLoadout = false;

    SlotOrder = {ESkaldAbilitySlot::Ability1, ESkaldAbilitySlot::Ability2, ESkaldAbilitySlot::Ability3};
}

void USkaldAbilityComponent::BeginPlay()
{
    Super::BeginPlay();

    CachedFighter = Cast<AFighterPawn>(GetOwner());
}

void USkaldAbilityComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(USkaldAbilityComponent, PassiveAbility);
    DOREPLIFETIME(USkaldAbilityComponent, AbilitySlots);
    DOREPLIFETIME(USkaldAbilityComponent, ReactionsRemaining);
    DOREPLIFETIME(USkaldAbilityComponent, bHasInitialisedLoadout);
}

void USkaldAbilityComponent::RefreshAbilityLoadout(const FFighterStats& InStats, ESkaldFaction InFaction)
{
    if (!CachedFighter.IsValid())
    {
        CachedFighter = Cast<AFighterPawn>(GetOwner());
    }

    PassiveAbility = GetFactionPassive(InFaction);

    AbilitySlots.Empty();

    const FSkaldAbilityDefinition ActiveAbility = GetFactionActiveAbility(InFaction, InStats.ArmyCost);
    if (ActiveAbility.IsValid())
    {
        FSkaldAbilityState& SlotState = AbilitySlots.Add(ESkaldAbilitySlot::Ability1);
        SlotState.Definition = ActiveAbility;
        SlotState.CooldownRemaining = 0;
        SlotState.bHasBeenUsed = false;
        SlotState.bIsOnCooldown = false;
    }

    bHasInitialisedLoadout = true;
    ReactionsRemaining = ReactionsPerRound;

    BroadcastStateChanged();
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
    BroadcastStateChanged();
}

void USkaldAbilityComponent::HandleActivationStarted()
{
    ReactionsRemaining = ReactionsPerRound;
    BroadcastStateChanged();
}

bool USkaldAbilityComponent::TryBeginAbility(ESkaldAbilitySlot Slot, FText& OutFailureReason)
{
    if (!bHasInitialisedLoadout)
    {
        OutFailureReason = NSLOCTEXT("SkaldAbilities", "AbilityNotInitialised", "Ability loadout not ready.");
        return false;
    }

    FSkaldAbilityState* State = AbilitySlots.Find(Slot);
    if (!State || !State->Definition.IsValid())
    {
        OutFailureReason = NSLOCTEXT("SkaldAbilities", "AbilityUnavailable", "No ability is assigned to that slot.");
        return false;
    }

    if (State->Definition.bOncePerBattle && State->bHasBeenUsed)
    {
        OutFailureReason = NSLOCTEXT("SkaldAbilities", "AbilityOncePerBattle", "This ability can only be used once per battle.");
        return false;
    }

    if (State->bIsOnCooldown)
    {
        OutFailureReason = NSLOCTEXT("SkaldAbilities", "AbilityOnCooldown", "Ability is on cooldown.");
        return false;
    }

    if (!CanPayCost(State->Definition, OutFailureReason))
    {
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

void USkaldAbilityComponent::HandleAbilityTriggeredLocal(const FSkaldAbilityDefinition& Definition)
{
    PlayAbilityFeedback(Definition);
    OnAbilityTriggered.Broadcast(this, Definition);
}

void USkaldAbilityComponent::MulticastAbilityTriggered_Implementation(const FSkaldAbilityDefinition& Definition)
{
    HandleAbilityTriggeredLocal(Definition);
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

