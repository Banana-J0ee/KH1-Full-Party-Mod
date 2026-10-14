/* Diagnostics.
 *
 * - Crash records: a vectored exception handler logs the first few access
 *   violations / illegal instructions with registers and the game return
 *   addresses found on the stack (it does not handle them).
 * - Debug channel: when kh1party_debug.txt exists next to the DLL, extra
 *   events are logged and a background thread runs commands from
 *   kh1party_cmd.txt (same folder). Write the file, the mod renames it to
 *   kh1party_cmd.run, runs it and deletes it. One command per line:
 *     r <addr> [len]        hex dump        w <addr> <hex bytes>   write
 *     call <rva> [a b c d]  call a game function on the main thread
 *     sleep <ms>            ?party          party / slot / AI state
 *     setparty f1 f2 rsv    party list      warp <world> <room>
 *     hudpos dx dy shift    menufig x0 step pct      table [all]
 *   <addr> is an RVA in the game, xADDR an absolute address, and leading
 *   '*'s dereference (e.g. **2868ba0+48e).
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "kh1party.h"

int g_debug;

/* ------------------------------------------------------------ crash log */

static volatile LONG g_crashes;

long veh(void *p)
{
    EXCEPTION_POINTERS *ep = p;
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    CONTEXT *c = ep->ContextRecord;
    char buf[1024];
    int n = 0;
    u64 *sp;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
        code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_STACK_OVERFLOW &&
        code != EXCEPTION_PRIV_INSTRUCTION)
        return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedIncrement(&g_crashes) > 6)
        return EXCEPTION_CONTINUE_SEARCH;
    logf_("!! EXCEPTION %08lx at %p (rva %llx) info0=%llx info1=%llx", code,
          (void *)c->Rip, (unsigned long long)(c->Rip - (u64)g_base),
          (unsigned long long)ep->ExceptionRecord->ExceptionInformation[0],
          (unsigned long long)ep->ExceptionRecord->ExceptionInformation[1]);
    logf_("   rax=%llx rbx=%llx rcx=%llx rdx=%llx rsi=%llx rdi=%llx rbp=%llx rsp=%llx",
          c->Rax, c->Rbx, c->Rcx, c->Rdx, c->Rsi, c->Rdi, c->Rbp, c->Rsp);
    logf_("   r8=%llx r9=%llx r10=%llx r11=%llx r12=%llx r13=%llx r14=%llx r15=%llx",
          c->R8, c->R9, c->R10, c->R11, c->R12, c->R13, c->R14, c->R15);
    /* Which party member, if any, a register points at. */
    {
        u64 regs[] = { c->Rax, c->Rbx, c->Rcx, c->Rdx, c->Rsi, c->Rdi, c->Rbp,
                       c->R8, c->R9, c->R12, c->R13, c->R14, c->R15 };
        static const char *names[] = { "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp",
                                       "r8", "r9", "r12", "r13", "r14", "r15" };
        for (int i = 0; i < 4; i++) {
            u8 *e = PARTY(i);
            if (!e) continue;
            for (int r = 0; r < (int)(sizeof regs / sizeof regs[0]); r++)
                if (regs[r] == (u64)e)
                    logf_("   %s = party slot %d entity", names[r], i);
        }
    }
    sp = (u64 *)c->Rsp;
    if (IsBadReadPtr(sp, 0x400))
        return EXCEPTION_CONTINUE_SEARCH;
    for (int i = 0; i < 0x7f && n < 800; i++) {
        u64 v = sp[i];
        if (v >= (u64)g_base + 0x1000 && v < (u64)g_base + CODE_END_RVA)
            n += snprintf(buf + n, sizeof buf - n, " %llx", (unsigned long long)(v - (u64)g_base));
    }
    buf[n] = 0;
    logf_("   stack rvas:%s", buf);
    return EXCEPTION_CONTINUE_SEARCH;
}

/* ------------------------------------------------- main-thread call service */

static volatile struct { LONG pending; u32 rva; u64 a[4]; u64 ret; } g_mc;

/* Called once per frame from the AI pre-update hook (main thread). */
void dbg_frame(void)
{
    if (g_mc.pending == 1) {
        u64 (*f)(u64, u64, u64, u64) = (u64 (*)(u64, u64, u64, u64))(g_base + g_mc.rva);
        g_mc.ret = f(g_mc.a[0], g_mc.a[1], g_mc.a[2], g_mc.a[3]);
        logf_("call %x(%llx,%llx,%llx,%llx) = %llx", g_mc.rva, g_mc.a[0], g_mc.a[1], g_mc.a[2],
              g_mc.a[3], g_mc.ret);
        g_mc.pending = 0;
    }
}

