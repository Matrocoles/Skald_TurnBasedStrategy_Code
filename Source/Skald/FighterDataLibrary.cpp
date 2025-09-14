#include "FighterDataLibrary.h"

#include "Engine/DataTable.h"
#include "Skald_GameInstance.h"

/** Resolve the fighter data table from game singletons or a fallback path. */
static UDataTable* ResolveFighterDataTable(UObject* WorldContext)
{
    static const TCHAR* FallbackPath = TEXT("/Game/DataTables/FighterTable.FighterTable");

    if (WorldContext)
    {
        if (UWorld* World = WorldContext->GetWorld())
        {
            if (USkaldGameInstance* GI = World->GetGameInstance<USkaldGameInstance>())
            {
                if (GI->GridBattleManager && GI->GridBattleManager->FighterDefinitions)
                {
                    return GI->GridBattleManager->FighterDefinitions;
                }
            }
        }
    }

    return LoadObject<UDataTable>(nullptr, FallbackPath);
}

TArray<FFighterDefinition> UFighterDataLibrary::GetFightersForFaction(UObject* WorldContext, ESkaldFaction Faction)
{
    TArray<FFighterDefinition> Out;
    if (UDataTable* DT = ResolveFighterDataTable(WorldContext))
    {
        TArray<FName> Rows;
        DT->GetRowNames(Rows);
        for (const FName& N : Rows)
        {
            if (const FFighterDefinition* Row = DT->FindRow<FFighterDefinition>(N, TEXT("FighterFilter")))
            {
                if (Row->Faction == Faction)
                {
                    Out.Add(*Row);
                }
            }
        }
    }
    return Out;
}

