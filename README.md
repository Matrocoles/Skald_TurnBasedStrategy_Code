# Skald

Developed with Unreal Engine 5

## Using the world map code

The gameplay classes included in this repository are intended to be used
from Blueprints and level actors. To hook everything up:

1. **World Map** – Create a Blueprint subclass of `AWorldMap` and place it in
   your level. This actor keeps track of all `ATerritory` instances and the
   currently selected territory.

2. **Territories** – Create Blueprint subclasses of `ATerritory` for each
   province on your map. Assign a static mesh and a material with a
   `Color` parameter. When a territory is placed in the level it will
   automatically register itself with the `AWorldMap` and bind its selection
   events.

3. **Player Character** – Ensure the level uses `ASkald_PlayerCharacter`.
   The character looks for a `AWorldMap` actor at startup and will call the
   `Select()` function on territories under the mouse cursor when the
   `Select` action is triggered.

4. **Input** – Add axis mappings for `MoveForward`, `MoveRight`, and `MoveUp` and
   action mappings for `Select`, `Ability1`, `Ability2`, and `Ability3` in the
   project settings. Also enable *Click Events* and *Mouse Over Events* on the
   player controller so territory meshes can generate the appropriate input
   callbacks.

With this setup the C++ logic handles selection, deselection, and movement
between adjacent territories without requiring additional Blueprint wiring.

## Save Game UI

The Save Game menu widget (`Skald_SaveGameWidget`) is not included in the repository. Create a UMG Widget Blueprint with this name under `Content/C++_BPs` and implement a text box for the save slot name and a button that calls `USkaldSaveGameLibrary::SaveSkaldGame`. A JSON description of the expected structure is provided in `Content/C++_BPs/Skald_SaveGameWidget.json`.
