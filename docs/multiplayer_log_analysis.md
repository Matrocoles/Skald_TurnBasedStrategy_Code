# Multiplayer Launch Investigation

## Summary
- Original behaviour: the listen server tore down and recreated its `GameNetDriver` during lobby → overview travel because `ALobbyGameMode::TryLaunchMatch` issued a local `UGameplayStatics::OpenLevel` call with `listen` options. That rebuild disconnected clients before the host finished loading. 【F:docs/multiplayer_log_analysis.md†L12-L19】
- Fix: `ALobbyGameMode::TryLaunchMatch` now calls `ServerTravel` when running in multiplayer so the listen server sends the travel order to connected clients instead of reopening the level locally. Standalone sessions continue to use `OpenLevel`. 【F:Source/Skald/LobbyGameMode.cpp†L362-L379】
- The original runtime log shows the lobby map closing, a new `IpNetDriver` opening on the same port, and gameplay starting only for the host—symptoms consistent with the local open behaviour that previously left clients behind. 【F:docs/multiplayer_log_analysis.md†L35-L47】
- After arriving in the OverviewMap the server reported missing gameplay state such as the `TurnManager`, confirming that remote clients never joined the map under the old flow. 【F:docs/multiplayer_log_analysis.md†L49-L56】

## Detailed Findings

### Lobby travel flow
Historically the lobby game mode always executed:

```
UGameplayStatics::OpenLevel(this, FName(TEXT("/Game/Blueprints/Maps/OverviewMap")), true, TEXT("listen"));
```

When run on a listen server that call rebuilt the world locally and severed connected clients. 【F:docs/multiplayer_log_analysis.md†L38-L64】

The lobby game mode now differentiates between standalone and multiplayer launches:

```
if (World->IsNetMode(NM_Standalone))
{
    UGameplayStatics::OpenLevel(...);
}
else
{
    World->ServerTravel(TEXT("/Game/Blueprints/Maps/OverviewMap?listen"));
}
```

When running as a listen server the `ServerTravel` path keeps the existing network driver alive and propagates the travel to all clients. 【F:Source/Skald/LobbyGameMode.cpp†L368-L378】

### Observed log sequence
The supplied log includes the following sequence while the host leaves the lobby:

```
LogWorld: BeginTearingDown for /Game/Blueprints/Maps/UEDPIE_0_Skald_Lobby
LogNet: World NetDriver shutdown IpNetDriver_11 [GameNetDriver]
...
LogNet: Name:GameNetDriver Def:GameNetDriver IpNetDriver_12 IpNetDriver listening on port 7777
LogWorld: Bringing World /Game/Blueprints/Maps/UEDPIE_0_OverviewMap.OverviewMap up for play
LogSkald: HandleWorldBeginPlay: World=OverviewMap NetMode=2
```

This pattern indicates the listen server destroyed its net driver and spun up a new one while clients were still connected, a hallmark of using `OpenLevel` instead of `ServerTravel`. 【F:docs/multiplayer_log_analysis.md†L14-L31】

### Post-travel warnings
Once the OverviewMap loads, the host immediately logs warnings:

```
LogSkald: Warning: EndPhase called without a TurnManager. Attempting to reacquire.
```

Since remote clients never finish the travel, their replicated actors (including the turn manager) never arrive, leading to server-side gameplay components trying to reacquire missing state. 【F:docs/multiplayer_log_analysis.md†L32-L36】

## Recommendations
1. Verify in-editor and packaged builds that the new `ServerTravel` path moves all clients to the OverviewMap and that replicated actors such as the turn manager initialise correctly. 【F:Source/Skald/LobbyGameMode.cpp†L362-L379】
2. Monitor multiplayer playtests for any lingering warnings about missing replicated state after travel and capture updated logs if issues persist. 【F:docs/multiplayer_log_analysis.md†L66-L75】

## Nov 2025 play session review
The supplied client log shows a successful seamless travel from the lobby to `OverviewMap` (8.97 seconds) without tearing down the net driver, so the previous travel regression is not present. However two gameplay issues stand out:

1. `SkaldMainHUDWidget` logs `could not find GameMode` immediately after arrival on `OverviewMap`. Clients do not own a GameMode, so this warning means the HUD still queries `GetAuthGameMode()` during `NativeConstruct`. The call succeeds only on the host and is expected to be `nullptr` for remote players, so the warning is noisy and signals the widget is binding to server-only state. The HUD should instead depend on `ASkald_GameState` and other replicated components when running on clients.
2. Turn ownership replication thrashes during startup. The client observes `ActivePlayerId` sequence `-1 → 0 → 261 → 262`, then later returns to `261`, toggling `LocalTurnActive` from false to true and back again within a few seconds. That oscillation likely comes from `ASkald_TurnManager` initializing the turn order and then resetting it when the second player joins. The rapid handoff can briefly show the wrong "my turn" UI and risks double-initializing per-turn UI logic. We should audit the turn manager to avoid broadcasting intermediate `ActivePlayerId` values to clients (e.g., stage initialization server-side and replicate only the final starting player/phase once all controllers are registered).

**Next steps:**
- Adjust `USkaldMainHUDWidget::NativeConstruct` to skip GameMode lookups on clients and bind via GameState instead, eliminating client-side warnings and avoiding server-only references.
- Add a stabilization guard in `ASkald_TurnManager` so the replicated `ActivePlayerId` does not flip through placeholder values while players finish joining the overview map.
