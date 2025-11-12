# Faction Passive & Unit Active Abilities

This document proposes faction-wide passives and tiered active abilities that plug directly into the existing `FFighterStats` data (attack dice, defence, strength, movement, critical bonus, and army cost). Each faction receives a single passive that all of its fighters benefit from, plus three suggested active abilities keyed to low-, mid-, and high-cost fighters (using `Stats.ArmyCost` as the driver for availability).

## Cost Tier Conventions

* **Skirmish** – `ArmyCost` 1-3. Designed for expendable troops and support pieces that can trade actions efficiently.
* **Line** – `ArmyCost` 4-7. Core units that should have impactful options at the price of an action or limited triggers.
* **Elite** – `ArmyCost` 8+. Signature units that justify premium costs with strong effects and/or combo potential.

Individual armies can nudge thresholds up or down if data-table tuning warrants it, but the names give designers and code a hook for conditional checks (e.g. expose an ability picker filtered by `Stats.ArmyCost`).

## Human Faction

* **Passive – Coordinated Volley:** After a Human fighter spends an action to attack, adjacent Human allies gain `+1 AttackDice` on their next attack before the round ends.
* **Skirmish Ability – Rallying Shot (Action):** Make a ranged attack. If at least one hit lands, choose an ally within `AttackRange`; that ally gains `+1 Movement` until the end of the round and may immediately shift 1 square.
* **Line Ability – Shield Wall Rally (Action, cooldown 2 rounds):** Spend an action to choose an adjacent ally and reform the line. You and the ally each gain `+1 Defence` until the start of your next activation and may immediately shift 1 square, swapping positions if desired.
* **Elite Ability – Tactical Reserves (Action, cooldown 2 rounds):** Spend an action to direct reinforcements. Choose up to two allies within 5 squares; each may immediately move up to 2 squares and gains `+1 AttackDice` on their next attack this round.

## Orc Faction

* **Passive – Blood Frenzy:** When an Orc fighter scores at least one critical hit, it gains `+1 Strength` until it misses an attack.
* **Skirmish Ability – Brutal Charge (Action, cooldown 2 rounds):** Begin a rush gaining `+2 Movement` toward a visible enemy. If the fighter travels at least 4 squares, their next attack this activation gains `+1 AttackDamage` and `+1 AttackDice`.
* **Line Ability – Smash Through (Action):** Make an attack. On any hit, push the target 1 square and reduce its `Defence` by 1 (stacking) until the end of the round.
* **Elite Ability – WAAAGH! Roar (Action, spend 2 actions, cooldown 3 rounds):** After this fighter activates, unleash a deafening roar. All Orc allies gain `+2 Movement`, `+1 AttackDice`, ignore difficult terrain on their first movement, and score critical hits on rolls of 5-6 until the round ends.

## Dwarf Faction

* **Passive – Stalwart Line:** Dwarven fighters standing adjacent to at least one Dwarf ally gain `+1 Defence` and cannot be pushed more than 1 square per effect.
* **Skirmish Ability – Forgeguard Brace (Action, cooldown 2 rounds):** Spend an action to lock shields. Gain `+2 Defence` until the start of your next activation. The first enemy that moves adjacent during that time suffers `1` automatic damage and `-1 Movement` until end of round.
* **Line Ability – Rune-etched Riposte (Action, cooldown 2 rounds):** Spend an action to etch retaliatory runes. Until the fighter next attacks, the first melee attacker that targets them suffers `AttackDamage` for each miss rolled during that attack.
* **Elite Ability – Deep Delve Mortar (Action, cooldown 2 rounds):** Choose a point within 6 squares and roll an attack with `AttackDice - 1` dice. On a hit, deal full `AttackDamage` to the target and half to adjacent enemies; on a critical, also inflict `-2 Movement` until their next activation.

## Elf Faction

* **Passive – Grace of the Sylphs:** After an Elf fighter resolves movement without taking damage, they gain `+1 Evasion` equivalent (treat incoming attack rolls as having `-1 AttackDice`) until hit.
* **Skirmish Ability – Veil Step (Action):** Teleport up to 3 squares to a visible empty tile. The next attack this turn gains `+1 AttackRange`.
* **Line Ability – Moonlance Flurry (Action):** Make a single sweeping attack with `+1 AttackDice`.
* **Elite Ability – Starfall Invocation (Action, once per battle):** Select a target within 8 squares. Roll an attack with `+1 AttackDice`; on a hit, deal normal damage and blind the target (they roll `-1 AttackDice`) until the end of their next activation.

