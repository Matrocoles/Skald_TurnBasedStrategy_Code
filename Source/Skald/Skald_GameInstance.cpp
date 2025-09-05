#include "Skald_GameInstance.h"

void USkaldGameInstance::Init()
{
    Super::Init();
    SeedCombatRandomStream(FMath::Rand());
    TakenFactions.Empty();
    if (Faction != ESkaldFaction::None)
    {
        TakenFactions.Add(Faction);
    }
}

void USkaldGameInstance::SeedCombatRandomStream(int32 Seed)
{
    CombatRandomStream.Initialize(Seed);
}

