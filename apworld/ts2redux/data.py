"""TimeSplitters 2 content catalog + Archipelago id tables (v2: tiered).

Names are the EXACT in-game labels as they appear in TimeSplitters 2. They become AP item/location names
and are PERMANENT once seeds exist. Story mission names are domain-knowledge (carousel) -- VERIFY before a
public seed.

Check model (per the user's design):
  * Story missions -> one location per DIFFICULTY (Easy / Normal / Hard).
  * Arcade matches + Challenges (the 66 "trophy events") -> one location per TROPHY TIER (Bronze..Platinum).
  * Items: one unlock per unit (mission or event); unlocking a unit enables all of its tier/difficulty checks.
Options select which difficulties / trophy tiers actually become checks; ALL possible ids are allocated here so
ids stay stable regardless of options.
"""

BASE_ID = 0x54533200  # "TS2\0"

# --- Arcade League: 3 leagues -> 5 series -> 3 matches (45 matches) ---
ARCADE = {
    "Amateur League": {
        "Beginners Series":   ["Adios Amigos!", "Casualty", "Top Shot"],
        "Mode Madness":       ["Chastity Chased", "Shrinking from the Cold", "Scrap Metal"],
        "It's a Blast":       ["Night Shift", "Spoils of War", "Demolition Derby"],
        "Too Hot to Handle":  ["Monkey Immolation", "Disco Inferno", "Burns Department"],
        "Team Series A":      ["Club Soda", "Station Stand", "Men In Gray"],
    },
    "Honorary League": {
        "Maximus":                          ["Cold Corpse Caper", "Killer Queen", "R109 Beta"],
        "Elimination Series":               ["Baking for the Taking", "Brace Yourself", "Starship Whoopers"],
        "Burns 'n' Bangs":                  ["Chinese Burns", "Snow Business", "Rocket Man"],
        "Outnumbered but Never Outpunned!": ["Someone Has Got to Pay...", "Time to Split", "Can't Handle This"],
        "Team Series B":                    ["Hack a Hacker", "Rice Cracker Rush", "Superfly Lady"],
    },
    "Elite League": {
        "One Shot Thrills":           ["Babes in the Woods", "Double Bill", "Nikki Jinki Bricky"],
        "Duel Meaning":               ["If I'm Ugly - You Smell!", "Golem Guru", "Golden Thighs"],
        "Frantic Series":             ["Hangar Hat's Off!", "Can't Please Everyone...", "Big Top Blowout"],
        "Team Series C":              ["Bags of Fun", "They're Not Pets!", "Nice Threads"],
        "Sincerest Form of Flattery": ["Aztec the Dino Hunter", "Half Death", "Dead Fraction"],
    },
}

# --- Challenges: 7 categories -> 3 each (21) ---
CHALLENGES = {
    "Glass Smash":                   ["Pane in the Neck", "Bricking it", "Stain Removal"],
    "Behead The Undead":             ["Fight Off The Living Dead", "Sergio's Last Stand", "Day of the Dammed"],
    "Infiltration":                  ["Silent but Deadly", "Trouble at the Docks", "Escape from NeoTokyo"],
    "Banana Chomp":                  ["Gone Bananas", "Monkey Business", "Playing With Fire"],
    "Cut-out Shoot-out":             ["Take 'em Down", "Fall Out", "Pick Yer Piece"],
    "TimeSplitters 'Story' Classic": ["Badass Buspass Impasse", "But Where do the Batteries Go?", "Hit Me Baby One Morgue Time"],
    "Monkeying Around":              ["Simian Shootout", "Monkey Mayhem", "Dam Bursters"],
}

# --- Story missions (carousel; VERIFY names/order) ---
STORY = [
    "Siberia", "Chicago", "Notre Dame", "Return to Planet X", "NeoTokyo",
    "Wild West", "Atom Smasher", "Aztec", "Robot Factory", "Space Station",
]
FINAL_STORY_MISSION = "Space Station"

# Per-difficulty story COMPLETION bitfields (profile offsets). One dword each; bit m = story mission m in this
# STORY order (confirmed Siberia=0, Chicago=1, Notre Dame=2). A bit sets ONLY on an actual clear (never on merely
# entering a mission), and clears are cumulative (a Normal clear implies the Easy bit). THIS is the reliable
# per-difficulty signal the client polls.
STORY_CLEAR_OFF = {"Easy": 0x18f8, "Normal": 0x18fc, "Hard": 0x1900}  # all 3 confirmed live (Siberia E/N/H)

# NOTE (do NOT use for checks): profile + 0xfc0 + level_id*8 is the per-level "last/selected difficulty" byte. It
# bumps on mission ENTRY, not completion, so polling it produced false positives (a quit-out still looked cleared).
# Kept for reference. level_id = first byte of each 0x14-byte row in the mission table @image+0x2507d32.
STORY_LEVEL_ID = [0x23, 0x14, 0x22, 0x28, 0x26, 0x20, 0x1f, 0x30, 0x29, 0x27]
STORY_DIFF_BASE = 0xfc0

STORY_DIFFICULTIES = ["Easy", "Normal", "Hard"]            # story per-difficulty checks
TROPHY_TIERS = ["Bronze", "Silver", "Gold", "Platinum"]     # arcade/challenge per-trophy checks (game tier i3 = 1..4)
FILLER_ITEM = "Banana"             # junk filler; the client ignores it (no in-game effect)
TIME_CRYSTAL_ITEM = "Time Crystal"     # progression Time Crystal (the TS2 plot object); collect N to unlock the final stage

# --- Bonus items (one-time, beneficial; the client grants the effect on the next in-mission/challenge tick) ---
BONUS_ITEMS = ["Health Pack", "Armor Pack", "Ammo Pack"]      # each = a FULL refill of health / armor / ammo
BONUS_ITEM_ID_BASE = 0x902     # item id = BASE_ID + 0x902.. (free block between filler 0x901 and weapon base 0xA00)

