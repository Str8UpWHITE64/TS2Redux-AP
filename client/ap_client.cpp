// AP networking + game-logic client (apclientpp).  PHASE 2b: connect, lock by received items, detect trophies.
//
//  * items_received -> g_unlocked[] (decoded via ts2_data.h, kept in lockstep with the apworld)
//  * each poll: walk the live menu and set the native lock bit (+0x44 bit0) on entries whose unit is NOT
//    unlocked; story missions are locked via the Easy/Normal/Hard list keyed on the selected mission index.
//  * each poll: read the 66-event trophy table (profile+0x11cc, i3=tier) and send LocationChecks for every
//    achieved tier (Bronze..tier).  (Server ignores checks for locations not in the seed, so over-sending is safe.)
//  * each poll: read the per-difficulty story COMPLETION bitfields (0x18f8 Easy+/0x18fc Normal+/0x1900 Hard,
//    bit m = mission m -- set only on a real clear, never on quit) and send LocationChecks for each cleared
//    difficulty; on Space Station completion send StatusUpdate(GOAL).
//  * per-seed save: on connect, autoload ap_saves/<seed>_<slot>.bin into the blob (a fresh profile if new), and
//    autosave it on each check -- isolates every AP run so completions never leak between seeds.
//  * websocket permessage-deflate compression enabled (zlib compiled in; WSWRAP_NO_COMPRESSION removed).
//
// Config: AP_client.cfg (game root): host=, slot=, password=.  Log: AP_client.log.

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <string>
#include <list>
#include <vector>
#include <set>
#include <unordered_map>
#include <fstream>

#include "apclient.hpp"
#include "ts2_data.h"
#include "ts2_fresh_profile.h"

using nlohmann::json;
static const char* GAME = "TimeSplitters 2";
// slot_data schema this client understands; must track SLOT_DATA_VERSION in the apworld's __init__.py.
static const int SLOT_DATA_VERSION = 7;

// ---- shared with dllmain.cpp ----
extern uintptr_t g_base;
extern volatile uintptr_t g_curPage;
extern volatile char g_objDone[640];   // per (mission,objective) completion flags; idx = mission*64 + (textID-1062)
extern volatile char      g_weaponGating, g_weaponShuffle, g_enemyShuffle;   // weapons-as-items (defined in dllmain); written here from slot_data
extern volatile char      g_ammoTopUp, g_mapShuffle;                         // ammo grant flag / cross-level TABLE-remap flag (the cave)
extern volatile char      g_weaponIsItem[64], g_weaponUnlocked[64];          // gated-slot membership / received unlocks (by WEAPON identity)
extern volatile int       g_weaponRemap[64];                                 // global slot->weapon shuffle (identity default)
extern volatile int       g_weaponRemapLvl[10][64];                          // within_level scope: per-mission slot->weapon
extern volatile char      g_weaponRemapPerLevel;                             // 1 = use g_weaponRemapLvl[mission]
extern volatile long long g_weaponItemBase;                                  // decode: weapon slot = itemId - BASE_ID - this
extern volatile char      g_inStory;                                         // STORY-mission flag (dllmain) -- traps fire ONLY when set
extern void applyHealthRefill(); extern void applyArmorRefill(); extern void applyAmmoRefill();  // bonus effects (dllmain, pawn access)
extern int  apInLevel();                                                     // in a mission/challenge (player pawn spawned)
extern void apSetCheat(int bit); extern void apClearCheat(int bit);          // trap switch: set/clear a cheat-active bit
extern int  apIsDead();                                                      // deathlink: in a level with health <= 0
extern int  apKillPlayer();                                                  // deathlink: run the real death (lethal damage)

// ---- game memory layout (mirrors dllmain.cpp) ----
static const uintptr_t RVA_PROFILES = 0x3f2f468, RVA_MISSION_IDX = 0x2f126f0, RVA_ACTIVE_SLOT = 0x2f2d6f0,
                       RVA_SCREEN_SLOT = 0x3f359d0;
static const uintptr_t SCREEN_PA = 0x320, SCREEN_PB = 0x328;
static const uintptr_t PAGE_WIDGETS = 0x70, WIDGET_NEXT = 0x3c0, WIDGET_TREE = 0x3a0;
static const uintptr_t PAGE_HEADER = 0x650, PAGE_DESC = 0x08, DESC_NAME = 0x28;  // page header char* + identity name
static const uintptr_t N_L1 = 0x10, N_L2 = 0x18, N_L3 = 0x20, E_LABEL = 0x38, E_FLAGS = 0x44, E_ACTION = 0x50;
static const uintptr_t P_SIZE = 0x20e4, P_TROPHYTABLE = 0x11cc;  // trophy record: +0xc=tier(i3), +0x10=completed(i4)

// ---- SEH-guarded memory access (no C++ objects in these functions) ----
static unsigned long long rd64(uintptr_t a) { __try { return *(volatile unsigned long long*)a; } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; } }
static unsigned int       rd32(uintptr_t a) { __try { return *(volatile unsigned int*)a; }       __except(EXCEPTION_EXECUTE_HANDLER) { return 0; } }
static void               wr32(uintptr_t a, unsigned int v) { __try { *(volatile unsigned int*)a = v; } __except(EXCEPTION_EXECUTE_HANDLER) {} }
static void               wr64(uintptr_t a, unsigned long long v) { __try { *(volatile unsigned long long*)a = v; } __except(EXCEPTION_EXECUTE_HANDLER) {} }
static void rdstr(uintptr_t a, char* o, int n) { __try { const char* s = (const char*)a; int i = 0; for (; i < n - 1 && s[i]; i++) o[i] = s[i]; o[i] = 0; } __except(EXCEPTION_EXECUTE_HANDLER) { o[0] = 0; } }
static void wrbuf(uintptr_t a, const void* src, int n) { __try { memcpy((void*)a, src, n); } __except(EXCEPTION_EXECUTE_HANDLER) {} }
static void rdbuf(uintptr_t a, void* dst, int n)       { __try { memcpy(dst, (void*)a, n); } __except(EXCEPTION_EXECUTE_HANDLER) {} }

