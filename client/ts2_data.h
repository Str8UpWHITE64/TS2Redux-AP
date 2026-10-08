#pragma once
// GENERATED from worlds/ts2redux/data.py -- do not edit by hand (run gen_header.py).
// Keeps the client's ids/names in lockstep with the apworld.
#include <cstdint>

namespace ts2 {
static const int64_t BASE_ID = 0x54533200LL;
static const int64_t FILLER_ID = 0x54533b01LL;  // Banana (junk)
static const int64_t TIME_CRYSTAL_ID = 0x54533b00LL;  // Time Crystal (collect time_crystals_required of these to unlock the final stage)

// --- bonus + trap items: the client maps a received item id -> an effect ---
// bonus = one-time FULL refill, granted on the next in-mission/challenge tick (queued if received in a menu).
struct BonusItem { const char* name; int64_t id; };
static const BonusItem BONUS_ITEMS[] = {
    { "Health Pack", 0x54533b02LL },
    { "Armor Pack", 0x54533b03LL },
    { "Ammo Pack", 0x54533b04LL },
};
static const int BONUS_ITEM_COUNT = 3;
// trap = enable a TS2 cheat for a few seconds -- STORY missions only. cheatBit = bit in the cheat-active
// bitfield at (g_base + CHEAT_ACTIVE_OFF): set it to switch the cheat on, clear it to end the trap.
static const uintptr_t CHEAT_ACTIVE_OFF = 0x3f2f998;  // uint bitfield; bit = cheat catalog index
// spawnTime=1 => cheat only takes effect on a level (re)load -> arm it while OUTSIDE a level (next entry).
struct TrapItem { const char* name; int64_t id; int cheatBit; int spawnTime; };
static const TrapItem TRAP_ITEMS[] = {
    { "Invisible Enemies Trap", 0x54533b10LL, 8, 1 },
    { "Big Head Trap", 0x54533b11LL, 1, 0 },
    { "Small Head Trap", 0x54533b12LL, 2, 0 },
    { "Big Hands Trap", 0x54533b13LL, 3, 0 },
    { "Fat Enemies Trap", 0x54533b14LL, 6, 0 },
    { "Rotating Heads Trap", 0x54533b15LL, 11, 0 },
    { "Cardboard Enemies Trap", 0x54533b16LL, 10, 0 },
    { "Slow Death Trap", 0x54533b17LL, 9, 0 },
};
static const int TRAP_ITEM_COUNT = 8;

// units (mission or trophy-event). itemId = BASE_ID + index.  index: events 0..65, story 66..75
struct Unit { const char* name; int index; int kind; };  // kind 0=arcade 1=challenge 2=story
static const Unit UNITS[] = {
    { "Adios Amigos!", 0, 0 },
    { "Casualty", 1, 0 },
    { "Top Shot", 2, 0 },
    { "Chastity Chased", 3, 0 },
    { "Shrinking from the Cold", 4, 0 },
    { "Scrap Metal", 5, 0 },
    { "Night Shift", 6, 0 },
    { "Spoils of War", 7, 0 },
    { "Demolition Derby", 8, 0 },
    { "Monkey Immolation", 9, 0 },
    { "Disco Inferno", 10, 0 },
    { "Burns Department", 11, 0 },
    { "Club Soda", 12, 0 },
    { "Station Stand", 13, 0 },
    { "Men In Gray", 14, 0 },
    { "Cold Corpse Caper", 15, 0 },
    { "Killer Queen", 16, 0 },
    { "R109 Beta", 17, 0 },
    { "Baking for the Taking", 18, 0 },
    { "Brace Yourself", 19, 0 },
    { "Starship Whoopers", 20, 0 },
    { "Chinese Burns", 21, 0 },
    { "Snow Business", 22, 0 },
    { "Rocket Man", 23, 0 },
    { "Someone Has Got to Pay...", 24, 0 },
    { "Time to Split", 25, 0 },
    { "Can't Handle This", 26, 0 },
    { "Hack a Hacker", 27, 0 },
    { "Rice Cracker Rush", 28, 0 },
    { "Superfly Lady", 29, 0 },
    { "Babes in the Woods", 30, 0 },
    { "Double Bill", 31, 0 },
    { "Nikki Jinki Bricky", 32, 0 },
    { "If I'm Ugly - You Smell!", 33, 0 },
    { "Golem Guru", 34, 0 },
    { "Golden Thighs", 35, 0 },
    { "Hangar Hat's Off!", 36, 0 },
    { "Can't Please Everyone...", 37, 0 },
    { "Big Top Blowout", 38, 0 },
    { "Bags of Fun", 39, 0 },
    { "They're Not Pets!", 40, 0 },
    { "Nice Threads", 41, 0 },
    { "Aztec the Dino Hunter", 42, 0 },
    { "Half Death", 43, 0 },
    { "Dead Fraction", 44, 0 },
    { "Pane in the Neck", 45, 1 },
    { "Bricking it", 46, 1 },
    { "Stain Removal", 47, 1 },
    { "Fight Off The Living Dead", 48, 1 },
    { "Sergio's Last Stand", 49, 1 },
    { "Day of the Dammed", 50, 1 },
    { "Silent but Deadly", 51, 1 },
    { "Trouble at the Docks", 52, 1 },
    { "Escape from NeoTokyo", 53, 1 },
    { "Gone Bananas", 54, 1 },
    { "Monkey Business", 55, 1 },
    { "Playing With Fire", 56, 1 },
    { "Take 'em Down", 57, 1 },
    { "Fall Out", 58, 1 },
    { "Pick Yer Piece", 59, 1 },
    { "Badass Buspass Impasse", 60, 1 },
    { "But Where do the Batteries Go?", 61, 1 },
    { "Hit Me Baby One Morgue Time", 62, 1 },
    { "Simian Shootout", 63, 1 },
    { "Monkey Mayhem", 64, 1 },
    { "Dam Bursters", 65, 1 },
    { "Siberia", 66, 2 },
    { "Chicago", 67, 2 },
    { "Notre Dame", 68, 2 },
    { "Return to Planet X", 69, 2 },
    { "NeoTokyo", 70, 2 },
    { "Wild West", 71, 2 },
    { "Atom Smasher", 72, 2 },
    { "Aztec", 73, 2 },
    { "Robot Factory", 74, 2 },
    { "Space Station", 75, 2 },
};
static const int UNIT_COUNT = 76;

// trophy events: ei = apworld order (0..65), gameIdx = in-game trophy-table index (0..65)
struct TrophyEvent { const char* name; int ei; int gameIdx; };
static const TrophyEvent TROPHY_EVENTS[] = {
    { "Adios Amigos!", 0, 21 },
    { "Casualty", 1, 22 },
    { "Top Shot", 2, 23 },
    { "Chastity Chased", 3, 25 },
    { "Shrinking from the Cold", 4, 24 },
    { "Scrap Metal", 5, 26 },
    { "Night Shift", 6, 27 },
    { "Spoils of War", 7, 28 },
    { "Demolition Derby", 8, 29 },
    { "Monkey Immolation", 9, 30 },
    { "Disco Inferno", 10, 32 },
    { "Burns Department", 11, 31 },
    { "Club Soda", 12, 33 },
    { "Station Stand", 13, 34 },
    { "Men In Gray", 14, 35 },
    { "Cold Corpse Caper", 15, 41 },
    { "Killer Queen", 16, 40 },
    { "R109 Beta", 17, 39 },
    { "Baking for the Taking", 18, 42 },
    { "Brace Yourself", 19, 43 },
    { "Starship Whoopers", 20, 44 },
    { "Chinese Burns", 21, 46 },
    { "Snow Business", 22, 45 },
    { "Rocket Man", 23, 47 },
    { "Someone Has Got to Pay...", 24, 48 },
    { "Time to Split", 25, 49 },
    { "Can't Handle This", 26, 50 },
    { "Hack a Hacker", 27, 36 },
    { "Rice Cracker Rush", 28, 37 },
    { "Superfly Lady", 29, 38 },
    { "Babes in the Woods", 30, 51 },
    { "Double Bill", 31, 52 },
    { "Nikki Jinki Bricky", 32, 53 },
    { "If I'm Ugly - You Smell!", 33, 58 },
    { "Golem Guru", 34, 59 },
    { "Golden Thighs", 35, 57 },
    { "Hangar Hat's Off!", 36, 61 },
    { "Can't Please Everyone...", 37, 60 },
    { "Big Top Blowout", 38, 62 },
    { "Bags of Fun", 39, 54 },
    { "They're Not Pets!", 40, 55 },
    { "Nice Threads", 41, 56 },
    { "Aztec the Dino Hunter", 42, 63 },
    { "Half Death", 43, 64 },
    { "Dead Fraction", 44, 65 },
    { "Pane in the Neck", 45, 0 },
    { "Bricking it", 46, 1 },
    { "Stain Removal", 47, 2 },
    { "Fight Off The Living Dead", 48, 15 },
    { "Sergio's Last Stand", 49, 17 },
    { "Day of the Dammed", 50, 16 },
    { "Silent but Deadly", 51, 3 },
    { "Trouble at the Docks", 52, 4 },
    { "Escape from NeoTokyo", 53, 5 },
    { "Gone Bananas", 54, 6 },
    { "Monkey Business", 55, 7 },
    { "Playing With Fire", 56, 8 },
    { "Take 'em Down", 57, 9 },
    { "Fall Out", 58, 10 },
    { "Pick Yer Piece", 59, 11 },
    { "Badass Buspass Impasse", 60, 13 },
    { "But Where do the Batteries Go?", 61, 14 },
    { "Hit Me Baby One Morgue Time", 62, 12 },
    { "Simian Shootout", 63, 18 },
    { "Monkey Mayhem", 64, 19 },
    { "Dam Bursters", 65, 20 },
};
static const int TROPHY_EVENT_COUNT = 66;

// story missions: index = data order (== goal/location order).
static const char* STORY[] = { "Siberia", "Chicago", "Notre Dame", "Return to Planet X", "NeoTokyo", "Wild West", "Atom Smasher", "Aztec", "Robot Factory", "Space Station" };
static const int STORY_COUNT = 10;
// per-difficulty story COMPLETION bitfields (bit m = mission m; set only on an actual clear, cumulative).
// THIS is what the client polls for story checks. (all 3 offsets confirmed live: Siberia Easy/Normal/Hard.)
static const uintptr_t STORY_CLEAR_EASY   = 0x18f8;  // Easy+ cleared (canonical 'mission completed')
static const uintptr_t STORY_CLEAR_NORMAL = 0x18fc;  // Normal+ cleared
static const uintptr_t STORY_CLEAR_HARD   = 0x1900;  // Hard cleared
// NOTE: profile + STORY_DIFF_BASE + level_id*8 is the per-level 'selected difficulty' byte -- it bumps on
// mission ENTRY (not completion), so it is NOT used for checks (caused false positives). Reference only.
static const int STORY_LEVEL_ID[] = { 0x23, 0x14, 0x22, 0x28, 0x26, 0x20, 0x1f, 0x30, 0x29, 0x27 };
static const uintptr_t STORY_DIFF_BASE = 0xfc0;
static inline uintptr_t storyDiffOff(int mission) { return STORY_DIFF_BASE + (uintptr_t)STORY_LEVEL_ID[mission]*8; }
static const char* DIFFICULTIES[] = { "Easy", "Normal", "Hard" };
static const char* TROPHY_TIERS[] = { "Bronze", "Silver", "Gold", "Platinum" };

// location id helpers (mirror data.py):
//   trophy:  BASE_ID + 0x1000 + ei*4 + tierIdx          ('<event> (<tier>)')
//   story:  BASE_ID + 0x1400 + missionDataIdx*3 + diffIdx  ('<mission> (<difficulty>)')
static inline int64_t trophyLocId(int ei, int tier) { return BASE_ID + 0x1000 + (int64_t)ei*4 + tier; }
static inline int64_t storyLocId(int mission, int diff) { return BASE_ID + 0x1400 + (int64_t)mission*3 + diff; }
}  // namespace ts2
