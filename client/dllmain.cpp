// TS2 Redux -> Archipelago client (game-side DLL).  Runs alongside ap_client.cpp (the AP networking half).
//
// At startup it auto-applies the production patches/hooks: the Robot Factory softlock fix (rfApply), the objective-
// render hook (objHookInstall) that feeds completion LocationChecks, and the weapon-sound precache retarget
// (wpnFilesPatch).  The diagThread loop then drives the per-frame work: objective-completion polling, the
// weapons-as-items pass, and the story-context latch (g_inStory).  It also exposes the player-effect API used by
// ap_client (health/armor/ammo refills, cheat toggles for traps, and the deathlink kill apKillPlayer).  All game-
// memory access is SEH-guarded.  The menu walk collects/locks across {tick page, screen+0x320, screen+0x328}.
//

#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cstdint>
#include <cstring>

static const uintptr_t RVA_STORY_PAGE     = 0x24f1868;
static const uintptr_t RVA_ARCADE_PAGE    = 0x24f1b70;
static const uintptr_t RVA_CHALLENGE_PAGE = 0x24f1ab0;
static const uintptr_t RVA_MODE           = 0x2f126ec;
static const uintptr_t RVA_PROFILES       = 0x3f2f468;
static const uintptr_t RVA_SCREEN_SLOT    = 0x3f359d0;
static const uintptr_t SCREEN_PAGE_A      = 0x320;
static const uintptr_t SCREEN_PAGE_B      = 0x328;
static const uintptr_t RVA_MISSION_IDX    = 0x2f126f0;  // DAT_142f126f0: selected story mission index
static const uintptr_t RVA_ACTIVE_SLOT    = 0x2f2d6f0;  // DAT_142f2d6f0[0]: active profile slot
// profile completion offsets (from the profile diffs): award/trophy bitfield + flag bytes
static const uintptr_t P_AWARD = 0x18f4, P_STAT = 0xd30, P_SIZE = 0x20e4;

static const uintptr_t PAGE_WIDGETS = 0x70;
static const uintptr_t WIDGET_NEXT  = 0x3c0;
static const uintptr_t WIDGET_TREE  = 0x3a0;
static const uintptr_t N_L1 = 0x10, N_L2 = 0x18, N_L3 = 0x20;
static const uintptr_t E_LABEL = 0x38, E_FLAGS = 0x44, E_ACTION = 0x50;

enum { CMD_DUMP = 1, CMD_SET1 = 2, CMD_CLEAR = 4, CMD_COLLECT = 5, CMD_FULLDUMP = 6, CMD_SCAVENGE = 7 };

extern volatile int g_annotate;       // defined in ap_client.cpp: menu annotation on/off (F5 toggles)

typedef void (*TickFn)(void*);

uintptr_t g_base = 0;                 // shared with ap_client.cpp
static volatile LONG g_run = 1;
volatile uintptr_t g_curPage = 0;     // shared with ap_client.cpp
static volatile int g_sticky = 0;     // CMD_SET1 sticky menu-lock (dormant; ap_client drives live locking)
static unsigned long long g_ticks[3] = {0,0,0};
static TickFn g_orig[3] = {0,0,0};

static char g_cap[128][80];
static int  g_capCount = 0;
static unsigned long long g_lastSig = 0;
static FILE* g_log = nullptr;   // AP_capture.txt (game root) -- the full catalog dump

// Arena-enlargement fix (see enlargeArena() near DllMain): declared here so diagThread's startup log can reference them.
static const uintptr_t RVA_ARENA_SIZE = 0x24e23d8;   // DAT_1424e23d8: total scratch-arena byte size (int)
static const uintptr_t RVA_ARENA_FLAG = 0x2f43900;   // DAT_142f43900: arena init-once flag (1 = already allocated)
static const unsigned  ARENA_STOCK    = 0x03a00000;  // 58MB -- the known stock value (sanity guard before patching)
static const unsigned  ARENA_NEW      = 0x08000000;  // 128MB -- headroom for cross-level weapon effects under shuffle
static volatile unsigned g_arenaWas = 0, g_arenaFlagAtPatch = 0; static volatile int g_arenaPatched = 0;

static unsigned long long rd64(uintptr_t a) { __try { return *(volatile unsigned long long*)a; } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; } }
static unsigned int       rd32(uintptr_t a) { __try { return *(volatile unsigned int*)a; }       __except(EXCEPTION_EXECUTE_HANDLER) { return 0; } }
static void               wr32(uintptr_t a, unsigned int v) { __try { *(volatile unsigned int*)a = v; } __except(EXCEPTION_EXECUTE_HANDLER) {} }
static void rdstr(uintptr_t a, char* out, int n) {
    __try { const char* s = (const char*)a; int i = 0; for (; i < n - 1 && s[i]; i++) out[i] = s[i]; out[i] = 0; }
    __except(EXCEPTION_EXECUTE_HANDLER) { out[0] = 0; }
}

static int capHas(const char* s) {
    for (int i = 0; i < g_capCount; i++) { int eq = 1; for (int j = 0; ; j++) { if (g_cap[i][j] != s[j]) { eq = 0; break; } if (!s[j]) break; } if (eq) return 1; }
    return 0;
}


// Walk a menu page's entry tree.  CMD_COLLECT APPENDS each entry label (caller resets g_capCount; dedups by label) for
// the live menu catalog; CMD_SET1/CMD_CLEAR set/clear the native "locked" flag bit.
static void walkPage(uintptr_t page, int cmd) {
    if (!page) return;
    static uintptr_t visited[2048];
    int vcount = 0;
    uintptr_t widget = (uintptr_t)rd64(page + PAGE_WIDGETS);
    int wg = 0;
    while (widget && wg++ < 64) {
        uintptr_t head = (uintptr_t)rd64(widget + WIDGET_TREE);
        if (head) {
            uintptr_t stack[2048]; int sp = 0, guard = 0;
            stack[sp++] = head;
            while (sp > 0 && guard++ < 2048) {
                uintptr_t n = stack[--sp];
                if (!n) continue;
                int seen = 0;
                for (int i = 0; i < vcount; i++) if (visited[i] == n) { seen = 1; break; }
                if (seen) continue;
                if (vcount < 2048) visited[vcount++] = n; else continue;

                unsigned int flags = rd32(n + E_FLAGS);
                uintptr_t action = (uintptr_t)rd64(n + E_ACTION);
                char label[80]; rdstr((uintptr_t)rd64(n + E_LABEL), label, sizeof(label));

                if (cmd == CMD_COLLECT) {
                    if (action && label[0] && g_capCount < 128 && !capHas(label)) { strncpy_s(g_cap[g_capCount], label, _TRUNCATE); g_capCount++; }
                } else if (action) {
                    if (cmd == CMD_SET1)  wr32(n + E_FLAGS, flags | 1u);
                    else if (cmd == CMD_CLEAR) wr32(n + E_FLAGS, flags & ~1u);
                }
                uintptr_t c;
                c = (uintptr_t)rd64(n + N_L1); if (c && sp < 2048) stack[sp++] = c;
                c = (uintptr_t)rd64(n + N_L2); if (c && sp < 2048) stack[sp++] = c;
                c = (uintptr_t)rd64(n + N_L3); if (c && sp < 2048) stack[sp++] = c;
            }
        }
        widget = (uintptr_t)rd64(widget + WIDGET_NEXT);
    }
}

// run a cmd over the candidate current pages: tick page + screen's active/transition pages.
static void forEachPage(int cmd) {
    uintptr_t screen = (uintptr_t)rd64(g_base + RVA_SCREEN_SLOT);
    uintptr_t pages[3] = { g_curPage,
                           screen ? (uintptr_t)rd64(screen + SCREEN_PAGE_A) : 0,
                           screen ? (uintptr_t)rd64(screen + SCREEN_PAGE_B) : 0 };
    for (int i = 0; i < 3; i++) {
        if (!pages[i]) continue;
        int dup = 0; for (int j = 0; j < i; j++) if (pages[j] == pages[i]) dup = 1;
        if (!dup) walkPage(pages[i], cmd);
    }
}

static unsigned long long capSignature() {
    unsigned long long h = 1469598103934665603ULL;
    h ^= (unsigned)g_capCount; h *= 1099511628211ULL;
    for (int i = 0; i < g_capCount; i++)
        for (const char* p = g_cap[i]; *p; p++) { h ^= (unsigned char)*p; h *= 1099511628211ULL; }
    return h;
}


static void tickStory(void* page)     { g_curPage = (uintptr_t)page; g_ticks[0]++; if (g_orig[0]) g_orig[0](page); }
static void tickArcade(void* page)    { g_curPage = (uintptr_t)page; g_ticks[1]++; if (g_orig[1]) g_orig[1](page); }
static void tickChallenge(void* page) { g_curPage = (uintptr_t)page; g_ticks[2]++; if (g_orig[2]) g_orig[2](page); }

static void installTickWrapper(uintptr_t pageRva, int idx, TickFn wrapper) {
    uintptr_t slot = g_base + pageRva; DWORD oldP = 0;
    if (VirtualProtect((void*)slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldP)) {
        g_orig[idx] = (TickFn)rd64(slot);
        *(void**)slot = (void*)wrapper;
        VirtualProtect((void*)slot, sizeof(void*), oldP, &oldP);
    }
}



// ---- Robot Factory SOFTLOCK FIX --------------------------------------------------------------------------------
// The A* main loops FUN_1411d1d28 (loop top 0x11d1d85) and twin FUN_1411d1e50 (loop top 0x11d1ea8) only exit when
// the goal is found -- they NEVER check for an empty open list, so an unreachable goal (closed gate) loops forever
// popping index -1 (OOB). We patch each loop top: when the open-list head (DAT_142f5e0b0) is < 0, bail out returning
// 0 (= "no path"; the callers already handle param[0xfd]==0). Done via a code cave allocated within +-2GB so a
// 5-byte rel32 jump reaches it. Toggle with F3. The open-list head is at g_base+0x2f5e0b0.
static unsigned char* g_rfCave  = nullptr;
static uintptr_t      g_rfCave1 = 0, g_rfCave2 = 0;
static unsigned char  g_rfOrig[2][7];
static volatile int   g_rfApplied = 0;
static const uintptr_t RF_LOOP1 = 0x11d1d85, RF_LOOP2 = 0x11d1ea8;

