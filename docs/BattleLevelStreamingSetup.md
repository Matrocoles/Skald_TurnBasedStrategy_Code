# Battle Level Streaming Setup

This repository now supports resolving tactical encounters by streaming a battle sub-level while the overworld remains loaded. The code paths fall back to the previous full-travel workflow if streaming fails, so no existing behaviour is lost. Follow the steps below to configure the editor for the new flow.

## 1. Prepare a streamable battle map

1. Create or open the level that should host your grid battle (for example `BattleMap`).
2. Convert it into a level asset that can be streamed:
   * In the persistent overworld map (`Skald_OverTop`), open the Levels window (`Window > Levels`).
   * Use **Add Existing...** to add your battle level as a streaming sub-level.
   * In the sub-level details, set **Streaming Method** to **Blueprint** so the runtime loader can control visibility.
3. If the battle scene needs to sit far from the overworld origin, set the sub-level transform (Location / Rotation) appropriately so the grid arena does not intersect the overworld terrain.

## 2. Register the map for random selection

1. Open `BP_TurnManager` (or the blueprint that sets up `BattleMaps`).
2. Add your battle level asset to the `Battle Maps` array. The C++ loader uses these soft object references when it picks the next arena to stream. If the array is empty, it falls back to `/Game/Blueprints/Maps/BattleMap`.

## 3. Ensure the battle mode actors live in the streamed level

1. Place the tactical scene actors (deployment volumes, grid overlay, etc.) inside the battle sub-level rather than the persistent overworld.
2. Make sure the battle level’s World Settings still point at `Skald_BattleGameMode` and `Skald_GameState`. These are now spawned inside the streamed world instead of being loaded via a full level change.

## 4. Testing the streaming workflow

1. Launch PIE in the overworld map.
2. Trigger a territory attack. The new `USkaldBattleLevelManager` streams the battle level, shows the loading overlay, and exposes the grid without unloading the overworld.
3. When the fight ends, the manager unloads the sub-level and resumes overworld play immediately.

