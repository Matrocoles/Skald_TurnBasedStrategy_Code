# Physical Dice Roll Flow

This document summarizes how the Skald dice module drives the in-game physics rolls
and how the combat systems interact with it.

## Runtime configuration

* `USkaldGameInstance` exposes a `DefaultDiceRollConfig` soft reference and calls
  `InitialiseDiceManager()` during init (and whenever `ApplyDiceRollConfig()` is
  invoked). The helper resolves the asset and feeds it into the dice subsystem so
  every world automatically shares the same tuning.【F:Source/Skald/Skald_GameInstance.h†L198-L200】【F:Source/Skald/Skald_GameInstance.cpp†L903-L935】
* The `UDiceRollConfig` asset supplies timing limits, impulse ranges, arena bounds,
  settle thresholds, per-side tint colours, and the arena/dice classes that need to be
  spawned for a roll. Designers can also toggle sprite-only fallback behaviour or set a
  deterministic seed for repeatable testing.【F:Source/SkaldDice/Public/DiceRollConfig.h†L34-L112】

## What happens when a roll starts

* Gameplay calls `USkaldDiceManager::RollDice_D6()` for live rolls or
  `PlayScriptedRoll()` when the combat system already knows the face values. Both
  allocate an `FActiveRoll`, start timers that emit `OnDiceRollStarted`, tick interim
  updates, and eventually call `CompleteRoll()` to publish the results.【F:Source/SkaldDice/Public/SkaldDiceManager.h†L33-L98】【F:Source/SkaldDice/Private/SkaldDiceManager.cpp†L36-L139】
* While building the active roll the subsystem spawns a transient `ADiceRollArena`
  at the configured bounds, scatters `ASkaldDiceD6` actors above it, applies the player
  or enemy tint, and fires randomized linear/angular impulses. Scripted rolls inject
  desired face values so the dice settle on the authoritative outcome delivered by the
  battle manager.【F:Source/SkaldDice/Private/SkaldDiceManager.cpp†L256-L387】
* Each die tracks its owning roll ID and broadcasts `OnDiceSettled` once its linear and
  angular velocities stay below the settle thresholds for the required hold time. If a
  scripted target is provided the actor snaps to that face before reporting the value.【F:Source/SkaldDice/Public/SkaldDiceD6.h†L19-L44】【F:Source/SkaldDice/Private/SkaldDiceD6.cpp†L41-L222】
* When every die has settled, or when the presentation timer elapses, `CompleteRoll()`
  clamps the face values, emits `OnDiceRollCompleted`, and schedules the dice and arena
  for delayed cleanup to avoid abrupt popping.【F:Source/SkaldDice/Private/SkaldDiceManager.cpp†L176-L229】【F:Source/SkaldDice/Private/SkaldDiceManager.cpp†L390-L411】

## UI reaction

* `USkaldDiceOverlayWidget` grabs the subsystem on construct, loads an optional config
  override, subscribes to the roll delegates, and automatically toggles visibility,
  timer text, and panel layout based on whether the roll is an attack or initiative
  presentation.【F:Source/SkaldDice/Public/SkaldDiceOverlayWidget.h†L24-L101】【F:Source/SkaldDice/Private/SkaldDiceOverlayWidget.cpp†L18-L189】
* Player-controller defaults point to overlay/result widget subclasses. The controller
  lazily spawns them on the local client the first time dice presentation is needed and
  keeps an initiative summary widget available while the linger timer runs.【F:Source/Skald/Skald_PlayerController.h†L205-L221】【F:Source/Skald/Skald_PlayerController.cpp†L5832-L6005】

## Combat integration

* When an attack resolves, the player controller marshals the dice outcomes from the
  authoritative combat result, feeds them into `PlayScriptedRoll()`, and switches the
  overlay into attack mode. This guarantees the physics animation reflects the exact
  numbers that will be revealed before pre-attack, reveal, and resolution FX continue.【F:Source/Skald/Skald_PlayerController.cpp†L5865-L5906】
* Initiative comparisons call `TriggerInitiativeDicePresentation()`, which determines
  whether the local player owns the attacker or defender slots, arranges the numbers
  into player/enemy arrays, fires `PlayScriptedRoll()` in initiative mode, and updates
  the summary widget with the same values.【F:Source/Skald/Skald_PlayerController.cpp†L5909-L5977】
* Because the controller and the dice overlay rely on the subsystem delegates, any
  roll initiated elsewhere (e.g., multiplayer server confirmations) automatically
  reuses the same arena and UI presentation without additional Blueprint wiring.【F:Source/SkaldDice/Private/SkaldDiceManager.cpp†L64-L139】【F:Source/Skald/Skald_PlayerController.cpp†L5902-L5977】

## Editor checklist

1. Create or tweak a `UDiceRollConfig` asset to set physics parameters, colour tints,
   and the arena/dice classes you want to instantiate.【F:Source/SkaldDice/Public/DiceRollConfig.h†L34-L112】
2. Assign that asset to `DefaultDiceRollConfig` on the game-instance defaults so the
   subsystem loads it on boot.【F:Source/Skald/Skald_GameInstance.h†L198-L200】【F:Source/Skald/Skald_GameInstance.cpp†L903-L935】
3. Point your player-controller blueprint defaults at widget subclasses derived from
   `USkaldDiceOverlayWidget` and `USkaldDiceResultWidget` (only layout work needed—the
   base classes wire themselves to the subsystem).【F:Source/Skald/Skald_PlayerController.h†L205-L221】【F:Source/SkaldDice/Public/SkaldDiceOverlayWidget.h†L24-L101】
4. With those defaults set, initiative and attack flows automatically spawn the arena,
   roll physical dice, and drive the reveal widgets whenever combat submits results.【F:Source/Skald/Skald_PlayerController.cpp†L5865-L5977】
