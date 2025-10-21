#include "GridBattleManager.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "FighterPawn.h"
#include "GridOverlayComponent.h"
#include "SkaldLogging.h"
#include "Skald_GameInstance.h"
#include "Skald_GameState.h"
#include "Skald_PlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "UObject/UnrealType.h"

namespace
{
constexpr float BattleConclusionBroadcastDelaySeconds = 1.5f;

FString DescribeFighter(const AFighterPawn* Fighter)
{
    if (!Fighter)
    {
        return TEXT("<None>");
    }

    const FString DisplayName = Fighter->GetHumanReadableName();
    return FString::Printf(TEXT("%s (%s | Attacker=%s | Activated=%s | Alive=%s)"),
        *GetNameSafe(Fighter),
        DisplayName.IsEmpty() ? TEXT("Unnamed") : *DisplayName,
        Fighter->bIsAttacker ? TEXT("true") : TEXT("false"),
        Fighter->HasActivatedThisRound() ? TEXT("true") : TEXT("false"),
        Fighter->IsAlive() ? TEXT("true") : TEXT("false"));
}

int32 CountFighters(const TArray<AFighterPawn*>& Fighters, bool bAttackerOnly)
{
    int32 Count = 0;
    for (const AFighterPawn* Fighter : Fighters)
    {
        if (!Fighter)
        {
            continue;
        }

        if (!Fighter->IsAlive())
        {
            continue;
        }

        if (Fighter->bIsAttacker != bAttackerOnly)
        {
            continue;
        }

        ++Count;
    }
    return Count;
}
} // namespace

UGridBattleManager::UGridBattleManager()
{
    // FighterDefinitions is provided via editor (EditDefaultsOnly)
}

void UGridBattleManager::SetRandomSeed(int32 Seed)
{
    Rng.Initialize(Seed);
}

void UGridBattleManager::InitBattle(const TArray<FFighter>& Attackers, const TArray<FFighter>& Defenders)
{
    AttackerTeam = Attackers;
    DefenderTeam = Defenders;
    CurrentRound = 0;
    InitiativeOrder.Empty();
    ActiveFighter = nullptr;
    CurrentTurn = 0;
    InitiativeWinnerFaction = ESkaldFaction::None;
    bIsAttackerTurn = true;
    AttackerInitialArmyCost = 0;
    for (const FFighter& Fighter : AttackerTeam)
    {
        if (Fighter.Stats.Health > 0)
        {
            AttackerInitialArmyCost += Fighter.Stats.ArmyCost;
        }
    }

    DefenderInitialArmyCost = 0;
    for (const FFighter& Fighter : DefenderTeam)
    {
        if (Fighter.Stats.Health > 0)
        {
            DefenderInitialArmyCost += Fighter.Stats.ArmyCost;
        }
    }

    AttackerSurvivorUnitCount = 0;
    DefenderSurvivorUnitCount = 0;
    AttackerSurvivorArmyCost = 0;
    DefenderSurvivorArmyCost = 0;
    bTeamsAssigned = false;
    bBattleConcluded = false;
    bAwaitingInitiativeRoll = false;
    LastInitiativeRollAttacker = 0;
    LastInitiativeRollDefender = 0;

    UE_LOG(LogSkaldBattle, Log, TEXT("[Battle] Initialised battle: Attackers=%d, Defenders=%d"), AttackerTeam.Num(), DefenderTeam.Num());
}

FDiceRollResult UGridBattleManager::ResolveAttackDice(const FFighterStats& AttackerStats, const FFighterStats& DefenderStats, FRandomStream& RandomStream)
{
    FDiceRollResult Result;
    Result.StartingHealth = DefenderStats.Health;
    Result.EndingHealth = DefenderStats.Health;

    if (AttackerStats.AttackDice <= 0 || Result.StartingHealth <= 0)
    {
        return Result;
    }

    const int32 RequiredRoll = AttackerStats.Strength > DefenderStats.Defence ? 3 :
        (AttackerStats.Strength < DefenderStats.Defence ? 5 : 4);

    const int32 DiceToRoll = FMath::Max(0, AttackerStats.AttackDice);
    Result.DiceOutcomes.Reserve(DiceToRoll);

    int32 SimulatedHealth = Result.StartingHealth;
    for (int32 DieIndex = 0; DieIndex < DiceToRoll && SimulatedHealth > 0; ++DieIndex)
    {
        FDiceRollOutcome& Outcome = Result.DiceOutcomes.AddDefaulted_GetRef();
        Outcome.RollValue = RandomStream.RandRange(1, 6);

        int32 Damage = 0;
        if (Outcome.RollValue == 6)
        {
            Damage = AttackerStats.AttackDamage + AttackerStats.CriticalBonusDamage;
        }
        else if (Outcome.RollValue >= RequiredRoll)
        {
            Damage = AttackerStats.AttackDamage;
        }

        Outcome.Damage = Damage;
        Outcome.bHit = Damage > 0;
        Outcome.bCritical = Outcome.bHit && Outcome.RollValue == 6 && Damage > AttackerStats.AttackDamage;

        if (Outcome.bHit)
        {
            const int32 AppliedDamage = FMath::Min(Damage, SimulatedHealth);
            SimulatedHealth -= AppliedDamage;
            Result.TotalDamage += AppliedDamage;
            ++Result.HitCount;

            if (Outcome.bCritical)
            {
                ++Result.CriticalHitCount;
                Result.HighestCriticalDamage = FMath::Max(Result.HighestCriticalDamage, Damage);
            }
        }
        else
        {
            ++Result.MissCount;
        }
    }

    Result.EndingHealth = SimulatedHealth;
    Result.bHighStakesCritical = Result.CriticalHitCount > 0 && Result.EndingHealth <= 0 && Result.StartingHealth > 0;
    return Result;
}

