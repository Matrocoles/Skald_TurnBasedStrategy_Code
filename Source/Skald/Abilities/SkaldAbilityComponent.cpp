#include "Abilities/SkaldAbilityComponent.h"

#include "Abilities/SkaldAbilityTypes.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Set.h"
#include "Engine/DataTable.h"
#include "FighterPawn.h"
#include "GridBattleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"
#include "SkaldLogging.h"
#include "Skald_GameInstance.h"
#include "Sound/SoundBase.h"

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

    SlotOrder = {ESkaldAbilitySlot::Ability1, ESkaldAbilitySlot::Ability2, ESkaldAbilitySlot::Ability3};
}

void USkaldAbilityComponent::BeginPlay()
{
    Super::BeginPlay();

    CachedFighter = Cast<AFighterPawn>(GetOwner());
    TryRegisterBattleDelegates();
}

void USkaldAbilityComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
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

    AbilitySlots.Empty();
    bApplyViralLashOnNextAttack = false;
    bApplyScrapperFeintOnNextMiss = false;

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
    if (GetOwnerRole() == ROLE_Authority)
    {
        RemoveExpiredModifiers(ESkaldAbilityModifierPhase::RoundStart);
    }
    UpdateReplicatedAbilitySlots();
    BroadcastStateChanged();
}

void USkaldAbilityComponent::HandleActivationStarted()
{
    ReactionsRemaining = ReactionsPerRound;
    bOwnerAttackedThisActivation = false;
    if (GetOwnerRole() == ROLE_Authority)
    {
        RemoveExpiredModifiers(ESkaldAbilityModifierPhase::ActivationStart);
    }
    BroadcastStateChanged();
}

void USkaldAbilityComponent::HandleActivationFinished()
{
    if (GetOwnerRole() == ROLE_Authority)
    {
        RemoveExpiredModifiers(ESkaldAbilityModifierPhase::ActivationEnd);
        bOwnerAttackedThisActivation = false;
        bApplyViralLashOnNextAttack = false;
        bApplyScrapperFeintOnNextMiss = false;
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
    if (GetOwnerRole() == ROLE_Authority)
    {
        ApplyAbilityEffects(Definition);
    }
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

    if (Definition.AbilityId == TEXT("Ability_Inflicted_Skirmish"))
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
    ApplyIntDelta(Target->Stats.Movement, Modifier.Delta.Movement);
    ApplyIntDelta(Target->Stats.Defence, Modifier.Delta.Defence);
    ApplyIntDelta(Target->Stats.Strength, Modifier.Delta.Strength);
    ApplyIntDelta(Target->Stats.CriticalBonusDamage, Modifier.Delta.CriticalBonusDamage);
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

