# TS2 Redux AP — client DLL

The in-game half of [TS2 Redux: Archipelago Edition](../README.md). Builds to `AP.dll`, which is
deployed as `Bin64\TS2Redux\Scotch.dll` and loaded through the TS2 Redux DLL-hijack chain. It
connects to an Archipelago server, locks content you haven't received, detects completions
(trophies / story difficulties / objectives), and applies items (weapon unlocks, traps, bonus refills,
DeathLink). Every game-memory access is SEH-guarded so a bad read can't crash the game.

## Layout

| File | Role |
| --- | --- |
| `dllmain.cpp` | DllMain, the menu lock + hooks (objective render, Robot Factory fix, weapon precache), traps/bonus, and the player-effect API (health/armor/ammo, cheats, the DeathLink kill). |
| `ap_client.cpp` | Archipelago networking (apclientpp): reads `AP_client.cfg`, sends checks, applies received items, per-seed saves in `ap_saves/`, DeathLink. |
| `ts2_data.h` | **Generated** from `apworld/timesplitters2/data.py` by `gen_header.py` — keeps ids/names in lockstep with the apworld. Committed; regenerate when ids change. |
| `ts2_fresh_profile.h` | Embedded clean-profile blob used to isolate each AP seed's save. On a new seed the client stamps your slot name into the profile's name field, so the auto-created profile is identifiable in-game. |
| `RFPatch/` | Standalone Robot Factory crash-fix DLL (the fix is also applied in-process by `dllmain.cpp`). |
| `DEPENDENCIES.md` | The third-party libraries the build needs (with versions) and where to place them under `libs/`. |

## Build

1. Obtain the third-party dependencies listed in [`DEPENDENCIES.md`](DEPENDENCIES.md) and place each under
   `client/libs/` using the exact folder names shown (they are not committed to this repo).
2. Open `AP.vcxproj` in **Visual Studio 2022** (toolset **v143**), select **Release | x64**, and build.
3. The output is `x64\Release\AP.dll`. Deploy it as `Bin64\TS2Redux\Scotch.dll`:
   - Set a **`TS2_GAME_DIR`** environment variable to your Homefront install root and the post-build
     step copies + renames it automatically, **or**
   - copy `x64\Release\AP.dll` to `<game>\Bin64\TS2Redux\Scotch.dll` yourself.

Command-line build (MSBuild):

```
msbuild client\AP.vcxproj /p:Configuration=Release /p:Platform=x64
```

## Regenerating `ts2_data.h`

`ts2_data.h` is generated from the apworld's `data.py`; the MSVC build does **not** run the generator,
so the header is committed and you only regenerate it when item/location ids change:

```
python apworld/timesplitters2/gen_header.py     # writes client/ts2_data.h
```

Then rebuild the DLL so the C++ and Python sides stay in lockstep.

## Configuration & runtime files

The client reads `AP_client.cfg` (`host` / `slot` / `password`) from the **game root** and writes
`AP_client.log` + per-seed saves under `ap_saves\` there too. See
[`../install/AP_client.cfg`](../install/AP_client.cfg) for the template and
[`../install/README.md`](../install/README.md) for the full install/payload details.

## Notes

- **Pinned EXE** — offsets assume the shipped Homefront2_Release.exe (image base `0x140000000`); the
  client self-gates on the executable, so other versions won't activate.
- **Transport** — TLS via statically-linked OpenSSL (see [`DEPENDENCIES.md`](DEPENDENCIES.md)), so the built
  `Scotch.dll` is self-contained. `AP_client.cfg`'s `host` is stored without a scheme on purpose: apclientpp
  only auto-negotiates when the URI has none, trying `wss://` first and falling back to `ws://`. An explicit
  prefix pins that scheme. Add `WSWRAP_NO_SSL` and drop OpenSSL for a plaintext-only build.