## LizardFolk Faction

* **Passive – Cold-blooded Focus:** Lizardfolk ignore the first `-1` penalty applied to their `Strength` or `AttackDice` each round.
* **Skirmish Ability – Tail Sweep (Action, cooldown 2 rounds):** Attack all adjacent enemies with `AttackDice - 1` dice. Any enemy hit suffers `-1 Movement` until end of round.
* **Line Ability – Amphibious Rush (Action):** When entering or starting in difficult terrain, gain `+2 Movement` and `+1 AttackDice` for the turn.
* **Elite Ability – Primeval Regeneration (Free, cooldown 3 rounds):** At the start of this fighter’s activation, restore `3` Health (capped by max) and cleanse one debuff (e.g. slowed, weakened).

## Undead Faction

* **Passive – Necrotic Resilience:** When reduced to 5 Health, Undead fighters gain `+1 Defence` and `+1 AttackDice` until they heal above 5 Health and ignore morale-based effects.
* **Skirmish Ability – Grave Grasp (Action, cooldown 2 rounds):** Attack a target within 3 squares. On a hit, root the enemy (Movement becomes 0) until end of its next activation.
* **Line Ability – Soul Harvest (Action, cooldown 2 rounds):** Make an attack with `+1 AttackDice`. Heal `+1 Health` for each hit scored on this attack (cannot exceed max).
* **Elite Ability – Deathless Advance (Passive, cooldown 10 rounds):** When reduced to 0 Health, immediately rise again with `5 Health`. This passive cannot trigger again until 10 rounds have passed.

## Gnoll Faction

* **Passive – Pack Instincts:** Gnoll fighters gain `+1 AttackDice` when an ally has already attacked the same target this round.
* **Skirmish Ability – Harrier Dash (Action, cooldown 2 rounds):** Move up to `Movement` without provoking reactions, then make an attack that causes `-1 Defence` on hit (each fighter can only be affected once).
* **Line Ability – Howl of the Alpha (Action):** Emit a rallying howl. All allies within 4 squares gain `+1 Movement` and may ignore disengage penalties until end of round.
* **Elite Ability – Rend and Tear (Action):** Make an attack; for each hit scored, inflict a stacking bleed that deals 1 damage at the start of the target’s activation (max 3 stacks).

## IronLegion Faction

* **Passive – Iron Resolve:** When an Iron Legion fighter is adjacent to two or more enemies, they gain `+1 Defence`.
* **Skirmish Ability – Suppressing Fire (Action):** Attack at range with `-1 AttackDamage`; on hit, the target suffers `-2 Movement` and cannot take reactions until its next activation.
* **Line Ability – Officer’s Command (Free, once per round):** After rolling attack dice, may transfer one unused hit to an adjacent ally’s pending attack before results are resolved.
* **Elite Ability – Artillery Strike (Action, spend 2 actions, cooldown 2 rounds):** Spend 2 actions to call artillery on a visible tile. Immediately roll an attack with `+2 AttackDamage` against all units within 2 squares (allies take half damage).

## The Inflicted Faction

* **Passive – Unstable Mutation:** When an Inflicted fighter is targeted by an attack, roll a die: on 4+, gain either `+1 Defence` or `+1 AttackDamage` (player choice) until end of round.
* **Skirmish Ability – Viral Lash (Action, cooldown 2 rounds):** Make an attack; on hit, apply a contagion that causes the target to suffer `-1 Defence` (once per fighter) and spread the debuff to adjacent enemies if they take damage this round.
* **Line Ability – Mutagenic Surge (Free, cooldown 2 rounds):** Before rolling attack dice, choose to gain `+2 AttackDice` and suffer `-1 Defence` until next activation.
* **Elite Ability – Aberrant Bloom (Action, once per battle):** Transform adjacent empty squares into hazardous terrain until end of battle. Enemies entering or starting in the hazard take `AttackDamage` and suffer `-1 Movement` (stacking once).

## ToadFolk Faction

