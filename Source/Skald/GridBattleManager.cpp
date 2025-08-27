#include "GridBattleManager.h"

void UGridBattleManager::InitBattle(const TArray<FFighter>& Attackers, const TArray<FFighter>& Defenders)
{
    AttackerTeam = Attackers;
    DefenderTeam = Defenders;
    CurrentRound = 1;
}

int32 UGridBattleManager::RollInitiative()
{
    return FMath::RandRange(1, 6);
}

bool UGridBattleManager::ResolveAttack(FFighter& Attacker, FFighter& Defender, int32& OutDamage)
{
    OutDamage = 0;
    bool bDefeated = false;
    const int32 RequiredRoll = Attacker.Stats.Strength > Defender.Stats.Defence ? 3 :
        (Attacker.Stats.Strength < Defender.Stats.Defence ? 5 : 4);

    for (int32 i = 0; i < Attacker.Stats.AttackDice; ++i)
    {
        int32 Roll = FMath::RandRange(1, 6);
        if (Roll == 6)
        {
            int32 Damage = FMath::RandRange(1, Attacker.Stats.DamageDie) + 3;
            Defender.Stats.Health -= Damage;
            OutDamage += Damage;
        }
        else if (Roll >= RequiredRoll)
        {
            int32 Damage = FMath::RandRange(1, Attacker.Stats.DamageDie);
            Defender.Stats.Health -= Damage;
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
    int32 Count = 0;
    for (const FFighter& Fighter : AttackerTeam)
    {
        if (Fighter.Stats.Health > 0)
        {
            ++Count;
        }
    }
    return Count;
}

int32 UGridBattleManager::GetDefenderSurvivors() const
{
    int32 Count = 0;
    for (const FFighter& Fighter : DefenderTeam)
    {
        if (Fighter.Stats.Health > 0)
        {
            ++Count;
        }
    }
    return Count;
}

