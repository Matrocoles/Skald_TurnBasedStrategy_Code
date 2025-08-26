# C++ Blueprint Assets

This folder holds Blueprint assets referenced by the C++ code. Some UI widgets are not committed to source control.

* **Skald_SaveGameWidget** – Create a `UserWidget` Blueprint named `Skald_SaveGameWidget` with an editable text box for the slot name and a button that triggers `USkaldSaveGameLibrary::SaveSkaldGame`. See `Skald_SaveGameWidget.json` for a text description of the expected setup.

* **Skald_MainHUDBP** – Blueprint subclass of `USkaldMainHUDWidget` used for the in-game HUD. Implements `BP_RebuildPlayerList` to rebuild the on-screen player list. See `Skald_MainHUDBP.json` for a description of the expected setup.