# --- Trap items (named set; the client switches on a TS2 cheat for a few seconds, STORY missions ONLY so a trap can
# never void an Arcade/Challenge trophy). The cheat-active state is the bitfield CHEAT_ACTIVE_OFF (RVA 0x3f2f998):
# bit <catalog index> = cheat on (the cheat-active bitfield written by FUN_1410c4470). To enable a trap the client
# sets that bit; to end it, clears it. AP trap item -> cheat catalog index (= bit). Catalog @ .data 0x2507a50,
# stride 0x28: 0 Gun Sounds, 1 Big Heads, 2 Small Heads, 3 Big Hands, 4 Infinite Ammo, 5 Paintball, 6 Fat Characters,
# 7 Skating, 8 All Cloaked, 9 Slow-Mo Deaths, 10 Cardboard, 11 Rotating Heads, 12 Silly Hats. ---
CHEAT_ACTIVE_OFF = 0x3f2f998   # uint cheat-active bitfield; bit i = catalog cheat i is on
TRAP_CHEATS = {
    "Invisible Enemies Trap":  8,   # All Characters Cloaked
    "Big Head Trap":           1,   # Big Heads
    "Small Head Trap":         2,   # Small Heads
    "Big Hands Trap":          3,   # Big Hands
    "Fat Enemies Trap":        6,   # Fat Characters
    "Rotating Heads Trap":    11,   # Rotating Heads
    "Cardboard Enemies Trap": 10,   # Cardboard Characters
    "Slow Death Trap":         9,   # Slow Motion Deaths
}
TRAP_ITEMS = list(TRAP_CHEATS)
TRAP_ITEM_ID_BASE = 0x910      # item id = BASE_ID + 0x910 + i
# Spawn-time cheats only take effect on a level (re)load (vs render-time ones that apply instantly mid-mission,
# confirmed in-game). The client arms a spawn-time trap while the player is OUTSIDE a level so it lands on the next
# entry. (Confirmed render-time: Big/Small Heads, Big Hands, Fat, Slow-Mo Deaths, Cardboard, Rotating Heads.)
TRAP_SPAWN_TIME = frozenset({"Invisible Enemies Trap"})   # All Characters Cloaked needs a reload

# difficulty sets used by the objective table
_ALL = frozenset(("Easy", "Normal", "Hard")); _NH = frozenset(("Normal", "Hard")); _EN = frozenset(("Easy", "Normal"))
_E = frozenset(("Easy",)); _N = frozenset(("Normal",)); _H = frozenset(("Hard",)); _NONE = frozenset()

# --- Story objectives -> AP locations ---
# Keyed by (mission, textID): the SAME objective-text id is reused across missions ("Retrieve the Time Crystal"=1062
# and "Escape through the Time Portal"=1063 appear in EVERY story mission), so a location is identified by both the
# mission and the textID; the client disambiguates via the live mission index.  Per objective:
#     (textID, name, primary_difficulties, secondary_difficulties)
# textID = the game's objective-text string id (resolved from the string table @ image+0x24e2440); the client fires
# the location when that objective's on-screen status reaches 5 (= COMPLETE).  Reachability (see __init__):
#   * progression-eligible  <=> PRIMARY (required to beat the mission) on >=1 selected story difficulty.
#   * appears only as SECONDARY on a selected difficulty -> filler-only location (EXCLUDED; the player may skip it).
#   * doesn't appear on any selected difficulty                 -> no location.
# loc id = BASE_ID + 0x1500 + missionIdx*64 + (textID - OBJ_TEXTID_BASE).  Siberia is verified; the other missions
# are filled in from AP/objective_mapping.md as the user confirms their textIDs in-game.
OBJ_TEXTID_BASE = 1062
# a few objectives use a textID outside the 1062-1117 block (Space Station "Collect the crystals" = 2715).  Remap
# those onto unused slots (1118+) inside the per-mission 64-slot window so the (mission,textID) location key still fits.
# The DLL applies the identical remap (objEff) so the location ids line up.
OBJ_REMAP = {2715: 1118}
def _obj_offset(tid):
    return OBJ_REMAP.get(tid, tid) - OBJ_TEXTID_BASE
