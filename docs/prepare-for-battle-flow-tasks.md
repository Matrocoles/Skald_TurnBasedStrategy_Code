# Prepare-for-Battle Flow Tasks

- **Add regression coverage for late PlayerState registration**  
  Build an automation test that spawns an `ASkaldPlayerController` without an attached `ASkaldPlayerState`, ticks the world until `ASkaldGameMode::RegisterPlayer` retries, and verifies the controller ends up in `ATurnManager::GetControllers()`. This guards against future regressions where pending controllers are dropped before their PlayerState replicates.【F:Source/Skald/Skald_GameMode.cpp†L222-L259】【F:Source/Skald/Skald_TurnManager.cpp†L746-L770】

