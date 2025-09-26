# Grid Highlight Decal Setup

The grid overlay component can now spawn decals for highlighted tiles. This lets the highlight conform to uneven terrain instead of relying on a flat instanced mesh. Use the steps below to configure the feature inside the editor.

## 1. Author a decal material

1. Create a new material that uses the **Deferred Decal** material domain. Existing grid materials are almost certainly set to the *Surface* domain so they cannot be reused directly; copy their node graph into the new material if you want the same look.
2. Set the **Decal Blend Mode** to `Translucent` (or another mode that suits your art style).
3. Add a `VectorParameter` node named **`TintColor`** (this matches the default parameter name expected by the component) and feed it into the decal's base/opacity inputs as required.
4. Expose any other parameters you need (mask textures, emissive, roughness, etc.).
5. Save the material and, if desired, create a material instance for runtime tweaking.

> **Tip:** If you want to share textures with the existing grid highlight, right-click the original material and choose **Create Material Instance**, then use that instance as the texture source inside your decal material. The domain still needs to be `Deferred Decal` for decals to render.

## 2. Configure the grid overlay component

1. Select the actor that owns `UGridOverlayComponent` (e.g. the grid overlay actor in your battle map).
2. In the **Grid | Highlight | Decal** category:
   - Enable **Use Decal Highlights**.
   - Assign your new decal material (or material instance) to **Highlight Decal Material**.
   - Adjust **Decal Projection Depth**, **Size Multiplier**, **Fade Screen Size**, **Life Span**, and **Fade Duration** to taste. Leave **Life Span** at `0` for persistent highlights that clear only when the component tells them to.
3. (Optional) Set **Highlight Decal Color Parameter** if your material expects a different vector parameter name for tinting.

Once configured, calling any of the existing highlight helpers (`HighlightCell`, `HighlightMovement`, `HighlightAttack`, `HighlightSelection`) will spawn decals instead of instanced meshes. Call `ClearHighlights` to remove any active decals when they are no longer needed.