# textIDs are authoritative -- read from each mission's static objective table in the exe (stride 0x30, the same
# layout as the live runtime array).  Difficulties are from the user's wiki list.  "extra" objectives present in the
# table but not in the wiki are defaulted to filler-only (prim=_NONE) and flagged -- they still check when actually
# completed, they just never gate progression.  Space Station's objectives are filler regardless (terminal/goal gate).
OBJECTIVES = {
    "Siberia": [
        (1062, "Retrieve the Time Crystal",                _ALL, _NONE),
        (1063, "Escape through the Time Portal",           _ALL, _NONE),
        (1064, "Deactivate the communications dish",       _ALL, _NONE),
        (1065, "Investigate the secret digging site",      _ALL, _NONE),
        (1066, "Destroy the bio-hazard container",         _ALL, _NONE),
        (1067, "Restore power",                            _NH,  _NONE),
        (1068, "Access the top of the dam",                _NH,  _NONE),
        (1069, "Eliminate the gunship",                    _NH,  _NONE),
        (1070, "Burn all evidence in the filing cabinets", _H,   _EN),
        (1071, "Don't allow any mutants to survive",       _H,   _N),
    ],
    "Chicago": [
        (1062, "Retrieve the Time Crystal",                _ALL,  _NONE),
        (1063, "Escape through the Time Portal",           _ALL,  _NONE),
        (1072, "Drain the whisky barrels",                 _NH,   _E),
        (1073, "Rendezvous with your informant",           _ALL,  _NONE),
        (1074, "Protect Marco",                            _ALL,  _NONE),
        (1075, "Obtain the Nightclub pass card",           _NH,   _NONE),    # MINE-gated; Chicago's only mine (TNT) is Normal/Hard-only -> not an Easy objective
        (1078, "Prevent Tony's brother from escaping",     _H,    _EN),
        (1079, "Eliminate Big Tony",                       _ALL,  _NONE),
        (1080, "Minimize civilian casualties",             _NONE, _ALL),    # extra -> filler, confirm difficulty
    ],
    "Notre Dame": [
        (1062, "Retrieve the Time Crystal",                _ALL,  _NONE),
        (1063, "Escape through the Time Portal",           _ALL,  _NONE),
        (1086, "Rescue the maidens",                       _ALL,  _NONE),
        (1087, "Help the hunchback escape with the maiden",_NH,   _E),
        (1088, "Fight off the Portal Daemon",              _ALL,  _NONE),
        (1090, "Eliminate Jacque de la Morte",             _ALL,  _NONE),
    ],
    "Return to Planet X": [
        (1062, "Retrieve the Time Crystal",                _ALL,  _NONE),
        (1063, "Escape through the Time Portal",           _ALL,  _NONE),
        (1105, "Locate the crashed UFO",                   _ALL,  _NONE),
        (1106, "Shoot down the escaping UFOs",             _NH,   _NONE),   # = "Fight off the UFO fleet"; distinct from 1109
        (1107, "Find the UFO base",                        _NH,   _NONE),   # = "Locate the alien base"
        (1108, "Repel the beach attack",                   _ALL,  _NONE),
        (1109, "Fight off the raid on the crashed UFO",    _NONE, _ALL),    # confirmed real; secondary on all difficulties
    ],
    "NeoTokyo": [
        (1062, "Retrieve the Time Crystal",                _ALL,  _NONE),
        (1063, "Escape through the Time Portal",           _ALL,  _NONE),
        (1091, "Follow the hacker to the research area",   _ALL,  _NONE),
        (1092, "Obtain the hacker's password",             _NH,   _NONE),   # = "Kill the Hacker Chief"
        (1093, "Gather evidence of TimeSplitter research", _NH,   _NONE),   # = "Photograph the evidence"
        (1094, "Upload the evidence to the police server", _NH,   _NONE),
        (1095, "Deactivate the TimeSplitter machine",      _ALL,  _NONE),
        (1096, "Eliminate the gang leader",                _E,    _NONE),
        (1098, "Minimize civilian casualties",             _NONE, _NH),    # Normal/Hard only; earned by FINISHING the mission (MISSION_COMPLETE_OBJS)
    ],
    "Wild West": [
        (1062, "Retrieve the Time Crystal",                _ALL,  _NONE),
        (1063, "Escape through the Time Portal",           _ALL,  _NONE),
        (1110, "Rescue Ramona from jail",                  _ALL,  _NONE),
        (1111, "Destroy the wanted posters",               _NH,   _E),
        (1112, "Put out the fire and rescue the girl",     _ALL,  _NONE),
        (1113, "Eliminate the Colonel",                    _ALL,  _NONE),
    ],
    "Atom Smasher": [
        (1062, "Retrieve the Time Crystal",                _ALL,  _NONE),
        (1063, "Escape through the Time Portal",           _ALL,  _NONE),
        (1099, "Activate the reactor",                     _ALL,  _NONE),
        (1100, "Defuse the bombs",                         _ALL,  _NONE),
        (1101, "Minimize scientist casualties",            _NH,   _E),
        (1102, "Eliminate Khallos",                        _ALL,  _NONE),
    ],
    "Aztec": [
        (1062, "Retrieve the Time Crystal",                _ALL,  _NONE),
        (1063, "Escape through the Time Portal",           _ALL,  _NONE),
        (1103, "Find the lost temple",                     _ALL,  _NONE),
        (1104, "Defeat the golems",                        _ALL,  _NONE),
    ],
    "Robot Factory": [
        (1062, "Retrieve the Time Crystal",                _ALL,  _NONE),
        (1063, "Escape through the Time Portal",           _ALL,  _NONE),
        (1081, "Gain access to the inner processing area", _ALL,  _NONE),
        (1082, "Locate and collect the ElectroTool",       _ALL,  _NONE),
        (1083, "Overload the energy nodes",                _ALL,  _NONE),
        (1084, "Find the factory core",                    _ALL,  _NONE),
        (1085, "Defeat the Machinist",                     _ALL,  _NONE),
    ],
    "Space Station": [   # all filler regardless (terminal -- behind the goal + Time Crystal gate)
        (1115, "Activate the self destruct sequence",      _ALL,  _NONE),
        (1116, "Shoot down the incoming TimeSplitter ships",_ALL, _NONE),
        (1117, "Escape from the spacestation",             _ALL,  _NONE),
        (2715, "Collect the crystals",                     _ALL,  _NONE),   # textID outside 1062-1117 -> see OBJ_REMAP
    ],
}

# --- Per-objective weapon access rules (from AP/objective_logic.md, keyed by (mission, textID)) ---------------------
# Each rule is f(h)->bool where h("REG"/"EXP"/"MINE"/"FIRE") = the level provides an UNLOCKED weapon of that group and
# h("CAMERA"/"ELECTRO"/"GUNPOWDER") = that tool item is unlocked (the tool is no-shuffle, always in its own level).
# Objectives not listed default to REG|EXP. "Escape through the Time Portal" (1063) and the per-difficulty mission-
# completion locations are DERIVED in __init__ as the AND of that mission's primary objectives' rules ("able to
# complete all primaries"), so they are not listed here. Only applied when Weapon Gating is on (else rules = unlock).
ESCAPE_TEXTID = 1063
# Objectives you complete by FINISHING the mission (like Escape) rather than a one-off action -- their access requires
# completing the mission (all primaries) on the easiest selected difficulty they appear on, not just their own weapon
# rule. e.g. "Minimize civilian casualties" spans the whole run, so you only earn it by finishing the mission on N/H.
MISSION_COMPLETE_OBJS = frozenset({("NeoTokyo", 1098)})
# A weapon requirement that applies only on SOME difficulties -- on the others the objective is doable another way
# (Aztec's golems fall to other means on Easy; Wild West's Ramona can just be shot out of the jail on Easy). The
# difficulty comes from h.difficulty, which make_h() in __init__ and mission_coverable() below both set.
def only_on(diffs, rule):
    return lambda h, _d=diffs, _r=rule: (getattr(h, "difficulty", "Hard") not in _d) or _r(h)

