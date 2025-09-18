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