bool UGridBattleManager::ResolveAttack(FFighter& Attacker, FFighter& Defender, int32& OutDamage, FRandomStream& RandomStream, FDiceRollResult& OutResult)
{
    const int32 StartingHealth = Defender.Stats.Health;

    FDiceRollResult Result = ResolveAttackDice(Attacker.Stats, Defender.Stats, RandomStream);
    Defender.Stats.Health = FMath::Max(0, Result.EndingHealth);

    OutDamage = FMath::Clamp(Result.TotalDamage, 0, StartingHealth);
    if (Result.bHighStakesCritical)
    {
        Result.HighStakesFaction = Attacker.Faction;
    }
    OutResult = Result;

    return Result.EndingHealth > 0;
}

int32 UGridBattleManager::GetAttackerSurvivors()
{
    AttackerSurvivorUnitCount = 0;
    for (const FFighter& Fighter : AttackerTeam)
    {
        if (Fighter.Stats.Health > 0)
        {
            ++AttackerSurvivorUnitCount;
        }
    }
    return AttackerSurvivorUnitCount;
}

int32 UGridBattleManager::GetDefenderSurvivors()
{
    DefenderSurvivorUnitCount = 0;
    for (const FFighter& Fighter : DefenderTeam)
    {
        if (Fighter.Stats.Health > 0)
        {
            ++DefenderSurvivorUnitCount;
        }
    }
    return DefenderSurvivorUnitCount;
}

int32 UGridBattleManager::GetAttackerSurvivorCost() const
{
    return AttackerSurvivorArmyCost;
}

int32 UGridBattleManager::GetDefenderSurvivorCost() const
{
    return DefenderSurvivorArmyCost;
}

AFighterPawn* UGridBattleManager::GetActiveFighter() const
{
    return ActiveFighter;
}

void UGridBattleManager::RegisterFighter(AFighterPawn* Fighter, bool bAsAttacker)
{
    if (!Fighter)
    {
        return;
    }

    Fighter->bIsAttacker = bAsAttacker;
    Fighter->ResetActivationState();

    if (!InitiativeOrder.Contains(Fighter))
    {
        InitiativeOrder.Add(Fighter);
    }
}

void UGridBattleManager::UnregisterFighter(AFighterPawn* Fighter)
{
    if (!Fighter)
    {
        return;
    }

    const bool bWasAttacker = Fighter->bIsAttacker;
    InitiativeOrder.Remove(Fighter);
    if (ActiveFighter == Fighter)
    {
        ActiveFighter = nullptr;
        OnActiveFighterChanged.Broadcast(nullptr);
    }

    EvaluateRoundProgress(bWasAttacker);
}

TArray<FFighterDefinition> UGridBattleManager::GetFightersForFaction(ESkaldFaction Faction) const
{
    TArray<FFighterDefinition> Out;
    if (!FighterDefinitions) return Out;

    FighterDefinitions->ForeachRow<FFighterDefinition>(
        TEXT("GetFightersForFaction"),
        [&](const FName, const FFighterDefinition& Row)
        {
            if (Row.Faction == Faction) Out.Add(Row);
        });
    return Out;
}