static uintptr_t activeProf() {                          // active profile slot's blob base (same slot detection reads)
    uintptr_t profBase = rd64(g_base + RVA_PROFILES); if (!profBase) return 0;
    int slot = (int)rd32(g_base + RVA_ACTIVE_SLOT); if (slot < 0) slot = 0;
    return profBase + (uintptr_t)slot * P_SIZE;
}
static std::string sanitize(const std::string& s) {     // make a seed/slot string filename-safe
    std::string o;
    for (char c : s) o += ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-') ? c : '_';
    if (o.empty()) o = "x"; if (o.size() > 64) o.resize(64); return o;
}

// ---- state ----
static FILE* g_log = nullptr;
static APClient* g_ap = nullptr;
static volatile bool g_connected = false;
static bool g_unlocked[ts2::UNIT_COUNT];                 // by unit index; written by items_received
static std::unordered_map<std::string, int> g_nameToUnit; // label -> unit index
static std::set<int64_t> g_sent;                         // location ids already sent this session
static int g_storyMaxTier[ts2::STORY_COUNT] = {0};       // per mission: 0=none 1=Easy 2=Normal 3=Hard (set by detectStory)
volatile int g_annotate = 1;                             // menu annotation on/off (toggled by dllmain F5; shared)
static int g_tcCount = 0;                               // Time Crystals received this session
static int g_tcRequired = 0;                            // from slot_data; the final stage (Space Station) needs this many

// --- bonus + trap pipeline ---  recv = re-derived from items each connect; granted persists per seed (one-time)
static int  g_bonusRecv[ts2::BONUS_ITEM_COUNT]   = {0};  // Health/Armor/Ammo Pack received
static int  g_bonusGranted[ts2::BONUS_ITEM_COUNT] = {0}; // ...applied (persisted)
static int  g_trapRecv[ts2::TRAP_ITEM_COUNT]      = {0};
static int  g_trapGranted[ts2::TRAP_ITEM_COUNT]   = {0}; // ...fired-and-finished (persisted)
static bool g_trapActive[ts2::TRAP_ITEM_COUNT]    = {false};
static unsigned int g_trapEnd[ts2::TRAP_ITEM_COUNT] = {0}; // GetTickCount when the cheat bit clears
static bool g_grantDirty = false;
static bool g_objectiveChecks = false;                  // from slot_data; send objective-complete LocationChecks

// --- deathlink ---  participate only when slot_data says so; detect OUR death (alive->dead edge) to Bounce, and apply
// incoming deaths by running the real kill.  g_deathSuppress stops the death WE cause from echoing back (loop guard).
static bool g_deathLink = false;                        // from slot_data
static volatile bool g_deathPending = false;            // an incoming death to apply on the next in-level poll
static volatile bool g_deathSuppress = false;           // the next own-death is one we applied -> don't re-send
static bool g_wasAlive = false;                          // player alive in a level last poll (edge detector)
static int  g_objPrimMask[640] = {0};                   // from slot_data; per-objective primary-difficulty bitmask
                                                        // (Easy=1 Normal=2 Hard=4); idx = mission*64 + (textID-1062)

// ---- per-seed save management (isolate each AP run's profile) ----
static std::string g_seedFile, g_loadedSeedFile;        // ap_saves/<seed>_<slot>.bin: target / currently-loaded
static volatile bool g_needLoad = false;                // autoload pending (deferred until the profile is allocated)
static volatile bool g_saveDirty = false;               // blob changed -> autosave to the seed file
static volatile uintptr_t g_loadedProfBase = 0;         // profBase we last loaded into; the game REALLOCATES the blob
                                                        // a few seconds after connect, so reload when this changes

static void aplog(const char* fmt, ...) {
    if (!g_log) return;
    va_list a; va_start(a, fmt); vfprintf(g_log, fmt, a); va_end(a);
    fputc('\n', g_log); fflush(g_log);
}

// The host is deliberately stored WITHOUT a ws:// / wss:// scheme. apclientpp only auto-negotiates TLS when the URI it
// receives has no scheme: it then tries wss:// first and flips to ws:// on a connection error (and back), so a player who
// has no idea which their room needs still connects. Writing a scheme pins it to that one and disables the fallback --
// which is what we want for someone who typed one on purpose, and what we must avoid by default.
struct ApCfg { std::string host = "localhost:38281"; std::string slot = "Player1"; std::string password; };

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

static ApCfg readCfg() {
    ApCfg c; std::ifstream f("AP_client.cfg"); std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line[0] == '#') continue;
        size_t eq = line.find('='); if (eq == std::string::npos) continue;
        std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        if      (k == "host")     { if (!v.empty()) c.host = v; }   // blank host would leave the client permanently idle
        else if (k == "slot")     { if (!v.empty()) c.slot = v; }
        else if (k == "password") c.password = v;                   // blank password is meaningful (room has none)
    }
    return c;
}

// ---- locking: walk the live menu, set/clear +0x44 bit0 per unlock state ----
static bool isDifficulty(const char* label, int* outDiff) {   // prefix match so annotated "Easy [Checked]" still counts
    if      (!strncmp(label, "Easy",   4)) { if (outDiff) *outDiff = 0; return true; }
    else if (!strncmp(label, "Normal", 6)) { if (outDiff) *outDiff = 1; return true; }
    else if (!strncmp(label, "Hard",   4)) { if (outDiff) *outDiff = 2; return true; }
    return false;
}

