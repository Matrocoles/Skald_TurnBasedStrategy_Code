# Dice resolution panel widget wiring

The dice resolution panel (`W_DiceResolutionPanel`) expects a very small amount of blueprint
setup so that the runtime code can take over scroll-box management and per-die entry creation.
If those bindings are missing, you will only see the entry widget instances spawn without
actually being inserted into a visible list.

## Starting point in the designer

When you create a Blueprint subclass of `UW_DiceResolutionPanel`, start by laying out the
overall panel as you normally would—there is no requirement that the root widget be a
`Canvas Panel`. Most teams anchor the panel inside another layout container, so a
`Canvas`, `Overlay`, `Border`, or any other panel widget is fine. The runtime code takes
care of preserving whatever slot data you use when it injects the scroll container (see
`InitializeOutcomeScrollContainer()` in `Source/Skald/UI/W_DiceResolutionPanel.cpp`), so you
do not need to reshape the hierarchy just to accommodate the scroll behaviour.

Inside that root, the very first thing to add for designers is the vertical box that will
receive the die rows. Create a `Vertical Box`, name it **OutcomeList**, and mark *Is
Variable*. Position it wherever you want the results to appear—this is the anchor the
runtime uses when it wraps the content in a size box and scroll box.

If your layout calls for additional labels (hit/miss tallies, total damage, etc.), feel
free to add them around the vertical box just like any other widget. As long as the widget
names match the optional bindings listed below, they will auto-populate when a resolution
begins.

## Required named widgets

Inside the `W_DiceResolutionPanel` blueprint, make sure the following widgets exist and are
marked as *Is Variable* so they can be bound to the properties declared in
`UW_DiceResolutionPanel`:

| Property | Type | Blueprint widget name | Notes |
| --- | --- | --- | --- |
| `OutcomeList` | `VerticalBox` | `OutcomeList` | Container that will receive the per-die rows. You can place it anywhere in the hierarchy; the code will wrap it in a scroll box at runtime. |
| `ResolveProgressPlaceholder` | `Widget` (any) | `ResolveProgressPlaceholder` | Optional. Hidden until a reveal starts. |
| `HitCountText` | `TextBlock` | `HitCountText` | Optional tally label. |
| `MissCountText` | `TextBlock` | `MissCountText` | Optional tally label. |
| `CritCountText` | `TextBlock` | `CritCountText` | Optional tally label. |
| `TotalDamageText` | `TextBlock` | `TotalDamageText` | Optional summary label. |
| `HealthSummaryText` | `TextBlock` | `HealthSummaryText` | Optional summary label. |
| `PlayerResultsText` | `TextBlock` | `PlayerResultsText` | Optional label that describes the local player's outcome summary. The runtime tints it using the player's faction colour. |
| `EnemyResultsText` | `TextBlock` | `EnemyResultsText` | Optional label that describes the opposing player's outcome summary. The runtime tints it using the enemy faction colour. |

Only `OutcomeList` is strictly required for the automatic scroll behaviour. The other bindings
are optional, but if they are present in the blueprint they must keep the exact names shown in
the table so the `BindWidgetOptional` properties resolve correctly.

## Runtime scroll-box wrapping

When the widget constructs or resets, `InitializeOutcomeScrollContainer()` runs. It searches for
an existing scroll box parent for `OutcomeList`; if none exists it dynamically creates the size box
and scroll box wrapper, maintaining the original slot layout so the blueprint hierarchy does not
need manual adjustment. The latest code is in `Source/Skald/UI/W_DiceResolutionPanel.cpp` at
`InitializeOutcomeScrollContainer()` and `ConfigureOutcomeScrollBox()`.

Because of this wrapping, you do *not* add the entry widget to a scroll box by hand. Instead, keep
the vertical box in the layout and let the panel add children during `RevealNextDie()`.

## Outcome entry widget

Just like the panel, the entry widget does not have to start with a `Canvas Panel`. Pick
whichever root container best matches the layout you want for a single die row—most
designers use a `HorizontalBox` or `Overlay` so the die icon and text stay aligned. The
entry ends up nested inside the runtime-created scroll box, so there is no risk of the
entry's root panel conflicting with the panel blueprint's hierarchy.

The per-die widget (`W_DiceResolutionEntryWidget`) also relies on named variables:

| Property | Type | Name |
| --- | --- | --- |
| `DiceFaceImage` | `Image` | `DiceFaceImage` |
| `RollValueText` | `TextBlock` | `RollValueText` |
| `OutcomeLabelText` | `TextBlock` | `OutcomeLabelText` |

When the panel creates an entry (see `RevealNextDie()`), it calls
`ConfigureOutcome()` on the entry widget, which fills in the dice face, roll value, and
hit/miss/crit label. If any of these bindings are missing, the entry will appear empty even though
it was added to the vertical box.

### Keeping the dice icon size under control

The entry widget exposes a `DiceImageSize` property (default `72×72`) that the runtime applies to
the dice face image after setting the texture. You do not need to wrap the image in additional size
boxes—the panel now avoids matching the texture's native resolution so the `DiceImageSize` override
is respected. If the dice appear oversized or only one entry fits in the scroll window, open the
entry blueprint, select the `DiceFaceImage`, and adjust the slot padding/alignment to keep the row
centered. As long as the entry layout is a single horizontal row (for example, a `HorizontalBox`
with the icon followed by the text blocks), the panel will clamp the scroll viewport so that two
rows are visible at a time.

## Verifying in-editor

1. Open `W_DiceResolutionPanel` in the UMG editor.
2. In the *Hierarchy* panel, locate the widgets listed above and ensure each has *Is Variable*
   enabled with the exact names.
3. Play in PIE and trigger a roll. You should now see the panel show two outcomes at a time
   in the auto-created scroll window, with the list scrolling when new dice are revealed.

Following the bindings above ensures the runtime code can locate the widgets it needs and perform
all of the scroll-box and entry wiring automatically.
