# Travel Snapshot Troubleshooting

When returning from a streamed battle map the overworld must be rebuilt from
the cached territory snapshot (`FSkaldTravelState::CachedTerritories`) before
battle results are applied. The Unreal Editor log shared in October 2025 shows
that the world loads (`UEngine::LoadMap /Game/Blueprints/Maps/OverviewMap`),
controllers register, and `ServerInitPlayerState` fires, but no
`USkaldGameInstance::RestoreWorldFromSnapshot` log line ever appears. Instead the game immediately
logs repeated `ServerSelectTerritory -1` calls, indicating that the overworld
was never hydrated before the turn system resumed. The absence of any
`USkaldGameInstance::RestoreWorldFromSnapshot` logging implies that `ASkaldGameMode::TryInitializeWorldAndStart`
never entered its restoration branch even though
`USkaldGameInstance::GetPendingTravelSnapshot` still held the cached data.

## Root cause

`TryInitializeWorldAndStart` only attempted to rebuild the overworld when the
`GameInstance`'s `bResumeTurns` flag was true. During some travel sequences the
turn manager clears `bResumeTurns` before the map finishes loading, which left
us with a valid pending snapshot but no signal to invoke
`USkaldGameInstance::RestoreWorldFromSnapshot`. As a result, the overworld never rehydrated and the
selected territory remained `-1`.

## Resolution

Treat the presence of a pending snapshot as a restoration trigger in addition
to the `bResumeTurns` flag. The updated logic now:

* Detects `GetPendingTravelSnapshot().Num() > 0` and keeps the cached turn
  indices intact while restoration is pending.
* Attempts `USkaldGameInstance::RestoreWorldFromSnapshot()` whenever either `bResumeTurns` is true
  **or** a pending snapshot exists, retrying on a timer until both conditions
  clear or the restore succeeds.

With this change the overworld rehydrates reliably even if the resume flag is
cleared before `TryInitializeWorldAndStart` runs.
