#include "GridBattleManager.h"
#include "Engine/DataTable.h"
#include "UObject/ConstructorHelpers.h"
#include "FighterPawn.h"

namespace
{
    /** Compute Manhattan distance between two positions. */
    int32 Distance(const FIntPoint& A, const FIntPoint& B)
    {
        return FMath::Abs(A.X - B.X) + FMath::Abs(A.Y - B.Y);
    }

    /** Move a fighter towards a target up to their movement allowance. */
    void MoveTowards(FFighter& Mover, const FFighter& Target)
    {
        int32 Required = Distance(Mover.Position, Target.Position) - Mover.Stats.AttackRange;
        if (Required <= 0)
        {
            return;
        }

        int32 Steps = FMath::Min(Mover.Stats.Movement, Required);

        if (Mover.Position.X < Target.Position.X)
        {
            int32 StepX = FMath::Min(Steps, Target.Position.X - Mover.Position.X);
            Mover.Position.X += StepX;
            Steps -= StepX;
        }
        else if (Mover.Position.X > Target.Position.X)
        {
            int32 StepX = FMath::Min(Steps, Mover.Position.X - Target.Position.X);
            Mover.Position.X -= StepX;
            Steps -= StepX;
        }

        if (Steps > 0)
        {
            if (Mover.Position.Y < Target.Position.Y)
            {
                int32 StepY = FMath::Min(Steps, Target.Position.Y - Mover.Position.Y);
                Mover.Position.Y += StepY;
            }
            else if (Mover.Position.Y > Target.Position.Y)
            {
                int32 StepY = FMath::Min(Steps, Mover.Position.Y - Target.Position.Y);
                Mover.Position.Y -= StepY;
            }
        }

        Mover.Position.X = FMath::Clamp(Mover.Position.X, 0, UGridBattleManager::GridSize - 1);
        Mover.Position.Y = FMath::Clamp(Mover.Position.Y, 0, UGridBattleManager::GridSize - 1);
    }

    bool IsInRange(const FFighter& Attacker, const FFighter& Defender)
    {
        return Distance(Attacker.Position, Defender.Position) <= Attacker.Stats.AttackRange;
    }
}

UGridBattleManager::UGridBattleManager()
{
    FighterDefinitions = LoadObject<UDataTable>(nullptr, TEXT("/Game/DataTables/FighterTable.FighterTable"));
}

void UGridBattleManager::InitBattle(const TArray<FFighter>& Attackers, const TArray<FFighter>& Defenders)
{
    AttackerTeam = Attackers;
    DefenderTeam = Defenders;
    CurrentRound = 1;
}

int32 UGridBattleManager::RollInitiativeDie(FRandomStream& RandomStream)
{
    return RandomStream.RandRange(1, 6);
}