OBJ_RULE_DEFAULT = lambda h: h("REG") or h("EXP")
OBJ_RULES = {
    ("Siberia", 1064):       lambda h: h("EXP") or h("MINE"),                          # Deactivate the communications dish
    ("Siberia", 1066):       lambda h: h("MINE") or h("PLASMA"),                       # Destroy the bio-hazard container (Plasma Autorifle's grenade also works)
    ("Siberia", 1070):       lambda h: h("EXP") or h("MINE") or h("FIRE"),             # Burn all evidence in the filing cabinets
    ("Chicago", 1075):       lambda h: h("MINE"),                                      # Obtain the Nightclub pass card
    ("NeoTokyo", 1093):      lambda h: h("CAMERA") and (h("REG") or h("EXP")),         # Gather evidence
    ("NeoTokyo", 1094):      lambda h: h("CAMERA") and (h("REG") or h("EXP")),         # Upload the evidence
    ("NeoTokyo", 1095):      lambda h: h("REG"),                                       # Deactivate the TimeSplitter machine
    # Ramona's jail: on NORMAL/HARD you have to blow it open -- the Gun Powder (the level's own answer) OR any explosive
    # / mine works (Rocket Launcher confirmed in-game, which only reaches Wild West via the weapon shuffle). On EASY you
    # just shoot her out, so nothing is required. This is also the level GATE (see OBJ_REQUIRES below).
    ("Wild West", 1110):     only_on(_NH, lambda h: h("GUNPOWDER") or h("EXP") or h("MINE")),   # Rescue Ramona from jail
    ("Wild West", 1112):     lambda h: h("REG"),                                       # Put out the fire and rescue the girl
    ("Atom Smasher", 1100):  lambda h: h("REG"),                                       # Defuse the bombs
    # Golems fall to "other means" on EASY; on NORMAL/HARD they REQUIRE an explosive or mine (the level hands you a
    # Grenade Launcher for exactly this).
    ("Aztec", 1104):         only_on(_NH, lambda h: h("EXP") or h("MINE")),            # Defeat the golems
    ("Robot Factory", 1083): lambda h: h("ELECTRO") and (h("REG") or h("EXP")),        # Overload the energy nodes
    ("Robot Factory", 1084): lambda h: h("ELECTRO") and (h("REG") or h("EXP")),        # Find the factory core
    ("Robot Factory", 1085): lambda h: h("ELECTRO") and (h("REG") or h("EXP")),        # Defeat the Machinist
}
# Inter-objective dependencies: some objectives can't be completed without ALSO being able to complete another (e.g.
# you can't stop the mutants surviving without destroying the bio-hazard container they spawn from). A dependent
# objective's access rule ANDs in each required objective's rule. Keyed (mission, textID) -> [required textIDs].
OBJ_REQUIRES = {
    ("Siberia", 1071): [1066],   # "Don't allow any mutants to survive" requires "Destroy the bio-hazard container" (MINE)
    # Wild West's jail is the LEVEL GATE: the rest of the map stays locked until Ramona is freed, so every other
    # objective depends on it -- which on Normal/Hard transitively requires the Gun Powder (rule 1110 above), and on
    # Easy costs nothing extra.
    ("Wild West", 1062): [1110],   # Retrieve the Time Crystal
    ("Wild West", 1063): [1110],   # Escape through the Time Portal
    ("Wild West", 1111): [1110],   # Destroy the wanted posters
    ("Wild West", 1112): [1110],   # Put out the fire and rescue the girl
    ("Wild West", 1113): [1110],   # Eliminate the Colonel
}
def objective_rule(mission, textid, _seen=None):
    base = OBJ_RULES.get((mission, textid), OBJ_RULE_DEFAULT)
    reqs = OBJ_REQUIRES.get((mission, textid))
    if not reqs:
        return base
    seen = (_seen or frozenset()) | {textid}
    rules = (base, *(objective_rule(mission, r, seen) for r in reqs if r not in seen))
    return lambda h, _rules=rules: all(r(h) for r in _rules)

# --- Weapons as items (valid weapon slots 1..0x29, slot 0/40/41 = Unarmed) ---
# slot = the per-pawn weapon index (pawn[slot+0x400] = "has weapon"). The DLL gates/shuffles by slot.
WEAPONS = {
    1: "Silenced Pistol", 2: "Silenced Pistol (x2)", 3: "Silenced Luger", 4: "Silenced Luger (x2)",
    5: "Luger Pistol", 6: "Luger Pistol (x2)", 7: "Garrett Revolver", 8: "Garrett Revolver (x2)",
    9: "Tactical 12-Gauge", 10: "Minigun", 11: "Sniper Rifle", 12: "Vintage Rifle", 13: "Flamethrower",
    14: "Fire Extinguisher", 15: "Soviet S47", 16: "Soviet S47 (x2)", 17: "ElectroTool", 18: "Scifi Handgun",
    19: "Rocket Launcher", 20: "Homing Launcher", 21: "Lasergun", 22: "Plasma Autorifle", 23: "Grenade Launcher",
    24: "Proximity Mine", 25: "Remote Mine", 26: "Timed Mine", 27: "TNT", 28: "Tommy Gun", 29: "Tommy Gun (x2)",
    30: "SBP90 Machinegun", 31: "SBP90 Machinegun (x2)", 32: "Shotgun", 33: "Shotgun (x2)", 34: "Brick",
    35: "Digital Camera", 36: "Temporal Uplink", 37: "Crossbow", 38: "Gun Powder",
}
# BASELINE: always usable, NEVER an AP item and never shuffled -- the Temporal Uplink (story tool, in every level) and
# the Fire Extinguisher (a non-combat utility used by no objective). Fists (slot 0) are implicitly baseline too.
WEAPON_BASELINE = frozenset({14, 36})
# Akimbo "(x2)" dual-wields are their OWN weapons: distinct Gun-IDs slots placed as their own floor pickups (confirmed
# in-game -- e.g. Atom Smasher drops slot 2 Silenced Pistol x2 AND slot 16 Soviet S47 x2). So they are first-class AP
# items and shuffle like any other gun. Each is a REGULAR-group weapon (its base is REG) -- added to WEAPON_REG below.
WEAPON_AKIMBO = frozenset({2, 4, 6, 8, 16, 29, 31, 33})
# NO-SHUFFLE: gated AP items that must keep their identity through a shuffle -- special-asset / story weapons a gun must
# never turn into (and that must never turn into a gun): ElectroTool (17), Brick (34), Digital Camera (35), Crossbow
# (37, Aztec's only weapon), Gun Powder (38). They ARE gated items (so they can gate objectives); they just self-map.
WEAPON_NOSHUFFLE = frozenset({17, 34, 35, 37, 38})