void UGridBattleManager::RollInitiative()
{
    if (bBattleConcluded)
    {
        return;
    }

    LastInitiativeRollAttacker = 0;
    LastInitiativeRollDefender = 0;

    const bool bAttackersPresent = HasLivingFighters(true);
    const bool bDefendersPresent = HasLivingFighters(false);

    if (!bAttackersPresent && !bDefendersPresent)
    {
        InitiativeWinnerFaction = ESkaldFaction::None;
        bIsAttackerTurn = true;
        return;
    }

    if (!bAttackersPresent)
    {
        InitiativeWinnerFaction = DefenderTeam.Num() > 0 ? DefenderTeam[0].Faction : ESkaldFaction::None;
        bIsAttackerTurn = false;
        return;
    }

    if (!bDefendersPresent)
    {
        InitiativeWinnerFaction = AttackerTeam.Num() > 0 ? AttackerTeam[0].Faction : ESkaldFaction::None;
        bIsAttackerTurn = true;
        return;
    }

    int32 AttackerRoll = PendingInitiativeRollAttacker.IsSet() ? PendingInitiativeRollAttacker.GetValue() : 0;
    int32 DefenderRoll = PendingInitiativeRollDefender.IsSet() ? PendingInitiativeRollDefender.GetValue() : 0;

    const bool bAttackerRollProvided = PendingInitiativeRollAttacker.IsSet();
    const bool bDefenderRollProvided = PendingInitiativeRollDefender.IsSet();

    int32 Attempts = 0;
    const int32 MaxAttempts = 100;

    while (Attempts < MaxAttempts)
    {
        if (!bAttackerRollProvided && AttackerRoll <= 0)
        {
            AttackerRoll = Rng.RandRange(1, InitiativeDiceSides);
        }

        if (!bDefenderRollProvided && DefenderRoll <= 0)
        {
            DefenderRoll = Rng.RandRange(1, InitiativeDiceSides);
        }

        if (AttackerRoll != DefenderRoll)
        {
            break;
        }

        if (bAttackerRollProvided && bDefenderRollProvided)
        {
            break;
        }

        if (!bAttackerRollProvided)
        {
            AttackerRoll = 0;
        }
        if (!bDefenderRollProvided)
        {
            DefenderRoll = 0;
        }

        ++Attempts;
    }

    if (AttackerRoll == DefenderRoll)
    {
        if (bAttackerRollProvided && !bDefenderRollProvided)
        {
            DefenderRoll = (DefenderRoll % InitiativeDiceSides) + 1;
        }
        else if (!bAttackerRollProvided && bDefenderRollProvided)
        {
            AttackerRoll = (AttackerRoll % InitiativeDiceSides) + 1;
        }
        else
        {
            // As a fallback, bias ties in favour of the attacker to avoid stalling the round start.
            AttackerRoll = InitiativeDiceSides;
            if (DefenderRoll == AttackerRoll)
            {
                DefenderRoll = FMath::Max(1, AttackerRoll - 1);
            }
        }
    }

    LastInitiativeRollAttacker = AttackerRoll;
    LastInitiativeRollDefender = DefenderRoll;

    PendingInitiativeRollAttacker.Reset();
    PendingInitiativeRollDefender.Reset();

    if (AttackerRoll >= DefenderRoll)
    {
        bIsAttackerTurn = true;
        InitiativeWinnerFaction = AttackerTeam.Num() > 0 ? AttackerTeam[0].Faction : ESkaldFaction::None;
    }
    else
    {
        bIsAttackerTurn = false;
        InitiativeWinnerFaction = DefenderTeam.Num() > 0 ? DefenderTeam[0].Faction : ESkaldFaction::None;
    }
}

void UGridBattleManager::StartRound()
{
    if (bBattleConcluded)
    {
        return;
    }

    bTeamsAssigned = true;

    ClearInactiveFighters();

    if (!HasLivingFighters(true) || !HasLivingFighters(false))
    {
        EndBattle();
        return;
    }

    ++CurrentRound;

    const int32 LivingAttackers = CountFighters(InitiativeOrder, true);
    const int32 LivingDefenders = CountFighters(InitiativeOrder, false);
    UE_LOG(LogSkaldBattle, Log, TEXT("[Battle] Starting round %d (Attackers=%d, Defenders=%d)"), CurrentRound, LivingAttackers, LivingDefenders);

    for (AFighterPawn* Fighter : InitiativeOrder)
    {
        if (Fighter && Fighter->IsAlive())
        {
            Fighter->ResetActivationState();
        }
    }

    ActiveFighter = nullptr;
    CurrentTurn = 0;

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(InitiativePresentationTimer);
        World->GetTimerManager().ClearTimer(InitiativeAIRollTimer);
    }

    bAwaitingInitiativeRoll = false;

    if (ShouldPauseForInitiativePrompt())
    {
        bAwaitingInitiativeRoll = true;
        OnActiveFighterChanged.Broadcast(nullptr);
        OnInitiativePhaseStarted.Broadcast(CurrentRound);
        return;
    }

    ResolveInitiativeRollInternal();
}