// Story mission m unlocked in-game?  Its own unlock item, plus -- for the final stage -- the required Time Crystals.
static bool storyUnlocked(int m) {
    if (m < 0 || m >= ts2::STORY_COUNT) return false;
    if (!g_unlocked[66 + m]) return false;                             // story units are 66..75
    if (m == ts2::STORY_COUNT - 1 && g_tcCount < g_tcRequired) return false;  // Space Station also needs Time Crystals
    return true;
}

static bool shouldLock(const char* label) {
    auto it = g_nameToUnit.find(label);
    if (it != g_nameToUnit.end()) return !g_unlocked[it->second];      // a match/challenge/(mission) by name
    if (isDifficulty(label, nullptr)) {
        int m = (int)rd32(g_base + RVA_MISSION_IDX);                    // story difficulty list -> selected mission
        if (m >= 0 && m < ts2::STORY_COUNT) return !storyUnlocked(m);   // final stage also gated on the Time Crystals
    }
    return false;  // leagues/series/nav containers: never lock (matches inside are locked individually)
}

// Annotate a story difficulty entry: point its label pointer (+0x38, a char*) at our own literal so the game
// renders "Easy [Checked]" when the selected mission has cleared that difficulty.  Re-applied every poll; matched
// by prefix (idempotent).  Keeps the "Easy"/"Normal"/"Hard" prefix so any game-side prefix logic still matches.
// Short "completed" marker appended to a cleared difficulty.  TS2's menu font is ASCII (the "[Checked]" text
// rendered fine but overran the field), so keep it tiny.  A real checkmark glyph likely won't render -- change
// DONE_MARK if you want to retune the look.
#define DONE_MARK " *"
static void annotateDifficulty(uintptr_t n, const char* label) {
    if (!g_annotate) return;
    static const char* PLAIN[3]   = { "Easy", "Normal", "Hard" };
    static const char* CHECKED[3] = { "Easy" DONE_MARK, "Normal" DONE_MARK, "Hard" DONE_MARK };
    int d;
    if (!isDifficulty(label, &d)) return;
    int m = (int)rd32(g_base + RVA_MISSION_IDX);
    if (m < 0 || m >= ts2::STORY_COUNT) return;
    bool checked = g_storyMaxTier[m] >= d + 1;
    wr64(n + E_LABEL, (uintptr_t)(checked ? CHECKED[d] : PLAIN[d]));
}

// Annotate the Story carousel / difficulty-screen HEADER (page+0x650, a char*).  The mission NAME isn't a
// reachable pointer in the menu (drawn from a string table by id), so we build it from our own STORY[] using the
// selected-mission index and write "<Mission> [Locked]"/"[Completed]" into the header.  Page identified by its
// own name at page->[+0x08]->[+0x28] ("story_page" / "storydifficulty_page").  Re-applied each poll.
static void annotateHeader(uintptr_t page) {
    if (!g_annotate || !page) return;
    uintptr_t desc = rd64(page + PAGE_DESC); if (!desc) return;
    char pname[40]; rdstr(rd64(desc + DESC_NAME), pname, sizeof(pname));
    if (strcmp(pname, "story_page") && strcmp(pname, "storydifficulty_page")) return;
    int m = (int)rd32(g_base + RVA_MISSION_IDX);
    if (m < 0 || m >= ts2::STORY_COUNT) return;
    char cur[8]; rdstr(rd64(page + PAGE_HEADER), cur, sizeof(cur));   // confirm +0x650 is a live string field
    if (!cur[0]) return;
    const char* status = !storyUnlocked(m)           ? " [Locked]"
                       : (g_storyMaxTier[m] >= 3)    ? " [Completed]" : "";
    static char buf[80];
    int i = 0; const char* nm = ts2::STORY[m];
    for (; nm[i] && i < 60; i++) buf[i] = nm[i];
    for (int j = 0; status[j] && i < 79; j++) buf[i++] = status[j];
    buf[i] = 0;
    wr64(page + PAGE_HEADER, (uintptr_t)buf);
}

static void lockTree(uintptr_t head) {
    static uintptr_t visited[2048];
    int vc = 0; uintptr_t stack[2048]; int sp = 0, guard = 0;
    stack[sp++] = head;
    while (sp > 0 && guard++ < 2048) {
        uintptr_t n = stack[--sp]; if (!n) continue;
        bool seen = false; for (int i = 0; i < vc; i++) if (visited[i] == n) { seen = true; break; }
        if (seen) continue; if (vc < 2048) visited[vc++] = n; else continue;

        if (rd64(n + E_ACTION)) {
            char label[80]; rdstr(rd64(n + E_LABEL), label, sizeof(label));
            if (label[0]) {
                unsigned int f = rd32(n + E_FLAGS);
                if (shouldLock(label)) wr32(n + E_FLAGS, f | 1u);
                else                   wr32(n + E_FLAGS, f & ~1u);     // clear so unlocking takes effect immediately
                annotateDifficulty(n, label);                          // "Easy" -> "Easy [Checked]" when cleared
            }
        }
        uintptr_t c;
        c = rd64(n + N_L1); if (c && sp < 2048) stack[sp++] = c;
        c = rd64(n + N_L2); if (c && sp < 2048) stack[sp++] = c;
        c = rd64(n + N_L3); if (c && sp < 2048) stack[sp++] = c;
    }
}

static void walkPageLock(uintptr_t page) {
    if (!page) return;
    annotateHeader(page);                        // story/difficulty header -> "<Mission> [Locked]/[Completed]"
    uintptr_t w = rd64(page + PAGE_WIDGETS);
    int wg = 0;
    while (w && wg++ < 64) {
        uintptr_t head = rd64(w + WIDGET_TREE);
        if (head) lockTree(head);
        w = rd64(w + WIDGET_NEXT);
    }
}