# Functional groups for the objective access logic ("any <group> weapon"). Plasma Autorifle (22) is in BOTH Regular
# (bullets) and Explosive (grenade ammo). Mines place; explosives launch; fire = flamethrower.
WEAPON_REG  = frozenset({1, 3, 5, 7, 9, 10, 11, 12, 15, 18, 21, 22, 28, 30, 32, 37}) | WEAPON_AKIMBO  # akimbo (x2) are REG
WEAPON_EXP  = frozenset({19, 20, 22, 23})
WEAPON_MINE = frozenset({24, 25, 26, 27})
WEAPON_FIRE = frozenset({13})
# Single-weapon group: the Plasma Autorifle's grenade secondary destroys things that otherwise need a PLACED mine
# (Siberia's bio-hazard container). Unique to this weapon, so it is NOT a widening of EXP. Kept as a GROUP (not a
# WEAPON_TOOL_SLOT entry) so h("PLASMA") stays level-aware -- it must be obtainable in that mission under the shuffle.
WEAPON_PLASMA = frozenset({22})
WEAPON_GROUPS = {"REG": WEAPON_REG, "EXP": WEAPON_EXP, "MINE": WEAPON_MINE, "FIRE": WEAPON_FIRE,
                 "PLASMA": WEAPON_PLASMA}
WEAPON_TOOL_SLOT = {"CAMERA": 35, "ELECTRO": 17, "GUNPOWDER": 38}   # objective tools (gated items, kept by their level)

WEAPON_ITEM_ID_BASE = 0xA00   # weapon item id = BASE_ID + 0xA00 + slot; free block between filler (0x901) and locations (0x1000)

def weapon_gated_slots():
    """Weapon slots that are AP items = every defined weapon except the baseline (always-on Temporal Uplink / Fire
    Extinguisher). Includes the akimbo (x2) dual-wields (their own pickups), the objective tools (ElectroTool/Camera/
    Gun Powder), Crossbow and Brick. Each gated slot is one received item (keyed by WEAPON IDENTITY)."""
    return [s for s in sorted(WEAPONS) if s not in WEAPON_BASELINE]

WEAPON_ITEM_OF = {s: f"{WEAPONS[s]} (Weapon)" for s in weapon_gated_slots()}   # weapon identity -> AP item name
WEAPON_ITEM_NAMES = frozenset(WEAPON_ITEM_OF.values())

# ---- weapon shuffle SCOPE -------------------------------------------------------------------------------------------
# Scope decides WHICH weapons may swap with each other. Every scope produces the same shape the client consumes: a
# {slot: weapon_identity} map (one PER LEVEL for within_level). Slots left out of a map are identity.
# A PARTITION of the shuffle pool. Plasma Autorifle (22) is in both WEAPON_REG and WEAPON_EXP, so it is counted REG here
# (it plays as a regular gun with a grenade secondary), which leaves EXP as the three true launchers.
WEAPON_SHUFFLE_CLASSES = {
    "MINE": WEAPON_MINE,
    "EXP":  WEAPON_EXP - WEAPON_REG,
    "FIRE": WEAPON_FIRE,
    "REG":  WEAPON_REG,
}


def _weapon_shuffle_pool():
    """Slots that may take part in a shuffle at all (gated, minus the never-shuffled tools/brick/crossbow)."""
    return [s for s in weapon_gated_slots() if s not in WEAPON_NOSHUFFLE]


def _derange(rng, group):
    """{orig: new} permuting `group` with NO fixed point, so every shuffled pickup visibly becomes a different gun.
    Fewer than 2 members cannot be deranged -> identity (e.g. FIRE is just the Flamethrower)."""
    group = list(group)
    if len(group) < 2:
        return {s: s for s in group}
    shuffled = list(group)
    for _ in range(1000):                              # derangements exist for n>=2; ~e tries on average
        rng.shuffle(shuffled)
        if all(o != n for o, n in zip(group, shuffled)):
            break
    return dict(zip(group, shuffled))


def weapon_remap_same_class(rng):
    """Scope 'same_class': a weapon only ever becomes another weapon of its OWN class -- regulars swap with regulars,
    launchers with launchers, mines with mines. {slot: weapon_identity} for 0..63."""
    remap = {s: s for s in range(64)}
    pool = set(_weapon_shuffle_pool())
    for cls in ("MINE", "EXP", "FIRE", "REG"):
        remap.update(_derange(rng, sorted(pool & set(WEAPON_SHUFFLE_CLASSES[cls]))))
    return remap


def weapon_remap_within_level(rng):
    """Scope 'within_level': {mission: {slot: weapon_identity}} -- each level permutes only its OWN pickup weapons.
    The SET of weapons a level offers is therefore unchanged (only which pickup yields which), so a level can never be
    stranded. The client re-applies the map for the level being loaded, which is what lets the SAME slot be a different
    gun in a different level (impossible with one global map, since levels share slots)."""
    pool = set(_weapon_shuffle_pool())
    return {m: _derange(rng, sorted(s for s in LEVEL_WEAPON_SLOTS.get(m, ()) if s in pool)) for m in STORY}