void UGridBattleManager::ConfirmInitiativeRoll(int32 AttackerRoll, int32 DefenderRoll)
{
    if (!bAwaitingInitiativeRoll || bBattleConcluded)
    {
        return;
    }

    if (AttackerRoll > 0)
    {
        PendingInitiativeRollAttacker = AttackerRoll;
    }

    if (DefenderRoll > 0)
    {
        PendingInitiativeRollDefender = DefenderRoll;
    }

    const bool bHasAttackerRoll = PendingInitiativeRollAttacker.IsSet();
    const bool bHasDefenderRoll = PendingInitiativeRollDefender.IsSet();

    if (bHasAttackerRoll && bHasDefenderRoll)
    {
        ResolveInitiativeRollInternal();
        return;
    }

    ScheduleAIRollIfNeeded();
}

void UGridBattleManager::ResolveInitiativeRollInternal()
{
    bAwaitingInitiativeRoll = false;

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(InitiativeAIRollTimer);
    }

    RollInitiative();

    UE_LOG(LogSkaldBattle, Log, TEXT("[Battle] Round %d initiative winner: %s (Attacker=%d Defender=%d)"), CurrentRound,
        *UEnum::GetValueAsString(InitiativeWinnerFaction), LastInitiativeRollAttacker, LastInitiativeRollDefender);

    const bool bHasPresentationListeners = OnInitiativeRollCompleted.IsBound();
    if (bHasPresentationListeners)
    {
        OnInitiativeRollCompleted.Broadcast(CurrentRound, LastInitiativeRollAttacker, LastInitiativeRollDefender, InitiativeWinnerFaction);
    }

    ScheduleRoundStart(bHasPresentationListeners);
}

void UGridBattleManager::ScheduleAIRollIfNeeded()
{
    if (bBattleConcluded || !bAwaitingInitiativeRoll)
    {
        return;
    }

    const bool bNeedsAttackerRoll = !PendingInitiativeRollAttacker.IsSet() && IsSideAIControlled(true);
    const bool bNeedsDefenderRoll = !PendingInitiativeRollDefender.IsSet() && IsSideAIControlled(false);

    if (!bNeedsAttackerRoll && !bNeedsDefenderRoll)
    {
        return;
    }

    if (UWorld* World = GetWorld())
    {
        FTimerManager& TimerManager = World->GetTimerManager();
        TimerManager.ClearTimer(InitiativeAIRollTimer);

        if (InitiativeAIRollDelay > 0.f)
        {
            TimerManager.SetTimer(InitiativeAIRollTimer, this, &UGridBattleManager::PerformAIRoll, InitiativeAIRollDelay, false);
            return;
        }
    }

    PerformAIRoll();
}

void UGridBattleManager::PerformAIRoll()
{
    if (bBattleConcluded || !bAwaitingInitiativeRoll)
    {
        return;
    }

    bool bRolledValue = false;

    if (!PendingInitiativeRollAttacker.IsSet() && IsSideAIControlled(true))
    {
        PendingInitiativeRollAttacker = Rng.RandRange(1, InitiativeDiceSides);
        bRolledValue = true;
    }

    if (!PendingInitiativeRollDefender.IsSet() && IsSideAIControlled(false))
    {
        PendingInitiativeRollDefender = Rng.RandRange(1, InitiativeDiceSides);
        bRolledValue = true;
    }

    if (PendingInitiativeRollAttacker.IsSet() && PendingInitiativeRollDefender.IsSet())
    {
        ResolveInitiativeRollInternal();
        return;
    }

    if (!bRolledValue)
    {
        if (UWorld* World = GetWorld())
        {
            World->GetTimerManager().ClearTimer(InitiativeAIRollTimer);
        }
    }
}

void UGridBattleManager::ScheduleRoundStart(bool bDelayForPresentation)
{
    if (UWorld* World = GetWorld())
    {
        FTimerManager& TimerManager = World->GetTimerManager();
        TimerManager.ClearTimer(InitiativePresentationTimer);

        if (bDelayForPresentation && InitiativePresentationDelay > 0.f)
        {
            TimerManager.SetTimer(InitiativePresentationTimer, this, &UGridBattleManager::FinalizeRoundStart,
                InitiativePresentationDelay, false);
            return;
        }
    }

    FinalizeRoundStart();
}

void UGridBattleManager::FinalizeRoundStart()
{
    if (bBattleConcluded)
    {
        return;
    }

    OnRoundStarted.Broadcast(CurrentRound, InitiativeWinnerFaction);
    OnActiveFighterChanged.Broadcast(nullptr);
}