/* ---------------------------------------------------------------- commands */

static int readable(const void *p, size_t n)
{
    MEMORY_BASIC_INFORMATION mi;
    const u8 *a = p, *end = a + n;
    while (a < end) {
        if (!VirtualQuery(a, &mi, sizeof mi) || mi.State != MEM_COMMIT ||
            (mi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) || !(mi.Protect & 0xee))
            return 0;
        a = (const u8 *)mi.BaseAddress + mi.RegionSize;
    }
    return 1;
}

static u8 *addr_of(const char *s)
{
    int deref = 0;
    char *end;
    u64 v;
    while (*s == '*') { deref++; s++; }
    if (*s == 'x')
        v = strtoull(s + 1, &end, 16);
    else
        v = (u64)g_base + strtoull(s, &end, 16);
    while (deref-- > 0) {
        if (!readable((void *)v, 8))
            return NULL;
        v = *(u64 *)v;
    }
    if (*end == '+')
        v += strtoull(end + 1, NULL, 16);
    return (u8 *)v;
}

static void cmd_party(void)
{
    u8 *save = SAVE();
    logf_("  world=%d room=%d list=%02x %02x %02x %02x cursor=%p ai_ready=%d ai_cur=%d gauge_on=%d summoning=%d",
          G32(0x233fe84), G32(0x233fe8c), save ? save[0x48e] : 0, save ? save[0x48f] : 0,
          save ? save[0x490] : 0, save ? save[0x491] : 0, *g_cursor, g_ai_ready, g_ai_cur,
          g_gauge_on, g_summoning);
    for (int i = 0; i < 4; i++) {
        u8 *e = PARTY(i), *st, *d, *ai;
        if (!e) {
            logf_("  slot %d: -", i);
            continue;
        }
        st = HGET(F32(e, ENT_STATS_H));
        d = HGET(F32(e, ENT_DESC_H));
        ai = HGET(F32(e, ENT_AI_H));
        logf_("  slot %d: ent=%p char=%d flags=%08x f374=%08x hp=%d/%d mp=%d block=%d pos=(%.1f %.1f %.1f) ai=%p voice=%d",
              i, e, d ? F16(d, 0x4c) : -1, F32(e, 0), F32(e, ENT_STATE), st ? FS32(st, 0x3c) : -1,
              st ? FS32(st, 0x40) : -1, st ? FS32(st, 0x44) : -1, d ? FS8(d, 0x55) : -99,
              FF(e, 0x10), FF(e, 0x14), FF(e, 0x18), ai, st ? FS8(st, 0xbd) : -9);
        if (ai)
            logf_("          ai idx=%d flags58=%08x", F32(ai, 0), F32(ai, 0x58));
    }
    for (int i = 0; i < 4; i++)
        logf_("  wslot %d: fl=%x char=%d se=%d", i, F32(g_wslot, i * 0x30), F32(g_wslot, i * 0x30 + 4),
              F32(g_wslot, i * 0x30 + 8));
    for (int i = 0; i < 4; i++)
        logf_("  voice %d: char=%d fl=%x %s", i, FS8(g_voice, i * 0x22), g_voice[i * 0x22 + 1],
              (char *)g_voice + i * 0x22 + 2);
}

static void cmd_table(const char *arg)
{
    s32 n = GS32(0x296b628);
    u8 *t = GP(0x296b630);
    if (n <= 0 || !t) return;
    if (n > 120) n = 120;
    for (int i = 0; i < n; i++) {
        u8 *r = t + i * 0x78;
        s8 type = FS8(r, 0x54);
        if (type >= 2 && *arg != 'a')
            continue;
        logf_("  rec %d id=%06x fl=%04x char=%d type=%d sp=%d run=%d h8=%08x %.16s", i, F32(r, 0),
              F32(r, 4), F16(r, 0x4c), type, FS8(r, 0x55), FS8(r, 0x56), F32(r, 8), (char *)r + 0x68);
    }
}

