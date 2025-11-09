#include "SkaldFactionColorConfig.h"

USkaldFactionColorConfig::USkaldFactionColorConfig()
{
    FactionColors.Add(ESkaldFaction::Human, FLinearColor::Blue);
    FactionColors.Add(ESkaldFaction::Orc, FLinearColor::Red);
    FactionColors.Add(ESkaldFaction::Dwarf, FLinearColor(0.55f, 0.35f, 0.15f, 1.f));
    FactionColors.Add(ESkaldFaction::Elf, FLinearColor(0.05f, 0.18f, 0.08f, 1.f));
    FactionColors.Add(ESkaldFaction::LizardFolk, FLinearColor(0.0f, 0.5f, 0.5f, 1.f));
    FactionColors.Add(ESkaldFaction::Undead, FLinearColor::Black);
    FactionColors.Add(ESkaldFaction::Gnoll, FLinearColor(1.0f, 0.45f, 0.05f, 1.f));
    FactionColors.Add(ESkaldFaction::Goblin, FLinearColor(0.196f, 0.804f, 0.196f, 1.f));
    FactionColors.Add(ESkaldFaction::Empire, FLinearColor(0.5f, 0.0f, 0.5f, 1.f));
    FactionColors.Add(ESkaldFaction::Inflicted, FLinearColor(1.0f, 0.85f, 0.1f, 1.f));
    FactionColors.Add(ESkaldFaction::FrogFolk, FLinearColor(1.f, 0.31f, 0.55f, 1.f));
    FactionColors.Add(ESkaldFaction::Ravpack, FLinearColor(0.54f, 0.f, 0.54f, 1.f));
}

FLinearColor USkaldFactionColorConfig::GetColor(ESkaldFaction Faction) const
{
    if (Faction == ESkaldFaction::None)
    {
        return DefaultColor;
    }

    if (const FLinearColor* Found = FactionColors.Find(Faction))
    {
        return *Found;
    }

    return GetFallbackColor(Faction);
}

FLinearColor USkaldFactionColorConfig::GetFallbackColor(ESkaldFaction Faction)
{
    if (Faction == ESkaldFaction::None)
    {
        return FLinearColor::White;
    }

    switch (Faction)
    {
    case ESkaldFaction::Human:
        return FLinearColor::Blue;
    case ESkaldFaction::Orc:
        return FLinearColor::Red;
    case ESkaldFaction::Dwarf:
        return FLinearColor(0.55f, 0.35f, 0.15f, 1.f);
    case ESkaldFaction::Elf:
        return FLinearColor(0.05f, 0.18f, 0.08f, 1.f);
    case ESkaldFaction::LizardFolk:
        return FLinearColor(0.0f, 0.5f, 0.5f, 1.f);
    case ESkaldFaction::Undead:
        return FLinearColor::Black;
    case ESkaldFaction::Gnoll:
        return FLinearColor(1.0f, 0.45f, 0.05f, 1.f);
    case ESkaldFaction::Goblin:
        return FLinearColor(0.196f, 0.804f, 0.196f, 1.f);
    case ESkaldFaction::Empire:
        return FLinearColor(0.5f, 0.0f, 0.5f, 1.f);
    case ESkaldFaction::Inflicted:
        return FLinearColor(1.0f, 0.85f, 0.1f, 1.f);
    case ESkaldFaction::FrogFolk:
        return FLinearColor(1.f, 0.31f, 0.55f, 1.f);
    case ESkaldFaction::Ravpack:
        return FLinearColor(0.54f, 0.f, 0.54f, 1.f);
    default:
        break;
    }

    return FLinearColor::White;
}
