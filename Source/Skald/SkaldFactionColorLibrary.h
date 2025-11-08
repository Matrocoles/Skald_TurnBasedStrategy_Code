#pragma once

#include "CoreMinimal.h"
#include "SkaldTypes.h"

namespace SkaldFactionColors
{
/**
 * Resolve a display color for the provided faction. Returns true when a
 * specific tint is known for the faction; false indicates the caller should
 * fall back to its default colouring.
 */
SKALD_API bool TryGetFactionColor(ESkaldFaction Faction, FLinearColor& OutColor);

/** Convenience helper that always returns a color, falling back to white. */
SKALD_API FLinearColor GetFactionColor(ESkaldFaction Faction);
} // namespace SkaldFactionColors