static unsigned char* allocNear(uintptr_t target, size_t size) {
    for (uintptr_t a = (target & ~(uintptr_t)0xffff) - 0x10000; a > 0x10000 && (target - a) < 0x7ff00000; a -= 0x10000) {
        void* p = VirtualAlloc((void*)a, size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        if (p) return (unsigned char*)p;
    }
    return nullptr;
}
static void patchBytes(uintptr_t a, const void* b, int n) {
    DWORD old;
    if (VirtualProtect((void*)a, n, PAGE_EXECUTE_READWRITE, &old)) {
        memcpy((void*)a, b, n);
        VirtualProtect((void*)a, n, old, &old);
        FlushInstructionCache(GetCurrentProcess(), (void*)a, n);
    }
}
static void put_rel32(unsigned char* p, uintptr_t fromEnd, uintptr_t target) {
    int rel = (int)((long long)target - (long long)fromEnd); memcpy(p, &rel, 4);
}
// Suspend every other thread so we can patch live code safely (e.g. applying DURING the hang). If a thread is caught
// mid-instruction inside a loop-top patch range, redirect its RIP to redir1/redir2 (a clean re-entry) so resuming it
// can't execute a half-written instruction. Returns the held handles (resume with rfResumeAll). No printf while held.
static int rfSuspendOthers(HANDLE* held, int cap, uintptr_t redir1, uintptr_t redir2) {
    int nh = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    THREADENTRY32 te; te.dwSize = sizeof(te);
    DWORD myPid = GetCurrentProcessId(), myTid = GetCurrentThreadId();
    if (Thread32First(snap, &te)) do {
        if (te.th32OwnerProcessID != myPid || te.th32ThreadID == myTid) continue;
        HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, te.th32ThreadID);
        if (!th) continue;
        SuspendThread(th);
        CONTEXT ctx; memset(&ctx, 0, sizeof(ctx)); ctx.ContextFlags = CONTEXT_CONTROL;
        if (GetThreadContext(th, &ctx)) {
            uintptr_t rip = (uintptr_t)ctx.Rip;
            if (rip >= g_base + RF_LOOP1 && rip < g_base + RF_LOOP1 + 7) { ctx.Rip = redir1; SetThreadContext(th, &ctx); }
            else if (rip >= g_base + RF_LOOP2 && rip < g_base + RF_LOOP2 + 7) { ctx.Rip = redir2; SetThreadContext(th, &ctx); }
        }
        if (nh < cap) held[nh++] = th; else { ResumeThread(th); CloseHandle(th); }
    } while (Thread32Next(snap, &te));
    CloseHandle(snap);
    return nh;
}
static void rfResumeAll(HANDLE* held, int nh) {
    for (int i = 0; i < nh; i++) { ResumeThread(held[i]); CloseHandle(held[i]); }
}

static void rfApply() {
    if (g_rfApplied || !g_base) return;
    uintptr_t openHead = g_base + 0x2f5e0b0;
    if (!g_rfCave) {
        g_rfCave = allocNear(g_base, 0x1000);
        if (!g_rfCave) { printf("[rf-fix] cave alloc failed\n"); return; }
        unsigned char* c = g_rfCave; int o = 0;
        g_rfCave1 = (uintptr_t)(c + o);                                                  // cave for FUN_1411d1d28 (R12)
        c[o++]=0x48; c[o++]=0xB8; memcpy(c+o,&openHead,8); o+=8;                          // MOV RAX, openHead
        c[o++]=0x4C; c[o++]=0x63; c[o++]=0x20;                                            // MOVSXD R12,[RAX]
        c[o++]=0x4D; c[o++]=0x85; c[o++]=0xE4;                                            // TEST R12,R12
        c[o++]=0x0F; c[o++]=0x88; put_rel32(c+o,(uintptr_t)(c+o+4),g_base+0x11d1e2d); o+=4;  // JS -> ret 0 (regs restored)
        c[o++]=0xE9;             put_rel32(c+o,(uintptr_t)(c+o+4),g_base+0x11d1d8c); o+=4;   // JMP -> loop body
        g_rfCave2 = (uintptr_t)(c + o);                                                  // cave for FUN_1411d1e50 (RSI)
        c[o++]=0x48; c[o++]=0xB8; memcpy(c+o,&openHead,8); o+=8;                          // MOV RAX, openHead
        c[o++]=0x48; c[o++]=0x63; c[o++]=0x30;                                            // MOVSXD RSI,[RAX]
        c[o++]=0x48; c[o++]=0x85; c[o++]=0xF6;                                            // TEST RSI,RSI
        c[o++]=0x0F; c[o++]=0x88; put_rel32(c+o,(uintptr_t)(c+o+4),g_base+0x11d1f5f); o+=4;  // JS -> ret 0 (regs restored)
        c[o++]=0xE9;             put_rel32(c+o,(uintptr_t)(c+o+4),g_base+0x11d1eaf); o+=4;   // JMP -> loop body
    }
    HANDLE held[512];
    int nh = rfSuspendOthers(held, 512, g_rfCave1, g_rfCave2);     // safe even if a thread is spinning the loop now
    memcpy(g_rfOrig[0], (void*)(g_base+RF_LOOP1), 7);
    memcpy(g_rfOrig[1], (void*)(g_base+RF_LOOP2), 7);
    unsigned char p1[7]={0xE9,0,0,0,0,0x90,0x90}; put_rel32(p1+1, g_base+RF_LOOP1+5, g_rfCave1);
    unsigned char p2[7]={0xE9,0,0,0,0,0x90,0x90}; put_rel32(p2+1, g_base+RF_LOOP2+5, g_rfCave2);
    patchBytes(g_base+RF_LOOP1, p1, 7);
    patchBytes(g_base+RF_LOOP2, p2, 7);
    rfResumeAll(held, nh);
    g_rfApplied = 1;
    printf("[rf-fix] APPLIED (cave=%p, parked %d threads). If a hang was in progress it should release now.\n", g_rfCave, nh);
}
static void rfRestore() {
    if (!g_rfApplied) return;
    HANDLE held[512];
    int nh = rfSuspendOthers(held, 512, g_base+RF_LOOP1, g_base+RF_LOOP2);   // mid-jmp threads -> back to the loop top
    patchBytes(g_base+RF_LOOP1, g_rfOrig[0], 7);
    patchBytes(g_base+RF_LOOP2, g_rfOrig[1], 7);
    rfResumeAll(held, nh);
    g_rfApplied = 0;
    printf("[rf-fix] RESTORED (parked %d threads)\n", nh);
}

// ---------------------------------------------------------------------------------------------------------------------
// Objective-render hook. FUN_1411ad4c8(obj /*RCX*/, ...) is the objective-tracker line renderer (confirmed by the read-
// BP unwind on the objective text). It's called every frame for each visible objective with the objective struct in RCX:
// obj[0] = text id (which objective), obj+0x24 = status (the field it branches on to print "Complete"). The HUD message
// array is a transient per-frame buffer (empty when async-polled), so we can't poll it -- but the renderer hands us the
// live obj pointer during render. We inline-detour it, read (text id, status), dedup per text id on the game thread, and
// push only on a status CHANGE into the ring that diagThread drains. That transition (active -> complete) is the signal.
static const uintptr_t OBJ_FN = 0x11ad4c8;          // FUN_1411ad4c8 (Ghidra 0x1411ad4c8; RVA drops to 0x11ad4c8)
static unsigned char*  g_objCave = nullptr;
static volatile int    g_objHooked = 0;
static unsigned char   g_objOrig[8];
static volatile int    g_objQTextID[256], g_objQStatus[256];
static volatile LONG   g_objQReady[256] = {0};     // per-slot: 0 = empty, 1 = data written & ready to consume
static volatile LONG   g_objQHead = 0;             // reservation counter (game thread, interlocked)
static LONG            g_objQTail = 0;             // consumer position (diag thread only)
// dedup cache -- game-thread only (the hook callback), so no locking needed
static int g_objCacheID[64], g_objCacheStatus[64], g_objCacheN = 0;
// shared with ap_client.cpp: per (mission, objective) completion flags. index = mission*64 + (textID - OBJ_TID_BASE).
// set to 1 when an objective's status reaches 5 (COMPLETE); the AP client polls these and sends LocationChecks.
static const int OBJ_TID_BASE = 1062;
volatile char g_objDone[640] = {0};
// a few objectives have a textID outside the 1062-1117 block (Space Station "Collect the crystals" = 2715); remap
// onto an unused in-window slot (1118+) so it fits the per-mission 64-slot index.  MUST match data.py OBJ_REMAP.
static inline int objEff(int tid) { return tid == 2715 ? 1118 : tid; }
// the render hook hands us each live objective struct; we KEEP the pointer (idx = mission*64 + textID-1062) and poll
// obj+0x24 every tick (pollObjPtrs) so a late status->5 is caught even if the objective never re-renders. obj[0]
// (=textID) is re-checked on each poll to guard against the struct being freed/reused after the mission ends.
static volatile uintptr_t g_objQObj[256];               // obj ptr per ring entry (for the [OBJ] log + struct analysis)
static volatile uintptr_t g_objPtr[640] = {0};
static int g_objPtrMission = -2;

static void wpnGameThreadTick();   // defined after the weapon block -- runs the weapon-sound precache on the GAME thread

static void objHookCb(uintptr_t obj) {
    wpnGameThreadTick();                         // GAME-thread weapon-sound precache (the FMOD bank loader crashes off-thread)
    int textID = -1, status = -1;
    __try { textID = *(int*)obj; status = *(int*)(obj + 0x24); } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    // record for AP, keyed on (current mission, textID). track the struct ptr; flag complete on status 5. idempotent.
    int eff = objEff(textID);
    if (eff >= OBJ_TID_BASE && eff < OBJ_TID_BASE + 64) {
        int m = (int)rd32(g_base + RVA_MISSION_IDX);
        if (m >= 0 && m < 10) {
            int idx = m * 64 + (eff - OBJ_TID_BASE);
            g_objPtr[idx] = obj;                        // keep the struct so pollObjPtrs catches a late completion
            if (status == 5) g_objDone[idx] = 1;
        }
    }
    bool changed; int i = 0;
    for (; i < g_objCacheN; i++) {
        if (g_objCacheID[i] == textID) { changed = (g_objCacheStatus[i] != status); g_objCacheStatus[i] = status; break; }
    }
    if (i == g_objCacheN) {                          // first time we've seen this objective
        if (g_objCacheN < 64) { g_objCacheID[g_objCacheN] = textID; g_objCacheStatus[g_objCacheN] = status; g_objCacheN++; }
        changed = true;
    }
    if (!changed) return;                            // unchanged status -> ignore (fires ~300x/sec otherwise)
    LONG s = (InterlockedIncrement(&g_objQHead) - 1) & 255;
    g_objQTextID[s] = textID; g_objQStatus[s] = status; g_objQObj[s] = obj;
    g_objQReady[s] = 1;
}

