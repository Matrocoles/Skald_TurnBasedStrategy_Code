# Grid Battle C++ Task Breakdown

## Victory detection and EndBattle broadcast
- Update `UGridBattleManager::AdvanceTurn` (or whichever server-side function removes dead fighters) to detect when only one faction remains in `InitiativeOrder` and call `EndBattle()` immediately instead of looping forever. Capture whether any defenders or attackers are still registered before the call so the survivor tallies remain correct.
- Make sure this path only runs once per battle and that `OnBattleEnded` fires on all clients so downstream listeners (turn manager, controllers, HUD) receive the notification.

## Remove the deprecated `StartBattle` simulator
- Delete `UGridBattleManager::StartBattle` and any helper code that exists solely for that method so only the pawn-driven tactical mode remains. Verify nothing in the project references `StartBattle` after removal.
- If any tests or blueprint exposures depend on the function, replace them with coverage around `InitBattle` + pawn-driven execution instead of leaving dead code paths behind.

## Drive fighter spawning from `PendingBattle`
- In `ASkald_BattleGameMode` and the fighter selection flow (`ASkaldPlayerController::HandleFighterSelectionLockedIn`), replace the "first two players" assumption with `USkaldGameInstance::PendingBattle`. Use the attacker/defender player IDs and army counts to decide which controllers must lock in, who is auto-filled by AI, and which roster spawns on each edge.
- Ensure the budgets respect `FS_BattlePayload::ArmyCountSent` for the attacker and `PendingBattle.DefenderArmySize` (or equivalent) for the defending side so cost validation in the selection UI matches the world-map armies that travelled.
- When spawning pawns, mark the correct `bIsAttacker` flag per side instead of mirroring the attacking list for defenders.

## Auto-resolve world-map results after `OnBattleEnded`
- When `UGridBattleManager::OnBattleEnded` fires, immediately call `ATurnManager::ResolveGridBattleResult` (or an equivalent authority-only entry point) so the overworld state updates even if no local controller triggers it manually.
- Guard against duplicate calls by clearing `USkaldGameInstance::GridBattleManager` (or marking a flag) after resolution so subsequent `OnBattleEnded` broadcasts do not reapply the same casualties.

## Sanitize battle-map travel targets
- Replace the hard-coded `"WorldMap"` string inside `ATurnManager::HandleGridBattleEnded` with a value carried in `FS_BattlePayload` (e.g., the level we departed from) or the `ATurnManager::PendingBattle` data so that returning from combat works even if level names change.
- Store the original map name when `TriggerGridBattle` initiates travel (perhaps by querying `GetWorld()->GetMapName()` before loading the battle map) and reuse it when travelling back.

## Distinct AI army generation
- Rework the AI setup in `ASkald_BattleGameMode::BeginPlay` to pull attacker and defender rosters separately. Use `PendingBattle` metadata (including defender budget) so the defending AI reflects the actual territory owner instead of mirroring the attacker list.
- Ensure the spawned pawns register with `UGridBattleManager` on the correct side and that initiative rolls only happen after both sides are registered.

## Battle conclusion UI hooks
- Extend the HUD/controller battle widgets to listen for `UGridBattleManager::OnBattleEnded` and present a victory/defeat banner (or modal) that differentiates attacker vs. defender outcomes.
- Tear down or hide battle-specific UI when the delegate fires and re-enable world-map widgets after the travel back to the overworld completes, preventing stuck buttons when the level transitions.

## Existing battle-map functionality to leverage
- `UGridOverlayComponent::HighlightMovement` and `HighlightAttack` already compute legal movement and attack targets from a fighter's stats, so the upcoming UI workflow can reuse them without new pathfinding code.
- `AFighterPawn::MoveToCell` handles grid occupancy, world-space alignment, and respects the fighter's movement stat when walking to a highlighted tile. `AFighterPawn::PerformAttack` likewise performs the die rolls and damage application against a target pawn.
- `ASkaldPlayerController::BeginMoveMode` and `BeginAttackMode` are wired to the battle HUD buttons and call into the overlay highlight helpers today. The activation flow can keep these entry points while tightening when they are available.

## Round tracking and initiative messaging
- Rework `UGridBattleManager` so rounds advance when both sides have activated every surviving fighter instead of chaining `AdvanceTurn`. Store the `CurrentRound`, expose it through an accessor, and broadcast a new `OnRoundStarted` delegate (or similar) that includes the side that won initiative.
- Roll initiative for attacker versus defender at the start of each round, keep the winning side in a replicated/accessible field, and use it to seed which team is allowed to activate first. Emit an announcement string for the HUD when the roll occurs.
- Make `ASkaldPlayerController` (or the battle HUD) listen for the new round delegate and update on-screen text with the round number and initiative winner.

## Fighter activation and action economy
- Extend `AFighterPawn` with replicated fields that track whether it has activated this round and how many actions remain (exactly two per activation). Update `BeginActivation` to set those values, add a `ResetActivationState` helper, and expose a getter the controller can query before sending commands.
- Adjust `MoveToCell` and `PerformAttack` so each call consumes one action, rejects attempts when the pawn is out of actions, and stops invoking `GridBattleManager::AdvanceTurn` automatically. The explicit End Turn button will now advance play instead of moves doing it implicitly.
- Ensure deactivation (e.g., a fighter dies mid-turn) clears grid occupancy, marks the pawn as no longer active, and notifies the manager so the owning side can pick a replacement.

## Side turn sequencing and round reset
- Replace the per-pawn `AdvanceTurn` loop in `UGridBattleManager` with side-based bookkeeping: maintain lists of living attacker and defender pawns plus a set of which ones have activated this round. Provide helpers such as `CanActivateFighter`, `ActivateFighter`, and `FinishActivation` so the controller can drive the flow.
- When a side ends its activation, swap to the opposing side only if they still have unactivated fighters. If the opponent is empty, keep the turn with the current side until they exhaust their roster. As soon as both sides are marked done, increment `CurrentRound`, clear the per-fighter flags, reset their action points, roll initiative again, and broadcast the round start event.
- Update any remaining references to `AdvanceTurn` (player controller, pawns, or UI) to call the new activation/finish helpers so the sequencing stays consistent across server and clients.

## Battle HUD commands and indicators
- Add BindWidget properties for an Activate button, an End Turn button, and text blocks that show the current round/initiative string plus the selected fighter's name in `UBattleHUDWidget`. Expose delegates for the new buttons so the player controller can subscribe in the same way it already listens to Move/Attack.
- Update `BindToFighter` to refresh the fighter name and stat panel whenever the player clicks a different pawn, and clear the panel when selection goes away. The End Turn button should remain hidden or disabled until a fighter is activated.
- Implement helper methods on the widget for setting the round/initiative text, toggling the End Turn button's visibility, and clearing any move/attack highlight state when right-click cancels a command.

## Player-controller activation flow and input handling
- Store the currently selected fighter and the fighter that has been activated this turn inside `ASkaldPlayerController`. Modify `HandleGridClick` so left-click selects friendly pawns when no command mode is active, clears selection on empty tiles (unless an activation is locked in), and only issues move/attack orders to the active fighter.
- Bind the HUD's new Activate and End Turn delegates. When Activate fires, validate ownership, call the manager's `ActivateFighter`, and surface a warning such as "Fighter Already Activated." through `NotifyActionError` if the pawn is no longer eligible. End Turn should inform the manager that the side is ready to rotate or start the next round.
- Add a right-click binding that cancels the current Move/Attack mode, clears grid highlights, and returns the controller to selection mode without consuming actions. This should respect the "selection locked while activated" rule from the design.
