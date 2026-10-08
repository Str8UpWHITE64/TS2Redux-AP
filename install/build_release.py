"""Assemble the player release zip.

The package contains only what this project adds -- players install TS2 Redux themselves -- so it is
exactly three files: the built client, the config template, and the install steps.

    python build_release.py                  -> ./TS2Redux-AP-<version>.zip
    python build_release.py --out DIR        -> DIR/TS2Redux-AP-<version>.zip

The version comes from the apworld manifest, so the download name always matches the world it pairs
with. Verifies the result: expected members present, nothing extra, zip integrity, and that the
bundled client byte-matches the build output.
"""

import argparse
import hashlib
import json
import os
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
CLIENT_DLL = os.path.join(ROOT, "client", "x64", "Release", "AP.dll")
MANIFEST = os.path.join(ROOT, "apworld", "ts2redux", "archipelago.json")

README_TXT = """\
TS2 Redux: Archipelago Edition -- client

This download contains only the files the randomizer adds. TS2 Redux itself is installed separately,
by its own installer, and nothing it lays down is replaced.

INSTALL

1. Install Homefront: The Revolution on Steam (app 223100).

2. Install TS2 Redux v0.2.7 using its installer:
       https://github.com/HFTSRedux/TS2Redux/releases/tag/v0.2.7

   Leave every selection at its default. Do not tick any extra or optional components -- the
   defaults are what this randomizer is tested against. Launch the game once and confirm TS2 Redux
   works on its own before continuing.

3. Copy Scotch.dll into:
       <game>\\Bin64\\TS2Redux\\

4. Copy AP_client.cfg into the GAME ROOT (the folder the game launches from, NOT Bin64):
       <game>\\AP_client.cfg
   Open it and set host, slot and password for your Archipelago room.

5. Launch the game the way you normally launch TS2 Redux. The client connects on startup, locks
   content according to the items you receive, and sends checks as you complete them.

   AP_client.log in the game root shows the connection status if something looks wrong.

UNINSTALL

Delete Scotch.dll and AP_client.cfg. Your TS2 Redux install is untouched.

SEED ROLLING

The host also needs ts2redux.apworld in Archipelago's custom_worlds/ folder, plus a
"TimeSplitters 2 Redux" player YAML. Options are documented with the apworld. Upgrading from
1.0.x: the old timesplitters2.apworld has to go. Delete it, or, if you also play the GameCube
version, install its timesplitters2.apworld over it (the GameCube world now uses that file name).
"""


def build(out_dir: str) -> str:
    version = json.load(open(MANIFEST, encoding="utf-8"))["world_version"]
    if not os.path.exists(CLIENT_DLL):
        sys.exit(f"client not built: {CLIENT_DLL}\nbuild client/AP.vcxproj as Release|x64 first")
    out = os.path.join(out_dir, f"TS2Redux-AP-{version}.zip")
    os.makedirs(out_dir, exist_ok=True)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        z.write(CLIENT_DLL, "Scotch.dll")
        z.write(os.path.join(HERE, "AP_client.cfg"), "AP_client.cfg")
        z.writestr("README.txt", README_TXT.replace("\n", "\r\n"))
    print(f"built {out}  ({os.path.getsize(out)} bytes)  version={version}")
    return out


def verify(pkg: str) -> bool:
    ok = True
    expected = {"Scotch.dll", "AP_client.cfg", "README.txt"}
    with zipfile.ZipFile(pkg) as z:
        if z.testzip() is not None:
            print("  FAIL: zip integrity"); ok = False
        names = set(z.namelist())
        if names != expected:
            print(f"  FAIL: members are {sorted(names)}, expected {sorted(expected)}"); ok = False
        if "Scotch.dll" in names:
            if hashlib.sha256(z.read("Scotch.dll")).hexdigest() != \
               hashlib.sha256(open(CLIENT_DLL, "rb").read()).hexdigest():
                print("  FAIL: Scotch.dll differs from the build output"); ok = False
    print("  verify:", "OK" if ok else "FAILED")
    return ok


def main() -> None:
    ap = argparse.ArgumentParser(description="Assemble the TS2 Redux AP player release zip")
    ap.add_argument("--out", default=HERE, help="output directory (default: this folder)")
    args = ap.parse_args()
    if not verify(build(args.out)):
        sys.exit(1)


if __name__ == "__main__":
    main()