// Poll the tracked objective structs directly (the authoritative status), so a status->5 that the render hook never
// re-displays still fires.  Re-validates obj[0]==expected textID to drop freed/reused structs; clears all on a mission
// change.  Runs on the diag thread; sets g_objDone (the AP client sends from there).
static void pollObjPtrs() {
    if (!g_base) return;
    int m = (int)rd32(g_base + RVA_MISSION_IDX);
    if (m != g_objPtrMission) {                          // mission change -> the previous mission's structs are stale
        for (int i = 0; i < 640; i++) g_objPtr[i] = 0;
        g_objPtrMission = m;
    }
    for (int i = 0; i < 640; i++) {
        uintptr_t p = g_objPtr[i];
        if (!p) continue;
        __try {
            if (*(int*)p == (i & 63) + OBJ_TID_BASE && *(int*)(p + 0x24) == 5) g_objDone[i] = 1;
        } __except(EXCEPTION_EXECUTE_HANDLER) { g_objPtr[i] = 0; }
    }
    // the mission's objectives are a CONTIGUOUS 0x30-stride array (confirmed live): from the base (min tracked ptr)
    // walk the whole array so even an objective that never re-renders is caught.  textID-validated + SEH-guarded;
    // any false match maps to a (mission,textID) the seed lacks, which the server harmlessly ignores.
    if (m >= 0 && m < 10) {
        uintptr_t base = 0;
        for (int off = 0; off < 64; off++) { uintptr_t p = g_objPtr[m * 64 + off]; if (p && (!base || p < base)) base = p; }
        if (base) for (int k = -2; k < 48; k++) {
            uintptr_t p = base + (intptr_t)k * 0x30;
            __try {
                int eff = objEff(*(int*)p);
                if (eff >= OBJ_TID_BASE && eff < OBJ_TID_BASE + 64 && *(int*)(p + 0x24) == 5)
                    g_objDone[m * 64 + (eff - OBJ_TID_BASE)] = 1;
            } __except(EXCEPTION_EXECUTE_HANDLER) {}
        }
    }
}

// Suspend every other thread; if any is caught with RIP inside [lo,hi) (the bytes we are about to overwrite), redirect
// it to redirTo so resuming can't run a half-written instruction. Returns held handles (resume with rfResumeAll).
static int suspendOthers(HANDLE* held, int cap, uintptr_t lo, uintptr_t hi, uintptr_t redirTo) {
    int nh = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    THREADENTRY32 te; te.dwSize = sizeof(te);
    DWORD myPid = GetCurrentProcessId(), myTid = GetCurrentThreadId();
    if (Thread32First(snap, &te)) do {
        if (te.th32OwnerProcessID != myPid || te.th32ThreadID == myTid) continue;
        HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, te.th32ThreadID);
        if (!th) continue;
        SuspendThread(th);
        CONTEXT ctx; memset(&ctx, 0, sizeof(ctx)); ctx.ContextFlags = CONTEXT_CONTROL;
        if (redirTo && GetThreadContext(th, &ctx)) {
            uintptr_t rip = (uintptr_t)ctx.Rip;
            if (rip >= lo && rip < hi) { ctx.Rip = redirTo; SetThreadContext(th, &ctx); }
        }
        if (nh < cap) held[nh++] = th; else { ResumeThread(th); CloseHandle(th); }
    } while (Thread32Next(snap, &te));
    CloseHandle(snap);
    return nh;
}

static void objHookInstall() {
    if (g_objHooked || !g_base) return;
    uintptr_t fn = g_base + OBJ_FN;
    if (!g_objCave) {
        g_objCave = allocNear(g_base, 0x200);
        if (!g_objCave) { printf("[obj-hook] cave alloc failed\n"); return; }
        unsigned char* c = g_objCave; int o = 0;
        c[o++]=0x50;                                                   // push rax
        c[o++]=0x51;                                                   // push rcx
        c[o++]=0x52;                                                   // push rdx
        c[o++]=0x41; c[o++]=0x50;                                      // push r8
        c[o++]=0x41; c[o++]=0x51;                                      // push r9
        c[o++]=0x41; c[o++]=0x52;                                      // push r10
        c[o++]=0x41; c[o++]=0x53;                                      // push r11   (7 pushes -> RSP 16-aligned)
        c[o++]=0x48; c[o++]=0x83; c[o++]=0xEC; c[o++]=0x20;            // sub rsp,0x20  (shadow space)
        // RCX is still the live obj (pushes don't modify it) -> it's already objHookCb's 1st arg; nothing to reload.
        uintptr_t cb = (uintptr_t)&objHookCb;
        c[o++]=0x48; c[o++]=0xB8; memcpy(c+o,&cb,8); o+=8;            // mov rax, &objHookCb
        c[o++]=0xFF; c[o++]=0xD0;                                      // call rax  (objHookCb(RCX = obj))
        c[o++]=0x48; c[o++]=0x83; c[o++]=0xC4; c[o++]=0x20;            // add rsp,0x20
        c[o++]=0x41; c[o++]=0x5B;                                      // pop r11
        c[o++]=0x41; c[o++]=0x5A;                                      // pop r10
        c[o++]=0x41; c[o++]=0x59;                                      // pop r9
        c[o++]=0x41; c[o++]=0x58;                                      // pop r8
        c[o++]=0x5A;                                                   // pop rdx
        c[o++]=0x59;                                                   // pop rcx
        c[o++]=0x58;                                                   // pop rax
        c[o++]=0x48; c[o++]=0x8B; c[o++]=0xC4;                         // mov rax,rsp           \ displaced prologue from
        c[o++]=0x44; c[o++]=0x89; c[o++]=0x48; c[o++]=0x20;            // mov [rax+0x20],r9d     / FUN_1411ad4c8 (7 bytes)
        c[o++]=0xFF; c[o++]=0x25; c[o++]=0x00; c[o++]=0x00; c[o++]=0x00; c[o++]=0x00;  // jmp qword ptr [rip+0]
        uintptr_t back = fn + 7; memcpy(c+o,&back,8); o+=8;            // -> FUN_1411ad4c8 + 7  (absolute, no clobber)
    }
    memcpy(g_objOrig, (void*)fn, 7);
    unsigned char p[7] = {0xE9,0,0,0,0,0x90,0x90}; put_rel32(p+1, fn+5, (uintptr_t)g_objCave);
    HANDLE held[512];
    int nh = suspendOthers(held, 512, fn, fn+7, (uintptr_t)g_objCave);   // mid-prologue threads -> run the full cave
    patchBytes(fn, p, 7);
    rfResumeAll(held, nh);
    g_objHooked = 1;
    printf("[obj-hook] INSTALLED on FUN_1411ad4c8 (g+%llx, cave=%p, parked %d). Objective status changes -> [OBJ] lines.\n",
           (unsigned long long)OBJ_FN, g_objCave, nh);
}
static void objHookRemove() {
    if (!g_objHooked) return;
    uintptr_t fn = g_base + OBJ_FN;
    HANDLE held[512];
    int nh = suspendOthers(held, 512, fn, fn+7, fn);     // mid-jmp threads -> back to the (restored) prologue top
    patchBytes(fn, g_objOrig, 7);
    rfResumeAll(held, nh);
    g_objHooked = 0;
    printf("[obj-hook] REMOVED (parked %d)\n", nh);       // cave is intentionally left mapped (a thread may still be in it)
}

// ---- GIVE-WEAPON hook: load a weapon's sound bank on the GAME thread when it enters the player's hands ----------------
// FUN_14112cea0(pawn[RCX], slot[RDX]) runs on the game thread on every weapon give (spawn loadout + pickups). A level
// keeps only a small set of weapon sounds resident, so a weapon outside it is silent. Calling the per-weapon loader
// FUN_141126880(slot) at give-entry loads its bank just-in-time -- and FMOD IS safe here (game thread), unlike the diag
// thread (which crashed). Prologue 48 8b c4 48 89 58 08 (mov rax,rsp; mov [rax+8],rbx) = clean 7-byte displacement.
static const uintptr_t GIVE_FN = 0x112cea0;              // FUN_14112cea0 (Ghidra 0x14112cea0)
static const uintptr_t WPN_PRECACHE_RVA = 0x1126880;    // FUN_141126880(int slot): per-weapon sound-bank loader (RVA: 0x141126880-0x140000000, do NOT drop the nibble)
static unsigned char*  g_giveCave = nullptr;
static volatile int    g_giveHooked = 0;
static unsigned char   g_giveOrig[8];
static volatile int    g_giveLoadOn = 0;   // load given weapons' sound only when ON (set by F7). The EARLY spawn-loadout give
                                           // (level load, sound system not up) crashes FUN_141126880, so default OFF.

static const uintptr_t WPN_PRECACHE_ALL_RVA = 0x1126994;   // FUN_141126994(void): the ARCADE "precache ALL weapons" routine
                                                           // -- loops slots 0..0x28 -> FUN_141126880, loads ammo sounds, then
                                                           // FUN_1411208fc() COMMITS the loads (the step a standalone loader call missed).
static volatile long      g_allWpnLoaded = 0;              // one-shot per LEVEL
static volatile uintptr_t g_lastSndReg = 0;               // last sound-registry ptr (changes per level) -> re-arm detector

static void giveHookCb(uintptr_t /*pawn*/, int slot) {
    if (!g_base || slot <= 0 || slot >= 0x2a) return;    // UN-gated: fire at the spawn-loadout give (the level-load phase),
    uintptr_t reg = 0;                                   // not just on F7 -- a precache only "takes" during load, not gameplay.
    __try { reg = *(volatile uintptr_t*)(g_base + 0x2fdb0a0); } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    if (reg < 0x10000) return;                                          // sound system not up yet -> skip (fires on a later give)
    if (reg != g_lastSndReg) { g_lastSndReg = reg; InterlockedExchange(&g_allWpnLoaded, 0); }   // new level (registry re-allocated)
    if (InterlockedExchange(&g_allWpnLoaded, 1)) return;               // ONCE per level (at the first give = spawn loadout)
    if (g_log) { fprintf(g_log, "[give-hook] >>> FUN_141126994() precache-ALL @ first-give (level-load phase) reg=%p\n", (void*)reg); fflush(g_log); }
    __try {
        ((void(*)(void))(g_base + WPN_PRECACHE_ALL_RVA))();
        if (g_log) { fprintf(g_log, "[give-hook] <<< FUN_141126994() OK -- full weapon-sound roster loaded + committed\n"); fflush(g_log); }
        printf("[give-hook] FUN_141126994() precache-ALL OK (level-load phase)\n");
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        if (g_log) { fprintf(g_log, "[give-hook] !!! FUN_141126994() FAULTED\n"); fflush(g_log); }
        printf("[give-hook] FUN_141126994() FAULTED (caught by SEH)\n");
    }
}