def weapon_global_remap(rng):
    """Deterministic slot->weapon permutation, shuffled WITHIN functional groups so every mission stays completable:
    mines shuffle only among mines, the rest of the shuffleable guns ('big list', now including the akimbo x2 variants)
    among themselves; baseline, no-shuffle items, and slot 0 map to themselves. {slot: weapon_identity} for 0..63.
    Each group is a DERANGEMENT -- no weapon maps to itself -- so every shuffled pickup visibly becomes a *different*
    gun (no accidental "this one didn't randomize"). Capability B; the DLL applies it to floor pickups, the player
    loadout, and (with C) enemies."""
    remap = {s: s for s in range(64)}
    pool = [s for s in weapon_gated_slots() if s not in WEAPON_NOSHUFFLE]
    mines = [s for s in pool if s in WEAPON_MINE]
    big = [s for s in pool if s not in WEAPON_MINE]
    for group in (big, mines):
        if len(group) < 2:
            continue                                   # a lone element can't be deranged -> leave it identity
        shuffled = group[:]
        for _ in range(1000):                          # reject fixed points; derangements exist for n>=2, ~e tries avg
            rng.shuffle(shuffled)
            if all(o != n for o, n in zip(group, shuffled)):
                break
        for orig, new in zip(group, shuffled):
            remap[orig] = new
    return remap

# --- per-level weapon sets (live precache dump + the TS2 wikis for early/difficulty; 2026-06-29) ---------------------
# The fixed pickup-slot set each mission provides (akimbo collapses to its base in the logic; slot 39 dropped; Notre
# Dame's Vintage Rifle [12] and Wild West's Grenade Launcher [23] are precached-but-not-obtainable -> removed).
LEVEL_WEAPON_SLOTS = {
    "Siberia":            frozenset({1, 9, 11, 13, 14, 15, 26, 36}),
    "Chicago":            frozenset({3, 12, 27, 28, 29, 32, 36}),
    "Notre Dame":         frozenset({5, 6, 32, 36}),
    "Return to Planet X": frozenset({18, 20, 22, 36}),
    "NeoTokyo":           frozenset({1, 11, 18, 30, 35, 36}),
    "Wild West":          frozenset({7, 8, 12, 36, 38}),
    "Atom Smasher":       frozenset({1, 2, 11, 14, 15, 16, 25, 36}),
    "Aztec":              frozenset({5, 12, 23, 36, 37}),
    "Robot Factory":      frozenset({17, 18, 20, 21, 22, 36}),
    "Space Station":      frozenset({10, 22, 36}),
}
# EARLY slots: obtainable at the start / early in the mission. Normal logic requires one of these (as a combat weapon)
# so the player is armed from the off rather than fisting until a deep pickup. (Slots not listed are "late".)
LEVEL_EARLY_SLOTS = {
    "Siberia": frozenset({1, 11, 26}), "Chicago": frozenset({3, 12}), "Notre Dame": frozenset({32}),
    "Return to Planet X": frozenset({18}), "NeoTokyo": frozenset({1, 11}), "Wild West": frozenset({7, 12}),
    "Atom Smasher": frozenset({1}), "Aztec": frozenset({5, 37}), "Robot Factory": frozenset({18, 22}),
    "Space Station": frozenset({22}),
}
# Slots obtainable ONLY on Normal/Hard (absent on Easy) -> they don't count for an Easy check.
LEVEL_NH_ONLY_SLOTS = {
    "Siberia": frozenset({9, 13}), "Chicago": frozenset({27, 32}), "Aztec": frozenset({12, 23}),
}
# The PRIMARY weapon each mission hands you first: precollected (remap-aware) when the mission is a STARTING unlock, so
# you begin armed for the mission(s) you start with. The SECONDARY weapon is additionally required to complete the
# mission on HARD (one gun is brutal on Hard). Both are pickup SLOTS in the mission -- under shuffle the requirement
# follows whatever gun lands at that slot. Space Station has no PRIMARY: it can never be a starting unlock (it sits
# behind the Time Crystal goal gate), so there is no start-of-seed loadout to seed.
LEVEL_PRIMARY_SLOT = {
    "Siberia": 1, "Chicago": 3, "Notre Dame": 32, "Return to Planet X": 18, "NeoTokyo": 1,
    "Wild West": 7, "Atom Smasher": 1, "Aztec": 37, "Robot Factory": 18,
}
# Space Station's only combat weapons are the Plasma Autorifle (22, its early pickup) and the Minigun (10) -- slot 36 is
# the baseline Temporal Uplink -- so the Minigun is the only possible second gun for the final mission on Hard.
LEVEL_HARD_SECONDARY = {
    "Siberia": 11, "Chicago": 12, "Notre Dame": 5, "Return to Planet X": 22, "NeoTokyo": 11,
    "Wild West": 12, "Atom Smasher": 2, "Aztec": 5, "Robot Factory": 22, "Space Station": 10,
}
WEAPON_COMBAT = WEAPON_REG | WEAPON_EXP | WEAPON_MINE | WEAPON_FIRE   # tools (Camera/ElectroTool/Gun Powder) are NOT combat

def level_category_items(mission, group, remap=None, difficulty="Hard"):
    """AP weapon-item names that satisfy "any <group> weapon" in <mission> on <difficulty>: each pickup slot the level
    provides (Normal/Hard-only slots dropped on Easy) yields weapon remap[slot]; if that's a gated item in <group>, its
    item unlocks it. Late slots still count for objectives (you pick them up along the way). Akimbo -> base."""
    remap = remap or {}
    gset = WEAPON_GROUPS[group]
    nh_only = LEVEL_NH_ONLY_SLOTS.get(mission, frozenset())
    items = set()
    for s in LEVEL_WEAPON_SLOTS.get(mission, ()):
        if difficulty == "Easy" and s in nh_only:
            continue                              # not obtainable on Easy
        w = remap.get(s, s)                       # weapon identity actually yielded at this pickup
        if w not in WEAPON_ITEM_OF:
            continue                              # baseline (Uplink/Fire Extinguisher) -> no own item
        if w in gset:
            items.add(WEAPON_ITEM_OF[w])
    return frozenset(items)

def level_early_combat_items(mission, remap=None, difficulty="Hard"):
    """AP weapon-item names for an EARLY combat weapon (REG/EXP/MINE/FIRE) available on <difficulty>. A mission is in
    logic only if one of these is unlocked -- the 'armed from the start' guarantee. Tools never count here."""
    remap = remap or {}
    nh_only = LEVEL_NH_ONLY_SLOTS.get(mission, frozenset())
    items = set()
    for s in LEVEL_EARLY_SLOTS.get(mission, ()):
        if difficulty == "Easy" and s in nh_only:
            continue
        w = remap.get(s, s)
        if w not in WEAPON_ITEM_OF:
            continue
        if w in WEAPON_COMBAT:
            items.add(WEAPON_ITEM_OF[w])
    return frozenset(items)

