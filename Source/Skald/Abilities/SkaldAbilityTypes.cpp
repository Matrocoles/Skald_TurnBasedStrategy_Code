#include "Abilities/SkaldAbilityTypes.h"

#include "Sound/SoundBase.h"
#include "NiagaraSystem.h"
#include "Animation/AnimMontage.h"
#include "Engine/Texture2D.h"

namespace
{
TMap<FName, FSkaldAbilityDefinition> AbilityDefinitionsById;

void RegisterAbilityDefinition(const FSkaldAbilityDefinition& Definition)
{
    if (!Definition.IsValid())
    {
        return;
    }

    AbilityDefinitionsById.Add(Definition.AbilityId, Definition);
}

ESkaldAbilityTier TierFromCost(int32 ArmyCost)
{
    if (ArmyCost >= 5)
    {
        return ESkaldAbilityTier::Elite;
    }
    if (ArmyCost >= 2)
    {
        return ESkaldAbilityTier::Line;
    }
    return ESkaldAbilityTier::Skirmish;
}

FSkaldAbilityDefinition MakePassive(const TCHAR* Id, const FText& Name, const FText& Description)
{
    FSkaldAbilityDefinition Definition;
    Definition.AbilityId = FName(Id);
    Definition.AbilityName = Name;
    Definition.AbilityDescription = Description;
    Definition.CostType = ESkaldAbilityCostType::Free;
    Definition.bIsPassive = true;
    return Definition;
}

FSkaldAbilityDefinition MakeActive(const TCHAR* Id, const FText& Name, const FText& Description, ESkaldAbilityCostType CostType, int32 CooldownRounds, bool bOncePerBattle = false)
{
    FSkaldAbilityDefinition Definition;
    Definition.AbilityId = FName(Id);
    Definition.AbilityName = Name;
    Definition.AbilityDescription = Description;
    Definition.CostType = CostType;
    Definition.CooldownRounds = CooldownRounds;
    Definition.bOncePerBattle = bOncePerBattle;
    Definition.bIsPassive = false;
    return Definition;
}

TMap<ESkaldFaction, FSkaldFactionAbilitySet> BuildFactionAbilityMap()
{
    TMap<ESkaldFaction, FSkaldFactionAbilitySet> Result;

    AbilityDefinitionsById.Reset();

    {
        FSkaldFactionAbilitySet Set;
        Set.Passive = MakePassive(
            TEXT("Ability_Human_Passive"),
            NSLOCTEXT("SkaldAbilities", "HumanPassiveName", "Coordinated Volley"),
            NSLOCTEXT("SkaldAbilities", "HumanPassiveDesc", "After a Human fighter spends an action to attack, adjacent Human allies gain +1 Attack Dice on their next attack before the round ends."));
        Set.SkirmishAbility = MakeActive(
            TEXT("Ability_Human_Skirmish"),
            NSLOCTEXT("SkaldAbilities", "HumanSkirmishName", "Rallying Shot"),
            NSLOCTEXT("SkaldAbilities", "HumanSkirmishDesc", "Make a ranged attack. If at least one hit lands, choose an ally within attack range; that ally gains +1 Movement until the end of the round and may immediately shift 1 square."),
            ESkaldAbilityCostType::Action,
            0);
        Set.LineAbility = MakeActive(
            TEXT("Ability_Human_Line"),
            NSLOCTEXT("SkaldAbilities", "HumanLineName", "Shield Wall Pivot"),
            NSLOCTEXT("SkaldAbilities", "HumanLineDesc", "When an adjacent ally is targeted, expend the reaction to grant that ally +1 Defence for that attack and swap positions after damage resolves if the ally survives."),
            ESkaldAbilityCostType::Reaction,
            0);
        Set.EliteAbility = MakeActive(
            TEXT("Ability_Human_Elite"),
            NSLOCTEXT("SkaldAbilities", "HumanEliteName", "Tactical Reserves"),
            NSLOCTEXT("SkaldAbilities", "HumanEliteDesc", "Skip this fighter’s attack to ready nearby troops. Up to two allies within 3 squares immediately refresh one spent action or reaction but cannot take the same ability twice per round."),
            ESkaldAbilityCostType::Action,
            1);
        Result.Add(ESkaldFaction::Human, Set);
        RegisterAbilityDefinition(Set.Passive);
        RegisterAbilityDefinition(Set.SkirmishAbility);
        RegisterAbilityDefinition(Set.LineAbility);
        RegisterAbilityDefinition(Set.EliteAbility);
    }

    {
        FSkaldFactionAbilitySet Set;
        Set.Passive = MakePassive(
            TEXT("Ability_Orc_Passive"),
            NSLOCTEXT("SkaldAbilities", "OrcPassiveName", "Blood Frenzy"),
            NSLOCTEXT("SkaldAbilities", "OrcPassiveDesc", "When an Orc fighter scores at least one critical hit, it gains +1 Strength until it misses an attack."));
        Set.SkirmishAbility = MakeActive(
            TEXT("Ability_Orc_Skirmish"),
            NSLOCTEXT("SkaldAbilities", "OrcSkirmishName", "Brutal Charge"),
            NSLOCTEXT("SkaldAbilities", "OrcSkirmishDesc", "Move up to Movement + 2 toward a visible enemy. If the fighter moved at least 4 squares, add +1 Attack Damage for this attack."),
            ESkaldAbilityCostType::Action,
            0);
        Set.LineAbility = MakeActive(
            TEXT("Ability_Orc_Line"),
            NSLOCTEXT("SkaldAbilities", "OrcLineName", "Smash Through"),
            NSLOCTEXT("SkaldAbilities", "OrcLineDesc", "Make an attack. On any hit, push the target 1 square and reduce its Defence by 1 until the end of the round."),
            ESkaldAbilityCostType::Action,
            0);
        Set.EliteAbility = MakeActive(
            TEXT("Ability_Orc_Elite"),
            NSLOCTEXT("SkaldAbilities", "OrcEliteName", "WAAAGH! Roar"),
            NSLOCTEXT("SkaldAbilities", "OrcEliteDesc", "Trigger after rolling initiative. All Orc allies gain +1 Attack Dice on their first attack this round and ignore difficult terrain for their first move."),
            ESkaldAbilityCostType::Free,
            0,
            true);
        Result.Add(ESkaldFaction::Orc, Set);
        RegisterAbilityDefinition(Set.Passive);
        RegisterAbilityDefinition(Set.SkirmishAbility);
        RegisterAbilityDefinition(Set.LineAbility);
        RegisterAbilityDefinition(Set.EliteAbility);
    }

    {
        FSkaldFactionAbilitySet Set;
        Set.Passive = MakePassive(
            TEXT("Ability_Dwarf_Passive"),
            NSLOCTEXT("SkaldAbilities", "DwarfPassiveName", "Stalwart Line"),
            NSLOCTEXT("SkaldAbilities", "DwarfPassiveDesc", "Dwarven fighters standing adjacent to at least one Dwarf ally gain +1 Defence and cannot be pushed more than 1 square per effect."));
        Set.SkirmishAbility = MakeActive(
            TEXT("Ability_Dwarf_Skirmish"),
            NSLOCTEXT("SkaldAbilities", "DwarfSkirmishName", "Forgeguard Brace"),
            NSLOCTEXT("SkaldAbilities", "DwarfSkirmishDesc", "When charged from 2+ squares away, deal 1 automatic damage before the attack roll and gain +2 Defence against that attack."),
            ESkaldAbilityCostType::Reaction,
            0);
        Set.LineAbility = MakeActive(
            TEXT("Ability_Dwarf_Line"),
            NSLOCTEXT("SkaldAbilities", "DwarfLineName", "Rune-etched Riposte"),
            NSLOCTEXT("SkaldAbilities", "DwarfLineDesc", "Expend an action to ready a rune. Until the fighter next attacks, the first melee attacker that hits them suffers Attack Damage back as retaliatory damage."),
            ESkaldAbilityCostType::Action,
            1);
        Set.EliteAbility = MakeActive(
            TEXT("Ability_Dwarf_Elite"),
            NSLOCTEXT("SkaldAbilities", "DwarfEliteName", "Deep Delve Mortar"),
            NSLOCTEXT("SkaldAbilities", "DwarfEliteDesc", "Choose a point within 6 squares and roll an attack with Attack Dice - 1 dice. On a hit, deal full Attack Damage to the target and half to adjacent enemies; on a critical, also inflict -2 Movement until their next activation."),
            ESkaldAbilityCostType::Action,
            2);
        Result.Add(ESkaldFaction::Dwarf, Set);
        RegisterAbilityDefinition(Set.Passive);
        RegisterAbilityDefinition(Set.SkirmishAbility);
        RegisterAbilityDefinition(Set.LineAbility);
        RegisterAbilityDefinition(Set.EliteAbility);
    }

    {
        FSkaldFactionAbilitySet Set;
        Set.Passive = MakePassive(
            TEXT("Ability_Elf_Passive"),
            NSLOCTEXT("SkaldAbilities", "ElfPassiveName", "Grace of the Sylphs"),
            NSLOCTEXT("SkaldAbilities", "ElfPassiveDesc", "After an Elf fighter resolves movement without taking damage, they gain an evasion boon (treat incoming attack rolls as having -1 Attack Dice) until hit."));
        Set.SkirmishAbility = MakeActive(
            TEXT("Ability_Elf_Skirmish"),
            NSLOCTEXT("SkaldAbilities", "ElfSkirmishName", "Veil Step"),
            NSLOCTEXT("SkaldAbilities", "ElfSkirmishDesc", "Teleport up to 3 squares to a visible empty tile. The next attack this turn gains +1 Attack Range."),
            ESkaldAbilityCostType::Action,
            1);
        Set.LineAbility = MakeActive(
            TEXT("Ability_Elf_Line"),
            NSLOCTEXT("SkaldAbilities", "ElfLineName", "Moonlance Flurry"),
            NSLOCTEXT("SkaldAbilities", "ElfLineDesc", "Make two attacks with -1 Attack Dice each; critical hits restore 1 spent reaction."),
            ESkaldAbilityCostType::Action,
            0);
        Set.EliteAbility = MakeActive(
            TEXT("Ability_Elf_Elite"),
            NSLOCTEXT("SkaldAbilities", "ElfEliteName", "Starfall Invocation"),
            NSLOCTEXT("SkaldAbilities", "ElfEliteDesc", "Select a target within 8 squares. Roll an attack with +1 Attack Dice; on a hit, deal normal damage and blind the target (they roll -1 Attack Dice) until the end of their next activation."),
            ESkaldAbilityCostType::Action,
            0,
            true);
        Result.Add(ESkaldFaction::Elf, Set);
        RegisterAbilityDefinition(Set.Passive);
        RegisterAbilityDefinition(Set.SkirmishAbility);
        RegisterAbilityDefinition(Set.LineAbility);
        RegisterAbilityDefinition(Set.EliteAbility);
    }

    {
        FSkaldFactionAbilitySet Set;
        Set.Passive = MakePassive(
            TEXT("Ability_Lizard_Passive"),
            NSLOCTEXT("SkaldAbilities", "LizardPassiveName", "Cold-blooded Focus"),
            NSLOCTEXT("SkaldAbilities", "LizardPassiveDesc", "Lizardfolk ignore the first -1 penalty applied to their Strength or Attack Dice each round."));
        Set.SkirmishAbility = MakeActive(
            TEXT("Ability_Lizard_Skirmish"),
            NSLOCTEXT("SkaldAbilities", "LizardSkirmishName", "Tail Sweep"),
            NSLOCTEXT("SkaldAbilities", "LizardSkirmishDesc", "Attack all adjacent enemies with Attack Dice - 1 dice. Any enemy hit suffers -1 Movement until end of round."),
            ESkaldAbilityCostType::Action,
            1);
        Set.LineAbility = MakeActive(
            TEXT("Ability_Lizard_Line"),
            NSLOCTEXT("SkaldAbilities", "LizardLineName", "Amphibious Rush"),
            NSLOCTEXT("SkaldAbilities", "LizardLineDesc", "When entering or starting in water terrain, gain +2 Movement and +1 Attack Dice for the turn."),
            ESkaldAbilityCostType::Action,
            0);
        Set.EliteAbility = MakeActive(
            TEXT("Ability_Lizard_Elite"),
            NSLOCTEXT("SkaldAbilities", "LizardEliteName", "Primeval Regeneration"),
            NSLOCTEXT("SkaldAbilities", "LizardEliteDesc", "At the start of this fighter’s activation, restore 1d3 Health (capped by max) and cleanse one debuff."),
            ESkaldAbilityCostType::Free,
            3);
        Result.Add(ESkaldFaction::LizardFolk, Set);
        RegisterAbilityDefinition(Set.Passive);
        RegisterAbilityDefinition(Set.SkirmishAbility);
        RegisterAbilityDefinition(Set.LineAbility);
        RegisterAbilityDefinition(Set.EliteAbility);
    }

    {
        FSkaldFactionAbilitySet Set;
        Set.Passive = MakePassive(
            TEXT("Ability_Undead_Passive"),
            NSLOCTEXT("SkaldAbilities", "UndeadPassiveName", "Necrotic Resilience"),
            NSLOCTEXT("SkaldAbilities", "UndeadPassiveDesc", "When reduced to 1 Health, Undead fighters gain +1 Defence and +1 Attack Dice until they heal above 1 Health and ignore morale-based effects."));
        Set.SkirmishAbility = MakeActive(
            TEXT("Ability_Undead_Skirmish"),
            NSLOCTEXT("SkaldAbilities", "UndeadSkirmishName", "Grave Grasp"),
            NSLOCTEXT("SkaldAbilities", "UndeadSkirmishDesc", "Attack a target within 3 squares. On a hit, root the enemy (Movement becomes 0) until end of its next activation."),
            ESkaldAbilityCostType::Action,
            0);
        Set.LineAbility = MakeActive(
            TEXT("Ability_Undead_Line"),
            NSLOCTEXT("SkaldAbilities", "UndeadLineName", "Soul Harvest"),
            NSLOCTEXT("SkaldAbilities", "UndeadLineDesc", "After defeating an enemy this activation, immediately gain +1 Attack Dice and heal 1 Health (cannot exceed max). If no enemy dies, regain the action instead."),
            ESkaldAbilityCostType::Action,
            1);
        Set.EliteAbility = MakeActive(
            TEXT("Ability_Undead_Elite"),
            NSLOCTEXT("SkaldAbilities", "UndeadEliteName", "Deathless Advance"),
            NSLOCTEXT("SkaldAbilities", "UndeadEliteDesc", "When reduced to 0 Health, stand back up at 1 Health and gain +2 Attack Dice for the rest of the round; afterwards, remove this ability."),
            ESkaldAbilityCostType::Free,
            0,
            true);
        Result.Add(ESkaldFaction::Undead, Set);
        RegisterAbilityDefinition(Set.Passive);
        RegisterAbilityDefinition(Set.SkirmishAbility);
        RegisterAbilityDefinition(Set.LineAbility);
        RegisterAbilityDefinition(Set.EliteAbility);
    }

    {
        FSkaldFactionAbilitySet Set;
        Set.Passive = MakePassive(
            TEXT("Ability_Gnoll_Passive"),
            NSLOCTEXT("SkaldAbilities", "GnollPassiveName", "Pack Instincts"),
            NSLOCTEXT("SkaldAbilities", "GnollPassiveDesc", "Gnoll fighters gain +1 Attack Dice when an ally has already attacked the same target this round."));
        Set.SkirmishAbility = MakeActive(
            TEXT("Ability_Gnoll_Skirmish"),
            NSLOCTEXT("SkaldAbilities", "GnollSkirmishName", "Harrier Dash"),
            NSLOCTEXT("SkaldAbilities", "GnollSkirmishDesc", "Move up to Movement without provoking reactions, then make an attack that causes -1 Defence on hit (each fighter can only be affected once)."),
            ESkaldAbilityCostType::Action,
            2);
        Set.LineAbility = MakeActive(
            TEXT("Ability_Gnoll_Line"),
            NSLOCTEXT("SkaldAbilities", "GnollLineName", "Howl of the Alpha"),
            NSLOCTEXT("SkaldAbilities", "GnollLineDesc", "Emit a rallying howl. All allies within 4 squares gain +1 Movement and may ignore disengage penalties until end of round."),
            ESkaldAbilityCostType::Action,
            2);
        Set.EliteAbility = MakeActive(
            TEXT("Ability_Gnoll_Elite"),
            NSLOCTEXT("SkaldAbilities", "GnollEliteName", "Rend and Tear"),
            NSLOCTEXT("SkaldAbilities", "GnollEliteDesc", "Make an attack; for each hit scored, inflict a stacking bleed that deals 1 damage at the start of the target’s activation (max 3 stacks)."),
            ESkaldAbilityCostType::Action,
            1);
        Result.Add(ESkaldFaction::Gnoll, Set);
        RegisterAbilityDefinition(Set.Passive);
        RegisterAbilityDefinition(Set.SkirmishAbility);
        RegisterAbilityDefinition(Set.LineAbility);
        RegisterAbilityDefinition(Set.EliteAbility);
    }

    {
        FSkaldFactionAbilitySet Set;
        Set.Passive = MakePassive(
            TEXT("Ability_Goblin_Passive"),
            NSLOCTEXT("SkaldAbilities", "GoblinPassiveName", "Mob Tactics"),
            NSLOCTEXT("SkaldAbilities", "GoblinPassiveDesc", "At the start of its activation, if a Goblin fighter has an ally within 2 squares it gains +1 Attack Dice for that activation; otherwise it gains +1 Movement instead."));
        Set.SkirmishAbility = MakeActive(
            TEXT("Ability_Goblin_Skirmish"),
            NSLOCTEXT("SkaldAbilities", "GoblinSkirmishName", "Flash Bomb"),
            NSLOCTEXT("SkaldAbilities", "GoblinSkirmishDesc", "Make an attack with -1 Attack Damage; on hit, the target suffers -1 Defence and loses all reactions until its next activation."),
            ESkaldAbilityCostType::Action,
            0);
        Set.LineAbility = MakeActive(
            TEXT("Ability_Goblin_Line"),
            NSLOCTEXT("SkaldAbilities", "GoblinLineName", "Tinkerer's Net"),
            NSLOCTEXT("SkaldAbilities", "GoblinLineDesc", "Make an attack; on hit, the target suffers -2 Movement and -1 Attack Damage until the start of its next activation."),
            ESkaldAbilityCostType::Action,
            1);
        Set.EliteAbility = MakeActive(
            TEXT("Ability_Goblin_Elite"),
            NSLOCTEXT("SkaldAbilities", "GoblinEliteName", "Smoke and Slash"),
            NSLOCTEXT("SkaldAbilities", "GoblinEliteDesc", "Free. Gain +1 Attack Dice and +2 Movement for this activation. If you attack during this activation, suffer -1 Defence until the start of the next round."),
            ESkaldAbilityCostType::Free,
            2);
        Result.Add(ESkaldFaction::Goblin, Set);
        RegisterAbilityDefinition(Set.Passive);
        RegisterAbilityDefinition(Set.SkirmishAbility);
        RegisterAbilityDefinition(Set.LineAbility);
        RegisterAbilityDefinition(Set.EliteAbility);
    }

    {
        FSkaldFactionAbilitySet Set;
        Set.Passive = MakePassive(
            TEXT("Ability_Empire_Passive"),
            NSLOCTEXT("SkaldAbilities", "EmpirePassiveName", "Iron Discipline"),
            NSLOCTEXT("SkaldAbilities", "EmpirePassiveDesc", "IronLegion units can reroll one initiative die per round and choose either result."));
        Set.SkirmishAbility = MakeActive(
            TEXT("Ability_Empire_Skirmish"),
            NSLOCTEXT("SkaldAbilities", "EmpireSkirmishName", "Suppressing Fire"),
            NSLOCTEXT("SkaldAbilities", "EmpireSkirmishDesc", "Attack at range with -1 Attack Damage; on hit, the target suffers -2 Movement and cannot take reactions until its next activation."),
            ESkaldAbilityCostType::Action,
            0);
        Set.LineAbility = MakeActive(
            TEXT("Ability_Empire_Line"),
            NSLOCTEXT("SkaldAbilities", "EmpireLineName", "Officer’s Command"),
            NSLOCTEXT("SkaldAbilities", "EmpireLineDesc", "After rolling attack dice, may transfer one unused hit to an adjacent ally’s pending attack before results are resolved."),
            ESkaldAbilityCostType::Free,
            1);
        Set.EliteAbility = MakeActive(
            TEXT("Ability_Empire_Elite"),
            NSLOCTEXT("SkaldAbilities", "EmpireEliteName", "Artillery Strike"),
            NSLOCTEXT("SkaldAbilities", "EmpireEliteDesc", "Spend an action to call artillery on a visible tile. At end of the round, roll an attack with +2 Attack Damage against all units within 2 squares (allies take half damage). Ability recharges after two rounds."),
            ESkaldAbilityCostType::Action,
            2);
        Result.Add(ESkaldFaction::Empire, Set);
        RegisterAbilityDefinition(Set.Passive);
        RegisterAbilityDefinition(Set.SkirmishAbility);
        RegisterAbilityDefinition(Set.LineAbility);
        RegisterAbilityDefinition(Set.EliteAbility);
    }

    {
        FSkaldFactionAbilitySet Set;
        Set.Passive = MakePassive(
            TEXT("Ability_Inflicted_Passive"),
            NSLOCTEXT("SkaldAbilities", "InflictedPassiveName", "Unstable Mutation"),
            NSLOCTEXT("SkaldAbilities", "InflictedPassiveDesc", "When an Inflicted fighter is targeted by an attack, roll a die: on 4+, gain either +1 Defence or +1 Attack Damage (player choice) until end of round."));
        Set.SkirmishAbility = MakeActive(
            TEXT("Ability_Inflicted_Skirmish"),
            NSLOCTEXT("SkaldAbilities", "InflictedSkirmishName", "Viral Lash"),
            NSLOCTEXT("SkaldAbilities", "InflictedSkirmishDesc", "Make an attack; on hit, apply a contagion that causes the target to suffer -1 Defence and spread the debuff to adjacent enemies if they take damage this round."),
            ESkaldAbilityCostType::Action,
            0);
        Set.LineAbility = MakeActive(
            TEXT("Ability_Inflicted_Line"),
            NSLOCTEXT("SkaldAbilities", "InflictedLineName", "Mutagenic Surge"),
            NSLOCTEXT("SkaldAbilities", "InflictedLineDesc", "Before rolling attack dice, choose to gain +2 Attack Dice and suffer -1 Defence until next activation."),
            ESkaldAbilityCostType::Free,
            2);
        Set.EliteAbility = MakeActive(
            TEXT("Ability_Inflicted_Elite"),
            NSLOCTEXT("SkaldAbilities", "InflictedEliteName", "Aberrant Bloom"),
            NSLOCTEXT("SkaldAbilities", "InflictedEliteDesc", "Transform adjacent empty squares into hazardous terrain until end of battle. Enemies entering or starting in the hazard take Attack Damage and suffer -1 Movement (stacking once)."),
            ESkaldAbilityCostType::Action,
            0,
            true);
        Result.Add(ESkaldFaction::Inflicted, Set);
        RegisterAbilityDefinition(Set.Passive);
        RegisterAbilityDefinition(Set.SkirmishAbility);
        RegisterAbilityDefinition(Set.LineAbility);
        RegisterAbilityDefinition(Set.EliteAbility);
    }

    {
        FSkaldFactionAbilitySet Set;
        Set.Passive = MakePassive(
            TEXT("Ability_Frog_Passive"),
            NSLOCTEXT("SkaldAbilities", "FrogPassiveName", "Mire Masters"),
            NSLOCTEXT("SkaldAbilities", "FrogPassiveDesc", "ToadFolk treat water and swamp tiles as normal terrain and impose -1 Movement on enemies who start adjacent to them."));
        Set.SkirmishAbility = MakeActive(
            TEXT("Ability_Frog_Skirmish"),
            NSLOCTEXT("SkaldAbilities", "FrogSkirmishName", "Tongue Snare"),
            NSLOCTEXT("SkaldAbilities", "FrogSkirmishDesc", "Pull a target within 4 squares 1 square closer and reduce its Attack Dice by 1 until end of round."),
            ESkaldAbilityCostType::Action,
            0);
        Set.LineAbility = MakeActive(
            TEXT("Ability_Frog_Line"),
            NSLOCTEXT("SkaldAbilities", "FrogLineName", "Bubble Ward"),
            NSLOCTEXT("SkaldAbilities", "FrogLineDesc", "When an ally within 3 squares is hit by a ranged attack, negate one hit and slide the ally 1 square."),
            ESkaldAbilityCostType::Reaction,
            1);
        Set.EliteAbility = MakeActive(
            TEXT("Ability_Frog_Elite"),
            NSLOCTEXT("SkaldAbilities", "FrogEliteName", "Raincaller Deluge"),
            NSLOCTEXT("SkaldAbilities", "FrogEliteDesc", "Create a rainstorm template (radius 2) for one round. Enemies inside suffer -1 Attack Dice, allies gain +1 Defence and amphibious movement bonuses."),
            ESkaldAbilityCostType::Action,
            2);
        Result.Add(ESkaldFaction::FrogFolk, Set);
        RegisterAbilityDefinition(Set.Passive);
        RegisterAbilityDefinition(Set.SkirmishAbility);
        RegisterAbilityDefinition(Set.LineAbility);
        RegisterAbilityDefinition(Set.EliteAbility);
    }

    {
        FSkaldFactionAbilitySet Set;
        Set.Passive = MakePassive(
            TEXT("Ability_Ravpack_Passive"),
            NSLOCTEXT("SkaldAbilities", "RavpackPassiveName", "Scavenged Momentum"),
            NSLOCTEXT("SkaldAbilities", "RavpackPassiveDesc", "After a Ravpack fighter defeats an enemy or picks up loot, they gain +2 Movement on their next activation."));
        Set.SkirmishAbility = MakeActive(
            TEXT("Ability_Ravpack_Skirmish"),
            NSLOCTEXT("SkaldAbilities", "RavpackSkirmishName", "Scrapper Feint"),
            NSLOCTEXT("SkaldAbilities", "RavpackSkirmishDesc", "Make a melee attack; on any miss, the fighter may immediately move 2 squares without provoking reactions."),
            ESkaldAbilityCostType::Action,
            0);
        Set.LineAbility = MakeActive(
            TEXT("Ability_Ravpack_Line"),
            NSLOCTEXT("SkaldAbilities", "RavpackLineName", "Jury-rigged Explosive"),
            NSLOCTEXT("SkaldAbilities", "RavpackLineDesc", "Plant a proximity charge on an adjacent tile. When an enemy enters, roll an attack with Attack Dice dice dealing Attack Damage + Critical Bonus Damage on critical hits."),
            ESkaldAbilityCostType::Action,
            2);
        Set.EliteAbility = MakeActive(
            TEXT("Ability_Ravpack_Elite"),
            NSLOCTEXT("SkaldAbilities", "RavpackEliteName", "Overclock Harness"),
            NSLOCTEXT("SkaldAbilities", "RavpackEliteDesc", "At the start of activation, gain +1 Attack Dice and +1 Movement for the turn; at end of activation, suffer 1 damage if you attacked."),
            ESkaldAbilityCostType::Free,
            2);
        Result.Add(ESkaldFaction::Ravpack, Set);
        RegisterAbilityDefinition(Set.Passive);
        RegisterAbilityDefinition(Set.SkirmishAbility);
        RegisterAbilityDefinition(Set.LineAbility);
        RegisterAbilityDefinition(Set.EliteAbility);
    }

    return Result;
}

} // namespace

