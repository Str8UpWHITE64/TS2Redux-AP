# TimeSplitters 2 — Archipelago world

The server-side `.apworld` for the TimeSplitters 2 Archipelago randomizer. It defines the items,
locations, and rules and generates the seed; the in-game client DLL (in [`../../client/`](../../client))
enforces the locks and reports checks. `game` name: **`TimeSplitters 2 Redux`**.

## Locations (362)

| Source | Count | Detail |
| --- | --- | --- |
| Arcade & Challenge trophies | 264 | the 66 trophy events × 4 tiers (Bronze / Silver / Gold / Platinum) |
| Story difficulties | 30 | 10 missions × 3 difficulties (Easy / Normal / Hard) |
| Objectives | 68 | per-mission objective completions |

The 66 trophy events are 45 Arcade matches + 21 Challenges, each toggled separately by `game_modes`. A trophy/story check fires when you reach that
tier/difficulty in-game; trophies are cumulative, so a single Gold clear fires Bronze + Silver + Gold. Which checks are *active* in a seed is option-driven (e.g. a
Bronze-tier seed only uses the Bronze checks), but every location id is pre-allocated and stable, so changing
options never renumbers existing seeds.

## Items (125 in the catalog)

- **Unit unlocks (76)** — one per arcade league / challenge / story mission; receiving it enables that unit's
  tier/difficulty checks and unlocks it in the menu.
- **Time Crystals (progression)** — gate the final mission.
- **Weapon items (36)** — used when **Weapon Gating** is on; each gates one weapon (the akimbo "(x2)"
  dual-wields are their own pickups, so they are their own items).
- **Traps (8)** — briefly switch on a disruptive TS2 cheat during a story mission.
- **Bonus / filler** — one-time Health / Armor / Ammo packs, plus junk filler.

The actual pool placed in a seed depends on the options below.

## Goal

Follows **`game_modes`**:

- **Story kept** — clear the final story mission (**Space Station**) on your chosen **Max Story Difficulty**,
  once you have collected enough **Time Crystals**.
- **Story removed** — there is no Space Station to clear, so instead complete **`trophy_goal_percentage`** of
  the trophy checks you kept. Counted over what the seed actually has, so a Challenge-only seed needs that share
  of its 21 Challenges alone. No Time Crystals are placed, and the weapon options are forced off.

**Time Crystals** are items sent to you from the multiworld — they are scattered among all players' checks, so
they arrive as other people play. Space Station stays locked until `time_crystals_required` of them have
arrived, which is what ties the final mission to the multiworld rather than to your own progress.

## Options

| Option | Type | Default | Notes |
| --- | --- | --- | --- |
| `game_modes` | any of Story / Arcade / Challenge | all three | which modes produce checks; at least one required (see **Goal**) |
| `trophy_goal_percentage` | 10–100 | 90 | share of your Arcade/Challenge checks needed to win; only when Story is removed |
| `story_difficulty` | Easy / Normal / Hard | Normal | the MAX difficulty; checks are produced up to it, and the goal fires on it |
| `trophy_tier` | Bronze / Silver / Gold / Platinum | Gold | the MAX trophy tier; checks are produced up to it |
| `time_crystals_required` | 0–60 | 8 | Time Crystals needed to unlock Space Station (auto-clamped to free slots) |
| `time_crystals_extra` | 0–30 | 4 | extra Time Crystals placed beyond the required count |
| `starting_unlocks_per_category` | 1–3 | 1 | randomly chosen starting unlocks per category, so the seed is enterable from the start |
| `weapon_gating` | toggle | off | you can only use weapons you've received as items. Story missions only |
| `weapon_shuffle` | toggle | off | swaps what each weapon is (pickups, your loadout, and enemy weapons). Story missions only |
| `weapon_shuffle_scope` | completely_random / same_class / within_level | completely_random | how far a swap may reach: anything, same class only, or only within the same level |
| `bonus_health_packs` | 0–30 | 3 | one-time full-health pickups in the pool |
| `bonus_armor_packs` | 0–30 | 3 | one-time full-armor pickups |
| `bonus_ammo_packs` | 0–30 | 3 | one-time full-ammo pickups |
| `trap_count` | 0–60 | 8 | number of trap items placed |
| `death_link` | toggle | off | share deaths with other DeathLink players |
| `arcade_death_link_threshold` | 0–50 | 10 | DeathLink: how many Arcade/Challenge deaths before one is sent out (they are deathmatches). Story sends every death; incoming deaths always apply everywhere and never count toward this. 0 = never send from Arcade/Challenge |

Plus the standard `start_inventory_from_pool`.

## Files

- `__init__.py` — `TS2World` / `TS2Web` (option-driven items, regions, rules, victory, `fill_slot_data`).
- `data.py` — content catalog + Archipelago id tables (validated at import: ids unique, counts consistent).
- `options.py` — the option set above.
- `gen_header.py` — regenerates `client/ts2_data.h` from `data.py` (run when ids change).
- `archipelago.json` — world manifest (game name, world version, minimum AP version).

## Generating

Place `ts2redux.apworld` in an Archipelago 0.6.x install's `custom_worlds/` (or copy the
`ts2redux/` folder into a source checkout's `worlds/`), add a `TimeSplitters 2 Redux:` player YAML using the
options above, and run **Generate**. To build the `.apworld`, zip this `ts2redux/` folder (archive root
= the folder, excluding `__pycache__`) and rename `.zip` → `.apworld`.