static void applyLocks() {
    uintptr_t screen = rd64(g_base + RVA_SCREEN_SLOT);
    uintptr_t pages[3] = { g_curPage, screen ? rd64(screen + SCREEN_PA) : 0, screen ? rd64(screen + SCREEN_PB) : 0 };
    for (int i = 0; i < 3; i++) {
        if (!pages[i]) continue;
        bool dup = false; for (int j = 0; j < i; j++) if (pages[j] == pages[i]) dup = true;
        if (!dup) walkPageLock(pages[i]);
    }
}


static bool g_goalSent = false;
static int  g_goalTier = 1;   // goal_difficulty as a tier (1=Easy 2=Normal 3=Hard); GOAL fires only when Space Station's cleared tier >= this
// Game Mode (slot_data game_mode): 0=both 1=story_only 2=arcade_only. In arcade_only there is no Space Station
// clear to finish on, so the GOAL instead fires once g_arcadeGoalChecks Arcade/Challenge checks are done (see
// detectTrophies). g_maxTiers bounds the per-event tier count to the tiers the seed actually made locations for.
static int  g_contentMode = 0;
static int  g_arcadeGoalChecks = 0;
static int  g_maxTiers = 4;

// ---- detection: trophy table -> LocationChecks for each achieved tier ----
static void detectTrophies() {
    uintptr_t profBase = rd64(g_base + RVA_PROFILES);
    if (!profBase) return;
    int slot = (int)rd32(g_base + RVA_ACTIVE_SLOT); if (slot < 0) slot = 0;
    uintptr_t prof = profBase + (uintptr_t)slot * P_SIZE;

    std::vector<int64_t> fresh;
    int achieved = 0;                            // Arcade/Challenge checks earned, bounded by the seed's tier count
    for (int k = 0; k < ts2::TROPHY_EVENT_COUNT; k++) {
        int ei = ts2::TROPHY_EVENTS[k].ei, gameIdx = ts2::TROPHY_EVENTS[k].gameIdx;
        uintptr_t e = prof + P_TROPHYTABLE + (uintptr_t)gameIdx * 0x14;
        int tier = (int)rd32(e + 0x0c);          // 1=Bronze 2=Silver 3=Gold 4=Platinum
        if (tier < 1) continue;
        for (int t = 0; t < tier && t < 4; t++) { // every achieved tier 0..tier-1
            int64_t loc = ts2::trophyLocId(ei, t);
            if (g_sent.insert(loc).second) fresh.push_back(loc);
        }
        achieved += (tier < g_maxTiers ? tier : g_maxTiers);
    }
    if (!fresh.empty() && g_ap) {
        g_ap->LocationChecks(std::list<int64_t>(fresh.begin(), fresh.end()));
        for (int64_t id : fresh) aplog("[check] trophy loc id=%lld", (long long)id);
        g_saveDirty = true;
    }
    // arcade_only GOAL: no Space Station to clear, so finish on the chosen share of the Arcade/Challenge checks.
    if (g_contentMode == 2 && !g_goalSent && g_ap && g_arcadeGoalChecks > 0 && achieved >= g_arcadeGoalChecks) {
        g_goalSent = true;
        g_ap->StatusUpdate(APClient::ClientStatus::GOAL);
        aplog("[GOAL] arcade_only: %d/%d Arcade+Challenge checks done -> StatusUpdate(GOAL)", achieved, g_arcadeGoalChecks);
    }
}

