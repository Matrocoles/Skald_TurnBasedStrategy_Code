# Skald

Skald is an Unreal Engine 5 prototype exploring a hybrid of grand-strategy and tactical grid combat. The C++ module in this repository drives the core gameplay loops—world map territorial control, lobby and turn sequencing, grid-based battles, and save/load helpers—while Blueprint assets supply the presentation.

## Project highlights
- **World map control** – `AWorldMap` and `ATerritory` keep track of every province, its owner, adjacency, and allow units to move or auto-distribute across friendly land. Spawn data can be sourced from data tables to quickly create new campaign layouts.
- **Turn management** – `ATurnManager` coordinates reinforcement, attack, and fortify phases, dispatches world-state updates to UI, and triggers grid battles when armies clash.
- **Tactical battles** – `UGridBattleManager` resolves dice-driven skirmishes, handles fighter activation and round flow, and broadcasts results so the overworld can resolve casualties.
- **Lobby and save systems** – Menu widgets manage hosting/joining and surface save slots that call into `USkaldSaveGameLibrary` helpers for persistence.

## Requirements
- Unreal Engine 5 (project authored with UE 5.3+).
- A C++ compiler toolchain supported by UE5 (Visual Studio 2022 on Windows, Xcode/Clang on macOS, or clang/gcc on Linux).
- Engine plugins used by the project (OnlineSubsystem) must be available in the selected engine installation.

## Repository layout
- `Source/Skald` – Gameplay code for world map, turn manager, grid battle, lobby UI, save/load utilities, and supporting data structures.
- `Content/C++_BPs` – Blueprint assets that pair with the C++ classes (maps, widgets, data tables, etc.).
- `docs/` – Design notes and follow-up task breakdowns for world-map flow and tactical battles.
- `Config/` – Default maps, game modes, and renderer settings used by the project.

## Getting started
1. **Clone or download** this repository into your Unreal projects directory (e.g. `~/Unreal Projects/Skald`).
2. **Open `Skald.uproject`** with Unreal Engine 5. The editor will prompt to build the C++ module—allow the build to finish before the project loads.
3. **(Optional) Generate IDE project files** by right-clicking the `.uproject` and selecting *Generate Visual Studio project files* (or run `GenerateProjectFiles.sh` on macOS/Linux). Build the `SkaldEditor` target for your platform.
4. **Launch the editor or packaged build**. The default startup map is the lobby; hosting a game from the lobby menu will travel to the world map when ready.

## Running the game
- **Singleplayer** – Use the lobby's start widget to choose a faction, optionally spawn AI players, and lock in. When the lobby transitions, `ATurnManager` begins the reinforcement phase and manages turn order for all registered controllers.
- **Multiplayer** – Online sessions are implemented at the Blueprint level using the included lobby widgets and the `OnlineSubsystem` plugin. Ensure the plugin is active in your engine build.
- **Battles** – When opposing armies engage, `ATurnManager::TriggerGridBattle` loads a battle map and hands combat off to `UGridBattleManager`. The manager emits `OnBattleEnded`; once it fires the turn manager applies casualties and returns players to the overworld.

## Working with the world map code
The world map C++ classes are intended to be placed via Blueprints and map actors. To wire everything up:
1. **World map actor** – Create a Blueprint subclass of `AWorldMap` and place it in your map. It tracks all `ATerritory` instances, handles selection, and can spawn territories from a data table of `FTerritorySpawnData` rows.
2. **Territories** – Derive Blueprint classes from `ATerritory` for each province. Assign meshes/materials with a `Color` parameter. Each territory registers with the world map on begin play and listens for selection events.
3. **Player character & controller** – Use `ASkald_PlayerCharacter`/`ASkaldPlayerController` so mouse input drives territory selection and army commands. Ensure the project input settings include axis mappings for `MoveForward`, `MoveRight`, `MoveUp` and action mappings for `Select`, `Ability1`, `Ability2`, `Ability3`. Enable Click and Mouse Over events on the player controller so territory meshes fire input callbacks.
4. **Data-driven maps** – Populate a `UDataTable` using `FTerritorySpawnData` to generate territories automatically. Territory adjacency can be defined in the table or inferred via distance thresholds.

## Save game UI expectations
The runtime expects a UMG widget named `Skald_SaveGameWidget` under `Content/C++_BPs` that exposes a text box for the save slot name and a button calling `USkaldSaveGameLibrary::SaveSkaldGame`. A JSON mock-up is available at `Content/C++_BPs/Skald_SaveGameWidget.json`. The provided `USaveGameWidget` binds optional buttons for three slots and a return-to-menu action; hook these to your Blueprint as desired.

## Additional documentation
- `docs/game-start-tasks.md` – Follow-up tasks for streamlining singleplayer startup and multiplayer lobbies.
- `docs/grid-battle-followup-tasks.md` – Planned improvements for the tactical combat loop.

Feel free to expand these systems or layer new UI/UX on top of the provided C++ scaffolding.
