#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "GridBattleManager.h" // for FFighterDefinition
#include "SkaldTypes.h"
#include "FighterDataLibrary.generated.h"

/** Utility functions for accessing fighter data tables. */
UCLASS()
class SKALD_API UFighterDataLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Return fighter definitions belonging to the specified faction. */
    UFUNCTION(BlueprintCallable, Category="Skald|Fighters", meta=(WorldContext="WorldContext"))
    static TArray<FFighterDefinition> GetFightersForFaction(UObject* WorldContext, ESkaldFaction Faction);
};