// ---- detection: per-difficulty story COMPLETION bitfields -> per-difficulty LocationChecks + goal ----
//   Three dwords, bit m = mission m: 0x18f8 Easy+ cleared, 0x18fc Normal+ cleared, 0x1900 Hard cleared.
//   These flip ONLY on an actual clear (NOT on entering/quitting a mission -- that only moves the separate
//   per-level 'selected difficulty' byte, which we deliberately do not poll).  Clears are cumulative, so the
//   highest set tier implies the lower ones: send difficulty 0..maxTier-1.  Space Station (last mission)
//   cleared on any difficulty => goal (the player could only reach it if AP unlocked it).
static void detectStory() {
    uintptr_t profBase = rd64(g_base + RVA_PROFILES);
    if (!profBase) return;
    int slot = (int)rd32(g_base + RVA_ACTIVE_SLOT); if (slot < 0) slot = 0;
    uintptr_t prof = profBase + (uintptr_t)slot * P_SIZE;

    unsigned int easy = rd32(prof + ts2::STORY_CLEAR_EASY);
    unsigned int norm = rd32(prof + ts2::STORY_CLEAR_NORMAL);
    unsigned int hard = rd32(prof + ts2::STORY_CLEAR_HARD);

    // DIAG (temporary): whenever the story-clear bitfields change, dump them raw + which STORY mission each set Easy-bit
    // maps to, and (in the send loop below) whether each cleared mission's check is SENT fresh or skipped as already-sent.
    bool storyChanged = false;
    { static unsigned int pe = ~0u, pn = ~0u, ph = ~0u;
      if (easy != pe || norm != pn || hard != ph) {
          storyChanged = true; pe = easy; pn = norm; ph = hard;
          char b[512]; int o = snprintf(b, sizeof(b), "[story-bits] easy=0x%x norm=0x%x hard=0x%x  Easy+ cleared:", easy, norm, hard);
          for (int m = 0; m < ts2::STORY_COUNT && o < (int)sizeof(b) - 40; m++)
              if (easy & (1u << m)) o += snprintf(b + o, sizeof(b) - o, "  bit%d=%s", m, ts2::STORY[m]);
          aplog("%s", b); printf("%s\n", b);
      } }

    std::vector<int64_t> fresh;
    for (int m = 0; m < ts2::STORY_COUNT; m++) {
        unsigned int bit = 1u << m;
        int maxTier = 0;                          // highest difficulty cleared: 1=Easy 2=Normal 3=Hard
        if (easy & bit) maxTier = 1;
        if (norm & bit) maxTier = 2;
        if (hard & bit) maxTier = 3;
        g_storyMaxTier[m] = maxTier;                 // expose for menu annotation ("Easy [Checked]" etc.)
        if (maxTier < 1) continue;
        // safety net: clearing the mission guarantees its PRIMARY objectives (at the cleared difficulty) are done,
        // so mark them complete even if the live render hook never caught the level-end ones.  detectObjectives()
        // (called right after this) sends them; g_sent dedups, so this is idempotent.
        if (g_objectiveChecks) {
            for (int off = 0; off < 64; off++) {
                int idx = m * 64 + off;
                if (g_objPrimMask[idx] & (1 << (maxTier - 1))) g_objDone[idx] = 1;
            }
        }
        for (int d = 0; d < maxTier && d < 3; d++) {
            int64_t loc = ts2::storyLocId(m, d);
            bool isNew = g_sent.insert(loc).second;
            if (isNew) fresh.push_back(loc);
            if (storyChanged) aplog("[story] %s (%s) id=%lld -> %s", ts2::STORY[m],
                d == 0 ? "Easy" : d == 1 ? "Normal" : "Hard", (long long)loc,
                isNew ? "SEND (fresh)" : "skip (already sent this session)");
        }
        // arcade_only finishes on the Arcade/Challenge share (detectTrophies), so a Space Station clear must NOT win --
        // the story is still fully playable there, it just has no checks.
        if (g_contentMode != 2 && m == ts2::STORY_COUNT - 1 && !g_goalSent && g_ap && maxTier >= g_goalTier) {   // Space Station cleared on the goal difficulty
            g_goalSent = true;
            g_ap->StatusUpdate(APClient::ClientStatus::GOAL);
            aplog("[GOAL] Space Station cleared (tier %d >= goal %d) -> StatusUpdate(GOAL)", maxTier, g_goalTier);
        }
    }
    if (!fresh.empty() && g_ap) {
        g_ap->LocationChecks(std::list<int64_t>(fresh.begin(), fresh.end()));
        for (int64_t id : fresh) aplog("[check] story loc id=%lld", (long long)id);
        g_saveDirty = true;
    }
}

// ---- detection: per-(mission,objective) completions from the DLL render hook -> LocationChecks ----
//   dllmain's objective-render hook sets g_objDone[mission*64 + (textID-1062)] = 1 when an objective's on-screen
//   status reaches 5 (COMPLETE).  loc id = BASE + 0x1500 + that index (matches the apworld).  Gated on the
//   slot_data flag; deduped with g_sent.  (Over-sending would be harmless -- the server ignores out-of-seed
//   locations -- but the flag keeps the log clean for seeds that didn't enable objective checks.)
static void detectObjectives() {
    if (!g_objectiveChecks) return;
    std::vector<int64_t> fresh;
    for (int i = 0; i < 640; i++) {
        if (!g_objDone[i]) continue;
        int64_t loc = ts2::BASE_ID + 0x1500 + i;
        if (g_sent.insert(loc).second) fresh.push_back(loc);
    }
    if (!fresh.empty() && g_ap) {
        g_ap->LocationChecks(std::list<int64_t>(fresh.begin(), fresh.end()));
        for (int64_t id : fresh) aplog("[check] objective loc id=%lld", (long long)id);
    }
}

// ---- per-seed persistence: blob <-> ap_saves/<seed>_<slot>.bin (the game's own TS2Save.bin is left untouched) ----
static void saveBlob() {
    uintptr_t prof = activeProf(); if (!prof || g_seedFile.empty()) return;
    unsigned char buf[P_SIZE]; rdbuf(prof, buf, (int)P_SIZE);
    CreateDirectoryA("ap_saves", nullptr);
    std::ofstream f(g_seedFile, std::ios::binary | std::ios::trunc);
    if (f) { f.write((const char*)buf, P_SIZE); g_saveDirty = false; }
}
// one-time bonus/trap state persists in a sidecar next to the seed file (so reconnects/restarts don't re-grant).
static void saveGrant() {
    if (g_seedFile.empty()) return;
    std::ofstream f(g_seedFile + ".grant", std::ios::binary | std::ios::trunc);
    if (f) { f.write((const char*)g_bonusGranted, sizeof(g_bonusGranted)); f.write((const char*)g_trapGranted, sizeof(g_trapGranted)); }
}
static void loadGrant() {
    for (int i = 0; i < ts2::BONUS_ITEM_COUNT; i++) g_bonusGranted[i] = 0;
    for (int i = 0; i < ts2::TRAP_ITEM_COUNT;  i++) { g_trapGranted[i] = 0; g_trapActive[i] = false; }
    if (g_seedFile.empty()) return;
    std::ifstream f(g_seedFile + ".grant", std::ios::binary);
    if (f) { f.read((char*)g_bonusGranted, sizeof(g_bonusGranted)); f.read((char*)g_trapGranted, sizeof(g_trapGranted)); }
}
static void autoLoad() {
    uintptr_t prof = activeProf(); if (!prof) return;            // profile not allocated yet -> retry next poll
    std::ifstream f(g_seedFile, std::ios::binary);
    if (f) {
        std::vector<unsigned char> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (buf.size() >= P_SIZE) { wrbuf(prof, buf.data(), (int)P_SIZE); aplog("[autoload] %s", g_seedFile.c_str()); }
        else aplog("[autoload] %s too small (%d) -> skipped", g_seedFile.c_str(), (int)buf.size());
    } else {
        wrbuf(prof, ts2_fresh_profile, (int)P_SIZE);             // new seed -> embedded clean profile
        aplog("[autoload] new seed -> fresh profile -> %s", g_seedFile.c_str());
        saveBlob();
    }
    g_loadedSeedFile = g_seedFile;
    g_loadedProfBase = rd64(g_base + RVA_PROFILES);              // remember the blob we loaded into; reload if it reallocs
    g_needLoad = false;
    g_sent.clear(); g_goalSent = false;                          // re-derive checks from the freshly-loaded blob
    loadGrant();                                                 // per-seed one-time bonus/trap grant counts
}