def mission_coverable(mission, remap, difficulty):
    """With ALL of the mission's (shuffled) weapons available on <difficulty>, is the mission completable -- armed (an
    early combat weapon exists) AND every objective that appears on that difficulty satisfiable? Used to re-roll a
    weapon shuffle that would otherwise strand a mission (e.g. a cross-category shuffle leaving Wild West with no
    Regular weapon for 'Put out the fire and rescue the girl')."""
    if not level_early_combat_items(mission, remap, difficulty):
        return False
    def h(tok):
        if tok in WEAPON_GROUPS:
            return bool(level_category_items(mission, tok, remap, difficulty))
        return True   # tools are no-shuffle -> always present in their own level
    h.difficulty = difficulty
    for tid, _name, prim, sec in OBJECTIVES.get(mission, []):
        if tid != ESCAPE_TEXTID and difficulty in (prim | sec) and not objective_rule(mission, tid)(h):
            return False
    return True


def trophy_events():
    """The 66 trophy-bearing events (45 arcade matches + 21 challenges), in catalog order: (kind, group, name)."""
    out = []
    for league, series in ARCADE.items():
        for sname, matches in series.items():
            for m in matches:
                out.append(("arcade", f"{league} / {sname}", m))
    for cat, chs in CHALLENGES.items():
        for c in chs:
            out.append(("challenge", cat, c))
    return out


TROPHY_EVENTS = trophy_events()                 # 66
TROPHY_EVENT_NAMES = [e[2] for e in TROPHY_EVENTS]
UNIT_NAMES = TROPHY_EVENT_NAMES + list(STORY)  # 76 IN-GAME names; the CLIENT matches menu labels by these -- do NOT rename
assert len(UNIT_NAMES) == len(set(UNIT_NAMES)), "duplicate unit name"

# ---- AP display names (what shows in Archipelago) -- DECOUPLED from the in-game names above (the client still
#      uses those verbatim to match menu entries & works in numeric ids, so display names are free to be verbose).
#      A unit's DISPLAY base carries league/series context; the ITEM name appends " Unlocked"; LOCATION names
#      append the trophy tier / story difficulty.  Ids stay index-based and stable regardless of names.
def _display_base(kind, group, ingame):
    if kind == "arcade":                       # group = "<League> / <Series>"
        league, series = group.split(" / ")
        return f"{league} - {series} - {ingame}"
    if kind == "challenge":                    # group = category
        return f"Challenge - {group} - {ingame}"
    return ingame                              # story: bare mission name (per user: "<Mission> Unlocked")

DISPLAY_OF = {n: _display_base(k, g, n) for (k, g, n) in TROPHY_EVENTS}
DISPLAY_OF.update({m: _display_base("story", "", m) for m in STORY})
ITEM_OF = {n: f"{DISPLAY_OF[n]} Unlocked" for n in UNIT_NAMES}   # in-game name -> AP item name
ITEM_NAMES = [ITEM_OF[n] for n in UNIT_NAMES]                    # all 76 AP item names, in unit order
STORY_ITEMS = [ITEM_OF[m] for m in STORY]                       # story AP item names, in STORY order
FINAL_STORY_ITEM = ITEM_OF[FINAL_STORY_MISSION]                 # "Space Station Unlocked"

# ---- item ids: one unlock per unit + filler (ids index-based, unchanged) ----
item_name_to_id = {ITEM_OF[n]: BASE_ID + i for i, n in enumerate(UNIT_NAMES)}
item_name_to_id[TIME_CRYSTAL_ITEM] = BASE_ID + 0x900   # Time Crystal -- client counts these to gate the final
item_name_to_id[FILLER_ITEM] = BASE_ID + 0x901     # junk filler (Banana)
for _ws in weapon_gated_slots():                   # weapons-as-items: one item per gated weapon slot
    item_name_to_id[WEAPON_ITEM_OF[_ws]] = BASE_ID + WEAPON_ITEM_ID_BASE + _ws
for _i, _bn in enumerate(BONUS_ITEMS):                  # bonus filler items (Health/Armor/Ammo Pack)
    item_name_to_id[_bn] = BASE_ID + BONUS_ITEM_ID_BASE + _i
for _i, _tn in enumerate(TRAP_ITEMS):                   # trap items
    item_name_to_id[_tn] = BASE_ID + TRAP_ITEM_ID_BASE + _i
assert len(set(item_name_to_id.values())) == len(item_name_to_id), "duplicate item id"  # id-collision guard

# ---- location ids (ALL allocated; options pick which to create) ----
#   trophy events:  BASE + 0x1000 + eventIdx*4 + tierIdx     ("<display> (<tier>)")        66*4 = 264
#   story:         BASE + 0x1400 + missionIdx*3 + diffIdx    ("<display> (<difficulty>)") 10*3 = 30
location_name_to_id = {}
for ei, (_, _, name) in enumerate(TROPHY_EVENTS):
    for ti, tier in enumerate(TROPHY_TIERS):
        location_name_to_id[f"{DISPLAY_OF[name]} ({tier})"] = BASE_ID + 0x1000 + ei * 4 + ti
for mi, m in enumerate(STORY):
    for di, d in enumerate(STORY_DIFFICULTIES):
        location_name_to_id[f"{DISPLAY_OF[m]} ({d})"] = BASE_ID + 0x1400 + mi * 3 + di
#   objectives:    BASE + 0x1500 + missionIdx*64 + (textID - OBJ_TEXTID_BASE)   ("<Mission> - <objective>")
def objective_location_name(mission, objname):
    return f"{DISPLAY_OF[mission]} - {objname}"

