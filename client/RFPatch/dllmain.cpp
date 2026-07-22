// TS2 Redux -- Robot Factory softlock FIX (standalone patch DLL). No Archipelago, no menu code -- just the fix,
// for anyone running the TS2 Redux mod.
//
// Bug (original Homefront/TS2 port; the Redux devs publicly gave up on it): the per-sector A* pathfinding loops
// FUN_1411d1d28 (loop top +0x11d1d85) and FUN_1411d1e50 (+0x11d1ea8) only exit when the goal is FOUND -- they never
// check for an empty open list. So when an enemy's path goal is unreachable (e.g. the Robot Factory gate before it opens),
// the loop spins forever popping list index -1 (out of bounds), and TS2 softlocks while HF2 keeps running. It's
// intermittent and only on Normal/Hard, which is why it was so hard to pin down.
//
// Fix: patch each loop top so that when the open-list head (DAT_142f5e0b0 @ +0x2f5e0b0) is < 0, it bails out
// returning 0 ("no path") -- exactly what the game's OWN correct cross-sector pathfinder FUN_1411d16e4 already does,
// and a value the callers already handle. Transparent to all normal (reachable-goal) pathfinding. Applied at load via
// a small code cave; version-checked so it never patches an unexpected build. Confirmed live: releases a real hang.
//
// Build -> RobotFactoryFix.dll; drop in Bin64\TS2Redux\ and load it the same way the other Redux DLLs are loaded.
#include <windows.h>
#include <cstring>
#include <cstdint>
#include <cstdio>

static const uintptr_t RF_LOOP1 = 0x11d1d85, RF_LOOP2 = 0x11d1ea8, RF_OPENHEAD = 0x2f5e0b0;

static void rflog(const char* s) {
    FILE* f = nullptr; fopen_s(&f, "RobotFactoryFix.log", "a");
    if (f) { fputs(s, f); fclose(f); }
}
static unsigned char* allocNear(uintptr_t target, size_t size) {           // RWX memory within +-2GB (for rel32 jumps)
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
static DWORD WINAPI applyThread(LPVOID) {
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    if (!base) { rflog("no base\n"); return 0; }
    // version check: the two loop tops must be the exact MOVSXD instructions we expect (RIP-relative, ASLR-stable)
    const unsigned char* lt1 = (const unsigned char*)(base + RF_LOOP1);   // MOVSXD R12,[0x142f5e0b0] -> 4c 63 25 ...
    const unsigned char* lt2 = (const unsigned char*)(base + RF_LOOP2);   // MOVSXD RSI,[0x142f5e0b0] -> 48 63 35 ...
    __try {
        if (lt1[0]!=0x4c || lt1[1]!=0x63 || lt1[2]!=0x25 || lt2[0]!=0x48 || lt2[1]!=0x63 || lt2[2]!=0x35) {
            rflog("loop-top bytes don't match (wrong build or already patched) -- not applying.\n"); return 0;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { rflog("read fault during version check -- not applying.\n"); return 0; }

    uintptr_t openHead = base + RF_OPENHEAD;
    unsigned char* cave = allocNear(base, 0x1000);
    if (!cave) { rflog("cave alloc failed\n"); return 0; }
    unsigned char* c = cave; int o = 0;
    uintptr_t cave1 = (uintptr_t)(c + o);                                            // FUN_1411d1d28 (R12)
    c[o++]=0x48; c[o++]=0xB8; memcpy(c+o,&openHead,8); o+=8;                          // MOV RAX, openHead
    c[o++]=0x4C; c[o++]=0x63; c[o++]=0x20;                                            // MOVSXD R12,[RAX]
    c[o++]=0x4D; c[o++]=0x85; c[o++]=0xE4;                                            // TEST R12,R12
    c[o++]=0x0F; c[o++]=0x88; put_rel32(c+o,(uintptr_t)(c+o+4),base+0x11d1e2d); o+=4;  // JS -> return 0 (regs restored)
    c[o++]=0xE9;             put_rel32(c+o,(uintptr_t)(c+o+4),base+0x11d1d8c); o+=4;   // JMP -> loop body
    uintptr_t cave2 = (uintptr_t)(c + o);                                            // FUN_1411d1e50 (RSI)
    c[o++]=0x48; c[o++]=0xB8; memcpy(c+o,&openHead,8); o+=8;                          // MOV RAX, openHead
    c[o++]=0x48; c[o++]=0x63; c[o++]=0x30;                                            // MOVSXD RSI,[RAX]
    c[o++]=0x48; c[o++]=0x85; c[o++]=0xF6;                                            // TEST RSI,RSI
    c[o++]=0x0F; c[o++]=0x88; put_rel32(c+o,(uintptr_t)(c+o+4),base+0x11d1f5f); o+=4;  // JS -> return 0 (regs restored)
    c[o++]=0xE9;             put_rel32(c+o,(uintptr_t)(c+o+4),base+0x11d1eaf); o+=4;   // JMP -> loop body
    unsigned char p1[7]={0xE9,0,0,0,0,0x90,0x90}; put_rel32(p1+1, base+RF_LOOP1+5, cave1);
    unsigned char p2[7]={0xE9,0,0,0,0,0x90,0x90}; put_rel32(p2+1, base+RF_LOOP2+5, cave2);
    patchBytes(base+RF_LOOP1, p1, 7);
    patchBytes(base+RF_LOOP2, p2, 7);
    rflog("Robot Factory softlock fix applied.\n");
    return 0;
}
BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(h); CreateThread(nullptr, 0, applyThread, nullptr, 0, nullptr); }
    return TRUE;
}
