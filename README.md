# TS2 Redux: Archipelago Edition

An [Archipelago](https://archipelago.gg) multiworld randomizer for **TimeSplitters 2** — the
complete, hidden TS2 port that ships inside *Homefront: The Revolution* and is re-enabled by the
[TS2 Redux](https://github.com/HFTSRedux/TS2Redux) mod.

> Archipelago is a multiworld randomizer: many games are shuffled into one combined seed, and the
> items that unlock *your* progress are scattered across *everyone's* games. Beating the seed is a
> group effort.

## How it works

Two halves talk to an Archipelago server:

- **The apworld** (`apworld/timesplitters2/`) runs **server-side**. It defines TS2's items,
  locations, and rules, and generates the seed.
- **The client DLL** (`client/`, built as `Scotch.dll`) runs **in-game**. It locks content you
  haven't received, watches for completions, and reports them back as checks.

The two stay in lockstep through a generated id header: `apworld/.../gen_header.py` reads `data.py`
and emits `client/ts2_data.h`, so the Python and C++ sides always agree on item/location ids.

## Repository layout

| Path | What it is |
| --- | --- |
| [`apworld/timesplitters2/`](apworld/timesplitters2) | The Archipelago world — items, locations, rules, options ([README](apworld/timesplitters2/README.md)) |
| [`apworld/build_apworld.py`](apworld/build_apworld.py) | Packages + verifies `timesplitters2.apworld` |
| [`client/`](client) | The in-game DLL, built as `Scotch.dll` ([README](client/README.md)) |
| [`client/RFPatch/`](client/RFPatch) | Standalone Robot Factory softlock fix, usable without Archipelago |
| [`install/`](install) | Player setup docs, the config template, and the release packager ([README](install/README.md)) |

## Features

- **Location checks** — arcade & challenge **trophy tiers** (Bronze → Platinum per event), **story
  per-difficulty** completions (Easy / Normal / Hard per mission), and **per-objective**
  completions.
- **Weapons as items** — two modes: **gating** (weapons stay locked until you receive them) and
  **shuffle** (one global mapping remaps your loadout, level pickups, and enemy weapons).
- **Game modes** — randomize the **story**, the **arcade & challenge** side, or **both**.
- **Progression goal** — in story modes the final mission (Space Station) is gated behind **Time
  Crystals**, and the goal is clearing it on your chosen difficulty once you've collected enough. In
  arcade-only, the goal is instead a chosen percentage of the arcade & challenge checks.
- **Traps** — short, comedic cheats (big/small heads, big hands, fat/cardboard/invisible enemies,
  rotating heads, slow-mo deaths) that fire briefly during story missions.
- **Bonus / filler** — one-time Health, Armor, and Ammo packs, granted the next time you're in a level.
- **[DeathLink](https://archipelago.gg/tutorial/Archipelago/death_link/en)** — opt-in; your death
  takes the linked players with you, and theirs takes you.

See [`apworld/timesplitters2/README.md`](apworld/timesplitters2/README.md) for the full item /
location / option breakdown.

## Requirements

- **Homefront: The Revolution** on Steam (PC) — you must own the game.
- A working **TS2 Redux v0.2.7** install, set up with its installer's default selections. See
  [HFTSRedux/TS2Redux](https://github.com/HFTSRedux/TS2Redux/releases/tag/v0.2.7).
- **Archipelago 0.6.x** (for generating/hosting).
- The built **`timesplitters2.apworld`**.

## Install (players)

1. Install Homefront: The Revolution, then install
   [TS2 Redux v0.2.7](https://github.com/HFTSRedux/TS2Redux/releases/tag/v0.2.7) with its own
   installer — **leave every selection at its default** and add nothing extra.
2. Drop the AP client (`Scotch.dll`) into `Bin64\TS2Redux\`, and put `AP_client.cfg` in the game root.
   Those two files are the entire download; nothing from TS2 Redux is replaced.
3. Install `timesplitters2.apworld` into Archipelago (`custom_worlds/`).

Full step-by-step is in [`install/README.md`](install/README.md). A ready-to-edit config template is
at [`install/AP_client.cfg`](install/AP_client.cfg).

> **Connection note:** put a plain `host:port` in `AP_client.cfg` and the client negotiates the
> transport itself — it tries `wss://` (TLS) first and falls back to `ws://` (plaintext), so both
> archipelago.gg rooms and local/self-hosted servers work without the player knowing the difference.
> Writing an explicit `ws://`/`wss://` prefix pins that scheme and disables the fallback.

## Archipelago setup (host / seed-roller)

1. Place `timesplitters2.apworld` in Archipelago's `custom_worlds/`.
2. Write a `TimeSplitters 2` player YAML — options are documented in the
   [apworld README](apworld/timesplitters2/README.md).
3. Run **Generate**, then host the server.

## Connecting in-game

Launch the game from its own folder (so the working directory is the game root). The client reads
`AP_client.cfg`, connects to your room, auto-creates a per-seed save under `ap_saves\`, locks content
by the items you've received, and sends checks as you earn trophies / clear difficulties / complete
objectives. Watch `AP_client.log` (written to the game root) to confirm the connection.

## Build from source

**Client DLL** — first obtain the third-party libraries listed in
[`client/DEPENDENCIES.md`](client/DEPENDENCIES.md) (with exact versions) and place them under
`client/libs/`. Then open [`client/AP.vcxproj`](client/AP.vcxproj) in Visual Studio 2022 (toolset
v143) and build **Release | x64**. The output `AP.dll` is the client — rename/copy it to
`Bin64\TS2Redux\Scotch.dll`. Set a `TS2_GAME_DIR` environment variable to your game install and the
post-build step deploys it automatically. See [`client/README.md`](client/README.md).

**apworld** — run `python apworld/build_apworld.py` (add `--install` to copy it straight into a local
Archipelago `custom_worlds/`). It packages the world and verifies the result — every member is checked
byte-for-byte against its source and `__pycache__` is asserted absent, since a stale `.pyc` inside the
archive can shadow the real module. If you change item/location ids in `data.py`, regenerate the shared
header with `gen_header.py` and rebuild the client.

## Troubleshooting

- **Nothing happens in-game** — confirm TS2 Redux itself launches without the randomizer, then that
  `Scotch.dll` is in `Bin64\TS2Redux\`; check `AP_client.log` in the game root. The client self-gates
  on the game executable, so an unexpected game version simply won't activate.
- **Can't connect** — verify `host`/`slot`/`password` in `AP_client.cfg`; `host` should be a plain
  `host:port` with no prefix. `AP_client.log` records each attempt, so a wrong port or slot name is
  visible there.
- **Locks/trophies look off** — you likely installed TS2 Redux with non-default components. The
  `[mods] loaded:` line in `AP_capture.txt` lists what actually loaded; compare it against the default
  set in [`install/README.md`](install/README.md).

## Credits & attribution

- This project is a derivative of **[TS2 Redux](https://github.com/HFTSRedux/TS2Redux)** (GPL-3.0) by
  the Redux Tech Team — DevilDwarf, Fanoto, RyanUKAus, Scotch, Skibbles, and Yossarian The Assyrian.
- Built on the **[Archipelago](https://archipelago.gg)** multiworld framework.
- Bundled libraries are credited in [`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md).

## License

**GPL-3.0** (see [`LICENSE`](LICENSE)) — inherited from TS2 Redux, which this work derives from.

## Disclaimer

This is a fan-made tool, not affiliated with or endorsed by Free Radical Design, Eidos, Crytek,
Dambuster Studios, Deep Silver, or the Archipelago project. It contains **no** TimeSplitters 2 or
Homefront: The Revolution game assets, code, or data, and does not let anyone play the game without
legally owning **Homefront: The Revolution**. The randomizer only modifies and observes your own
legally-owned installation at runtime.