static void giveHookInstall() {
    if (g_giveHooked || !g_base) return;
    uintptr_t fn = g_base + GIVE_FN;
    if (!g_giveCave) {
        g_giveCave = allocNear(g_base, 0x200);
        if (!g_giveCave) { printf("[give-hook] cave alloc failed\n"); return; }
        unsigned char* c = g_giveCave; int o = 0;
        c[o++]=0x50; c[o++]=0x51; c[o++]=0x52;                         // push rax,rcx,rdx
        c[o++]=0x41; c[o++]=0x50; c[o++]=0x41; c[o++]=0x51;            // push r8,r9
        c[o++]=0x41; c[o++]=0x52; c[o++]=0x41; c[o++]=0x53;            // push r10,r11   (7 pushes -> RSP 16-aligned)
        c[o++]=0x48; c[o++]=0x83; c[o++]=0xEC; c[o++]=0x20;            // sub rsp,0x20   (shadow space)
        // RCX=pawn, RDX=slot are untouched by the pushes -> already giveHookCb(pawn, slot)'s 1st/2nd args.
        uintptr_t cb = (uintptr_t)&giveHookCb;
        c[o++]=0x48; c[o++]=0xB8; memcpy(c+o,&cb,8); o+=8;            // mov rax, &giveHookCb
        c[o++]=0xFF; c[o++]=0xD0;                                      // call rax
        c[o++]=0x48; c[o++]=0x83; c[o++]=0xC4; c[o++]=0x20;            // add rsp,0x20
        c[o++]=0x41; c[o++]=0x5B; c[o++]=0x41; c[o++]=0x5A;            // pop r11,r10
        c[o++]=0x41; c[o++]=0x59; c[o++]=0x41; c[o++]=0x58;            // pop r9,r8
        c[o++]=0x5A; c[o++]=0x59; c[o++]=0x58;                         // pop rdx,rcx,rax
        c[o++]=0x48; c[o++]=0x8B; c[o++]=0xC4;                         // mov rax,rsp           \ displaced prologue from
        c[o++]=0x48; c[o++]=0x89; c[o++]=0x58; c[o++]=0x08;            // mov [rax+8],rbx        / FUN_14112cea0 (7 bytes)
        c[o++]=0xFF; c[o++]=0x25; c[o++]=0x00; c[o++]=0x00; c[o++]=0x00; c[o++]=0x00;  // jmp qword ptr [rip+0]
        uintptr_t back = fn + 7; memcpy(c+o,&back,8); o+=8;            // -> FUN_14112cea0 + 7
    }
    memcpy(g_giveOrig, (void*)fn, 7);
    unsigned char p[7] = {0xE9,0,0,0,0,0x90,0x90}; put_rel32(p+1, fn+5, (uintptr_t)g_giveCave);
    HANDLE held[512];
    int nh = suspendOthers(held, 512, fn, fn+7, (uintptr_t)g_giveCave);
    patchBytes(fn, p, 7);
    rfResumeAll(held, nh);
    g_giveHooked = 1;
    printf("[give-hook] INSTALLED on FUN_14112cea0 (g+%llx, cave=%p, parked %d). Loads each given weapon's sound bank.\n",
           (unsigned long long)GIVE_FN, g_giveCave, nh);
}