bool UGridBattleManager::ShouldPauseForInitiativePrompt() const
{
    const bool bAttackersHuman = !IsSideAIControlled(true);
    const bool bDefendersHuman = !IsSideAIControlled(false);
    return bAttackersHuman || bDefendersHuman;
}

void UGridBattleManager::AdvanceTurn()
{
    UE_LOG(LogSkaldBattle, Verbose, TEXT("[Battle] AdvanceTurn called. Active fighter: %s"), *DescribeFighter(ActiveFighter));
    if (ActiveFighter)
    {
        FinishActivation(ActiveFighter, EGridActivationFinishReason::Auto);
        return;
    }

    const bool bPreviousWasAttacker = bIsAttackerTurn;
    EvaluateRoundProgress(bPreviousWasAttacker);
    OnActiveFighterChanged.Broadcast(nullptr);
}

void UGridBattleManager::ReportAttackResolution(AFighterPawn* Attacker, AFighterPawn* Defender, const FDiceRollResult& Result)
{
    const int32 DiceRolled = Result.DiceOutcomes.Num();
    UE_LOG(LogSkaldBattle, Log,
        TEXT("[Battle] Attack resolved: %s -> %s | Dice=%d Hits=%d Crits=%d Misses=%d Damage=%d RemainingHP=%d"),
        *DescribeFighter(Attacker), *DescribeFighter(Defender), DiceRolled, Result.HitCount, Result.CriticalHitCount,
        Result.MissCount, Result.TotalDamage, Result.EndingHealth);

    if (Result.bHighStakesCritical)
    {
        if (UWorld* World = GetWorld())
        {
            if (ASkaldGameState* GameState = World->GetGameState<ASkaldGameState>())
            {
                GameState->RequestTransientSlowdown(0.2f, 0.25f);
            }
        }
    }

    OnAttackResolved.Broadcast(Attacker, Defender, Result);
}

void UGridBattleManager::ReportSimulatedAttackResolution(const FDiceRollResult& Result)
{
    ReportAttackResolution(nullptr, nullptr, Result);
}

void UGridBattleManager::ReportAttackRejected(AFighterPawn* Attacker, AFighterPawn* Defender, const FText& Reason)
{
    if (!Attacker)
    {
        return;
    }

    const FString ReasonString = Reason.ToString();
    UE_LOG(LogSkaldBattle, Verbose, TEXT("[Battle] Attack rejected: %s -> %s | %s"),
        *DescribeFighter(Attacker), *DescribeFighter(Defender), ReasonString.IsEmpty() ? TEXT("<No Reason>") : *ReasonString);

    OnAttackRejected.Broadcast(Attacker, Defender, Reason);
}

bool UGridBattleManager::CanActivateFighter(AFighterPawn* Fighter) const
{
    if (bBattleConcluded || !Fighter)
    {
        UE_LOG(LogSkaldBattle, Verbose, TEXT("[Battle] CanActivateFighter rejected (BattleConcluded=%s, Fighter=%s)"),
            bBattleConcluded ? TEXT("true") : TEXT("false"), *DescribeFighter(Fighter));
        return false;
    }

    if (!Fighter->IsAlive() || !InitiativeOrder.Contains(Fighter))
    {
        UE_LOG(LogSkaldBattle, Verbose, TEXT("[Battle] CanActivateFighter rejected (Alive=%s, InOrder=%s) -> %s"),
            Fighter->IsAlive() ? TEXT("true") : TEXT("false"),
            InitiativeOrder.Contains(Fighter) ? TEXT("true") : TEXT("false"), *DescribeFighter(Fighter));
        return false;
    }

    if (ActiveFighter)
    {
        if (ActiveFighter == Fighter)
        {
            UE_LOG(LogSkaldBattle, Verbose, TEXT("[Battle] CanActivateFighter rejected (Fighter already active) -> %s"),
                *DescribeFighter(Fighter));
            return false;
        }

        UE_LOG(LogSkaldBattle, Verbose, TEXT("[Battle] CanActivateFighter rejected (Another active fighter %s)"),
            *DescribeFighter(ActiveFighter));
        return false;
    }

    if (bAwaitingInitiativeRoll)
    {
        UE_LOG(LogSkaldBattle, Verbose, TEXT("[Battle] CanActivateFighter rejected (Awaiting initiative roll) -> %s"),
            *DescribeFighter(Fighter));
        return false;
    }

    if (Fighter->HasActivatedThisRound())
    {
        UE_LOG(LogSkaldBattle, Verbose, TEXT("[Battle] CanActivateFighter rejected (Already activated this round) -> %s"),
            *DescribeFighter(Fighter));
        return false;
    }

    if (Fighter->bIsAttacker != bIsAttackerTurn)
    {
        UE_LOG(LogSkaldBattle, Verbose, TEXT("[Battle] CanActivateFighter rejected (Wrong side. bIsAttackerTurn=%s) -> %s"),
            bIsAttackerTurn ? TEXT("true") : TEXT("false"), *DescribeFighter(Fighter));
        return false;
    }

    return true;
}