// apply pending bonuses (in any mission/challenge) + fire pending traps (STORY only, timed). Granted counts persist,
// so each item applies exactly once across reconnects/restarts. Traps: cheat bit ON for a window, cleared on timeout
// or if you leave story -- so the bit is NEVER set outside a story mission (Arcade/Challenge trophies can't void).
static void applyBonusTraps() {
    unsigned int now = GetTickCount();
    bool inLevel = apInLevel() != 0;
    bool inStory = (g_inStory != 0);
    if (inLevel) {
        if (g_bonusGranted[0] < g_bonusRecv[0]) { applyHealthRefill(); g_bonusGranted[0] = g_bonusRecv[0]; g_grantDirty = true; aplog("[bonus] Health Pack -> full"); }
        if (g_bonusGranted[1] < g_bonusRecv[1]) { applyArmorRefill();  g_bonusGranted[1] = g_bonusRecv[1]; g_grantDirty = true; aplog("[bonus] Armor Pack -> full"); }
        if (g_bonusGranted[2] < g_bonusRecv[2]) { applyAmmoRefill();   g_bonusGranted[2] = g_bonusRecv[2]; g_grantDirty = true; aplog("[bonus] Ammo Pack -> full"); }
    }
    // Traps fire ONE AT A TIME. Some TS2 cheats conflict when active together -- Big Head and Small Head are opposing
    // head-scale cheats and CRASH the game if both bits are set at once (seen loading Chicago with a batch of queued
    // traps). So never hold two active: first clear any that finished (or if we left the story), then start the next
    // pending trap only when nothing else is running. A queued burst thus drains 30s at a time across the mission.
    const unsigned int dur = 30000u;   // 30s each (minor/comedic cheats; spawn-time ones also catch respawns)
    bool trapActive = false;
    for (int i = 0; i < ts2::TRAP_ITEM_COUNT; i++) {
        if (!g_trapActive[i]) continue;
        if (now >= g_trapEnd[i] || !inStory) {
            apClearCheat(ts2::TRAP_ITEMS[i].cheatBit); g_trapActive[i] = false; g_trapGranted[i]++; g_grantDirty = true;
            aplog("[trap] %s ended", ts2::TRAP_ITEMS[i].name);
        } else trapActive = true;
    }
    if (!trapActive && inStory) {
        for (int i = 0; i < ts2::TRAP_ITEM_COUNT; i++)
            if (g_trapGranted[i] < g_trapRecv[i]) {
                apSetCheat(ts2::TRAP_ITEMS[i].cheatBit); g_trapActive[i] = true; g_trapEnd[i] = now + dur;
                aplog("[trap] %s fired (%us)", ts2::TRAP_ITEMS[i].name, dur / 1000);
                break;   // one at a time -- never two conflicting cheats simultaneously
            }
    }
    if (g_grantDirty) { saveGrant(); g_grantDirty = false; }
}