// Resolve an objective text id -> its string. The string table is a static array at g+0x24e2440 (its entries are
// pointers relocated to runtime addresses); entry[textID] points to the objective text. SEH-guarded.
static void resolveObjText(int textID, char* out, int cap) {
    out[0] = 0;
    __try {
        const char* s = *(const char**)(g_base + 0x24e2440 + (uintptr_t)textID * 8);
        int i = 0; for (; i < cap - 2 && s[i]; i++) out[i] = (s[i] == '\n') ? ' ' : s[i]; out[i] = 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { out[0] = 0; }
}



// ===================== WEAPONS-AS-ITEMS (A gate / B player-loadout shuffle; pickup hook + enemy C added next) =========
// Shared with ap_client.cpp (writes these from slot_data + received items). Weapon model:
// pawn (int*); pawn[slot+0x400] = "has weapon" (int @ byte 0x1000 + slot*4); pawn[0x36] = current slot (byte 0xd8);
// *(longlong*)(pawn+0x1a68) (= pawn[0x69a]) != 0 => the PLAYER (HUD/notify object). Local-player views @ DAT_142f5ead8
// (stride 0x12a0), count @ DAT_143f27f58; pawn = *(int**)(*(longlong*)(view+0x1c8)+0x140). Valid weapon slots 1..0x29.
volatile char      g_weaponGating = 0, g_weaponShuffle = 0, g_enemyShuffle = 0, g_ammoTopUp = 0, g_mapShuffle = 0, g_ammoTopped = 0;
volatile char      g_weaponIsItem[64] = {0}, g_weaponUnlocked[64] = {0};
static const char* g_wpnNames[42] = {   // slot -> name (mirrors apworld data.WEAPONS) for the [wpn-map] shuffle log
    "Fists", "Silenced Pistol", "Silenced Pistol x2", "Silenced Luger", "Silenced Luger x2",
    "Luger Pistol", "Luger Pistol x2", "Garrett Revolver", "Garrett Revolver x2", "Tactical 12-Gauge",
    "Minigun", "Sniper Rifle", "Vintage Rifle", "Flamethrower", "Fire Extinguisher",
    "Soviet S47", "Soviet S47 x2", "ElectroTool", "Scifi Handgun", "Rocket Launcher",
    "Homing Launcher", "Lasergun", "Plasma Autorifle", "Grenade Launcher", "Proximity Mine",
    "Remote Mine", "Timed Mine", "TNT", "Tommy Gun", "Tommy Gun x2",
    "SBP90 Machinegun", "SBP90 Machinegun x2", "Shotgun", "Shotgun x2", "Brick",
    "Digital Camera", "Temporal Uplink", "Crossbow", "Gun Powder", "Scifi Handgun(2)",
    "Unarmed", "Blank",
};
volatile int       g_weaponRemap[64];
// Weapon Shuffle Scope 'within_level': the seed ships one slot->weapon map PER MISSION instead of one global map. The
// weapon tables are rebuilt from the pristine snapshot on EVERY level load (wpnCaveRemap), so we just load the map for
// the level being loaded -- which is what lets the same slot be a different gun in a different level.
volatile int       g_weaponRemapLvl[10][64];
volatile char      g_weaponRemapPerLevel = 0;   // 1 = use g_weaponRemapLvl[mission] instead of the global g_weaponRemap
char               g_prevOwned[0x2a] = {0};   // player's per-slot owned state last frame -> grant ammo once when a weapon is acquired
int                g_prevAmmo [0x2a] = {0};   // player's per-ammo-TYPE count last frame -> a rise = a pickup (see the drop top-up)
// Share of a weapon's MAX ammo granted when you first ACQUIRE it, per difficulty. This is the ONLY thing scaled by
// difficulty -- floor/enemy-drop pickups always hand over the game's own AmmoOnPickup amounts, unscaled. Hard was 20,
// which made the opening of a Hard mission very thin (you get one grant, then live off the level's pickups); raised to
// 40. Tune these three numbers if the balance still feels off.
static const int AMMO_PCT_EASY = 80, AMMO_PCT_NORMAL = 50, AMMO_PCT_HARD = 40;
volatile int       g_difficulty = 1;          // 0=Easy 1=Normal 2=Hard. Drives ammo grant % (Easy 80 / Normal 50 / Hard 20). Default Normal until the real global is wired.
volatile long long g_weaponItemBase = 0xA00;
// Weapons-as-items (gating + shuffle) apply ONLY to STORY missions -- Arcade/Challenge matches hand out their own fixed
// loadouts, so gating would strand you weaponless and shuffling would scramble them. g_inStory is refreshed at each level
// load (the cave) from the level-load mode (DAT_143f27f54); the story modes are the per-mission level ids (= apworld
// STORY_LEVEL_ID). Arcade/challenge levels carry other ids, so isStoryMode() is false and the feature stands down.
static const int   STORY_MODES[10] = { 0x23, 0x14, 0x22, 0x28, 0x26, 0x20, 0x1f, 0x30, 0x29, 0x27 };
volatile char      g_inStory = 0;
static inline bool isStoryMode(int mode) { for (int i = 0; i < 10; i++) if (STORY_MODES[i] == mode) return true; return false; }

static const uintptr_t RVA_LP_VIEWS = 0x2f5ead8, RVA_LP_COUNT = 0x3f27f58;
static const uintptr_t WP_STRIDE = 0x12a0, OFF_VIEW_ENT = 0x1c8, OFF_ENT_PAWN = 0x140;
static const uintptr_t OFF_WFLAGS = 0x1000, OFF_WCUR = 0xd8, OFF_WHUD = 0x1a68, OFF_WAMMO = 0x10a8;   // pawn byte offsets
// weapon-SOUND precache: a cross-level shuffled/granted weapon fires SILENTLY because the mission only loads the sound
// banks for ITS OWN weapon set. The per-weapon sound config is static (baked in), but the bank must be registered into the
// runtime sound registry. FUN_141126880(slot) reads the static ids and registers a slot's banks -- idempotent, no pawn arg.
static const uintptr_t RVA_WPN_PRECACHE = 0x1126880;  // FUN_141126880(int slot): load a weapon slot's sound banks (0x141126880-base; NOT 0x126880)
static const uintptr_t RVA_SFX_PLAY_TBL = 0x2512a2c;  // DAT_142512a2c: play-path slot->sfx_idx (int stride 0xc) -- diagnostic probe
static const uintptr_t RVA_SND_CFG_SFX  = 0x250c2cc;  // DAT_14250c2cc: 0x240-stride sound cfg (sfx-view); field0 = firing sound id
static const uintptr_t RVA_SND_REGISTRY = 0x2fdb0a0;  // DAT_142fdb0a0: POINTER to the sound registry; [id*0x20+0x18]=loaded-bank handle (0=silent)

// The weapon a slot currently YIELDS = its shuffle-remap identity (identity if no shuffle). Gating keys on THIS, so
// unlocking a WEAPON lets you use it wherever the shuffle placed it, and a level pickup is usable iff ITS (shuffled)
// weapon is one you've unlocked. The akimbo "(x2)" dual-wields are their OWN pickups and their OWN AP items, so they
// gate on their own identity -- do NOT collapse them onto their base single (that made an owned akimbo unusable).
static inline int wpnEff(int s) {
    int w = (s >= 0 && s < 64) ? g_weaponRemap[s] : s;
    if (w < 0 || w >= 0x2a) w = s;
    return w;
}
static inline bool wpnLocked(int s) {
    if (!g_weaponGating || s <= 0 || s >= 0x2a) return false;
    int w = wpnEff(s);
    return g_weaponIsItem[w] && !g_weaponUnlocked[w];
}
// The placeable-mine class by WEAPON IDENTITY (shuffle-aware via wpnEff): Proximity / Remote / Timed / TNT. These double
// as the "explosive" answer to destruction objectives, but TNT stacks to a SINGLE charge -- so a mission that expects
// several detonations (Siberia's Timed Mine, Chicago's TNT) is unwinnable from one mine with no floor resupply. weaponPass
// keeps an owned mine topped to its ammo-max every poll, making it effectively unlimited once you've acquired it.
static inline bool wpnIsMine(int identity) {
    return identity == 24 || identity == 25 || identity == 26 || identity == 27;
}

// once-per-pawn remap tracking, reset on mission change
static uintptr_t g_wpnSeen[1024]; static int g_wpnSeenN = 0; static int g_wpnSeenMission = -2; static bool g_wpnLogged = false;
static volatile int g_wpnPrecached = -2;   // mission whose weapon-sound banks we've force-loaded; set on the GAME thread (FMOD-unsafe off-thread)
static bool wpnSeen(uintptr_t p) { for (int i = 0; i < g_wpnSeenN; i++) if (g_wpnSeen[i] == p) return true; if (g_wpnSeenN < 1024) g_wpnSeen[g_wpnSeenN++] = p; return false; }

// diagnostic: the runtime loaded-bank handle for a weapon slot, via the PLAY path (slot -> sfx_idx -> soundId -> registry).
// 0 = the bank is not resident -> the weapon fires silently. Mirrors give-weapon's play lookup (FUN_14112cea0).
static unsigned long long wpnSoundHandle(int slot) {
    __try {
        int sfx = *(volatile int*)(g_base + RVA_SFX_PLAY_TBL + (uintptr_t)slot * 0xc);
        if (sfx < 0) return 0;
        int sid = *(volatile int*)(g_base + RVA_SND_CFG_SFX + (uintptr_t)sfx * 0x240);
        uintptr_t reg = *(volatile uintptr_t*)(g_base + RVA_SND_REGISTRY);
        if (reg < 0x10000) return 0;
        return *(volatile unsigned long long*)(reg + (uintptr_t)sid * 0x20 + 0x18);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// Force-load the sound banks for the whole valid weapon roster (slots 1..0x29) so cross-level shuffled/granted weapons are
// audible. FUN_141126880(slot) is idempotent and needs no pawn/world arg, but the sound registry must be up (mission
// precache done) first -- returns false to retry next frame until it is. The before/after null-bank counts prove the fix.
static bool wpnPrecacheRoster() {
    uintptr_t reg = 0;
    __try { reg = *(volatile uintptr_t*)(g_base + RVA_SND_REGISTRY); } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    if (reg < 0x10000) return false;                                  // sound system not up yet -- retry next frame
    int nullBefore = 0; for (int s = 1; s < 0x2a; s++) if (!wpnSoundHandle(s)) nullBefore++;
    typedef void (*precache_t)(int);
    precache_t precache = (precache_t)(g_base + RVA_WPN_PRECACHE);
    for (int s = 1; s < 0x2a; s++) { __try { precache(s); } __except(EXCEPTION_EXECUTE_HANDLER) {} }
    int nullAfter = 0; for (int s = 1; s < 0x2a; s++) if (!wpnSoundHandle(s)) nullAfter++;
    printf("[wpn-snd] precached weapon roster 1..0x29: unloaded sound banks %d -> %d (%d newly loaded)\n",
           nullBefore, nullAfter, nullBefore - nullAfter);
    return true;
}

// Called from the objective-render hook (objHookCb) = the GAME thread. Force-loads the whole weapon roster's sound banks
// once per mission when the weapon feature is on, so cross-level shuffled/granted weapons are audible. MUST be on the game
// thread (the FMOD bank loader FUN_141126880 crashes if called from the diag thread). The g_wpnPrecached != m guard makes
// it a cheap no-op after the first success; a new mission (m differs) re-runs it; F7 resets it for the test.
static void wpnGameThreadTick() {
    static bool liveLogged = false;
    if (!g_base || (!g_weaponShuffle && !g_weaponGating && !g_mapShuffle)) return;
    int m = (int)rd32(g_base + RVA_MISSION_IDX);
    if (!liveLogged) {                                   // one-shot: confirms objHookCb (game thread) reaches us + shows the registry ptr
        liveLogged = true;
        uintptr_t reg = 0; __try { reg = *(volatile uintptr_t*)(g_base + RVA_SND_REGISTRY); } __except(EXCEPTION_EXECUTE_HANDLER) {}
        printf("[wpn-snd] game-thread tick LIVE (objHookCb firing): mission=%d sndRegistry(deref 0x%llx)=%p\n",
               m, (unsigned long long)RVA_SND_REGISTRY, (void*)reg);
    }
    if (g_wpnPrecached != m) { if (wpnPrecacheRoster()) g_wpnPrecached = m; }
}

// Remap a pawn's loadout ONCE: compute the new owned-slot set then apply (safe for permutation cycles -- never drops a
// weapon). Returns false if the pawn has no weapons yet (not spawned) so the caller can retry next frame.
static bool wpnRemapPawn(uintptr_t pawn) {
    char nf[64]; for (int i = 0; i < 64; i++) nf[i] = 0;
    bool any = false;
    for (int s = 0; s < 0x2a; s++)
        if (*(volatile int*)(pawn + OFF_WFLAGS + s * 4)) { int r = g_weaponRemap[s]; if (r < 0 || r >= 0x2a) r = s; nf[r] = 1; any = true; }
    if (!any) return false;
    for (int s = 0; s < 0x2a; s++) *(volatile int*)(pawn + OFF_WFLAGS + s * 4) = nf[s] ? 1 : 0;
    // ammo is per-ammo-TYPE, not per-slot (a per-slot transfer left most remapped weapons empty), so we can't move it by
    // slot. v1: top each type up to a usable amount ONCE so the remapped weapons can fire (proper per-weapon ammo is TBD).
    for (int i = 0; i < 0x2a; i++) { volatile int* a = (volatile int*)(pawn + OFF_WAMMO + i * 4); if (*a < 100) *a = 100; }
    int cur = *(volatile int*)(pawn + OFF_WCUR);
    if (cur > 0 && cur < 0x2a) { int rc = g_weaponRemap[cur]; if (rc > 0 && rc < 0x2a) *(volatile int*)(pawn + OFF_WCUR) = rc; }
    return true;
}

// per-frame pass over local-player pawns: B remaps the starting loadout once, A clears locked slots every frame.
static void weaponPass() {
    if (!g_base || (!g_weaponGating && !g_weaponShuffle && !g_ammoTopUp)) return;
    int diffRaw = -1;
    __try {   // TS2 STORY difficulty: direct global @ rva 0x2f126f4 -- sits right after the selected-mission index (0x2f126f0).
        diffRaw = *(volatile int*)(g_base + 0x2f126f4);   // found empirically (2 on Hard, 0 on Easy). 0=Easy 1=Normal 2=Hard
        if ((unsigned)diffRaw < 3) g_difficulty = diffRaw;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    { static int lastD = -99; if ((int)g_difficulty != lastD) {   // log every CHANGE so you can watch it settle to your selection (re-read every frame, not frozen at F7)
        lastD = (int)g_difficulty; printf("[wpn] difficulty -> %d (ammo %d%%) -- the value at SPAWN is what your loadout ammo uses\n", (int)g_difficulty, g_difficulty==0?80:g_difficulty==2?20:50); } }
    int m = (int)rd32(g_base + RVA_MISSION_IDX);
    if (m != g_wpnSeenMission) { g_wpnSeenMission = m; g_wpnSeenN = 0; g_wpnLogged = false; memset(g_prevOwned, 0, sizeof(g_prevOwned)); memset(g_prevAmmo, 0, sizeof(g_prevAmmo));
        printf("[wpn] mission=%d  SP-difficulty raw=%d -> g_difficulty=%d (ammo %d%%)\n", m, diffRaw, (int)g_difficulty, g_difficulty==0?80:g_difficulty==2?20:50); }   // new mission
    int n = (int)rd32(g_base + RVA_LP_COUNT); if (n < 1) n = 1; if (n > 4) n = 4;   // always process view 0 (SP count may be 0)
    uintptr_t arrBase = 0;                                          // DAT_142f5ead8 is a POINTER to the local-player view array
    __try { arrBase = *(volatile uintptr_t*)(g_base + RVA_LP_VIEWS); } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    if (arrBase < 0x100000000ULL || arrBase >= 0x700000000000ULL) return;           // view array not allocated yet
    for (int i = 0; i < n; i++) {
        __try {
            uintptr_t view = arrBase + (uintptr_t)i * WP_STRIDE;                     // view_i = *DAT_142f5ead8 + i*0x12a0
            uintptr_t ent = *(volatile uintptr_t*)(view + OFF_VIEW_ENT); if (!ent) continue;   // +0x1c8 = entity
            uintptr_t pawn = *(volatile uintptr_t*)(ent + OFF_ENT_PAWN); if (!pawn) continue;   // +0x140 = pawn
            if (!g_wpnLogged) {
                g_wpnLogged = true;
                int cnt = 0; for (int s = 1; s < 0x2a; s++) if (*(volatile int*)(pawn + OFF_WFLAGS + s * 4)) cnt++;
                printf("[wpn] player pawn=%p mission=%d owned=%d gating=%d shuffle=%d\n", (void*)pawn, m, cnt, (int)g_weaponGating, (int)g_weaponShuffle);
            }
            if (g_weaponShuffle && g_inStory && !wpnSeen(pawn)) { if (!wpnRemapPawn(pawn)) g_wpnSeenN--; }    // B: remap loadout once (retry until spawned)
            if (g_ammoTopUp && g_inStory && i == 0) {   // PLAYER: grant ammo when a weapon is newly ACQUIRED (spawn loadout + each pickup), then let it deplete
                for (int s = 1; s < 0x2a; s++) {
                    int owned = *(volatile int*)(pawn + OFF_WFLAGS + s * 4) ? 1 : 0;
                    if (owned && !g_prevOwned[s]) {                                                       // 0 -> 1: just spawned-with / picked up slot s
                        int sid = *(volatile int*)(g_base + 0x2512a20 + (uintptr_t)s * 0x30 + 0xc);       // slot s's live Gun-Stats index
                        if ((unsigned)sid < 0x40) {
                            // The game's own ammo loader (FUN_141121324) fills TWO ammo types per weapon: PRIMARY at
                            // Gun-Stats +0x8 and SECONDARY at +0x10 (skipped when 0, or the same as primary). The Plasma
                            // Autorifle is the dual-ammo case -- its GRENADE secondary uses the +0x10 type, so granting
                            // only the primary left the grenade side empty and made EXP-gated objectives unreachable.
                            uintptr_t gs = g_base + 0x250c2a8 + (uintptr_t)sid * 0x240;
                            int ats[2] = { *(volatile int*)(gs + 0x8), *(volatile int*)(gs + 0x10) };
                            for (int k = 0; k < 2; k++) {
                                int at = ats[k];
                                if (k == 1 && (at == 0 || at == ats[0])) continue;                    // no/duplicate secondary
                                if ((unsigned)at >= 0x2a) continue;
                                int mx = *(volatile int*)(g_base + 0x250a130 + (uintptr_t)at * 0xf0); // ammo-MAX table: this type's FULL amount (laser=3000, pistol=60, ...)
                                if (mx <= 0 || mx >= 0x100000) mx = 100;                              // fallback if max looks odd
                                int pct = (g_difficulty == 0) ? AMMO_PCT_EASY
                                        : (g_difficulty == 2) ? AMMO_PCT_HARD : AMMO_PCT_NORMAL;
                                int give = mx * pct / 100; if (give < 1) give = 1;
                                volatile int* a = (volatile int*)(pawn + OFF_WAMMO + at * 4);
                                if (*a < give) *a = give;
                            }
                        }
                    } else if (owned) {
                        // ALREADY owned: a RISE in this weapon's PRIMARY ammo means a pickup just landed (enemy drop or
                        // a floor weapon). The game's drop transfer (FUN_141126398) only hands over the ammo the DROP
                        // carried, and enemies never carry SECONDARY ammo -- so a dual-ammo gun (Plasma Autorifle,
                        // Soviet S47) replenishes its bullets but never its grenades. Top the secondary up by the
                        // weapon's own designed per-pickup amount (Gun-Stats +0x1c), capped at that type's max.
                        // (An ammo-box pickup also raises the primary and will grant grenades too -- intentional:
                        // the grenade side is otherwise unreplenishable, and it can be the only explosive in logic.)
                        int sid = *(volatile int*)(g_base + 0x2512a20 + (uintptr_t)s * 0x30 + 0xc);
                        if ((unsigned)sid < 0x40) {
                            uintptr_t gs = g_base + 0x250c2a8 + (uintptr_t)sid * 0x240;
                            int p = *(volatile int*)(gs + 0x8), q = *(volatile int*)(gs + 0x10);
                            if (q != 0 && q != p && (unsigned)p < 0x2a && (unsigned)q < 0x2a) {
                                int cur = *(volatile int*)(pawn + OFF_WAMMO + p * 4);
                                if (cur > g_prevAmmo[p]) {                                   // primary rose -> pickup
                                    int add = *(volatile int*)(gs + 0x1c); if (add < 1) add = 1;
                                    int mxs = *(volatile int*)(g_base + 0x250a130 + (uintptr_t)q * 0xf0);
                                    if (mxs <= 0 || mxs >= 0x100000) mxs = 100;
                                    volatile int* a = (volatile int*)(pawn + OFF_WAMMO + q * 4);
                                    int nv = *a + add; if (nv > mxs) nv = mxs;
                                    if (nv > *a) *a = nv;
                                }
                            }
                        }
                    }
                    // Placeable mines (see wpnIsMine): refill to the type's max every poll while owned+unlocked, so the
                    // single-charge TNT (and any mine used as an objective's explosive) supports repeated detonations.
                    if (owned && !wpnLocked(s) && wpnIsMine(wpnEff(s))) {
                        int sid = *(volatile int*)(g_base + 0x2512a20 + (uintptr_t)s * 0x30 + 0xc);
                        if ((unsigned)sid < 0x40) {
                            int at = *(volatile int*)(g_base + 0x250c2a8 + (uintptr_t)sid * 0x240 + 0x8);   // primary type = mine count
                            if ((unsigned)at < 0x2a) {
                                int mx = *(volatile int*)(g_base + 0x250a130 + (uintptr_t)at * 0xf0);
                                if (mx <= 0 || mx >= 0x100000) mx = 1;
                                volatile int* a = (volatile int*)(pawn + OFF_WAMMO + at * 4);
                                if (*a < mx) *a = mx;
                            }
                        }
                    }
                    g_prevOwned[s] = (char)owned;
                }
                for (int t = 0; t < 0x2a; t++)                                   // snapshot ammo for next poll's rise test
                    g_prevAmmo[t] = *(volatile int*)(pawn + OFF_WAMMO + t * 4);
            }
            if (g_weaponGating && g_inStory) {                                           // A: gate the inventory to the level's UNLOCKED pickups (STORY only; per-level model -- NO global grant)
                for (int s = 1; s < 0x2a; s++)
                    if (wpnLocked(s)) *(volatile int*)(pawn + OFF_WFLAGS + s * 4) = 0;     //   a pickup whose weapon you haven't unlocked -> drop it (you keep only unlocked level weapons)
                int cur = *(volatile int*)(pawn + OFF_WCUR);                              //   holding a now-locked weapon? -> switch to a usable owned slot (else fists)
                if (cur > 0 && cur < 0x2a && wpnLocked(cur)) {
                    int nw = 0;
                    for (int s = 1; s < 0x2a; s++)
                        if (*(volatile int*)(pawn + OFF_WFLAGS + s * 4) && !wpnLocked(s)) { nw = s; break; }
                    *(volatile int*)(pawn + OFF_WCUR) = nw;
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
}

// REPLACES FUN_141126918 (the map-list precache). Same guards (don't load until the weapon list is ready), but loads ALL
// guns instead of just the map's list, with a SINGLE commit. Calling 918 separately + then a load-all double-loaded +
// double-committed the map guns -> crash AFTER the (successful) loads. This does it once.
// *** CORRECTED weapon remap (from the YossariansMods decode). The LIVE Gun-Stats index for each weapon slot is Gun IDs
// entry +0xc (RVA 0x2512a20 + slot*0x30 + 0xc) -- VERIFIED it matches every in-game weapon (slot1->0=Silenced Pistol,
// slot7->3=Revolver, slot10->5=Minigun). My earlier change of +0x4 = a legacy field the runtime ignores for the equipped
// weapon, which is why it had no effect. Re-pointing slot s's +0xc to remap[s]'s pristine +0xc makes slot s GLOBALLY become
// that weapon (player loadout + world pickups + enemies, since all read these static tables). Also copy the per-slot MODEL
// table (0x2513200) so the dropped/held graphic matches. We DO NOT load any cross-map SOUND (that is what crashes -- those
// assets aren't in the level pak); models are available cross-level, sound is present only when the target is native/common
// to the level (silent otherwise, no crash). Snapshot pristine once so a permutation always copies originals. ***
static const uintptr_t RVA_GUNIDS = 0x2512a20, RVA_GUNMODEL = 0x2513200;
static const int GID_STRIDE = 0x30, MDL_STRIDE = 0x48, GID_N = 43;
static unsigned char g_gidSnap[GID_N][GID_STRIDE], g_mdlSnap[GID_N][MDL_STRIDE];
static bool g_tblSnapped = false, g_tblProt = false;
static void wpnTableRemap() {
    if (!g_base) return;
    __try {
        uintptr_t gid = g_base + RVA_GUNIDS, mdl = g_base + RVA_GUNMODEL;
        if (!g_tblProt) { DWORD o; VirtualProtect((void*)gid, (SIZE_T)GID_N*GID_STRIDE, PAGE_EXECUTE_READWRITE, &o); VirtualProtect((void*)mdl, (SIZE_T)GID_N*MDL_STRIDE, PAGE_EXECUTE_READWRITE, &o); g_tblProt = true; }
        if (!g_tblSnapped) { for (int i = 0; i < GID_N; i++) { memcpy(g_gidSnap[i], (void*)(gid + (uintptr_t)i*GID_STRIDE), GID_STRIDE); memcpy(g_mdlSnap[i], (void*)(mdl + (uintptr_t)i*MDL_STRIDE), MDL_STRIDE); } g_tblSnapped = true; }
        int n = 0;
        for (int s = 1; s < GID_N; s++) {
            int y = g_weaponRemap[s]; if (y < 1 || y >= GID_N) y = s;
            *(volatile unsigned int*)(gid + (uintptr_t)s*GID_STRIDE + 0xc) = *(unsigned int*)(g_gidSnap[y] + 0xc);  // LIVE Gun-Stats index (behavior + asset precache)
            *(volatile unsigned int*)(gid + (uintptr_t)s*GID_STRIDE + 0x8) = *(unsigned int*)(g_gidSnap[y] + 0x8);  // right hand (akimbo dual-wield; FFFFFFFF single)
            memcpy((void*)(mdl + (uintptr_t)s*MDL_STRIDE), g_mdlSnap[y], MDL_STRIDE);                                // full weapon-INFO entry (name/model/graphic)
            if (y != s) n++;
        }
        if (g_log) { fprintf(g_log, "[wpn-tbl] remapped %d slots (GunIDs +0xc live statsID + model table)\n", n); fflush(g_log); }
    } __except(EXCEPTION_EXECUTE_HANDLER) { if (g_log){fprintf(g_log,"[wpn-tbl] FAULTED\n");fflush(g_log);} }
}
// Restore the pristine GunIDs/model snapshot (undo the shuffle remap). Run for non-story levels so Arcade/Challenge keep
// their native weapons. No-op until a remap has been applied (g_tblSnapped) -- the tables are pristine until then.
static void wpnTableRestore() {
    if (!g_base || !g_tblSnapped) return;
    __try {
        uintptr_t gid = g_base + RVA_GUNIDS, mdl = g_base + RVA_GUNMODEL;
        for (int s = 1; s < GID_N; s++) {
            *(volatile unsigned int*)(gid + (uintptr_t)s*GID_STRIDE + 0xc) = *(unsigned int*)(g_gidSnap[s] + 0xc);
            *(volatile unsigned int*)(gid + (uintptr_t)s*GID_STRIDE + 0x8) = *(unsigned int*)(g_gidSnap[s] + 0x8);
            memcpy((void*)(mdl + (uintptr_t)s*MDL_STRIDE), g_mdlSnap[s], MDL_STRIDE);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// Installed at the level-load precache call site (RVA 0x114beaa, was 'call FUN_141126918'). Applies the +0xc/model remap
// BEFORE the original precache runs, so the level's NORMAL precache loads each (remapped) slot's assets -- model AND SOUND --
// exactly like YossariansMods' All Rockets (the rocket isn't in any level, yet it gets sound because the table was changed
// before precache). This is the correct timing; doing the remap in-mission (after precache) is what left guns silent.
static void wpnCaveRemap() {
    int mode = (int)rd32(g_base + 0x3f27f54);                                  // DAT_143f27f54: 0x6a = frontend pass (weapon list not built yet). g_inStory is driven by the
                                                                              // MENU page names (diag thread) -- the level-load mode was stale across menu/arcade transitions.
    // within_level scope: load the map for the level being loaded BEFORE anything reads g_weaponRemap (the [wpn-map]
    // log below and wpnTableRemap). Safe to do every load: wpnTableRemap always rebuilds every slot from the pristine
    // snapshot, so a different permutation per level never accumulates.
    if (g_weaponRemapPerLevel) {
        int lm = (int)rd32(g_base + RVA_MISSION_IDX);
        if ((unsigned)lm < 10) for (int s = 0; s < 64; s++) g_weaponRemap[s] = g_weaponRemapLvl[lm][s];
    }
    // Log the weapon-shuffle MAPPING once per mission (the one support artifact worth keeping: it tells you exactly what
    // the seed's shuffle did in this level).
    __try {
        static int lastDumpM = -999;
        int m = (int)rd32(g_base + RVA_MISSION_IDX);
        long long list = ((long long(*)(void))(g_base + 0x114e134))();
        if (mode != 0x6a && m != lastDumpM && list && g_log) {                 // only the REAL level load (skip the frontend pass)
            lastDumpM = m;
            int count = *(volatile int*)(list + 4);
            // for each slot the level provides, the weapon it now YIELDS after the global
            // remap (g_weaponRemap[slot]).  "Name->Name" (src==dst) = shuffled back to itself OR shuffle inactive;
            // a different target = actually remapped.  shuffle/inStory show whether the table remap is even applied
            // (it runs only when g_mapShuffle && g_inStory).  Lets you tell "randomized to itself" from "not randomized".
            char mb[1024]; int mo = snprintf(mb, sizeof(mb), "[wpn-map] mission=%d shuffle=%d inStory=%d:", m, (int)g_mapShuffle, (int)g_inStory);
            int changed = 0;
            for (int i = 0; i < count && i < 64; i++) {
                int s = *(volatile int*)(list + 8 + (long long)i * 4);
                if (s < 0 || s >= 42) continue;
                int d = g_weaponRemap[s]; if (d < 0 || d >= 42) d = s;
                if (d != s) changed++;
                if (mo < (int)sizeof(mb) - 70)
                    mo += snprintf(mb + mo, sizeof(mb) - mo, "  %s->%s", g_wpnNames[s], g_wpnNames[d]);
            }
            snprintf(mb + mo, sizeof(mb) - mo, "   (%d remapped)", changed);
            printf("%s\n", mb);
            fprintf(g_log, "%s\n", mb); fflush(g_log);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    if (g_mapShuffle && g_inStory) {
        wpnTableRemap();                                     // STORY only: re-point the tables to the shuffle targets
        // FUN_141126918 loads weapon SOUNDS but NOT projectile/effect assets, so a cross-level swapped weapon's projectile
        // (e.g. crossbow bolt) renders as a placeholder. Run FUN_141126994's PROJECTILE loop only (FUN_1411f4078 over the
        // projectile set @ 0x250a2d0) -- NOT its all-sound loop (which OOMs) -- so those projectile meshes load. 918's
        // commit (right after) flushes them, so no separate/double commit.
        __try {
            typedef void (*pl_t)(int); pl_t pl = (pl_t)(g_base + 0x11f4078);            // FUN_1411f4078 (projectile/effect loader)
            unsigned int* p = (unsigned int*)(g_base + 0x250a2d0);                      // DAT_14250a2d0 projectile set, stride 0x3c dwords
            while ((uintptr_t)p < g_base + 0x250bc20) { pl((int)p[-1]); pl((int)p[0]); p += 0x3c; }
            pl(0x435); pl(0x436); pl(0x437); pl(0x438);
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    } else if (g_tblSnapped) {
        wpnTableRestore();                                  // arcade/challenge (or shuffle off): restore the pristine/native weapon tables
    }
    if (g_base) ((void(*)(void))(g_base + 0x1126918))();     // ORIGINAL FUN_141126918 -> precaches the remapped weapons' sounds + commits
}

// *** The HOMEFRONT "Files" code (per DevilDwarf's GC notes): make story load ALL guns, not just the map's native set, so
// cross-level shuffled/granted weapons have their assets (sound + effects) precached. FUN_14114bdf4 (level load) calls
// FUN_141126918 (precache the map's weapon LIST = the restriction) at RVA 0x114beaa; we retarget that one call to
// FUN_141126994 (precache ALL guns). Applied once at startup; verifies the E8 call -> FUN_141126918 before patching. ***
static const uintptr_t RVA_FILES_CALL = 0x114beaa;
static bool g_filesPatched = false;
static void wpnFilesPatch() {
    if (!g_base || g_filesPatched) return;
    uintptr_t call = g_base + RVA_FILES_CALL;
    __try {
        if (*(volatile unsigned char*)call != 0xE8) { printf("[wpn-files] call site not E8 (%#x) -- abort\n", *(volatile unsigned char*)call); return; }
        int rel = *(volatile int*)(call + 1);
        if ((uintptr_t)(call + 5 + rel) != g_base + 0x1126918) { printf("[wpn-files] call target != FUN_141126918 -- abort\n"); return; }
        // The 0x114beaa "call FUN_141126918" -> retarget to a cave that FIRST applies the Gun IDs remap, THEN runs the
        // ORIGINAL FUN_141126918 (the normal precache, which now loads each map slot's REMAPPED assets). No replacement of
        // 918's behaviour -- we just remap the index table immediately before it reads it.
        unsigned char* cave = allocNear(g_base, 0x40);
        if (!cave) { printf("[wpn-files] cave alloc failed\n"); return; }
        uintptr_t fpre = (uintptr_t)&wpnCaveRemap;    // remap the tables THEN run the original precache (load remapped assets+sound)
        unsigned char* c = cave; int o = 0;
        c[o++]=0x48; c[o++]=0x83; c[o++]=0xEC; c[o++]=0x28;                              // sub rsp,0x28  (shadow + 16-align)
        c[o++]=0x48; c[o++]=0xB8; memcpy(c+o,&fpre,8); o+=8; c[o++]=0xFF; c[o++]=0xD0;   // mov rax,&wpnCaveRemap ; call rax
        c[o++]=0x48; c[o++]=0x83; c[o++]=0xC4; c[o++]=0x28;                              // add rsp,0x28
        c[o++]=0xC3;                                                                     // ret
        unsigned char p[5] = {0xE8,0,0,0,0}; put_rel32(p + 1, call + 5, (uintptr_t)cave);
        patchBytes(call, p, 5);
        g_filesPatched = true;
        printf("[wpn-files] PATCHED -- Gun IDs remap + normal precache at level load. cave=%p\n", cave);
        if (g_log) { fprintf(g_log, "[wpn-files] PATCHED gunid-remap + original 918 cave\n"); fflush(g_log); }
    } __except(EXCEPTION_EXECUTE_HANDLER) { printf("[wpn-files] patch faulted\n"); }
}


// DIAG: resolve a menu page's identity name (page->[+0x08]->[+0x28], same path ap_client uses). "" if not a named page.
static void dbgPageName(uintptr_t page, char* o, int n) {
    o[0] = 0; if (!page) return;
    __try {
        uintptr_t desc = *(volatile uintptr_t*)(page + 0x08); if (!desc) return;
        const char* s = *(const char**)(desc + 0x28); if (!s) return;
        int i = 0; for (; i < n - 1 && s[i]; i++) o[i] = s[i]; o[i] = 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { o[0] = 0; }
}


static uintptr_t playerObj() {   // OBJECT struct: ent = view+0x1c8 (health here); pawn = ent+0x140 (weapons/ammo)
    if (!g_base) return 0;
    uintptr_t arr = rd64(g_base + RVA_LP_VIEWS);
    if (arr < 0x100000000ULL || arr >= 0x700000000000ULL) return 0;
    return rd64(arr + OFF_VIEW_ENT);
}
// One-time bonus EFFECTS (the AP bonus-item pipeline calls these). Each reads the LIVE max so the refill auto-respects
// difficulty + level: health = ent+0x204 current / ent+0x208 max, armor = pawn+0x18 / pawn+0x1c, ammo max = the
// per-type table @ 0x250a130.  Exposed to ap_client (pawn/cheat access lives here).
void applyHealthRefill() { uintptr_t o = playerObj(); if (o) wr32(o + 0x204, rd32(o + 0x208)); }     // current=max (ent)
void applyArmorRefill()  { uintptr_t o = playerObj(); if (!o) return; uintptr_t p = rd64(o + OFF_ENT_PAWN); if (p) wr32(p + 0x18, rd32(p + 0x1c)); } // current=max (pawn)
int  apInLevel() { return playerObj() != 0 ? 1 : 0; }   // player pawn spawned => in a mission/challenge (vs a menu)
// DEATHLINK: current health is a float at ent+0x204; the damage path clamps it to 0 on death.  Dead = in a level
// with health <= 0.  (Used by ap_client to detect OUR death -> send a Bounce.)
int  apIsDead() { uintptr_t o = playerObj(); if (!o) return 0; unsigned int r = rd32(o + 0x204); float hp; memcpy(&hp, &r, 4); return hp <= 0.0f ? 1 : 0; }
// DEATHLINK: kill the local player by running the game's real hit processor (FUN_1411e7ec8) with a minimal damage
// event {[0]=target object, [+0x30]=lethal damage}.  It applies the damage (FUN_1411e7414) and, when health hits 0,
// invokes the death routine (FUN_1411e5d44) -> proper death + respawn.  Returns 1 if it ran.  (This is the F6 test
// path, confirmed live.)  SEH-guarded; the event is local POD so concurrent callers don't share it.
int  apKillPlayer() {
    uintptr_t ent = playerObj(); if (!ent || !g_base) return 0;
    long long ev[32]; for (int i = 0; i < 32; i++) ev[i] = 0;
    ev[0] = (long long)ent;                              // damage-event target object
    float dmg = 9999.0f; memcpy((char*)ev + 0x30, &dmg, 4);   // param_1[6] = damage amount
    __try { ((void(*)(long long*))(g_base + 0x11e7ec8))(ev); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
    return 1;
}
void apSetCheat(int bit)   { if (g_base && (unsigned)bit < 32) wr32(g_base + 0x3f2f998, rd32(g_base + 0x3f2f998) |  (1u << bit)); }
void apClearCheat(int bit) { if (g_base && (unsigned)bit < 32) wr32(g_base + 0x3f2f998, rd32(g_base + 0x3f2f998) & ~(1u << bit)); }
void applyAmmoRefill() {
    uintptr_t o = playerObj(); if (!o || !g_base) return;
    uintptr_t pawn = rd64(o + OFF_ENT_PAWN); if (!pawn) return;
    for (int s = 1; s < 0x2a; s++) {
        if (!rd32(pawn + OFF_WFLAGS + s*4)) continue;                          // slot not owned
        int sid = (int)rd32(g_base + 0x2512a20 + (uintptr_t)s*0x30 + 0xc);     // live Gun-Stats index
        if ((unsigned)sid >= 0x40) continue;
        uintptr_t gs = g_base + 0x250c2a8 + (uintptr_t)sid*0x240;
        int ats[2] = { (int)rd32(gs + 0x8), (int)rd32(gs + 0x10) };            // PRIMARY + SECONDARY ammo type (see the
        for (int k = 0; k < 2; k++) {                                          // on-acquire grant: Plasma Autorifle's
            int at = ats[k];                                                   // grenade side lives on the +0x10 type)
            if (k == 1 && (at == 0 || at == ats[0])) continue;                 // no/duplicate secondary
            if ((unsigned)at >= 0x2a) continue;
            int mx = (int)rd32(g_base + 0x250a130 + (uintptr_t)at*0xf0);       // ammo-max for that type
            if (mx <= 0 || mx >= 0x100000) mx = 100;
            wr32(pawn + OFF_WAMMO + at*4, mx);                                  // -> full
        }
    }
}

// Report which of the TS2Redux loader's optional mod DLLs are actually live in the process. dinput8.dll walks a fixed
// name list and LoadLibrary's whatever the installer put in Bin64\TS2Redux, so "the file exists" and "it is loaded" are
// different questions -- this answers the second one. Run it a few seconds in: Scotch sits mid-list, so the DLLs after us
// alphabetically have not loaded yet when DllMain runs.
static void logLoadedMods() {
    static const char* kMods[] = {
        "DevilDwarfUI", "Devildwarf", "DevildwarfMenu", "DisableBreathingByDevilDwarf", "DisablePreferences",
        "Fanoto", "FanotoMenu", "GameFixesByFanotoAndDevilDwarf", "InvisibleFixByDevilDwarf",
        "MinigamesByDevilDwarf", "Mods", "Network", "StraightIntoTS2byDevilDwarf",
        "UnlockArcadeAndChallengeByFanoto", "Yossarian", "YossarianRemix", "YossariansHauntedMod", "YossariansMods",
    };
    char line[512]; line[0] = 0;
    for (int i = 0; i < (int)(sizeof(kMods) / sizeof(kMods[0])); i++) {
        char dll[96]; sprintf_s(dll, "%s.dll", kMods[i]);
        if (!GetModuleHandleA(dll)) continue;
        if (line[0]) strcat_s(line, ", ");
        strcat_s(line, kMods[i]);
    }
    printf("[mods] loaded: %s\n", line[0] ? line : "(none)");
    if (g_log) { fprintf(g_log, "[mods] loaded: %s\n", line[0] ? line : "(none)"); fflush(g_log); }
}

static DWORD WINAPI diagThread(LPVOID) {
    AllocConsole();
    FILE* f = nullptr; freopen_s(&f, "CONOUT$", "w", stdout);
    fopen_s(&g_log, "AP_capture.txt", "w");   // written to the game's working dir (game root)
    SetConsoleTitleA("TS2 AP diag");
    printf("=== TS2 Redux AP client loaded. base=%p ===\n", (void*)g_base);
    printf("RF-fix + objective hook + weapon-sound retarget auto-applied.\n");
    printf("[arena] size 0x%x -> 0x%x (patched=%d, init-flag@patch=%d) %s\n", g_arenaWas, ARENA_NEW,
           g_arenaPatched, g_arenaFlagAtPatch, g_arenaFlagAtPatch ? "!! TOO LATE (arena already allocated)" : "OK (before alloc)");
    if (g_log) { fprintf(g_log, "[arena] size 0x%x -> 0x%x (patched=%d, init-flag@patch=%d) %s\n", g_arenaWas, ARENA_NEW,
           g_arenaPatched, g_arenaFlagAtPatch, g_arenaFlagAtPatch ? "TOO LATE" : "OK"); fflush(g_log); }

    rfApply();       // auto-apply the Robot Factory softlock fix at startup (proven live)
    objHookInstall(); // auto-install the objective-render hook so completions are captured for AP
    // giveHookInstall();  // DISABLED -- superseded by the Files patch below
    wpnFilesPatch();      // retargets the level-load precache to wpnCaveRemap: apply the +0xc/model remap, THEN the original
                          // precache loads each remapped weapon's assets (model + SOUND) -- the YossariansMods timing.
    // NOTE: audio/music volume experiments removed -- TS2 shares FMOD with the host (Homefront) and its music volume is
    // latched per-track at StartPlaying from the slider, so it already applies correctly on every level load. We leave the
    // game's native audio path untouched (no hooks on FUN_141773500 / FUN_1417d0d90).

    // (g_weaponRemap identity-init moved to DllMain, before startApClient -- it must not race slot_connected's remap fill)
    DWORD lastLog = GetTickCount(), started = GetTickCount();
    bool modsLogged = false;
    while (g_run) {
        if (!modsLogged && GetTickCount() - started > 5000) { modsLogged = true; logLoadedMods(); }

        // drain the objective-status queue (filled by objHookCb on the game thread, only on a status change)
        while (g_objQReady[g_objQTail & 255]) {
            LONG s = g_objQTail & 255;
            int tid = g_objQTextID[s], st = g_objQStatus[s]; uintptr_t op = (uintptr_t)g_objQObj[s];
            char txt[96]; resolveObjText(tid, txt, sizeof(txt));
            const char* lbl = st == 5 ? "COMPLETE" : st == 4 ? "FAILED" : st == 2 ? "NEW" : st == 3 ? "REMINDER" : "active";
            printf("[OBJ%s] m=%d textID=%d status=%d (%s)  obj=%p  \"%s\"\n",
                   st == 5 ? " *DONE*" : "", (int)rd32(g_base + RVA_MISSION_IDX), tid, st, lbl, (void*)op, txt);
            if (g_log) { fprintf(g_log, "[OBJ] m=%d textID=%d status=%d (%s) obj=%p \"%s\"\n",
                                 (int)rd32(g_base + RVA_MISSION_IDX), tid, st, lbl, (void*)op, txt); fflush(g_log); }
            g_objQReady[s] = 0;                     // free the slot after consuming
            g_objQTail++;
        }

        pollObjPtrs();   // poll tracked objective structs so late status->5 completions still register (reliability)
        weaponPass();    // weapons-as-items: A clears locked weapon slots on the player; B remaps the starting loadout

        if (g_sticky) forEachPage(CMD_SET1);

        g_capCount = 0;
        forEachPage(CMD_COLLECT);
        if (g_capCount > 0) {
            unsigned long long sig = capSignature();
            if (sig != g_lastSig) {
                g_lastSig = sig;
                int mode = (int)rd32(g_base + RVA_MODE);
                printf("\n[MENU  mode=%d  %d entries] ---------------------------------\n", mode, g_capCount);
                for (int i = 0; i < g_capCount; i++) printf("   %2d  %s\n", i, g_cap[i]);
                if (g_log) {
                    fprintf(g_log, "\n[MENU mode=%d  %d entries]\n", mode, g_capCount);
                    for (int i = 0; i < g_capCount; i++) fprintf(g_log, "   %2d  %s\n", i, g_cap[i]);
                    fflush(g_log);
                }
            }
        }

        // DIAG (story-context hunt): log the active page NAMES + level-load mode + g_inStory whenever the context changes,
        // so we can see exactly which signal distinguishes Story from Arcade/Challenge (menus AND gameplay).
        {
            char pn[40], pa[40], pb[40];
            dbgPageName(g_curPage, pn, sizeof(pn));
            uintptr_t scr = rd64(g_base + 0x3f359d0);
            dbgPageName(scr ? rd64(scr + 0x320) : 0, pa, sizeof(pa));
            dbgPageName(scr ? rd64(scr + 0x328) : 0, pb, sizeof(pb));
            // STORY-context LATCH (drives g_inStory ~every 20ms; weapons-as-items apply ONLY in story missions): a story menu
            // page (story_page / storydifficulty_page) sets it; any OTHER real menu page (gametype/arcade/challenge/character
            // select) clears it; neutral overlays (loading/autosave) and empty pages (gameplay) HOLD the latched value. This
            // stays correct across every transition, unlike the level-load mode which was stale leaving story for arcade.
            { bool story = false, otherMenu = false; const char* nm[3] = { pn, pa, pb };
              for (int k = 0; k < 3; k++) {
                  if (!nm[k][0]) continue;
                  if (!strncmp(nm[k], "story", 5)) story = true;                                         // story_page / storydifficulty_page
                  else if (strcmp(nm[k], "loading_page") && strcmp(nm[k], "autosave_page")) otherMenu = true;   // any other named page = a non-story menu
              }
              if (story) g_inStory = 1; else if (otherMenu) g_inStory = 0; }                             // else hold (overlay / gameplay)
            static char lpn[40] = "\x01", lpa[40] = "\x01", lpb[40] = "\x01"; static int lInStory = -1;
            if (strcmp(pn, lpn) || strcmp(pa, lpa) || strcmp(pb, lpb) || (int)g_inStory != lInStory) {
                strcpy(lpn, pn); strcpy(lpa, pa); strcpy(lpb, pb); lInStory = (int)g_inStory;
                printf("[ctx] inStory=%d lvlMode=%#x menuMode=%d missionIdx=%d  curPage='%s' PA='%s' PB='%s'\n",
                       (int)g_inStory, (int)rd32(g_base + 0x3f27f54), (int)rd32(g_base + RVA_MODE),
                       (int)rd32(g_base + RVA_MISSION_IDX), pn, pa, pb);
                if (g_log) { fprintf(g_log, "[ctx] inStory=%d lvlMode=%#x menuMode=%d missionIdx=%d curPage='%s' PA='%s' PB='%s'\n",
                       (int)g_inStory, (int)rd32(g_base + 0x3f27f54), (int)rd32(g_base + RVA_MODE),
                       (int)rd32(g_base + RVA_MISSION_IDX), pn, pa, pb); fflush(g_log); }
            }
        }
        DWORD now = GetTickCount();
        if (now - lastLog >= 5000) {
            printf("[%lus] mode=%d  missionIdx=%d  page=%p *profiles=%p\n", now / 1000,
                   (int)rd32(g_base + RVA_MODE), (int)rd32(g_base + RVA_MISSION_IDX),
                   (void*)g_curPage, (void*)rd64(g_base + RVA_PROFILES));
            lastLog = now;
        }
        Sleep(20);
    }
    return 0;
}

extern "C" void startApClient();  // ap_client.cpp

// ---- WEAPON-SHUFFLE CRASH FIX: enlarge the engine's "LOCA/LMEM" scratch arena --------------------------------------
// Root cause (crash dump + HW watchpoint + 51_arena.c): a particle worker allocates a width*height*4 render buffer from
// a global scratch arena (bump allocator FUN_141a9a10). The arena is [base, base+SIZE), split into a fixed 0x80000
// reserve + a variable region of capacity (SIZE - 0x80000). Under weapon shuffle, the cross-level weapons' aggregate
// render/particle demand exhausts the variable region in some levels (Chicago) -> the allocator's overflow guard calls
// exit(1) -> the CRT exit cleanup DeleteCriticalSection's a particle mutex that a concurrent worker then Enters -> AV.
// The arena SIZE is a static constant DAT_1424e23d8 (RVA 0x24e23d8, stock 0x03a00000 = 58MB), read ONCE at engine
// startup by FUN_141069da0: buf = alloc(SIZE); FUN_1411a98f8(buf, SIZE). capacity = SIZE - 0x80000, and the per-level
// reset recomputes it from the stored SIZE -- so simply raising SIZE before that startup read gives permanent headroom
// (a bigger scratch buffer is harmless otherwise). We patch it in DllMain (earliest point, before the engine allocates).
// (RVA_ARENA_SIZE / ARENA_STOCK / ARENA_NEW / g_arena* declared near the top so diagThread's log can reference them.)
static void enlargeArena() {
    if (!g_base) return;
    __try {
        void* addr = (void*)(g_base + RVA_ARENA_SIZE);
        g_arenaFlagAtPatch = *(volatile unsigned*)(g_base + RVA_ARENA_FLAG);  // 0 = we're before the alloc (patch works)
        g_arenaWas = *(volatile unsigned*)addr;
        if (g_arenaWas == ARENA_STOCK) {                                       // only touch the exact known value
            DWORD old;
            if (VirtualProtect(addr, 4, PAGE_READWRITE, &old)) {
                *(volatile unsigned*)addr = ARENA_NEW;
                VirtualProtect(addr, 4, old, &old);
                g_arenaPatched = 1;
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        g_base = (uintptr_t)GetModuleHandle(NULL);
        enlargeArena();   // FIX: raise the scratch-arena size before the engine allocates it (must be early)
        // Identity-init the weapon remap HERE, before startApClient(): slot_connected (AP thread) fills g_weaponRemap
        // from slot_data. Doing this init in diagThread instead raced that fill and could wipe a loaded remap to identity.
        for (int i = 0; i < 64; i++) g_weaponRemap[i] = i;
        for (int m = 0; m < 10; m++) for (int i = 0; i < 64; i++) g_weaponRemapLvl[m][i] = i;
        installTickWrapper(RVA_STORY_PAGE,     0, tickStory);
        installTickWrapper(RVA_ARCADE_PAGE,    1, tickArcade);
        installTickWrapper(RVA_CHALLENGE_PAGE, 2, tickChallenge);
        CreateThread(nullptr, 0, diagThread, nullptr, 0, nullptr);
        startApClient();
    } else if (reason == DLL_PROCESS_DETACH) {
        g_run = 0;
    }
    return TRUE;
}
