#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GridBattleManager.h"
#include "SkaldTypes.h"
#include "SiegeBattleLibrary.generated.h"

/**
 * Utility helpers that translate siege metadata into fighter definitions so
 * battle maps can treat siege weapons as unique pawns.
 */
UCLASS()
class SKALD_API USiegeBattleLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** Build battle-ready fighter stats from the numeric payload stored on a siege weapon. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category="Skald|Siege")
    static FFighterStats BuildFighterStatsFromSiege(const FS_Siege& Siege);

    /** Convert siege data into a fighter definition that can be appended to an army roster. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category="Skald|Siege")
    static FFighterDefinition BuildFighterDefinitionFromSiege(const FS_Siege& Siege, ESkaldFaction OwningFaction);

    /** Append all siege weapons to the provided fighter roster. */
    static void AppendSiegeFighters(const TArray<FS_Siege>& Sieges, ESkaldFaction Faction, TArray<FFighterDefinition>& InOutRoster);

private:
    static int32 ResolveStat(const FS_Siege& Siege, EBattleStats Stat, int32 DefaultValue);
    static EFighterAttackType InferAttackType(ESiegeWeapon Type);
};