FText FSkaldAbilityDefinition::BuildCostLabel() const
{
    switch (CostType)
    {
    case ESkaldAbilityCostType::Action:
        return NSLOCTEXT("SkaldAbilities", "AbilityCostAction", "Action");
    case ESkaldAbilityCostType::Reaction:
        return NSLOCTEXT("SkaldAbilities", "AbilityCostReaction", "Reaction");
    case ESkaldAbilityCostType::Free:
        return NSLOCTEXT("SkaldAbilities", "AbilityCostFree", "Free");
    default:
        break;
    }
    return FText::GetEmpty();
}

ESkaldAbilityTier ResolveAbilityTierForCost(int32 ArmyCost)
{
    return TierFromCost(ArmyCost);
}

const FSkaldFactionAbilitySet* FindFactionAbilitySet(ESkaldFaction Faction)
{
    static const TMap<ESkaldFaction, FSkaldFactionAbilitySet> AbilitySets = BuildFactionAbilityMap();
    return AbilitySets.Find(Faction);
}

FSkaldAbilityDefinition GetFactionPassive(ESkaldFaction Faction)
{
    if (const FSkaldFactionAbilitySet* Set = FindFactionAbilitySet(Faction))
    {
        return Set->Passive;
    }
    return FSkaldAbilityDefinition();
}

