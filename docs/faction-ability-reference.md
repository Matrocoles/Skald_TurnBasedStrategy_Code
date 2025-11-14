# Faction Ability Reference

The following tables mirror the data baked into `FSkaldFactionAbilitySet` rows (see `Source/Skald/Abilities/SkaldAbilityTypes.cpp`).
Each entry lists the public-facing ability name, its stable `AbilityId`, and the exact description text that designers should
copy into faction ability row assets.

## Human

| Slot | Ability Name | Ability ID | Description |
| --- | --- | --- | --- |
| Passive | Coordinated Volley | Ability_Human_Passive | After a Human fighter spends an action to attack, adjacent Human allies gain +1 Attack Dice on their next attack before the round ends. |
| Skirmish | Rallying Shot | Ability_Human_Skirmish | Make a ranged attack. If at least one hit lands, choose an ally within attack range; that ally gains +1 Movement until the end of the round and may immediately shift 1 square. |
| Line | Shield Wall Rally | Ability_Human_Line | Spend an action to choose an adjacent ally and reform the line. You and the ally each gain +1 Defence until the start of your next activation and may immediately shift 1 square, swapping positions if desired. |
| Elite | Tactical Reserves | Ability_Human_Elite | Spend an action to direct reinforcements. Choose up to two allies within 5 squares; each may immediately move up to 2 squares and gains +1 Attack Dice on their next attack this round. |

## Orc

| Slot | Ability Name | Ability ID | Description |
| --- | --- | --- | --- |
| Passive | Blood Frenzy | Ability_Orc_Passive | When an Orc fighter scores at least one critical hit, it gains +1 Strength until it misses an attack. |
| Skirmish | Brutal Charge | Ability_Orc_Skirmish | Begin a rush gaining +2 Movement for this move toward a visible enemy. If the fighter travels at least 4 squares, their next attack this activation gains +1 Attack Damage and +1 Attack Dice. |
| Line | Smash Through | Ability_Orc_Line | Make an attack. On any hit, push the target 1 square and reduce its Defence by 1 until the end of the round. |
| Elite | WAAAGH! Roar | Ability_Orc_Elite | After this fighter activates, unleash a deafening roar by spending 2 actions. All Orc allies gain +2 Movement, +1 Attack Dice, ignore difficult terrain on their first movement, and score critical hits on rolls of 5-6 until the round ends. |

## Dwarf

| Slot | Ability Name | Ability ID | Description |
| --- | --- | --- | --- |
| Passive | Stalwart Line | Ability_Dwarf_Passive | Dwarven fighters standing adjacent to at least one Dwarf ally gain +1 Defence and cannot be pushed more than 1 square per effect. |
| Skirmish | Forgeguard Brace | Ability_Dwarf_Skirmish | Spend an action to lock shields. Gain +2 Defence until the start of your next activation. The first enemy that moves adjacent during that time suffers 1 automatic damage and -1 Movement until end of round. |
| Line | Rune-etched Riposte | Ability_Dwarf_Line | Spend an action to etch retaliatory runes. Until the fighter next attacks, the first melee attacker that targets them suffers Attack Damage for each miss rolled during that attack. |
| Elite | Deep Delve Mortar | Ability_Dwarf_Elite | Choose a point within 6 squares and roll an attack with Attack Dice - 1 dice. On a hit, deal full Attack Damage to the target and half to adjacent enemies; on a critical, also inflict -2 Movement until their next activation. |

## Elf

| Slot | Ability Name | Ability ID | Description |
| --- | --- | --- | --- |
| Passive | Grace of the Sylphs | Ability_Elf_Passive | After an Elf fighter resolves movement without taking damage, they gain an evasion boon (treat incoming attack rolls as having -1 Attack Dice) until hit. |
| Skirmish | Veil Step | Ability_Elf_Skirmish | Teleport up to 3 squares to a visible empty tile. The next attack this turn gains +1 Attack Range. |
| Line | Moonlance Flurry | Ability_Elf_Line | Make a single sweeping attack with +1 Attack Dice. |
| Elite | Starfall Invocation | Ability_Elf_Elite | Select a target within 8 squares. Roll an attack with +1 Attack Dice; on a hit, deal normal damage and blind the target (they roll -1 Attack Dice) until the end of their next activation. |

## Lizardfolk

| Slot | Ability Name | Ability ID | Description |
| --- | --- | --- | --- |
| Passive | Cold-blooded Focus | Ability_Lizard_Passive | Lizardfolk ignore the first -1 penalty applied to their Strength or Attack Dice each round. |
| Skirmish | Tail Sweep | Ability_Lizard_Skirmish | Attack all adjacent enemies with Attack Dice - 1 dice. Any enemy hit suffers -1 Movement until end of round. |
| Line | Amphibious Rush | Ability_Lizard_Line | When entering or starting in difficult terrain, gain +2 Movement and +1 Attack Dice for the turn. |
| Elite | Primeval Regeneration | Ability_Lizard_Elite | At the start of this fighter’s activation, restore 3 Health (capped by max) and cleanse one debuff. |

## Undead

| Slot | Ability Name | Ability ID | Description |
| --- | --- | --- | --- |
| Passive | Necrotic Resilience | Ability_Undead_Passive | When reduced to 5 Health, Undead fighters gain +1 Defence and +1 Attack Dice until they heal above 5 Health and ignore morale-based effects. |
| Skirmish | Grave Grasp | Ability_Undead_Skirmish | Attack a target within 3 squares. On a hit, root the enemy (Movement becomes 0) until end of its next activation. |
| Line | Soul Harvest | Ability_Undead_Line | Make an attack with +1 Attack Dice. Heal 1 Health for each hit scored on this attack (cannot exceed max). |
| Elite | Deathless Advance (Passive) | Ability_Undead_Elite | When reduced to 0 Health, immediately rise again with 5 Health. This passive cannot trigger again for 10 rounds. |

