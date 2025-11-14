#include "SiegeBattleLibrary.h"

namespace
{
static int32 ToIndex(EBattleStats Stat)
{
    return static_cast<int32>(Stat);
}
}

int32 USiegeBattleLibrary::ResolveStat(const FS_Siege& Siege, EBattleStats Stat, int32 DefaultValue)
{
    const int32 Index = ToIndex(Stat);
    if (Index >= 0 && Siege.BattleStats.IsValidIndex(Index))
    {
        return Siege.BattleStats[Index];
    }
    return DefaultValue;
}

EFighterAttackType USiegeBattleLibrary::InferAttackType(ESiegeWeapon Type)
{
    switch (Type)
    {
    case ESiegeWeapon::Trebuchet:
    case ESiegeWeapon::Catapult:
        return EFighterAttackType::Ranged;
    default:
        return EFighterAttackType::Melee;
    }
}

FFighterStats USiegeBattleLibrary::BuildFighterStatsFromSiege(const FS_Siege& Siege)
{
    FFighterStats Stats;
    Stats.Health = ResolveStat(Siege, EBattleStats::Health, Stats.Health);
    Stats.Defence = ResolveStat(Siege, EBattleStats::Defence, Stats.Defence);
    Stats.Strength = ResolveStat(Siege, EBattleStats::Strength, Stats.Strength);
    Stats.AttackDice = ResolveStat(Siege, EBattleStats::AttackDice, Stats.AttackDice);
    Stats.AttackRange = ResolveStat(Siege, EBattleStats::AttackRange, Stats.AttackRange);
    Stats.Movement = ResolveStat(Siege, EBattleStats::Movement, Stats.Movement);
    Stats.AttackDamage = ResolveStat(Siege, EBattleStats::AttackDamage, Stats.AttackDamage);
    Stats.CriticalBonusDamage = ResolveStat(Siege, EBattleStats::CriticalBonusDamage, Stats.CriticalBonusDamage);
    Stats.ArmyCost = ResolveStat(Siege, EBattleStats::ArmyCost, 0);
    return Stats;
}

FFighterDefinition USiegeBattleLibrary::BuildFighterDefinitionFromSiege(const FS_Siege& Siege, ESkaldFaction OwningFaction)
{
    FFighterDefinition Definition;
    Definition.Id = *FString::Printf(TEXT("Siege_%d"), Siege.SiegeID);
    Definition.Faction = OwningFaction;
    Definition.Stats = BuildFighterStatsFromSiege(Siege);
    Definition.AttackType = InferAttackType(Siege.Type);
    Definition.Portrait = Siege.Portrait;
    return Definition;
}

void USiegeBattleLibrary::AppendSiegeFighters(const TArray<FS_Siege>& Sieges, ESkaldFaction Faction, TArray<FFighterDefinition>& InOutRoster)
{
    if (Sieges.Num() == 0)
    {
        return;
    }

    const int32 StartingCount = InOutRoster.Num();
    InOutRoster.Reserve(StartingCount + Sieges.Num());

    for (const FS_Siege& Siege : Sieges)
    {
        InOutRoster.Add(BuildFighterDefinitionFromSiege(Siege, Faction));
    }
}
