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
    CurrentRound = 1;
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
    if (!Fighter) return;
    if (!InitiativeOrder.Contains(Fighter))
    {
        InitiativeOrder.Add(Fighter);
    }
    Fighter->bIsAttacker = bAsAttacker;
}

void UGridBattleManager::UnregisterFighter(AFighterPawn* Fighter)
{
    InitiativeOrder.Remove(Fighter);
    if (ActiveFighter == Fighter)
    {
        ActiveFighter = nullptr;
        OnActiveFighterChanged.Broadcast(nullptr);
    }
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
    struct FInitiativeEntry { AFighterPawn* Fighter; int32 Roll; };

    TArray<FInitiativeEntry> Rolls;
    Rolls.Reserve(InitiativeOrder.Num());
    for (AFighterPawn* Fighter : InitiativeOrder)
    {
        if (!Fighter) continue;
        Rolls.Add({ Fighter, Rng.RandRange(1, 20) });
    }

    Rolls.Sort([](const FInitiativeEntry& A, const FInitiativeEntry& B)
    {
        return A.Roll > B.Roll;
    });

    InitiativeOrder.Empty(Rolls.Num());
    for (const FInitiativeEntry& Entry : Rolls)
    {
        InitiativeOrder.Add(Entry.Fighter);
    }

    CurrentTurn = 0;
    ActiveFighter = InitiativeOrder.Num() > 0 ? InitiativeOrder[0] : nullptr;
    OnActiveFighterChanged.Broadcast(ActiveFighter);
    if (ActiveFighter)
    {
        ActiveFighter->BeginActivation();
    }
}

void UGridBattleManager::StartRound()
{
    const int32 EdgeRange = 3;

    if (!bTeamsAssigned)
    {
        bTeamsAssigned = true;
    }

    for (AFighterPawn* Fighter : InitiativeOrder)
    {
        if (!Fighter) continue;

        bool bPlaced = false;
        for (int32 tries = 0; tries < 50 && !bPlaced; ++tries)
        {
            FIntPoint Cell;
            Cell.Y = Rng.RandRange(0, GridSize - 1);
            Cell.X = Fighter->bIsAttacker
                ? Rng.RandRange(0, EdgeRange - 1)
                : Rng.RandRange(GridSize - EdgeRange, GridSize - 1);

            if (UGridOverlayComponent* Grid = Fighter->GetGrid())
            {
                if (!Grid->IsOccupied(Cell) && !Grid->IsObscured(Cell))
                {
                    Fighter->MoveToCell(Cell);
                    bPlaced = true;
                }
            }
            else
            {
                Fighter->MoveToCell(Cell);
                bPlaced = true;
            }
        }

        Fighter->ActionsRemaining = 0;
    }

    CurrentTurn = 0;
    ActiveFighter = InitiativeOrder.Num() > 0 ? InitiativeOrder[0] : nullptr;
    OnActiveFighterChanged.Broadcast(ActiveFighter);
    if (ActiveFighter)
    {
        ActiveFighter->BeginActivation();
    }
}

void UGridBattleManager::AdvanceTurn()
{
    if (bBattleConcluded)
    {
        return;
    }

    if (ActiveFighter && ActiveFighter->IsAlive() && ActiveFighter->ActionsRemaining > 0)
    {
        return;
    }

    InitiativeOrder.RemoveAll([](AFighterPawn* Fighter)
    {
        return !Fighter || !Fighter->IsAlive();
    });

    bool bAttackerAlive = false;
    bool bDefenderAlive = false;
    for (AFighterPawn* Fighter : InitiativeOrder)
    {
        if (!Fighter)
        {
            continue;
        }
        if (Fighter->bIsAttacker)
        {
            bAttackerAlive = true;
        }
        else
        {
            bDefenderAlive = true;
        }

        if (bAttackerAlive && bDefenderAlive)
        {
            break;
        }
    }

    if (!bBattleConcluded && (!bAttackerAlive || !bDefenderAlive || InitiativeOrder.Num() == 0))
    {
        EndBattle();
        return;
    }

    if (InitiativeOrder.Num() == 0)
    {
        ActiveFighter = nullptr;
        OnActiveFighterChanged.Broadcast(ActiveFighter);
        return;
    }

    CurrentTurn = (CurrentTurn + 1) % InitiativeOrder.Num();
    ActiveFighter = InitiativeOrder[CurrentTurn];
    OnActiveFighterChanged.Broadcast(ActiveFighter);
    if (ActiveFighter)
    {
        ActiveFighter->BeginActivation();
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

