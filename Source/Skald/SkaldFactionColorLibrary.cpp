#include "SkaldFactionColorLibrary.h"

namespace SkaldFactionColors
{
namespace
{
FLinearColor ResolveColorInternal(ESkaldFaction Faction, bool& bOutHasColor)
{
    bOutHasColor = true;
    switch (Faction)
    {
    case ESkaldFaction::Human:
        return FLinearColor::Blue;
    case ESkaldFaction::Orc:
        return FLinearColor::Red;
    case ESkaldFaction::Dwarf:
        return FLinearColor(0.55f, 0.35f, 0.15f, 1.f); // Brown
    case ESkaldFaction::Elf:
        return FLinearColor(0.05f, 0.18f, 0.08f, 1.f); // Dark vine green
    case ESkaldFaction::LizardFolk:
        return FLinearColor(0.0f, 0.5f, 0.5f, 1.f); // Teal
    case ESkaldFaction::Undead:
        return FLinearColor::Black;
    case ESkaldFaction::Gnoll:
        return FLinearColor(1.0f, 0.45f, 0.05f, 1.f); // Orange
    case ESkaldFaction::Goblin:
        return FLinearColor(0.196f, 0.804f, 0.196f, 1.f); // Lime green
    case ESkaldFaction::Empire:
        return FLinearColor(0.5f, 0.0f, 0.5f, 1.f); // Purple (IronLegion)
    case ESkaldFaction::Inflicted:
        return FLinearColor(1.0f, 0.85f, 0.1f, 1.f); // Yellow
    case ESkaldFaction::FrogFolk:
        return FLinearColor(1.f, 0.31f, 0.55f, 1.f); // Rose (ToadFolk)
    case ESkaldFaction::Ravpack:
        return FLinearColor(0.54f, 0.f, 0.54f, 1.f); // Violet
    default:
        bOutHasColor = false;
        return FLinearColor::White;
    }
}
} // namespace

bool TryGetFactionColor(ESkaldFaction Faction, FLinearColor& OutColor)
{
    bool bHasColor = false;
    const FLinearColor Color = ResolveColorInternal(Faction, bHasColor);
    if (bHasColor)
    {
        OutColor = Color;
    }
    return bHasColor;
}

FLinearColor GetFactionColor(ESkaldFaction Faction)
{
    bool bHasColor = false;
    const FLinearColor Color = ResolveColorInternal(Faction, bHasColor);
    return Color;
}

} // namespace SkaldFactionColors

