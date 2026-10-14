/* Logging, mod memory near the game image, and the patch / hook engine.
 *
 * Every byte the mod writes into the game's code goes through write_code(),
 * which remembers the original bytes so that a failed install can be undone
 * completely (patches_rollback). */
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include "kh1party.h"

u8  *g_base;
s64  g_code_delta, g_data_delta;
int  g_patch_errors;
char g_dir[260];

/* ---------------------------------------------------------------- logging */

static HANDLE g_log = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION g_logcs;
static int g_logcs_init;

void log_open(void)
{
    char path[300];
    InitializeCriticalSection(&g_logcs);
    g_logcs_init = 1;
    snprintf(path, sizeof path, "%skh1party_log.txt", g_dir);
    g_log = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
}

void logf_(const char *fmt, ...)
{
    char buf[2048];
    DWORD wr;
    int n;
    va_list ap;
    if (g_log == INVALID_HANDLE_VALUE)
        return;
    n = snprintf(buf, sizeof buf, "[%8lu] ", GetTickCount());
    va_start(ap, fmt);
    n += vsnprintf(buf + n, sizeof buf - 2 - n, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof buf - 2)
        n = sizeof buf - 2;
    buf[n++] = '\n';
    if (g_logcs_init) EnterCriticalSection(&g_logcs);
    WriteFile(g_log, buf, n, &wr, NULL);
    if (g_logcs_init) LeaveCriticalSection(&g_logcs);
}

/* ------------------------------------------------------- near-image memory */

/* Hook stubs and relocated game tables must be reachable with a rel32 from
 * the game's code, so they are carved out of blocks allocated within
 * +-1.75 GB of the image. */
static u8 *g_near, *g_near_end;

void *near_alloc(size_t n)
{
    n = (n + 15) & ~(size_t)15;
    if (!g_near || g_near + n > g_near_end) {
        size_t sz = n > 0x40000 ? (n + 0xffff) & ~(size_t)0xffff : 0x40000;
        u8 *img_end = g_base + ((IMAGE_NT_HEADERS64 *)(g_base + ((IMAGE_DOS_HEADER *)g_base)->e_lfanew))
                                   ->OptionalHeader.SizeOfImage;
        u8 *p = NULL, *a;
        a = (u8 *)(((uintptr_t)img_end + 0xffff) & ~(uintptr_t)0xffff);
        for (; a < g_base + 0x70000000 && !p; a += 0x10000)
            p = VirtualAlloc(a, sz, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!p) {
            a = (u8 *)(((uintptr_t)g_base - sz) & ~(uintptr_t)0xffff);
            for (; a > g_base - 0x70000000 && !p; a -= 0x10000)
                p = VirtualAlloc(a, sz, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        }
        if (!p) {
            logf_("near_alloc FAILED");
            g_patch_errors++;
            return NULL;
        }
        g_near = p;
        g_near_end = p + sz;
        logf_("near block %p (+%llx from base)", p, (unsigned long long)(p - g_base));
    }
    {
        u8 *r = g_near;
        g_near += n;
        memset(r, 0, n);
        return r;
    }
}

/* ------------------------------------------------------- code writes, undo */

typedef struct Undo { u8 *addr; u8 len; u8 orig[23]; } Undo;
static Undo g_undo[1024];
static int g_nundo;

static int write_code(u8 *addr, const void *bytes, size_t n)
{
    DWORD old;
    if (n > sizeof g_undo[0].orig || g_nundo >= (int)(sizeof g_undo / sizeof g_undo[0]))
        return 0;
    if (!VirtualProtect(addr, n, PAGE_EXECUTE_READWRITE, &old))
        return 0;
    g_undo[g_nundo].addr = addr;
    g_undo[g_nundo].len = (u8)n;
    memcpy(g_undo[g_nundo].orig, addr, n);
    g_nundo++;
    memcpy(addr, bytes, n);
    VirtualProtect(addr, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), addr, n);
    return 1;
}

void patches_rollback(void)
{
    while (g_nundo > 0) {
        Undo *u = &g_undo[--g_nundo];
        DWORD old;
        if (VirtualProtect(u->addr, u->len, PAGE_EXECUTE_READWRITE, &old)) {
            memcpy(u->addr, u->orig, u->len);
            VirtualProtect(u->addr, u->len, old, &old);
            FlushInstructionCache(GetCurrentProcess(), u->addr, u->len);
        }
    }
}

/* ----------------------------------------------------------------- patches */

int patch_bytes(u32 rva, const void *expect, const void *repl, size_t n, const char *name)
{
    u8 *p = A(rva);
    if (expect && memcmp(p, expect, n) != 0) {
        logf_("PATCH MISMATCH %s @%x", name, rva);
        g_patch_errors++;
        return 0;
    }
    if (!write_code(p, repl, n)) {
        logf_("PATCH WRITE FAIL %s @%x", name, rva);
        g_patch_errors++;
        return 0;
    }
    return 1;
}