bool UGridBattleManager::ActivateFighter(AFighterPawn* Fighter)
{
    if (!CanActivateFighter(Fighter))
    {
        UE_LOG(LogSkaldBattle, Warning, TEXT("[Battle] ActivateFighter failed for %s"), *DescribeFighter(Fighter));
        return false;
    }

    ActiveFighter = Fighter;
    CurrentTurn = InitiativeOrder.IndexOfByKey(Fighter);
    AFighterPawn* const ActivatedFighter = ActiveFighter;
    if (ActivatedFighter)
    {
        ActivatedFighter->BeginActivation();
    }
    OnActiveFighterChanged.Broadcast(ActiveFighter);
    UE_LOG(LogSkaldBattle, Log, TEXT("[Battle] Fighter activated: %s (Round=%d, TurnIndex=%d, AttackerTurn=%s)"),
        *DescribeFighter(ActivatedFighter), CurrentRound, CurrentTurn, bIsAttackerTurn ? TEXT("true") : TEXT("false"));
    return ActivatedFighter != nullptr;
}

void UGridBattleManager::FinishActivation(AFighterPawn* Fighter, EGridActivationFinishReason Reason)
{
    if (bBattleConcluded)
    {
        UE_LOG(LogSkaldBattle, Verbose, TEXT("[Battle] FinishActivation ignored because battle concluded"));
        return;
    }

    AFighterPawn* FighterToFinish = Fighter ? Fighter : ActiveFighter;
    if (!FighterToFinish)
    {
        UE_LOG(LogSkaldBattle, Verbose, TEXT("[Battle] FinishActivation aborted (No fighter)"));
        return;
    }

    if (FighterToFinish->IsResolvingQueuedAttack())
    {
        TWeakObjectPtr<AFighterPawn> FighterPtr = FighterToFinish;
        FDeferredActivationFinish& DeferredRequest = DeferredActivationFinishes.FindOrAdd(FighterPtr);
        DeferredRequest.Reason = Reason;
        DeferredRequest.bWasAttacker = FighterToFinish->bIsAttacker;

        if (!DeferredFinishDelegateHandles.Contains(FighterPtr))
        {
            FDelegateHandle Handle = FighterToFinish->OnQueuedAttackFinalized.AddLambda(
                [this, FighterPtr]()
                {
                    HandleDeferredActivationFinalized(FighterPtr);
                });
            DeferredFinishDelegateHandles.Add(FighterPtr, Handle);
        }

        UE_LOG(LogSkaldBattle, Verbose,
            TEXT("[Battle] Deferring FinishActivation for %s until queued attack completes"),
            *DescribeFighter(FighterToFinish));
        return;
    }

    const bool bWasAttacker = FighterToFinish->bIsAttacker;

    if (Reason == EGridActivationFinishReason::Auto && !IsSideAIControlled(bWasAttacker))
    {
        UE_LOG(LogSkaldBattle, Verbose, TEXT("[Battle] Ignoring automatic FinishActivation for human-controlled %s"),
            bWasAttacker ? TEXT("attackers") : TEXT("defenders"));
        return;
    }

    const bool bWasActiveFighter = ActiveFighter == FighterToFinish;

    if (bWasActiveFighter)
    {
        ActiveFighter->FinishActivation();
        ActiveFighter = nullptr;
        UE_LOG(LogSkaldBattle, Log, TEXT("[Battle] Fighter finished activation: %s"), *DescribeFighter(FighterToFinish));
    }
    else if (InitiativeOrder.Contains(FighterToFinish))
    {
        FighterToFinish->FinishActivation();
        UE_LOG(LogSkaldBattle, Log, TEXT("[Battle] Manually finished activation for non-active fighter: %s"), *DescribeFighter(FighterToFinish));
    }

    EvaluateRoundProgress(bWasAttacker);

    if (bWasActiveFighter)
    {
        OnActiveFighterChanged.Broadcast(nullptr);
    }
}