## Gnoll

| Slot | Ability Name | Ability ID | Description |
| --- | --- | --- | --- |
| Passive | Pack Instincts | Ability_Gnoll_Passive | Gnoll fighters gain +1 Attack Dice when an ally has already attacked the same target this round. |
| Skirmish | Harrier Dash | Ability_Gnoll_Skirmish | Move up to Movement without provoking reactions, then make an attack that causes -1 Defence on hit (each fighter can only be affected once). |
| Line | Howl of the Alpha | Ability_Gnoll_Line | Emit a rallying howl. All allies within 4 squares gain +1 Movement and may ignore disengage penalties until end of round. |
| Elite | Rend and Tear | Ability_Gnoll_Elite | Make an attack; for each hit scored, inflict a stacking bleed that deals 1 damage at the start of the target’s activation (max 3 stacks). |

## Goblin

| Slot | Ability Name | Ability ID | Description |
| --- | --- | --- | --- |
| Passive | Mob Tactics | Ability_Goblin_Passive | At the start of its activation, if a Goblin fighter has an ally within 2 squares it gains +1 Attack Dice for that activation; otherwise it gains +1 Movement instead. |
| Skirmish | Flash Bomb | Ability_Goblin_Skirmish | Make an attack with -1 Attack Damage; on hit, the target suffers -1 Defence and loses all reactions until its next activation. |
| Line | Tinkerer's Net | Ability_Goblin_Line | Make an attack; on hit, the target suffers -2 Movement and -1 Attack Damage until the start of its next activation. |
| Elite | Smoke and Slash | Ability_Goblin_Elite | Free. Gain +1 Attack Dice and +2 Movement for this activation. If you attack during this activation, suffer -1 Defence until the start of the next round. |

## Iron Legion (Empire)

| Slot | Ability Name | Ability ID | Description |
| --- | --- | --- | --- |
| Passive | Iron Resolve | Ability_Empire_Passive | When an Iron Legion fighter is adjacent to two or more enemies, they gain +1 Defence. |
| Skirmish | Suppressing Fire | Ability_Empire_Skirmish | Attack at range with -1 Attack Damage; on hit, the target suffers -2 Movement and cannot take reactions until its next activation. |
| Line | Officer’s Command | Ability_Empire_Line | After rolling attack dice, may transfer one unused hit to an adjacent ally’s pending attack before results are resolved. |
| Elite | Artillery Strike | Ability_Empire_Elite | Spend 2 actions to call artillery on a visible tile. Immediately roll an attack with +2 Attack Damage against all units within 2 squares (allies take half damage). Ability recharges after two rounds. |

## The Inflicted

| Slot | Ability Name | Ability ID | Description |
| --- | --- | --- | --- |
| Passive | Unstable Mutation | Ability_Inflicted_Passive | When an Inflicted fighter is targeted by an attack, roll a die: on 4+, gain either +1 Defence or +1 Attack Damage (player choice) until end of round. |
| Skirmish | Viral Lash | Ability_Inflicted_Skirmish | Make an attack; on hit, apply a contagion that causes the target to suffer -1 Defence (once per fighter) and spread the debuff to adjacent enemies if they take damage this round. |
| Line | Mutagenic Surge | Ability_Inflicted_Line | Before rolling attack dice, choose to gain +2 Attack Dice and suffer -1 Defence until next activation. |
| Elite | Aberrant Bloom | Ability_Inflicted_Elite | Transform adjacent empty squares into hazardous terrain until end of battle. Enemies entering or starting in the hazard take Attack Damage and suffer -1 Movement (stacking once). |

## Frogfolk

| Slot | Ability Name | Ability ID | Description |
| --- | --- | --- | --- |
| Passive | Mire Masters | Ability_Frog_Passive | ToadFolk ignore penalties from water or swamp tiles and leave clinging mire behind. Squares they exit become difficult terrain for enemies until the end of the round. |
| Skirmish | Tongue Snare | Ability_Frog_Skirmish | Pull a target within 4 squares 1 square closer and reduce its Attack Dice by 1 until end of round. |
| Line | Bubble Ward | Ability_Frog_Line | Envelope an ally within 3 squares in a buoyant ward. Until the start of your next activation, that ally gains +1 Defence and the next ranged attack against them loses one success before damage is applied. |
| Elite | Raincaller Deluge | Ability_Frog_Elite | Create a rainstorm template (radius 2) for one round. Enemies inside suffer -1 Attack Dice, allies gain +1 Defence and amphibious movement bonuses. |

## Ravpack

| Slot | Ability Name | Ability ID | Description |
| --- | --- | --- | --- |
| Passive | Scavenged Momentum | Ability_Ravpack_Passive | After a Ravpack fighter defeats an enemy or picks up loot, they gain +2 Movement on their next activation. |
| Skirmish | Scrapper Feint | Ability_Ravpack_Skirmish | Make a melee attack; on any miss, the fighter may immediately move 2 squares without provoking reactions. |
| Line | Jury-rigged Explosive | Ability_Ravpack_Line | Plant a proximity charge on an adjacent tile. When an enemy enters, roll an attack with Attack Dice dice dealing Attack Damage + Critical Bonus Damage on critical hits. |
| Elite | Overclock Harness | Ability_Ravpack_Elite | After completing this fighter’s activation, engage the harness to gain +1 Attack Dice and +1 Movement until the start of their next activation; suffer 1 damage if they attacked before triggering it. |