static void run_command(char *line)
{
    char cmd[32] = "", *args;
    int n = 0;
    while (*line == ' ') line++;
    if (!*line || *line == '#') return;
    sscanf(line, "%31s%n", cmd, &n);
    args = line + n;
    while (*args == ' ') args++;
    logf_("> %s %s", cmd, args);

    if (!strcmp(cmd, "r")) {
        char a[64];
        unsigned len = 0x40;
        u8 *p;
        sscanf(args, "%63s %x", a, &len);
        p = addr_of(a);
        if (!p || !len || !readable(p, len)) {
            logf_("  %p not readable", p);
            return;
        }
        for (unsigned o = 0; o < len; o += 16) {
            char out[160];
            int k = snprintf(out, sizeof out, "  %p:", p + o);
            for (unsigned i = o; i < len && i < o + 16; i++)
                k += snprintf(out + k, sizeof out - k, " %02x", p[i]);
            logf_("%s", out);
        }
    } else if (!strcmp(cmd, "w")) {
        char a[64];
        int k = 0;
        u8 *p;
        sscanf(args, "%63s%n", a, &k);
        p = addr_of(a);
        if (!p) { logf_("  bad addr"); return; }
        for (char *s = args + k; *s;) {
            char hx[3];
            DWORD old;
            while (*s == ' ') s++;
            if (!s[0] || !s[1]) break;
            hx[0] = s[0]; hx[1] = s[1]; hx[2] = 0;
            s += 2;
            if (VirtualProtect(p, 1, PAGE_EXECUTE_READWRITE, &old)) {
                *p = (u8)strtoul(hx, NULL, 16);
                VirtualProtect(p, 1, old, &old);
            }
            p++;
        }
    } else if (!strcmp(cmd, "call")) {
        unsigned rva = 0;
        unsigned long long a[4] = { 0 };
        sscanf(args, "%x %llx %llx %llx %llx", &rva, &a[0], &a[1], &a[2], &a[3]);
        g_mc.rva = rva;
        for (int i = 0; i < 4; i++) g_mc.a[i] = a[i];
        g_mc.pending = 1;
        for (int i = 0; i < 300 && g_mc.pending; i++) Sleep(10);
        if (g_mc.pending) {
            g_mc.pending = 0;
            logf_("  call not serviced (no main-thread hook running)");
        }
    } else if (!strcmp(cmd, "sleep")) {
        Sleep(atoi(args));
    } else if (!strcmp(cmd, "?party")) {
        cmd_party();
    } else if (!strcmp(cmd, "setparty")) {
        unsigned f1 = 0xff, f2 = 0xff, rs = 0xff;
        sscanf(args, "%x %x %x", &f1, &f2, &rs);
        if (SAVE()) {
            PARTY_LIST(1) = (u8)f1;
            PARTY_LIST(2) = (u8)f2;
            PARTY_LIST(3) = (u8)rs;
        }
    } else if (!strcmp(cmd, "warp")) {
        unsigned w = 0, r = 0;
        sscanf(args, "%u %u", &w, &r);
        G8(0x233fbc0) = 5;
        G8(0x22ec0b0) = 10;
        G8(0x233feb8) = (u8)w;
        G8(0x233febc) = (u8)r;
        G8(0x22ec0ac) = 2;
    } else if (!strcmp(cmd, "hudpos")) {
        sscanf(args, "%d %d %d", &g_hud_dx, &g_hud_dy, &g_hud_shift);
    } else if (!strcmp(cmd, "menufig")) {
        sscanf(args, "%d %d %d", &g_mf_x0, &g_mf_step, &g_mf_pct);
        hud_menu_rebuild();
        logf_("  menufig x0=%d step=%d scale=%d%% count=%d", g_mf_x0, g_mf_step, g_mf_pct, G32(0x2e1f570));
    } else if (!strcmp(cmd, "table")) {
        cmd_table(args);
    } else {
        logf_("  unknown command");
    }
}

static DWORD __stdcall dbg_thread(void *arg)
{
    static char buf[0x10000];
    char cmdp[300], runp[300];
    (void)arg;
    snprintf(cmdp, sizeof cmdp, "%skh1party_cmd.txt", g_dir);
    snprintf(runp, sizeof runp, "%skh1party_cmd.run", g_dir);
    for (;;) {
        HANDLE f;
        DWORD got = 0;
        char *ctx = NULL, *line;
        Sleep(200);
        if (GetFileAttributesA(cmdp) == INVALID_FILE_ATTRIBUTES)
            continue;
        DeleteFileA(runp);
        if (!MoveFileA(cmdp, runp))
            continue;
        f = CreateFileA(runp, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (f == INVALID_HANDLE_VALUE)
            continue;
        ReadFile(f, buf, sizeof buf - 1, &got, NULL);
        CloseHandle(f);
        DeleteFileA(runp);
        buf[got] = 0;
        for (line = strtok_s(buf, "\r\n", &ctx); line; line = strtok_s(NULL, "\r\n", &ctx))
            run_command(line);
        logf_("> done");
    }
    return 0;
}

void dbg_start(void)
{
    char p[300];
    snprintf(p, sizeof p, "%skh1party_debug.txt", g_dir);
    if (GetFileAttributesA(p) == INVALID_FILE_ATTRIBUTES)
        return;
    g_debug = 1;
    logf_("debug channel enabled");
    CreateThread(NULL, 0, dbg_thread, NULL, 0, NULL);
}
