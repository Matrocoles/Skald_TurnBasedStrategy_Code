#include "Skald_GameInstance.h"

void USkaldGameInstance::Init()
{
    Super::Init();
    SeedCombatRandomStream(FMath::Rand());
}

void USkaldGameInstance::SeedCombatRandomStream(int32 Seed)
{
    CombatRandomStream.Initialize(Seed);
}