void UGridBattleManager::HandleDeferredActivationFinalized(TWeakObjectPtr<AFighterPawn> FighterPtr)
{
    FDeferredActivationFinish Request;
    if (!DeferredActivationFinishes.RemoveAndCopyValue(FighterPtr, Request))
    {
        ClearDeferredActivationTracking(FighterPtr);
        return;
    }

    AFighterPawn* Fighter = FighterPtr.Get();
    ClearDeferredActivationTracking(FighterPtr);

    if (!Fighter)
    {
        UE_LOG(LogSkaldBattle, Verbose, TEXT("[Battle] Deferred activation target no longer valid; advancing state"));
        ActiveFighter = nullptr;
        EvaluateRoundProgress(Request.bWasAttacker);
        OnActiveFighterChanged.Broadcast(nullptr);
        return;
    }

    FinishActivation(Fighter, Request.Reason);
}

void UGridBattleManager::ClearDeferredActivationTracking(TWeakObjectPtr<AFighterPawn> FighterPtr)
{
    if (AFighterPawn* Fighter = FighterPtr.Get())
    {
        if (FDelegateHandle* Handle = DeferredFinishDelegateHandles.Find(FighterPtr))
        {
            Fighter->OnQueuedAttackFinalized.Remove(*Handle);
        }
    }

    DeferredFinishDelegateHandles.Remove(FighterPtr);
    DeferredActivationFinishes.Remove(FighterPtr);
}

bool UGridBattleManager::HasLivingFighters(bool bForAttackers) const
{
    for (AFighterPawn* Fighter : InitiativeOrder)
    {
        if (Fighter && Fighter->IsAlive() && Fighter->bIsAttacker == bForAttackers)
        {
            return true;
        }
    }
    return false;
}

bool UGridBattleManager::HasAvailableFighters(bool bForAttackers) const
{
    for (AFighterPawn* Fighter : InitiativeOrder)
    {
        if (!Fighter)
        {
            continue;
        }

        if (!Fighter->IsAlive() || Fighter->bIsAttacker != bForAttackers)
        {
            continue;
        }

        if (!Fighter->HasActivatedThisRound())
        {
            return true;
        }
    }
    return false;
}

void UGridBattleManager::EvaluateRoundProgress(bool bPreviousWasAttacker)
{
    if (bBattleConcluded)
    {
        return;
    }

    if (CurrentRound <= 0)
    {
        ClearInactiveFighters();
        return;
    }

    ClearInactiveFighters();

    if (!HasLivingFighters(true) || !HasLivingFighters(false))
    {
        EndBattle();
        return;
    }

    const bool bOpponentsRemain = HasAvailableFighters(!bPreviousWasAttacker);
    const bool bCurrentRemain = HasAvailableFighters(bPreviousWasAttacker);

    UE_LOG(LogSkaldBattle, Log, TEXT("[Battle] EvaluateRoundProgress: PreviousWasAttacker=%s, CurrentRemain=%s, OpponentsRemain=%s"),
        bPreviousWasAttacker ? TEXT("true") : TEXT("false"),
        bCurrentRemain ? TEXT("true") : TEXT("false"),
        bOpponentsRemain ? TEXT("true") : TEXT("false"));

    if (bOpponentsRemain)
    {
        bIsAttackerTurn = !bPreviousWasAttacker;
        UE_LOG(LogSkaldBattle, Log, TEXT("[Battle] Switching turn to %s"), bIsAttackerTurn ? TEXT("Attackers") : TEXT("Defenders"));
        return;
    }

    if (bCurrentRemain)
    {
        bIsAttackerTurn = bPreviousWasAttacker;
        UE_LOG(LogSkaldBattle, Log, TEXT("[Battle] Keeping turn with %s"), bIsAttackerTurn ? TEXT("Attackers") : TEXT("Defenders"));
        return;
    }

    UE_LOG(LogSkaldBattle, Log, TEXT("[Battle] No fighters remain with actions. Advancing to next round."));
    StartRound();
}

void UGridBattleManager::ClearInactiveFighters()
{
    const bool bActiveInvalid = ActiveFighter && (!ActiveFighter->IsAlive() || !InitiativeOrder.Contains(ActiveFighter));

    const int32 BeforeCount = InitiativeOrder.Num();
    InitiativeOrder.RemoveAll([](AFighterPawn* Fighter)
    {
        return !Fighter || !Fighter->IsAlive();
    });
    const int32 Removed = BeforeCount - InitiativeOrder.Num();

    if (bActiveInvalid || (ActiveFighter && !InitiativeOrder.Contains(ActiveFighter)))
    {
        ActiveFighter = nullptr;
        OnActiveFighterChanged.Broadcast(nullptr);
    }

    if (Removed > 0)
    {
        UE_LOG(LogSkaldBattle, Log, TEXT("[Battle] Cleared %d inactive fighters from initiative order"), Removed);
    }
}

