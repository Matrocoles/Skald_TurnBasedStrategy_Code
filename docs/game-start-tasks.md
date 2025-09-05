# Game Start Improvement Tasks

This document captures follow-up tasks for streamlining singleplayer startup and introducing a host/join lobby for multiplayer.

## Singleplayer start

- **Skip in-game faction UI for solo games**  
  Move player configuration (name, faction, AI count) into the lobby flow so that `UStartGameWidget::StartGame` sets up `USkaldGameInstance` before level travel. This avoids showing `ChoosePlayerWidget` after loading the world【F:Source/Skald/StartGameWidget.cpp†L47-L55】.
- **Auto-lock human player and spawn AI**  
  Update `ASkaldGameMode::TryInitializeWorldAndStart` to auto-lock the local player and call `PopulateAIPlayers` immediately in singleplayer mode. Ensure `PopulateAIPlayers` respects `GI->AIPlayersToSpawn` to spawn and lock AI players【F:Source/Skald/Skald_GameMode.cpp†L469-L546】.
- **Consolidate initialization logic**  
  `RegisterPlayer` currently queues pending controllers and retries via timers. Simplify this for singleplayer by directly registering the lone player and skipping timer-based retries when running without network peers.
- **Centralize default values**  
  Provide sensible defaults in `USkaldGameInstance` so a singleplayer game can launch without manual input when desired (e.g., default faction, display name, one AI opponent).

## Multiplayer host/join lobby

- **Add host/join menu**  
  Extend `UStartGameWidget` with two flows: Host (existing) and Join (enter IP). For host, continue using `UGameplayStatics::OpenLevel` with `listen`; for join, use `ClientTravel` to the provided address【F:Source/Skald/StartGameWidget.cpp†L59-L70】.
- **Lobby state management**  
  Introduce a simple session manager (within `USkaldGameInstance`) that stores the selected IP and whether the instance is acting as host or client.
- **Connection feedback**  
  Implement UI feedback for connection success/failure and a way to return to the main menu if joining fails.
- **Player configuration once connected**  
  Reuse the existing `ChoosePlayerWidget` after connection so each client can lock in name and faction. Ensure the lobby waits until all connected players lock in before `TryInitializeWorldAndStart` proceeds.

These tasks aim to clarify responsibilities between lobby/menu code and in-game startup so the project can evolve toward a smoother user experience.
