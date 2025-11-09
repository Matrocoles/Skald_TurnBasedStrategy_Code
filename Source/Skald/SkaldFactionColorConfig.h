#pragma once

#include "Engine/DataAsset.h"
#include "SkaldTypes.h"
#include "SkaldFactionColorConfig.generated.h"

/**
 * Data asset that exposes customizable faction colors for world map territories,
 * dice tinting and any other systems that need faction specific colouring.
 */
UCLASS(BlueprintType)
class SKALD_API USkaldFactionColorConfig : public UDataAsset
{
    GENERATED_BODY()

public:
    USkaldFactionColorConfig();

    /** Explicit colour overrides per faction. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skald|Faction Colours")
    TMap<ESkaldFaction, FLinearColor> FactionColors;

    /**
     * Colour returned when no faction override exists or when the faction is None.
     * Also used for editor previews when territories are unowned.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skald|Faction Colours")
    FLinearColor DefaultColor = FLinearColor::White;

    /** Resolve the configured colour for a specific faction, falling back to defaults. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Skald|Faction Colours")
    FLinearColor GetColor(ESkaldFaction Faction) const;

    /** Static helper that exposes the built-in fallback palette. */
    static FLinearColor GetFallbackColor(ESkaldFaction Faction);
};