bool UGridBattleManager::IsSideAIControlled(bool bForAttackers) const
{
    const UWorld* World = GetWorld();
    if (!World)
    {
        return false;
    }

    const USkaldGameInstance* GameInstance = World->GetGameInstance<USkaldGameInstance>();
    int32 TargetPlayerId = INDEX_NONE;
    bool bPendingBattleAIFlag = true;

    if (GameInstance)
    {
        const FS_BattlePayload& Battle = GameInstance->PendingBattle;
        TargetPlayerId = bForAttackers ? Battle.AttackerPlayerID : Battle.DefenderPlayerID;
        bPendingBattleAIFlag = bForAttackers ? Battle.bAttackerIsAI : Battle.bDefenderIsAI;

        if (bPendingBattleAIFlag)
        {
            return true;
        }

        if (TargetPlayerId <= 0)
        {
            return false;
        }
    }

    if (!GameInstance)
    {
        return true;
    }

    bool bMatchedPlayerState = false;
    if (const AGameStateBase* GameState = World->GetGameState())
    {
        for (APlayerState* PlayerState : GameState->PlayerArray)
        {
            if (const ASkaldPlayerState* SkaldPlayerState = Cast<ASkaldPlayerState>(PlayerState))
            {
                if (TargetPlayerId > 0 && SkaldPlayerState->GetPlayerId() == TargetPlayerId)
                {
                    bMatchedPlayerState = true;
                    return SkaldPlayerState->bIsAI;
                }
            }
        }
    }

    if (!bMatchedPlayerState)
    {
        UE_LOG(LogSkaldBattle, Warning, TEXT("[Battle] Unable to resolve controller for %s; assuming %s control."),
            bForAttackers ? TEXT("attackers") : TEXT("defenders"),
            bPendingBattleAIFlag ? TEXT("AI") : TEXT("player"));
        return bPendingBattleAIFlag;
    }

    return bPendingBattleAIFlag;
}

void UGridBattleManager::EndBattle()
{
    if (bBattleConcluded)
    {
        return;
    }
    bBattleConcluded = true;

    AttackerSurvivorUnitCount = 0;
    DefenderSurvivorUnitCount = 0;
    AttackerSurvivorArmyCost = 0;
    DefenderSurvivorArmyCost = 0;

    for (AFighterPawn* Fighter : InitiativeOrder)
    {
        if (Fighter && Fighter->IsAlive())
        {
            if (Fighter->bIsAttacker)
            {
                ++AttackerSurvivorUnitCount;
                AttackerSurvivorArmyCost += Fighter->Stats.ArmyCost;
            }
            else
            {
                ++DefenderSurvivorUnitCount;
                DefenderSurvivorArmyCost += Fighter->Stats.ArmyCost;
            }
        }
    }

    ESkaldFaction Winner = ESkaldFaction::None;
    if (AttackerSurvivorUnitCount > 0 && DefenderSurvivorUnitCount <= 0)
    {
        Winner = AttackerTeam.Num() > 0 ? AttackerTeam[0].Faction : ESkaldFaction::None;
    }
    else if (DefenderSurvivorUnitCount > 0 && AttackerSurvivorUnitCount <= 0)
    {
        Winner = DefenderTeam.Num() > 0 ? DefenderTeam[0].Faction : ESkaldFaction::None;
    }

    const int32 AttackerCasualties = AttackerInitialArmyCost - AttackerSurvivorArmyCost;
    const int32 DefenderCasualties = DefenderInitialArmyCost - DefenderSurvivorArmyCost;

    BattleConclusionWinner = Winner;
    BattleConclusionAttackerCasualties = AttackerCasualties;
    BattleConclusionDefenderCasualties = DefenderCasualties;

    ActiveFighter = nullptr;
    OnActiveFighterChanged.Broadcast(nullptr);

    UWorld* World = GetWorld();
    if (World && BattleConclusionBroadcastDelaySeconds > 0.f)
    {
        World->GetTimerManager().SetTimer(BattleConclusionTimerHandle, this, &UGridBattleManager::BroadcastBattleConcluded,
            BattleConclusionBroadcastDelaySeconds, false);
    }
    else
    {
        BroadcastBattleConcluded();
    }
}

void UGridBattleManager::BroadcastBattleConcluded()
{
    if (!bBattleConcluded)
    {
        return;
    }

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(BattleConclusionTimerHandle);
    }

    UE_LOG(LogSkaldBattle, Log, TEXT("[Battle] Battle ended. Winner=%s, AttackerCasualties=%d, DefenderCasualties=%d"),
        *UEnum::GetValueAsString(BattleConclusionWinner), BattleConclusionAttackerCasualties, BattleConclusionDefenderCasualties);
    OnBattleEnded.Broadcast(BattleConclusionWinner, BattleConclusionAttackerCasualties, BattleConclusionDefenderCasualties);
}

