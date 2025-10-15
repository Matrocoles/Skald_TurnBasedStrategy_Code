#include "GridBattleManager.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "FighterPawn.h"
#include "GridOverlayComponent.h"
#include "SkaldLogging.h"
#include "Skald_GameInstance.h"
#include "Skald_PlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "UObject/UnrealType.h"

namespace
{
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

bool UGridBattleManager::ResolveAttack(FFighter& Attacker, FFighter& Defender, int32& OutDamage, FRandomStream& RandomStream)
{
    OutDamage = 0;
    bool bDefeated = false;
    const int32 RequiredRoll = Attacker.Stats.Strength > Defender.Stats.Defence ? 3 :
        (Attacker.Stats.Strength < Defender.Stats.Defence ? 5 : 4);

    for (int32 i = 0; i < Attacker.Stats.AttackDice; ++i)
    {
        int32 Roll = RandomStream.RandRange(1, 6);
        if (Roll == 6)
        {
            int32 Damage = Attacker.Stats.AttackDamage + Attacker.Stats.CriticalBonusDamage;
            Defender.Stats.Health -= Damage;
            Defender.Stats.Health = FMath::Max(Defender.Stats.Health, 0);
            OutDamage += Damage;
        }
        else if (Roll >= RequiredRoll)
        {
            int32 Damage = Attacker.Stats.AttackDamage;
            Defender.Stats.Health -= Damage;
            Defender.Stats.Health = FMath::Max(Defender.Stats.Health, 0);
            OutDamage += Damage;
        }
    }

    if (Defender.Stats.Health <= 0)
    {
        bDefeated = true;
    }

    return bDefeated;
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

    int32 AttackerRoll = 0;
    int32 DefenderRoll = 0;
    int32 Attempts = 0;
    const int32 MaxAttempts = 10;
    do
    {
        AttackerRoll = Rng.RandRange(1, 20);
        DefenderRoll = Rng.RandRange(1, 20);
        ++Attempts;
    }
    while (AttackerRoll == DefenderRoll && Attempts < MaxAttempts);

    if (AttackerRoll == DefenderRoll)
    {
        // As a fallback, bias ties in favour of the attacker to avoid stalling the round start.
        ++AttackerRoll;
    }

    LastInitiativeRollAttacker = AttackerRoll;
    LastInitiativeRollDefender = DefenderRoll;

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
    }

    bAwaitingInitiativeRoll = false;

    if (ShouldPauseForInitiativePrompt())
    {
        bAwaitingInitiativeRoll = true;
        OnInitiativePhaseStarted.Broadcast(CurrentRound);
        return;
    }

    ResolveInitiativeRollInternal();
}

void UGridBattleManager::ConfirmInitiativeRoll()
{
    if (!bAwaitingInitiativeRoll || bBattleConcluded)
    {
        return;
    }

    ResolveInitiativeRollInternal();
}

void UGridBattleManager::ResolveInitiativeRollInternal()
{
    bAwaitingInitiativeRoll = false;

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
    if (!OnInitiativePhaseStarted.IsBound())
    {
        return false;
    }

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

void UGridBattleManager::ReportAttackRoll(AFighterPawn* Attacker, AFighterPawn* Defender, int32 Roll, bool bHit, int32 Damage)
{
    if (!Attacker || !Defender)
    {
        return;
    }

    UE_LOG(LogSkaldBattle, Log, TEXT("[Battle] Attack roll: %s -> %s | Roll=%d Result=%s Damage=%d"),
        *DescribeFighter(Attacker), *DescribeFighter(Defender), Roll, bHit ? TEXT("Hit") : TEXT("Miss"), Damage);

    OnAttackResolved.Broadcast(Attacker, Defender, Roll, bHit, Damage);
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
    bool bPendingBattleAIFlag = false;

    if (GameInstance)
    {
        const FS_BattlePayload& Battle = GameInstance->PendingBattle;
        TargetPlayerId = bForAttackers ? Battle.AttackerPlayerID : Battle.DefenderPlayerID;
        bPendingBattleAIFlag = bForAttackers ? Battle.bAttackerIsAI : Battle.bDefenderIsAI;

        if (bPendingBattleAIFlag)
        {
            return true;
        }
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
        UE_LOG(LogSkaldBattle, Warning, TEXT("[Battle] Unable to resolve controller for %s; defaulting to AI automation."),
            bForAttackers ? TEXT("attackers") : TEXT("defenders"));
        return true;
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

    ActiveFighter = nullptr;
    OnActiveFighterChanged.Broadcast(nullptr);
    UE_LOG(LogSkaldBattle, Log, TEXT("[Battle] Battle ended. Winner=%s, AttackerCasualties=%d, DefenderCasualties=%d"),
        *UEnum::GetValueAsString(Winner), AttackerCasualties, DefenderCasualties);
    OnBattleEnded.Broadcast(Winner, AttackerCasualties, DefenderCasualties);
}

