"""Build ts2redux.apworld from the source folder.

An .apworld is just a zip whose archive root is the world package folder. Only the source files are shipped --
never __pycache__: a stale .pyc inside the archive can shadow the real module and make an install behave like an
older build.

    python build_apworld.py                     -> ./ts2redux.apworld
    python build_apworld.py --out DIR           -> DIR/ts2redux.apworld
    python build_apworld.py --install           -> also copy into the local Archipelago custom_worlds/

Verifies the result: every member byte-matches its source, the zip passes an integrity check, and no
__pycache__ slipped in.
"""

import argparse
import hashlib
import json
import os
import shutil
import sys
import zipfile

PKG = "ts2redux"
# Everything the world needs at runtime, plus the README that documents it. Keep this list explicit rather than
# globbing, so a stray scratch file in the folder can never end up in a release.
MEMBERS = ["__init__.py", "data.py", "options.py", "gen_header.py", "archipelago.json", "README.md"]
DEFAULT_INSTALL = r"C:\ProgramData\Archipelago\custom_worlds"


def build(src_dir: str, out_dir: str) -> str:
    manifest = json.load(open(os.path.join(src_dir, "archipelago.json"), encoding="utf-8"))
    out = os.path.join(out_dir, f"{PKG}.apworld")
    os.makedirs(out_dir, exist_ok=True)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for name in MEMBERS:
            path = os.path.join(src_dir, name)
            if not os.path.exists(path):
                sys.exit(f"missing required file: {path}")
            z.write(path, f"{PKG}/{name}")
    print(f"built {out}  ({os.path.getsize(out)} bytes)  world_version={manifest.get('world_version')}")
    return out


def verify(apworld: str, src_dir: str) -> bool:
    ok = True
    with zipfile.ZipFile(apworld) as z:
        if z.testzip() is not None:
            print("  FAIL: zip integrity"); ok = False
        names = z.namelist()
        if any("__pycache__" in n for n in names):
            print("  FAIL: __pycache__ present"); ok = False
        for n in names:
            src = os.path.join(src_dir, os.path.basename(n))
            if hashlib.sha256(z.read(n)).hexdigest() != hashlib.sha256(open(src, "rb").read()).hexdigest():
                print(f"  FAIL: {n} differs from source"); ok = False
        missing = {f"{PKG}/{m}" for m in MEMBERS} - set(names)
        if missing:
            print(f"  FAIL: missing {sorted(missing)}"); ok = False
    print("  verify:", "OK" if ok else "FAILED")
    return ok


def main() -> None:
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description="Build the TimeSplitters 2 .apworld")
    ap.add_argument("--out", default=here, help="output directory (default: this folder)")
    ap.add_argument("--install", nargs="?", const=DEFAULT_INSTALL, default=None,
                    metavar="DIR", help=f"also copy into an Archipelago custom_worlds dir (default: {DEFAULT_INSTALL})")
    args = ap.parse_args()

    src = os.path.join(here, PKG)
    built = build(src, args.out)
    if not verify(built, src):
        sys.exit(1)
    if args.install:
        if not os.path.isdir(args.install):
            sys.exit(f"install dir not found: {args.install}")
        dest = os.path.join(args.install, f"{PKG}.apworld")
        shutil.copyfile(built, dest)
        print(f"installed -> {dest}")


if __name__ == "__main__":
    main()