void UGridBattleManager::StartBattle(FRandomStream& RandomStream)
{
    bool bAttackerTurn = RollInitiativeDie(RandomStream) >= RollInitiativeDie(RandomStream);

    TArray<FIntPoint> PreviousAttackerPositions;
    PreviousAttackerPositions.Reserve(AttackerTeam.Num());
    for (const FFighter& Fighter : AttackerTeam)
    {
        PreviousAttackerPositions.Add(Fighter.Position);
    }
    TArray<FIntPoint> PreviousDefenderPositions;
    PreviousDefenderPositions.Reserve(DefenderTeam.Num());
    for (const FFighter& Fighter : DefenderTeam)
    {
        PreviousDefenderPositions.Add(Fighter.Position);
    }

    int32 StalemateTurns = 0;

    while (GetAttackerSurvivors() > 0 && GetDefenderSurvivors() > 0 && CurrentRound <= MaxRounds)
    {
        bool bDamageDealt = false;
        TArray<FFighter>& ActingTeam = bAttackerTurn ? AttackerTeam : DefenderTeam;
        TArray<FFighter>& TargetTeam = bAttackerTurn ? DefenderTeam : AttackerTeam;

        for (FFighter& Fighter : ActingTeam)
        {
            if (Fighter.Stats.Health <= 0)
            {
                continue;
            }

            FFighter* Target = nullptr;
            for (FFighter& Candidate : TargetTeam)
            {
                if (Candidate.Stats.Health > 0)
                {
                    Target = &Candidate;
                    break;
                }
            }
            if (!Target)
            {
                break;
            }

            MoveTowards(Fighter, *Target);
            if (IsInRange(Fighter, *Target))
            {
                int32 Damage = 0;
                ResolveAttack(Fighter, *Target, Damage, RandomStream);
                if (Damage > 0)
                {
                    bDamageDealt = true;
                }
            }

            if (GetAttackerSurvivors() <= 0 || GetDefenderSurvivors() <= 0)
            {
                break;
            }
        }

        bool bPositionsChanged = false;
        for (int32 Index = 0; Index < AttackerTeam.Num(); ++Index)
        {
            if (AttackerTeam[Index].Position != PreviousAttackerPositions[Index])
            {
                bPositionsChanged = true;
                break;
            }
        }
        if (!bPositionsChanged)
        {
            for (int32 Index = 0; Index < DefenderTeam.Num(); ++Index)
            {
                if (DefenderTeam[Index].Position != PreviousDefenderPositions[Index])
                {
                    bPositionsChanged = true;
                    break;
                }
            }
        }

        if (!bDamageDealt && !bPositionsChanged)
        {
            ++StalemateTurns;
        }
        else
        {
            StalemateTurns = 0;
        }

        for (int32 Index = 0; Index < AttackerTeam.Num(); ++Index)
        {
            PreviousAttackerPositions[Index] = AttackerTeam[Index].Position;
        }
        for (int32 Index = 0; Index < DefenderTeam.Num(); ++Index)
        {
            PreviousDefenderPositions[Index] = DefenderTeam[Index].Position;
        }

        if (StalemateTurns >= 2)
        {
            break;
        }

        bAttackerTurn = !bAttackerTurn;
        ++CurrentRound;
    }

    ESkaldFaction Winner = ESkaldFaction::None;
    if (GetAttackerSurvivors() > 0 && GetDefenderSurvivors() <= 0)
    {
        Winner = AttackerTeam.Num() > 0 ? AttackerTeam[0].Faction : ESkaldFaction::None;
    }
    else if (GetDefenderSurvivors() > 0 && GetAttackerSurvivors() <= 0)
    {
        Winner = DefenderTeam.Num() > 0 ? DefenderTeam[0].Faction : ESkaldFaction::None;
    }

    int32 AttackerCasualties = 0;
    for (const FFighter& Fighter : AttackerTeam)
    {
        if (Fighter.Stats.Health <= 0)
        {
            ++AttackerCasualties;
        }
    }
    int32 DefenderCasualties = 0;
    for (const FFighter& Fighter : DefenderTeam)
    {
        if (Fighter.Stats.Health <= 0)
        {
            ++DefenderCasualties;
        }
    }

    OnBattleEnded.Broadcast(Winner, AttackerCasualties, DefenderCasualties);
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

int32 UGridBattleManager::GetAttackerSurvivors() const
{
    return AttackerSurvivorCount;
}

int32 UGridBattleManager::GetDefenderSurvivors() const
{
    return DefenderSurvivorCount;
}

int32 UGridBattleManager::GetAttackerSurvivorCost() const
{
    int32 Cost = 0;
    for (const FFighter& Fighter : AttackerTeam)
    {
        if (Fighter.Stats.Health > 0)
        {
            Cost += Fighter.Stats.ArmyCost;
        }
    }
    return Cost;
}

int32 UGridBattleManager::GetDefenderSurvivorCost() const
{
    int32 Cost = 0;
    for (const FFighter& Fighter : DefenderTeam)
    {
        if (Fighter.Stats.Health > 0)
        {
            Cost += Fighter.Stats.ArmyCost;
        }
    }
    return Cost;
}

void UGridBattleManager::RollInitiative()
{
    struct FInitiativeEntry
    {
        AFighterPawn* Fighter;
        int32 Roll;
    };

    TArray<FInitiativeEntry> Rolls;
    Rolls.Reserve(InitiativeOrder.Num());
    for (AFighterPawn* Fighter : InitiativeOrder)
    {
        if (!Fighter)
        {
            continue;
        }
        FInitiativeEntry Entry{Fighter, FMath::RandRange(1, 20)};
        Rolls.Add(Entry);
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
    if (ActiveFighter)
    {
        ActiveFighter->BeginActivation();
    }
}

void UGridBattleManager::StartRound(FRandomStream& RandomStream)
{
    const int32 EdgeRange = 3;
    const int32 Half = InitiativeOrder.Num() / 2;
    for (int32 Index = 0; Index < InitiativeOrder.Num(); ++Index)
    {
        AFighterPawn* Fighter = InitiativeOrder[Index];
        if (!Fighter)
        {
            continue;
        }

        Fighter->BeginActivation();

        FIntPoint Cell;
        Cell.Y = RandomStream.RandRange(0, GridSize - 1);
        if (Index < Half)
        {
            Cell.X = RandomStream.RandRange(0, EdgeRange - 1);
        }
        else
        {
            Cell.X = RandomStream.RandRange(GridSize - EdgeRange, GridSize - 1);
        }

        Fighter->MoveToCell(Cell);
        Fighter->ActionsRemaining = 0;
    }

    CurrentTurn = 0;
    ActiveFighter = InitiativeOrder.Num() > 0 ? InitiativeOrder[0] : nullptr;
    if (ActiveFighter)
    {
        ActiveFighter->BeginActivation();
    }
}

void UGridBattleManager::AdvanceTurn()
{
    if (ActiveFighter && ActiveFighter->IsAlive() && ActiveFighter->ActionsRemaining > 0)
    {
        return;
    }

    InitiativeOrder.RemoveAll([](AFighterPawn* Fighter)
    {
        return !Fighter || !Fighter->IsAlive();
    });

    if (InitiativeOrder.Num() == 0)
    {
        ActiveFighter = nullptr;
        return;
    }

    CurrentTurn = (CurrentTurn + 1) % InitiativeOrder.Num();
    ActiveFighter = InitiativeOrder[CurrentTurn];
    if (ActiveFighter)
    {
        ActiveFighter->BeginActivation();
    }
}

void UGridBattleManager::EndBattle()
{
    const int32 Half = InitiativeOrder.Num() / 2;
    AttackerSurvivorCount = 0;
    DefenderSurvivorCount = 0;
    for (int32 Index = 0; Index < InitiativeOrder.Num(); ++Index)
    {
        AFighterPawn* Fighter = InitiativeOrder[Index];
        if (Fighter && Fighter->IsAlive())
        {
            if (Index < Half)
            {
                ++AttackerSurvivorCount;
            }
            else
            {
                ++DefenderSurvivorCount;
            }
        }
    }

    ESkaldFaction Winner = ESkaldFaction::None;
    if (AttackerSurvivorCount > 0 && DefenderSurvivorCount <= 0)
    {
        Winner = AttackerTeam.Num() > 0 ? AttackerTeam[0].Faction : ESkaldFaction::None;
    }
    else if (DefenderSurvivorCount > 0 && AttackerSurvivorCount <= 0)
    {
        Winner = DefenderTeam.Num() > 0 ? DefenderTeam[0].Faction : ESkaldFaction::None;
    }

    const int32 AttackerCasualties = AttackerTeam.Num() - AttackerSurvivorCount;
    const int32 DefenderCasualties = DefenderTeam.Num() - DefenderSurvivorCount;
    OnBattleEnded.Broadcast(Winner, AttackerCasualties, DefenderCasualties);
}

