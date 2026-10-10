# Entrance test session (F33-T04, T05, T06)

Two short runs of the game with the test build. Neither touches your own saves: the first loads a *copy* of one, and the second makes a new save named `FalloutMP_Template`.

The settings go in `Data\F4SE\Plugins\FalloutMP.json` (in your Fallout 4 folder). While an `"entry"` block is in that file, FalloutMP does not connect to any server. Remove the block when you're done.

The log is `Documents\My Games\Fallout4\F4SE\FalloutMP.log`. It is rewritten on every launch, so copy it after each run.

## Before you start
1. Install the test build from the latest "FalloutMP client (Windows)" run (the `FalloutMP-client` download), as usual.
2. Your re-saved file (`Save5_…Commonwealth…`) must be in `Documents\My Games\Fallout4\Saves`.

## Run 1: load a generated save from the main menu (about 5 minutes)
1. Add this block to `FalloutMP.json`, inside the outer `{ }`, after the `"features"` block (mind the comma before it):
   ```json
   "entry": {
     "test": {
       "source": "Save5_",
       "patch": true,
       "pos": [-79800, 90500, 7800],
       "angleZ": 180,
       "gameHour": 13.0
     }
   }
   ```
2. Start the game through F4SE as usual and **don't press anything**.
3. **What should happen:**
   - shortly after the game starts, a loading screen appears on its own;
   - you appear at the FalloutMP start point in Sanctuary Hills (where the server put you before), **not** at the Vault 111 exit;
   - it's early afternoon (13:00).
4. Look around for 30 seconds, then quit to desktop.
5. Copy `FalloutMP.log` and send it. Also tell me what you saw: where you stood, the time of day, and whether the screen ever went black or stuck.

**If nothing loads, or the screen stays black:** quit, add `"trigger": "mainMenu",` inside `"test"`, and try again. If that also fails, add `"route": "B",` as well, and try once more. Send the log from every try.

## Run 2: make the entry template (about 5 minutes)
1. Move these files out of `Fallout 4\Data` into a backup folder: `DLCUltraHighResolution.esm` and its `.ba2` files, plus any paid DLC (`DLCRobot`, `DLCCoast`, `DLCNukaWorld`, `DLCworkshop01`–`03`, `.esm` and `.ba2`). Keep the free `cc…` files where they are.
2. In `FalloutMP.json`, replace the `"entry"` block with:
   ```json
   "entry": { "capture": true }
   ```
3. Start the game, choose **Load**, and load the `Save5_…` save.
4. **Don't touch anything for about 30 seconds.** Messages appear at the top left:
   - "FalloutMP capture: stripping the inventory";
   - then, a few seconds later, "FalloutMP capture: saved as FalloutMP_Template".
5. Quit to desktop.
6. Send `Documents\My Games\Fallout4\Saves\FalloutMP_Template.fos` and `FalloutMP.log`.

## Afterwards
- Remove the `"entry"` block from `FalloutMP.json`.
- Move the backed-up files into `Data` again.
- `FalloutMP_Template.fos` can stay in your Saves folder, or be deleted once sent.

## What the developers read in the log
Every line of this test starts with `entry:`:
- the engine function addresses (`id …`);
- the save folder;
- the save editor's patch and verify time;
- whether the game listed the generated save;
- route A's result and how long `LoadGame` blocked;
- the threads of `kPreLoadGame`/`kPostLoadGame`;
- which menus opened and closed, and when;
- three reports after the load (position, distance to the target, GameHour, menus);
- for the capture, the Pip-Boy count and the console `save` command.

These answer the checks in F33 §9 item 1.