FSkaldAbilityDefinition GetFactionActiveAbility(ESkaldFaction Faction, int32 ArmyCost)
{
    if (const FSkaldFactionAbilitySet* Set = FindFactionAbilitySet(Faction))
    {
        const ESkaldAbilityTier Tier = ResolveAbilityTierForCost(ArmyCost);
        switch (Tier)
        {
        case ESkaldAbilityTier::Skirmish:
            return Set->SkirmishAbility;
        case ESkaldAbilityTier::Line:
            return Set->LineAbility;
        case ESkaldAbilityTier::Elite:
            return Set->EliteAbility;
        default:
            break;
        }
    }
    return FSkaldAbilityDefinition();
}

FSkaldAbilityDefinition GetAbilityDefinitionById(FName AbilityId)
{
    if (AbilityId.IsNone())
    {
        return FSkaldAbilityDefinition();
    }

    FindFactionAbilitySet(ESkaldFaction::None);

    if (const FSkaldAbilityDefinition* Definition = AbilityDefinitionsById.Find(AbilityId))
    {
        return *Definition;
    }

    return FSkaldAbilityDefinition();
}

bool IsPassiveAbilityId(FName AbilityId)
{
    if (AbilityId.IsNone())
    {
        return false;
    }

    const FSkaldAbilityDefinition Definition = GetAbilityDefinitionById(AbilityId);
    return Definition.IsValid() && Definition.bIsPassive;
}

