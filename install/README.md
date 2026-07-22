# Install payload & player setup

This folder describes the **release package** a player downloads. The package contains only the files
this project adds — TS2 Redux itself is installed by the player from its own installer, so nothing
belonging to that project is redistributed here.

## Release package contents

```
Scotch.dll        <- the AP client (the built AP.dll, renamed)
AP_client.cfg     <- connection settings template
README.txt        <- the "Player install" steps below
```

That is the complete list. `Scotch.dll` is the only game binary this project ships: TS2 Redux's
`dinput8.dll` loader walks a fixed list of `Bin64\TS2Redux\*.dll` names, and `Scotch.dll` is an unused
slot in that list — so dropping the client there loads it automatically without replacing or patching
anything the installer laid down.

Runtime files the client creates on its own (never ship these): `AP_client.log`, `AP_capture.txt`, and
`ap_saves\<seed>_<slot>.bin` (+`.grant`), all in the game root.

## Player install

`{game}` is your Homefront: The Revolution folder — the directory the game launches from.

1. Own and install **Homefront: The Revolution** on Steam (app 223100). The client self-gates on the
   pinned game executable, so an unexpected game version simply will not activate.
2. Install **TS2 Redux v0.2.7** with its own installer:
   <https://github.com/HFTSRedux/TS2Redux/releases/tag/v0.2.7>
   **Leave every selection at its default.** Do not tick any extra or optional components — the
   defaults are what this randomizer is tested against, and extras can alter level content, unlock
   modes, or skip menus that Archipelago relies on. Launch the game once to confirm TS2 Redux works
   on its own before continuing.
3. Copy `Scotch.dll` into `{game}\Bin64\TS2Redux\`.
4. Copy `AP_client.cfg` into the **game root** `{game}\` and set `host` / `slot` / `password` for
   your room.
5. Launch the game the same way you normally launch TS2 Redux, so the working directory is the game
   root. The client reads `AP_client.cfg`, connects, creates `ap_saves\<seed>_<slot>.bin`, locks
   content according to the items you receive, and sends checks as you complete them. Watch
   `AP_client.log` to confirm the connection.

To uninstall, delete `Scotch.dll` and `AP_client.cfg`. Your TS2 Redux install is untouched.

### If you added extra components

Anything beyond the installer defaults is unsupported — components that rewrite level or weapon
content (cheat and overhaul packs) desync completion detection and item identities. Re-run the
TS2 Redux installer with the defaults if you are unsure what is installed.

To see what is actually loaded, launch the game and look for a line like this in `AP_capture.txt`
(game root), printed a few seconds after startup:

```
[mods] loaded: DevilDwarfUI, DevildwarfMenu, DisablePreferences,
FanotoMenu, GameFixesByFanotoAndDevilDwarf, InvisibleFixByDevilDwarf, Mods,
StraightIntoTS2byDevilDwarf, UnlockArcadeAndChallengeByFanoto
```

That is the default v0.2.7 set, and it is the configuration this randomizer is developed and tested
against. Include that line when reporting a problem.

## Server (host / seed-roller)

Separate from the in-game payload: drop `timesplitters2.apworld` into Archipelago's `custom_worlds/`,
add a `TimeSplitters 2` player YAML (see [`apworld/timesplitters2/README.md`](../apworld/timesplitters2/README.md)
for the options), run `Generate`, and host the server.