static DWORD WINAPI apThread(LPVOID) {
    fopen_s(&g_log, "AP_client.log", "w");
    for (const auto& u : ts2::UNITS) g_nameToUnit[u.name] = u.index;

    ApCfg cfg = readCfg();
    bool pinned = cfg.host.find("://") != std::string::npos;
    aplog("AP client (TS2) starting. host=%s slot=%s (%s)", cfg.host.c_str(), cfg.slot.c_str(),
          pinned ? "scheme given -- using it as-is, no fallback"
                 : "no scheme -- trying wss:// first, falling back to ws://");
    g_ap = new APClient("ts2redux", GAME, cfg.host);

    g_ap->set_socket_connected_handler([]() { aplog("[socket connected]"); });
    g_ap->set_socket_disconnected_handler([]() { g_connected = false; aplog("[socket disconnected]"); });
    g_ap->set_socket_error_handler([](const std::string& e) { aplog("[socket error] %s", e.c_str()); });

    g_ap->set_room_info_handler([cfg]() {
        aplog("[room info] connecting slot '%s'", cfg.slot.c_str());
        g_ap->ConnectSlot(cfg.slot, cfg.password, 0b111, {"AP"}, {0, 5, 1});
    });
    g_ap->set_slot_connected_handler([](const json& slotData) {
        for (int i = 0; i < ts2::UNIT_COUNT; i++) g_unlocked[i] = false;  // re-derived from items each connect
        g_tcCount = 0;                                                   // re-counted from items each connect
        for (int i = 0; i < ts2::BONUS_ITEM_COUNT; i++) g_bonusRecv[i] = 0;   // recv re-counted from items; granted (persisted) decides what's new
        for (int i = 0; i < ts2::TRAP_ITEM_COUNT;  i++) g_trapRecv[i]  = 0;
        g_tcRequired = slotData.value("time_crystals_required", 0);          // final-stage gate (0 = no Time Crystal gate)
        { std::string gd = slotData.value("goal_difficulty", std::string("Easy"));   // GOAL = clearing Space Station on the chosen MAX difficulty
          g_goalTier = (gd == "Hard") ? 3 : (gd == "Normal") ? 2 : 1; }
        // slot_data schema check. Not fatal (older/newer seeds mostly still work, since every key is read with a
        // default), but a loud line beats silently mis-reading a seed when the apworld and this DLL drift apart.
        { int sv = slotData.value("version", 0);
          if (sv != SLOT_DATA_VERSION)
              aplog("[warn] slot_data schema v%d, this client expects v%d -- apworld and client versions differ. "
                    "Update whichever is older if anything behaves oddly.", sv, SLOT_DATA_VERSION); }
        // "game_mode" as of the release rename; fall back to the old "content_mode" so a seed made before it
        // still reports the right mode instead of silently defaulting to both.
        g_contentMode      = slotData.value("game_mode", slotData.value("content_mode", 0));
        g_arcadeGoalChecks = slotData.value("arcade_goal_checks", 0);     // arcade_only: GOAL at this many Arcade/Challenge checks
        g_maxTiers = 4;
        if (slotData.contains("trophy_tier_checks")) {                    // how many tiers the seed made locations for
            int n = (int)slotData["trophy_tier_checks"].size();
            if (n >= 1 && n <= 4) g_maxTiers = n;
        }
        if (g_contentMode == 2)
            aplog("[GOAL] arcade_only: need %d Arcade/Challenge checks (tiers per event: %d)", g_arcadeGoalChecks, g_maxTiers);
        g_objectiveChecks = slotData.value("objective_checks", false);    // enable objective-complete checks
        g_deathLink = slotData.value("death_link", false);                // deathlink: opt in -> advertise the tag + send/apply deaths
        g_deathPending = false; g_deathSuppress = false; g_wasAlive = false;
        if (g_deathLink) g_ap->ConnectUpdate(0b111, std::list<std::string>{"AP", "DeathLink"});
        memset(g_objPrimMask, 0, sizeof(g_objPrimMask));
        if (slotData.contains("objective_primary")) {                     // per-objective primary-difficulty masks
            for (auto it = slotData["objective_primary"].begin(); it != slotData["objective_primary"].end(); ++it) {
                int idx = atoi(it.key().c_str());
                if (idx >= 0 && idx < 640) g_objPrimMask[idx] = it.value().get<int>();
            }
        }
        // weapons-as-items (A gate / B global shuffle / C enemy). Reset, then apply slot_data. g_weaponUnlocked is
        // re-derived from received items (below), like g_unlocked.
        bool wpnShuffle = slotData.value("weapon_shuffle", false);
        g_weaponGating  = slotData.value("weapon_gating", false);
        g_weaponShuffle = 0;                                          // legacy per-pawn loadout remap -- superseded by the table remap (g_mapShuffle)
        g_mapShuffle    = wpnShuffle ? 1 : 0;                         // cross-level shuffle = the TABLE remap applied before each level's precache (keeps sound)
        g_ammoTopUp     = (g_weaponGating || wpnShuffle) ? 1 : 0;     // top up ammo on acquisition so unlocked/shuffled weapons can fire
        g_enemyShuffle  = slotData.value("enemy_weapon_shuffle", false);
        g_weaponItemBase = slotData.value("weapon_item_base", (long long)0xA00);
        for (int i = 0; i < 64; i++) { g_weaponIsItem[i] = 0; g_weaponUnlocked[i] = 0; g_weaponRemap[i] = i; }
        if (slotData.contains("weapon_item_slots"))
            for (auto& s : slotData["weapon_item_slots"]) { int sl = s.get<int>(); if (sl >= 0 && sl < 64) g_weaponIsItem[sl] = 1; }
        if (slotData.contains("weapon_remap"))
            for (auto it = slotData["weapon_remap"].begin(); it != slotData["weapon_remap"].end(); ++it) {
                int sl = atoi(it.key().c_str()); int r = it.value().get<int>();
                if (sl >= 0 && sl < 64 && r >= 0 && r < 64) g_weaponRemap[sl] = r;
            }
        // Weapon Shuffle Scope 'within_level' ships one map PER MISSION INDEX instead of the single global map above.
        // Reset to identity first so a reconnect (or a seed change) can't leave stale entries behind.
        for (int m = 0; m < 10; m++) for (int s = 0; s < 64; s++) g_weaponRemapLvl[m][s] = s;
        g_weaponRemapPerLevel = 0;
        if (slotData.contains("weapon_remap_by_level") && !slotData["weapon_remap_by_level"].empty()) {
            int levels = 0;
            for (auto lm = slotData["weapon_remap_by_level"].begin(); lm != slotData["weapon_remap_by_level"].end(); ++lm) {
                int mi = atoi(lm.key().c_str());
                if (mi < 0 || mi >= 10) continue;
                for (auto it = lm.value().begin(); it != lm.value().end(); ++it) {
                    int sl = atoi(it.key().c_str()); int r = it.value().get<int>();
                    if (sl >= 0 && sl < 64 && r >= 0 && r < 64) g_weaponRemapLvl[mi][sl] = r;
                }
                levels++;
            }
            if (levels) { g_weaponRemapPerLevel = 1; aplog("[wpn] within-level shuffle: per-mission maps for %d level(s)", levels); }
        }
        g_sent.clear();
        g_goalSent = false;
        g_connected = true;
        std::string newFile = "ap_saves/" + sanitize(g_ap->get_seed()) + "_" + sanitize(g_ap->get_slot()) + ".bin";
        if (newFile != g_loadedSeedFile) {                  // switching seeds (or first connect) -> reload
            if (!g_loadedSeedFile.empty()) saveBlob();      // persist the seed we're leaving (g_seedFile still = old)
            g_seedFile = newFile; g_needLoad = true;
            memset((void*)g_objDone, 0, 640);               // fresh run -> drop live objective completions
        } else { g_seedFile = newFile; }                    // reconnect to same seed -> keep current blob
        aplog("[CONNECTED] seed=%s file=%s slot_data=%s", g_ap->get_seed().c_str(), g_seedFile.c_str(), slotData.dump().c_str());
    });
    g_ap->set_slot_refused_handler([](const std::list<std::string>& errs) {
        std::string s; for (auto& e : errs) { s += e; s += " "; } aplog("[slot refused] %s", s.c_str());
    });
    g_ap->set_items_received_handler([](const std::list<APClient::NetworkItem>& items) {
        for (const auto& it : items) {
            int64_t off = it.item - ts2::BASE_ID;
            if (off >= 0 && off < ts2::UNIT_COUNT) { g_unlocked[(int)off] = true; aplog("[unlock] %s", ts2::UNITS[(int)off].name); continue; }
            if (it.item == ts2::TIME_CRYSTAL_ID) { ++g_tcCount; aplog("[time crystal] Time Crystal %d/%d", g_tcCount, g_tcRequired); continue; }
            bool handled = false;
            for (int b = 0; b < ts2::BONUS_ITEM_COUNT; b++) if (it.item == ts2::BONUS_ITEMS[b].id) { ++g_bonusRecv[b]; handled = true; aplog("[bonus] recv %s (%d)", ts2::BONUS_ITEMS[b].name, g_bonusRecv[b]); break; }
            if (handled) continue;
            for (int t = 0; t < ts2::TRAP_ITEM_COUNT; t++) if (it.item == ts2::TRAP_ITEMS[t].id) { ++g_trapRecv[t]; handled = true; aplog("[trap] recv %s (%d)", ts2::TRAP_ITEMS[t].name, g_trapRecv[t]); break; }
            if (handled) continue;
            int64_t w = it.item - ts2::BASE_ID - g_weaponItemBase;     // weapons-as-items: id block at +0xA00
            if (w >= 0 && w < 64) { g_weaponUnlocked[(int)w] = 1; aplog("[weapon] unlock slot %lld", (long long)w); }
        }
    });
    g_ap->set_print_handler([](const std::string& s) { aplog("[print] %s", s.c_str()); });
    // DEATHLINK in: a "Bounced" packet tagged DeathLink from another player -> queue a kill (skip our own echo).
    g_ap->set_bounced_handler([cfg](const json& cmd) {
        if (!g_deathLink || !cmd.contains("tags")) return;
        bool isDL = false;
        for (const auto& t : cmd["tags"]) if (t.is_string() && t.get<std::string>() == "DeathLink") { isDL = true; break; }
        if (!isDL) return;
        std::string source, cause;
        if (cmd.contains("data") && cmd["data"].is_object()) {
            const auto& d = cmd["data"];
            if (d.contains("source") && d["source"].is_string()) source = d["source"].get<std::string>();
            if (d.contains("cause")  && d["cause"].is_string())  cause  = d["cause"].get<std::string>();
        }
        if (source == cfg.slot) return;                 // our own death echoed back -> ignore
        g_deathPending = true;
        aplog("[deathlink] incoming from '%s'%s%s -> kill queued", source.c_str(), cause.empty() ? "" : ": ", cause.c_str());
    });

    aplog("polling...");
    unsigned int tick = 0;
    for (;;) {
        g_ap->poll();
        if (g_connected && g_base) {
            // DEATHLINK: send on our alive->dead edge; apply any queued incoming death (independent of profile state).
            if (g_deathLink) {
                bool inLvl = apInLevel() != 0;
                bool dead  = inLvl && apIsDead() != 0;
                bool alive = inLvl && !dead;
                if (g_wasAlive && dead) {                                  // we just died this poll
                    if (g_deathSuppress) { g_deathSuppress = false; aplog("[deathlink] own death (we applied it) -- not echoed"); }
                    else {
                        json d = { {"time", g_ap->get_server_time()}, {"source", cfg.slot}, {"cause", cfg.slot + " bit the dust"} };
                        g_ap->Bounce(d, {}, {}, {"DeathLink"}); aplog("[deathlink] our death -> Bounce sent");
                    }
                }
                g_wasAlive = alive;
                if (g_deathPending && alive) {                            // apply incoming kill only when alive in a level
                    g_deathPending = false; g_deathSuppress = true;
                    if (apKillPlayer()) aplog("[deathlink] incoming death applied (player killed)");
                    else { g_deathSuppress = false; aplog("[deathlink] incoming death -- kill failed"); }
                }
            }
            uintptr_t pb = rd64(g_base + RVA_PROFILES);
            int aslot = (int)rd32(g_base + RVA_ACTIVE_SLOT);
            bool ready = (pb != 0 && aslot >= 0);                    // profile subsystem up + a slot active = STABLE blob
            // (re)load the seed once the blob is live, and again whenever the game reallocates it (pb changes)
            if (!g_seedFile.empty() && ready && (g_needLoad || pb != g_loadedProfBase)) autoLoad();
            // normal operation only on the same blob we loaded (skip the transient pre-realloc window)
            if (ready && !g_needLoad && pb == g_loadedProfBase) {
                applyLocks(); detectTrophies(); detectStory(); detectObjectives();
                applyBonusTraps();                                                 // bonus refills (in-mission) + traps (story, timed)
                ++tick;
                if (g_saveDirty || tick % 600u == 0u) saveBlob();    // on new check, or every ~30s
            }
        }
        Sleep(50);
    }
    return 0;
}

extern "C" void startApClient() {
    CreateThread(nullptr, 0, apThread, nullptr, 0, nullptr);
}
