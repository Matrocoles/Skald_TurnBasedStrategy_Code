# Combat Resolution Overhaul Tasks

This backlog breaks the dice resolution revamp into startable tasks. Each task can be tracked individually while keeping the per-die attack flow intact.

## Minimal implementation (fast wins)

- **Create pooled world-space floater widget**
  - Build `W_FloatingText` (world-space, 256×128) with Retainer + Invalidation boxes.
  - Implement pooling utility (e.g., subsystem with `TArray<UUserWidget*> InactivePool`) to reuse widgets and avoid GC spikes.
  - Animate floaters with an ease-out arc (24–36px up) and fade (0.6–0.8s) before returning to pool.
  - Clamp projected screen position with `UGameplayStatics::ProjectWorldToScreen`; hide when obstructed by HUD.

- **Replace per-die popups with consolidated panel**
  - Author `W_DiceResolutionPanel` containing hit/miss tallies, per-die reveal row, and resolve bar placeholder.
  - Buffer the dice outcomes for a single attack (per attacker swing) and feed them to the panel via `FDiceRollResult`.
  - Stagger reveals (80–120 ms) with timers or UMG animation; add 0.08–0.12s anticipation pause post-roll.
  - Emit `OnResolutionComplete` so downstream systems (floaters, VFX) trigger once per attack.

- **Hook impact cues**
  - On hit: trigger micro camera shake (0.1s, 3–6px amplitude), defender flash (material MID scalar 0→2→0), and Niagara spark burst.
  - On miss: play whoosh SFX and slide a narrow angled “MISS” tag past defender (10–16px drift then fade).
  - Ensure success uses teal/blue-green palette, misses neutral gray, and shapes differ (✔ vs ×) for accessibility.

- **Wire baseline combat audio cues**
  - Randomize dice-roll sound effects by selecting from a designer-filled `TArray<USoundBase*> DiceRollVariants` as each die reveals.
  - Trigger three attack-start cues (`AttackPrepare`, `AttackResolve`, `AttackCrit`) right as the attacker enters resolution so animation, UI, and audio stay aligned.
  - Loop a movement bed while fighters traverse the grid, updating attenuation from the mover each tick and stopping cleanly when locomotion finishes.
  - Feed all new cues through the existing master audio mix so the global settings slider scales them alongside current sounds.

## Medium polish (premium feel)

- **Resolve bar feedback**
  - Drive a horizontal bar that fills with successes and dims misses over ~0.6s in `W_DiceResolutionPanel`.
  - Synchronize the bar animation with the staggered per-die reveal cues.

- **Die outcome trails**
  - Spawn quick trails: successes streak toward defender (UI line/Niagara), misses drop with gravity (1000–1400 uu/s) and fade.
  - Respect low-motion settings by disabling trails/camera shake when toggled.

- **Floater batching & throttling**
  - Aggregate floaters for attacks resolving within 300 ms, showing a single "-XX" with short counting animation.
  - Cap concurrent floaters on screen (~6) and queue extras for later display.

- **Layer audio sweeteners and variations**
  - Add lightweight success pings and miss whooshes that sync with the staggered die reveals and resolve bar progress.
  - Blend movement loop texture variants (stone, dirt, metal) based on traversed tile material, crossfading when the surface changes.
  - Fire the hit/miss/crit outcome cues from the same handler that batches floaters so audio aligns with aggregated damage feedback.

## Deluxe stretch goals

- **Cinematic crit moments**
  - Slow time to 0.2× for ~250 ms on high-stakes crits, then restore to normal.
  - Give crit floaters unique color/scale (gold, 1.15×) and optional extra VFX per faction.

- **Per-faction VFX variants**
  - Swap Niagara systems and defender flash materials based on attacker faction (e.g., sparks vs dust streaks).

- **Combat log panel**
  - Append concise log entries ("Lancer hit (2) • Guard blocked (1) • Bleed applied") tied to `FDiceRollResult` data for reference.

- **Crit stingers and ambient swells**
  - Layer a faction-aware crit stinger that ducks other channels during the cinematic slowdown.
  - Fade in a short tonal swell as the resolve bar nears completion, releasing it when the bar resolves for added drama.

## Accessibility

- **Low-motion toggle**
  - Add settings flag to disable trails, camera shake, and intense flashes, substituting crossfade highlights.
  - Maintain success/failure differentiation through icons and text when motion is reduced.

- **Audio intensity toggle alignment**
  - Pair the low-motion option with an audio-intensity reduction that mutes sweeteners while leaving core informational cues (dice roll, hit/miss/crit) audible.
  - Expose the cue arrays in data assets so designers can add or swap sounds without additional code churn.

Each bullet above represents a self-contained task you can start; together they satisfy the full plan while respecting the one-attack-per-die flow.