int patch_u8(u32 rva, u8 expect, u8 repl, const char *name)
{
    return patch_bytes(rva, &expect, &repl, 1, name);
}

/* Retarget the rel32 at `site` (followed by `extra` more bytes of the same
 * instruction) from the game address old_target_rva to new_target. */
int patch_riprel(u32 site, u8 extra, u32 old_target_rva, const void *new_target, const char *name)
{
    u8 *next = A(site) + 4 + extra;
    s64 old_disp = A(old_target_rva) - next;
    s64 new_disp = (const u8 *)new_target - next;
    s32 o = (s32)old_disp, nw = (s32)new_disp;
    if ((s64)nw != new_disp || (s64)o != old_disp) {
        logf_("PATCH RANGE %s @%x", name, site);
        g_patch_errors++;
        return 0;
    }
    return patch_bytes(site, &o, &nw, 4, name);
}

/* Replace a len-byte instruction whose rel32 (at dispoff) points at
 * old_target_rva with NOPs. */
int patch_nop_riprel(u32 site, u8 len, u8 dispoff, u32 old_target_rva, const char *name)
{
    static const u8 nops[8][8] = {
        {0}, {0x90}, {0x66, 0x90}, {0x0f, 0x1f, 0x00}, {0x0f, 0x1f, 0x40, 0x00},
        {0x0f, 0x1f, 0x44, 0x00, 0x00}, {0x66, 0x0f, 0x1f, 0x44, 0x00, 0x00},
        {0x0f, 0x1f, 0x80, 0x00, 0x00, 0x00, 0x00},
    };
    u8 *p = A(site);
    s32 disp = *(s32 *)(p + dispoff);
    if (len > 7 || p + len + disp != A(old_target_rva)) {
        logf_("PATCH MISMATCH %s @%x", name, site);
        g_patch_errors++;
        return 0;
    }
    return patch_bytes(site, NULL, nops[len], len, name);
}

/* ------------------------------------------------------------------- hooks */

/* Does the game code at h->rva hold the expected bytes? Bytes inside
 * rip-relative fields are not compared (they differ between builds). */
int site_matches(const HookSite *h)
{
    const u8 *p = A(h->rva);
    for (int i = 0; i < h->len; i++) {
        int in_rel = 0;
        for (int r = 0; r < h->nrel; r++)
            if (i >= h->reloff[r] && i < h->reloff[r] + 4)
                in_rel = 1;
        if (!in_rel && p[i] != h->expect[i])
            return 0;
    }
    return 1;
}

/* Copy the hooked instructions to dst, fixing up their rip-relative fields. */
static int copy_site(const HookSite *h, u8 *dst)
{
    const u8 *src = A(h->rva);
    memcpy(dst, src, h->len);
    for (int r = 0; r < h->nrel; r++) {
        const u8 *tgt = src + h->relend[r] + *(const s32 *)(src + h->reloff[r]);
        s64 d = tgt - (dst + h->relend[r]);
        if ((s64)(s32)d != d)
            return 0;
        *(s32 *)(dst + h->reloff[r]) = (s32)d;
    }
    return 1;
}

static u8 *put_jmp_abs(u8 *p, const void *target)   /* jmp [rip+0]; dq target */
{
    p[0] = 0xff; p[1] = 0x25; *(u32 *)(p + 2) = 0;
    *(const void **)(p + 6) = target;
    return p + 14;
}

/* Overwrite the hooked site with a jmp rel32 to stub, padded with NOPs. */
static int install_jmp(const HookSite *h, u8 *stub)
{
    u8 buf[24];
    u8 *site = A(h->rva);
    s64 d = stub - (site + 5);
    if ((s64)(s32)d != d) {
        logf_("HOOK RANGE %s", h->name);
        g_patch_errors++;
        return 0;
    }
    memset(buf, 0x90, sizeof buf);
    buf[0] = 0xe9;
    *(s32 *)(buf + 1) = (s32)d;
    if (!write_code(site, buf, h->len)) {
        logf_("PATCH WRITE FAIL %s @%x", h->name, h->rva);
        g_patch_errors++;
        return 0;
    }
    return 1;
}

/* Function-entry hook. The site is replaced by a jump to `handler`; the
 * returned pointer runs the original function (moved instructions, then a
 * jump back). */
void *hook_fn(const HookSite *h, void *handler)
{
    u8 *stub, *tramp;
    if (!site_matches(h)) {
        logf_("HOOK MISMATCH %s @%x", h->name, h->rva);
        g_patch_errors++;
        return NULL;
    }
    stub = near_alloc(14 + h->len + 14);
    if (!stub)
        return NULL;
    put_jmp_abs(stub, handler);
    tramp = stub + 14;
    if (!copy_site(h, tramp)) {
        logf_("HOOK RELOC FAIL %s", h->name);
        g_patch_errors++;
        return NULL;
    }
    put_jmp_abs(tramp + h->len, A(h->rva) + h->len);
    return install_jmp(h, stub) ? tramp : NULL;
}

