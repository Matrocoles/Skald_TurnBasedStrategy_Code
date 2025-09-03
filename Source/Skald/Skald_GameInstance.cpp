#include "Skald_GameInstance.h"
#include "GridBattleManager.h"

void USkaldGameInstance::Init()
{
    Super::Init();
    SeedCombatRandomStream(FMath::Rand());
}

void USkaldGameInstance::SeedCombatRandomStream(int32 Seed)
{
    CombatRandomStream.Initialize(Seed);
}

UGridBattleManager* USkaldGameInstance::GetGridBattleManager()
{
    if (!GridBattleManager)
    {
        GridBattleManager = NewObject<UGridBattleManager>(this);
    }
    return GridBattleManager;
}

