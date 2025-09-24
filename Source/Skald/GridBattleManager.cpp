#include "GridBattleManager.h"
#include "Engine/DataTable.h"
#include "FighterPawn.h"
#include "GridOverlayComponent.h"

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
            int32 Damage = Attacker.Stats.AttackDamage + 3;
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

    for (AFighterPawn* Fighter : InitiativeOrder)
    {
        if (Fighter && Fighter->IsAlive())
        {
            Fighter->ResetActivationState();
        }
    }

    ActiveFighter = nullptr;
    CurrentTurn = 0;
    OnActiveFighterChanged.Broadcast(nullptr);

    RollInitiative();

    OnRoundStarted.Broadcast(CurrentRound, InitiativeWinnerFaction);
}

void UGridBattleManager::AdvanceTurn()
{
    FinishActivation(ActiveFighter);
}

bool UGridBattleManager::CanActivateFighter(AFighterPawn* Fighter) const
{
    if (bBattleConcluded || !Fighter)
    {
        return false;
    }

    if (!Fighter->IsAlive() || !InitiativeOrder.Contains(Fighter))
    {
        return false;
    }

    if (ActiveFighter && ActiveFighter != Fighter)
    {
        return false;
    }

    if (Fighter->HasActivatedThisRound())
    {
        return false;
    }

    if (Fighter->bIsAttacker != bIsAttackerTurn)
    {
        return false;
    }

    return true;
}

bool UGridBattleManager::ActivateFighter(AFighterPawn* Fighter)
{
    if (!CanActivateFighter(Fighter))
    {
        return false;
    }

    ActiveFighter = Fighter;
    CurrentTurn = InitiativeOrder.IndexOfByKey(Fighter);
    if (ActiveFighter)
    {
        ActiveFighter->BeginActivation();
    }
    OnActiveFighterChanged.Broadcast(ActiveFighter);
    return ActiveFighter != nullptr;
}

void UGridBattleManager::FinishActivation(AFighterPawn* Fighter)
{
    if (bBattleConcluded)
    {
        return;
    }

    AFighterPawn* FighterToFinish = Fighter ? Fighter : ActiveFighter;
    if (!FighterToFinish)
    {
        return;
    }

    const bool bWasAttacker = FighterToFinish->bIsAttacker;

    if (ActiveFighter == FighterToFinish)
    {
        ActiveFighter->FinishActivation();
        ActiveFighter = nullptr;
        OnActiveFighterChanged.Broadcast(nullptr);
    }
    else if (InitiativeOrder.Contains(FighterToFinish))
    {
        FighterToFinish->FinishActivation();
    }

    EvaluateRoundProgress(bWasAttacker);
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

    if (bOpponentsRemain)
    {
        bIsAttackerTurn = !bPreviousWasAttacker;
        return;
    }

    if (bCurrentRemain)
    {
        bIsAttackerTurn = bPreviousWasAttacker;
        return;
    }

    StartRound();
}

void UGridBattleManager::ClearInactiveFighters()
{
    const bool bActiveInvalid = ActiveFighter && (!ActiveFighter->IsAlive() || !InitiativeOrder.Contains(ActiveFighter));

    InitiativeOrder.RemoveAll([](AFighterPawn* Fighter)
    {
        return !Fighter || !Fighter->IsAlive();
    });

    if (bActiveInvalid || (ActiveFighter && !InitiativeOrder.Contains(ActiveFighter)))
    {
        ActiveFighter = nullptr;
        OnActiveFighterChanged.Broadcast(nullptr);
    }
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
    OnBattleEnded.Broadcast(Winner, AttackerCasualties, DefenderCasualties);
}