* **Passive – Mire Masters:** ToadFolk ignore penalties from water or swamp tiles and leave clinging mire behind. Squares they exit become difficult terrain for enemies until the end of the round.
* **Skirmish Ability – Tongue Snare (Action, cooldown 2 rounds):** Pull a target within 4 squares 1 square closer and reduce its `AttackDice` by 1 until end of round.
* **Line Ability – Bubble Ward (Action, cooldown 2 rounds):** Envelope an ally within 3 squares in a buoyant ward. Until the start of your next activation, that ally gains `+1 Defence` and the next ranged attack against them loses one success before damage is applied.
* **Elite Ability – Raincaller Deluge (Action, cooldown 2 rounds):** Create a rainstorm template (radius 2) for one round. Enemies inside suffer `-1 AttackDice`, allies gain `+1 Defence` and amphibious movement bonuses.

## Ravpack Faction

* **Passive – Scavenged Momentum:** After a Ravpack fighter defeats an enemy or picks up loot, they gain `+2 Movement` on their next activation.
* **Skirmish Ability – Scrapper Feint (Action):** Make a melee attack; on any miss, the fighter may immediately move 2 squares without provoking reactions.
* **Line Ability – Jury-rigged Explosive (Action):** Plant a proximity charge on an adjacent tile. When an enemy enters, roll an attack with `AttackDice` dice dealing `AttackDamage + CriticalBonusDamage` on critical hits.
* **Elite Ability – Overclock Harness (Free, cooldown 2 rounds):** After completing this fighter’s activation, engage the harness to gain `+1 AttackDice` and `+1 Movement` until the start of their next activation; suffer 1 damage if they attacked before triggering it.

## Implementation Notes

* Passives can be applied automatically when instantiating fighters from a faction (e.g. via `UFighterDataLibrary::GetFightersForFaction` hooks or status-effect components).
* Action costs map cleanly to the existing activation flow—use the ability system to flag actions as consumed, reactions as once-per-round, and free/limited uses as boolean flags in fighter state.
* Cooldowns and “once per battle” limits should be tracked per fighter instance, ideally piggybacking on the turn manager’s round counter.
* Movement/attack modifiers translate directly into temporary stat adjustments on `FFighterStats` for the duration described.
* **UI Triggering:** The existing player character already exposes three `Ability` input bindings, so no new HUD button is strictly required. To surface abilities visually, extend `UBattleHUDWidget` with optional ability slots that mirror those bindings (icons + tooltips) and enable/disable them based on the selected fighter's available actives. Battles that prefer a pure keyboard flow can rely solely on the hotkeys; otherwise wire the new widget buttons to call the same ability handlers the bindings use.
* **Optional SFX/VFX Hooks:** Treat each ability definition as a data asset entry that includes soft references to Niagara systems, animations, and sound cues. When the ability state machine fires (e.g. `UAbilityComponent::ExecuteAbility`), spawn the referenced FX at the fighter’s socket and play the cue on the owning audio component. Guard the spawn behind null checks so that designers can opt out of providing bespoke FX.

### Binding Abilities to Inputs

1. Extend the fighter data table (or a companion ability map) with `AbilitySlotA/B/C` identifiers that correspond to the three `Ability` bindings already defined in the project settings.
2. When a fighter becomes the active unit, call `UInputAbilityRouter::RegisterAbility(Slot, AbilityHandle)` for each populated slot so the input binding routes to the ability system component.
3. In `UInputAbilityRouter::OnAbilityPressed`, invoke `UAbilityComponent::TryBeginAbility(AbilityHandle)`. This reuses the same execution path triggered by HUD buttons and guarantees cooldown/action costs stay in sync.
4. On end turn or fighter swap, unregister the slots to prevent stale bindings.

### Surfacing Abilities in UI

* **Fighter Entry Widget:** Add columns/rows in the fighter encyclopedia/roster entry for "Faction Passive" and "Unit Skills." Populate these with localized text pulled from the faction ability data asset so players can review them while preparing a squad. Include small FX/audio preview buttons if available (play the associated cue and spawn a miniature Niagara effect in a preview canvas).
* **Battle HUD:** Mirror the selected fighter’s ability slots in the HUD. Display the faction passive as a passive badge (with tooltip) and show active abilities in the three input slots. Disable or grey out buttons for abilities on cooldown or lacking action economy. Hovering the HUD icons should recap costs, ranges, and FX cues so players know what feedback to expect.
* **Clear Usage Flow:** When a player presses an ability key or clicks the HUD button, highlight valid target tiles, prime the reticle, and queue any startup SFX/VFX (e.g. glow on the fighter). Once the ability resolves, flash the passive/active badges briefly with their associated FX to reinforce the ability source.