OBJ_LOC_INFO = {}   # AP location name -> (textID, primary_difficulties, secondary_difficulties, mission)
for _m, _objs in OBJECTIVES.items():
    _mi = STORY.index(_m)
    for _tid, _oname, _prim, _sec in _objs:
        _ln = objective_location_name(_m, _oname)
        location_name_to_id[_ln] = BASE_ID + 0x1500 + _mi * 64 + _obj_offset(_tid)
        OBJ_LOC_INFO[_ln] = (_tid, _prim, _sec, _m)

_n_obj = sum(len(o) for o in OBJECTIVES.values())
assert len(item_name_to_id) == len(UNIT_NAMES) + 2 + len(weapon_gated_slots()) + len(BONUS_ITEMS) + len(TRAP_ITEMS), \
    "item name collision"   # units + Time Crystal + filler + weapons + bonus packs + trap items
assert len(location_name_to_id) == 66 * len(TROPHY_TIERS) + len(STORY) * len(STORY_DIFFICULTIES) + _n_obj, "location name collision"
assert len(set(location_name_to_id.values())) == len(location_name_to_id), "duplicate location id"

# helpers for the World (return AP display names; arg is the IN-GAME name)
def trophy_locations(name):
    return [f"{DISPLAY_OF[name]} ({t})" for t in TROPHY_TIERS]

def story_locations(mission):
    return [f"{DISPLAY_OF[mission]} ({d})" for d in STORY_DIFFICULTIES]

def objective_locations(mission):
    return [objective_location_name(mission, o[1]) for o in OBJECTIVES.get(mission, [])]

def objective_primary_mask():
    """{ str(missionIdx*64 + textID-OBJ_TEXTID_BASE): primary-difficulty bitmask }  (Easy=1 Normal=2 Hard=4).
    Sent in slot_data so the client can auto-fire an objective's check on a mission clear when it is PRIMARY
    (guaranteed done) at the cleared difficulty -- a safety net for level-end objectives the live hook may miss."""
    bit = {"Easy": 1, "Normal": 2, "Hard": 4}
    out = {}
    for m, objs in OBJECTIVES.items():
        mi = STORY.index(m)
        for tid, _name, prim, _sec in objs:
            mask = 0
            for d in prim:
                mask |= bit[d]
            out[str(mi * 64 + _obj_offset(tid))] = mask
    return out

item_name_groups = {
    "Arcade":    {ITEM_OF[e[2]] for e in TROPHY_EVENTS if e[0] == "arcade"},
    "Challenge": {ITEM_OF[e[2]] for e in TROPHY_EVENTS if e[0] == "challenge"},
    "Story":     {ITEM_OF[m] for m in STORY},
    "Weapons":   set(WEAPON_ITEM_OF.values()),
    "Traps":     set(TRAP_ITEMS),
    "Bonus":     set(BONUS_ITEMS),
}

# --- game trophy-table index map (for the CLIENT, not generation) ---
# The 66-entry trophy record table (profile +0x11cc) is indexed by a FIXED record order baked into the EXE, NOT
# by any seriesIndex*3+matchPos formula. The game's profile aggregator walks record idx 0..65 calling
# FUN_141106fc0(idx) = PTR_DAT_142507660[idx] to get record idx's match. That order is internally permuted in BOTH
# halves -- e.g. arcade "Maximus" is reversed (41/40/39) and challenge categories interleave (Behead The Undead at
# 2,15,16,17). So instead of a formula we read the 66 records verbatim from the EXE static data and invert. Every
# name is unique and all 66 events are covered.
TROPHY_TABLE_ORDER = [   # record idx 0..65, straight from Homefront2_Release.exe PTR_DAT_142507660[idx] head name id
    # --- challenges (0..20) ---
    'Pane in the Neck', 'Bricking it', 'Stain Removal', 'Silent but Deadly', 'Trouble at the Docks',
    'Escape from NeoTokyo', 'Gone Bananas', 'Monkey Business', 'Playing With Fire', "Take 'em Down", 'Fall Out',
    'Pick Yer Piece', 'Hit Me Baby One Morgue Time', 'Badass Buspass Impasse', 'But Where do the Batteries Go?',
    'Fight Off The Living Dead', 'Day of the Dammed', "Sergio's Last Stand", 'Simian Shootout', 'Monkey Mayhem',
    'Dam Bursters',
    # --- arcade Amateur (21..35) ---
    'Adios Amigos!', 'Casualty', 'Top Shot', 'Shrinking from the Cold', 'Chastity Chased', 'Scrap Metal',
    'Night Shift', 'Spoils of War', 'Demolition Derby', 'Monkey Immolation', 'Burns Department', 'Disco Inferno',
    'Club Soda', 'Station Stand', 'Men In Gray',
    # --- arcade Honorary (36..50) ---
    'Hack a Hacker', 'Rice Cracker Rush', 'Superfly Lady', 'R109 Beta', 'Killer Queen', 'Cold Corpse Caper',
    'Baking for the Taking', 'Brace Yourself', 'Starship Whoopers', 'Snow Business', 'Chinese Burns', 'Rocket Man',
    'Someone Has Got to Pay...', 'Time to Split', "Can't Handle This",
    # --- arcade Elite (51..65) ---
    'Babes in the Woods', 'Double Bill', 'Nikki Jinki Bricky', 'Bags of Fun', "They're Not Pets!", 'Nice Threads',
    'Golden Thighs', "If I'm Ugly - You Smell!", 'Golem Guru', "Can't Please Everyone...", "Hangar Hat's Off!",
    'Big Top Blowout', 'Aztec the Dino Hunter', 'Half Death', 'Dead Fraction',
]
TROPHY_EVENT_INDEX = {name: idx for idx, name in enumerate(TROPHY_TABLE_ORDER)}  # event name -> trophy-table index (0..65)

# safety net: the baked record order MUST be exactly the arcade+challenge match-name set (catches any name drift)
assert len(TROPHY_TABLE_ORDER) == 66, "trophy record table must list 66 events"
assert set(TROPHY_TABLE_ORDER) == set(TROPHY_EVENT_NAMES), \
    "TROPHY_TABLE_ORDER vs ARCADE/CHALLENGES name mismatch: %r" % (set(TROPHY_TABLE_ORDER) ^ set(TROPHY_EVENT_NAMES),)