/* Mid-function hook: saves flags, the general registers and xmm0-5, calls
 * handler(ctx) with the stack aligned, restores everything (including any
 * register the handler changed in ctx), then either jumps to the address the
 * handler returned or runs the moved instructions and continues. */
static const u8 ctx_stub[] = {
    0x9c,                                     /* 00 pushfq */
    0x50, 0x51, 0x52, 0x53, 0x55, 0x56, 0x57, /* 01 push rax rcx rdx rbx rbp rsi rdi */
    0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53,
    0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, /* 08 push r8..r15 */
    0x48, 0x83, 0xec, 0x60,                   /* 18 sub rsp,0x60 */
    0xf3, 0x0f, 0x7f, 0x04, 0x24,             /* 1c movdqu [rsp],xmm0 */
    0xf3, 0x0f, 0x7f, 0x4c, 0x24, 0x10,
    0xf3, 0x0f, 0x7f, 0x54, 0x24, 0x20,
    0xf3, 0x0f, 0x7f, 0x5c, 0x24, 0x30,
    0xf3, 0x0f, 0x7f, 0x64, 0x24, 0x40,
    0xf3, 0x0f, 0x7f, 0x6c, 0x24, 0x50,       /* .. xmm5 */
    0x48, 0x89, 0xe1,                         /* 3f mov rcx,rsp */
    0x48, 0x89, 0xe3,                         /* 42 mov rbx,rsp */
    0x48, 0x83, 0xe4, 0xf0,                   /* 45 and rsp,-16 */
    0x48, 0x83, 0xec, 0x20,                   /* 49 sub rsp,0x20 */
    0xff, 0x15, 0, 0, 0, 0,                   /* 4d call [rip+handler] */
    0x48, 0x89, 0xdc,                         /* 53 mov rsp,rbx */
    0x48, 0x89, 0x05, 0, 0, 0, 0,             /* 56 mov [rip+result],rax */
    0xf3, 0x0f, 0x6f, 0x04, 0x24,             /* 5d movdqu xmm0,[rsp] */
    0xf3, 0x0f, 0x6f, 0x4c, 0x24, 0x10,
    0xf3, 0x0f, 0x6f, 0x54, 0x24, 0x20,
    0xf3, 0x0f, 0x6f, 0x5c, 0x24, 0x30,
    0xf3, 0x0f, 0x6f, 0x64, 0x24, 0x40,
    0xf3, 0x0f, 0x6f, 0x6c, 0x24, 0x50,
    0x48, 0x83, 0xc4, 0x60,                   /* 80 add rsp,0x60 */
    0x41, 0x5f, 0x41, 0x5e, 0x41, 0x5d, 0x41, 0x5c,
    0x41, 0x5b, 0x41, 0x5a, 0x41, 0x59, 0x41, 0x58, /* 84 pop r15..r8 */
    0x5f, 0x5e, 0x5d, 0x5b, 0x5a, 0x59, 0x58, /* 94 pop rdi rsi rbp rbx rdx rcx rax */
    0x48, 0x83, 0x3d, 0, 0, 0, 0, 0x00,       /* 9b cmp qword [rip+result],0 */
    0x74, 0x07,                               /* a3 je +7 */
    0x9d,                                     /* a5 popfq */
    0xff, 0x25, 0, 0, 0, 0,                   /* a6 jmp [rip+result] */
    0x9d,                                     /* ac popfq */
};                                            /* ad: moved instructions follow */

int hook_ctx(const HookSite *h, CtxHandler handler)
{
    u8 *stub, *moved, *slots;
    if (!site_matches(h)) {
        logf_("HOOK MISMATCH %s @%x", h->name, h->rva);
        g_patch_errors++;
        return 0;
    }
    stub = near_alloc(sizeof ctx_stub + h->len + 6 + 24);
    if (!stub)
        return 0;
    memcpy(stub, ctx_stub, sizeof ctx_stub);
    moved = stub + sizeof ctx_stub;
    if (!copy_site(h, moved)) {
        logf_("HOOK RELOC FAIL %s", h->name);
        g_patch_errors++;
        return 0;
    }
    /* jmp [rip+0] back to the site, then the three 8-byte slots:
     * return address, handler, handler result. */
    moved[h->len] = 0xff; moved[h->len + 1] = 0x25; *(u32 *)(moved + h->len + 2) = 0;
    slots = moved + h->len + 6;
    *(u8 **)(slots + 0) = A(h->rva) + h->len;
    *(void **)(slots + 8) = (void *)handler;
    *(u64 *)(slots + 16) = 0;
    *(s32 *)(stub + 0x4f) = (s32)((slots + 8) - (stub + 0x53));
    *(s32 *)(stub + 0x59) = (s32)((slots + 16) - (stub + 0x5d));
    *(s32 *)(stub + 0x9e) = (s32)((slots + 16) - (stub + 0xa3));
    *(s32 *)(stub + 0xa8) = (s32)((slots + 16) - (stub + 0xac));
    return install_jmp(h, stub);
}
