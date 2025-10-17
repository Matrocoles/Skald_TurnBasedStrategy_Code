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

## Deluxe stretch goals

- **Cinematic crit moments**
  - Slow time to 0.2× for ~250 ms on high-stakes crits, then restore to normal.
  - Give crit floaters unique color/scale (gold, 1.15×) and optional extra VFX per faction.

- **Per-faction VFX variants**
  - Swap Niagara systems and defender flash materials based on attacker faction (e.g., sparks vs dust streaks).

- **Combat log panel**
  - Append concise log entries ("Lancer hit (2) • Guard blocked (1) • Bleed applied") tied to `FDiceRollResult` data for reference.

## Accessibility

- **Low-motion toggle**
  - Add settings flag to disable trails, camera shake, and intense flashes, substituting crossfade highlights.
  - Maintain success/failure differentiation through icons and text when motion is reduced.

Each bullet above represents a self-contained task you can start; together they satisfy the full plan while respecting the one-attack-per-die flow.
