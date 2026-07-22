"""Generate client/ts2_data.h from data.py so the client's ids/names stay in lockstep with the apworld.

Run from the repo root:  python -m timesplitters2.gen_header   (or:  python apworld/timesplitters2/gen_header.py)
after adding apworld/ to PYTHONPATH, or simply run this file directly.
"""
import os, sys

# allow running as a loose script (no package context)
sys.path.insert(0, os.path.dirname(__file__))
import data as d  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
# repo layout: apworld/timesplitters2/gen_header.py -> client/ts2_data.h
OUT = os.path.normpath(os.path.join(HERE, "..", "..", "client", "ts2_data.h"))

KIND = {"arcade": 0, "challenge": 1}  # story = 2


def cstr(s: str) -> str:
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main():
    L = []
    w = L.append
    w("#pragma once")
    w("// GENERATED from worlds/timesplitters2/data.py -- do not edit by hand (run gen_header.py).")
    w("// Keeps the client's ids/names in lockstep with the apworld.")
    w("#include <cstdint>")
    w("")
    w("namespace ts2 {")
    w("static const int64_t BASE_ID = 0x%08xLL;" % d.BASE_ID)
    w("static const int64_t FILLER_ID = 0x%08xLL;  // %s (junk)" % (d.item_name_to_id[d.FILLER_ITEM], d.FILLER_ITEM))
    w("static const int64_t TIME_CRYSTAL_ID = 0x%08xLL;  // %s (collect time_crystals_required of these to unlock the final stage)" % (d.item_name_to_id[d.TIME_CRYSTAL_ITEM], d.TIME_CRYSTAL_ITEM))
    w("")
    w("// --- bonus + trap items: the client maps a received item id -> an effect ---")
    w("// bonus = one-time FULL refill, granted on the next in-mission/challenge tick (queued if received in a menu).")
    w("struct BonusItem { const char* name; int64_t id; };")
    w("static const BonusItem BONUS_ITEMS[] = {")
    for b in d.BONUS_ITEMS:
        w("    { %s, 0x%08xLL }," % (cstr(b), d.item_name_to_id[b]))
    w("};")
    w("static const int BONUS_ITEM_COUNT = %d;" % len(d.BONUS_ITEMS))
    w("// trap = enable a TS2 cheat for a few seconds -- STORY missions only. cheatBit = bit in the cheat-active")
    w("// bitfield at (g_base + CHEAT_ACTIVE_OFF): set it to switch the cheat on, clear it to end the trap.")
    w("static const uintptr_t CHEAT_ACTIVE_OFF = 0x%x;  // uint bitfield; bit = cheat catalog index" % d.CHEAT_ACTIVE_OFF)
    w("// spawnTime=1 => cheat only takes effect on a level (re)load -> arm it while OUTSIDE a level (next entry).")
    w("struct TrapItem { const char* name; int64_t id; int cheatBit; int spawnTime; };")
    w("static const TrapItem TRAP_ITEMS[] = {")
    for t in d.TRAP_ITEMS:
        w("    { %s, 0x%08xLL, %d, %d }," % (cstr(t), d.item_name_to_id[t], d.TRAP_CHEATS[t], 1 if t in d.TRAP_SPAWN_TIME else 0))
    w("};")
    w("static const int TRAP_ITEM_COUNT = %d;" % len(d.TRAP_ITEMS))
    w("")
    w("// units (mission or trophy-event). itemId = BASE_ID + index.  index: events 0..65, story 66..75")
    w("struct Unit { const char* name; int index; int kind; };  // kind 0=arcade 1=challenge 2=story")
    w("static const Unit UNITS[] = {")
    for i, name in enumerate(d.UNIT_NAMES):
        if i < len(d.TROPHY_EVENTS):
            kind = KIND[d.TROPHY_EVENTS[i][0]]
        else:
            kind = 2
        w("    { %s, %d, %d }," % (cstr(name), i, kind))
    w("};")
    w("static const int UNIT_COUNT = %d;" % len(d.UNIT_NAMES))
    w("")
    w("// trophy events: ei = apworld order (0..65), gameIdx = in-game trophy-table index (0..65)")
    w("struct TrophyEvent { const char* name; int ei; int gameIdx; };")
    w("static const TrophyEvent TROPHY_EVENTS[] = {")
    for ei, (_kind, _grp, name) in enumerate(d.TROPHY_EVENTS):
        w("    { %s, %d, %d }," % (cstr(name), ei, d.TROPHY_EVENT_INDEX[name]))
    w("};")
    w("static const int TROPHY_EVENT_COUNT = %d;" % len(d.TROPHY_EVENTS))
    w("")
    w("// story missions: index = data order (== goal/location order).")
    w("static const char* STORY[] = { %s };" % ", ".join(cstr(s) for s in d.STORY))
    w("static const int STORY_COUNT = %d;" % len(d.STORY))
    w("// per-difficulty story COMPLETION bitfields (bit m = mission m; set only on an actual clear, cumulative).")
    w("// THIS is what the client polls for story checks. (all 3 offsets confirmed live: Siberia Easy/Normal/Hard.)")
    w("static const uintptr_t STORY_CLEAR_EASY   = 0x%x;  // Easy+ cleared (canonical 'mission completed')" % d.STORY_CLEAR_OFF["Easy"])
    w("static const uintptr_t STORY_CLEAR_NORMAL = 0x%x;  // Normal+ cleared" % d.STORY_CLEAR_OFF["Normal"])
    w("static const uintptr_t STORY_CLEAR_HARD   = 0x%x;  // Hard cleared" % d.STORY_CLEAR_OFF["Hard"])
    w("// NOTE: profile + STORY_DIFF_BASE + level_id*8 is the per-level 'selected difficulty' byte -- it bumps on")
    w("// mission ENTRY (not completion), so it is NOT used for checks (caused false positives). Reference only.")
    w("static const int STORY_LEVEL_ID[] = { %s };" % ", ".join("0x%02x" % x for x in d.STORY_LEVEL_ID))
    w("static const uintptr_t STORY_DIFF_BASE = 0x%x;" % d.STORY_DIFF_BASE)
    w("static inline uintptr_t storyDiffOff(int mission) { return STORY_DIFF_BASE + (uintptr_t)STORY_LEVEL_ID[mission]*8; }")
    w("static const char* DIFFICULTIES[] = { %s };" % ", ".join(cstr(s) for s in d.STORY_DIFFICULTIES))
    w("static const char* TROPHY_TIERS[] = { %s };" % ", ".join(cstr(s) for s in d.TROPHY_TIERS))
    w("")
    w("// location id helpers (mirror data.py):")
    w("//   trophy:  BASE_ID + 0x1000 + ei*4 + tierIdx          ('<event> (<tier>)')")
    w("//   story:  BASE_ID + 0x1400 + missionDataIdx*3 + diffIdx  ('<mission> (<difficulty>)')")
    w("static inline int64_t trophyLocId(int ei, int tier) { return BASE_ID + 0x1000 + (int64_t)ei*4 + tier; }")
    w("static inline int64_t storyLocId(int mission, int diff) { return BASE_ID + 0x1400 + (int64_t)mission*3 + diff; }")
    w("}  // namespace ts2")
    w("")

    with open(OUT, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(L))
    print("wrote %s (%d units, %d trophy events, %d story)" % (OUT, len(d.UNIT_NAMES), len(d.TROPHY_EVENTS), len(d.STORY)))


if __name__ == "__main__":
    main()
