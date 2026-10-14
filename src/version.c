/* Game build detection.
 *
 * All addresses in the mod are RVAs in the Steam global release of
 * KINGDOM HEARTS FINAL MIX.exe. Other releases (the Japanese Steam build, for
 * one) have the same code at slightly different addresses. At start-up the mod
 * works out how far the code and the data have moved by searching for the
 * bytes of a set of hooked sites, then checks every patch site before writing
 * anything; if anything does not line up, nothing is applied. */
#include <windows.h>
#include "kh1party.h"

/* Sites used to find the code offset: their non-relocated bytes must match at
 * the same offset. */
static const HookSite *const anchors[] = {
    &H_party_init, &H_friendfilter, &H_slotassign, &H_ai_tick, &H_ai_pre,
    &H_hud_create, &H_lvlprompt, &H_ev_orders, &H_menu_mtx, &H_partyswap,
};

static int anchors_match(void)
{
    for (size_t i = 0; i < sizeof anchors / sizeof anchors[0]; i++)
        if (!site_matches(anchors[i]))
            return 0;
    return 1;
}

/* Target of the rel32 at site (+extra bytes after it), as an RVA in the
 * running image. */
static s64 riprel_target(u32 site, u8 extra)
{
    u8 *p = A(site);
    return (p + 4 + extra + *(s32 *)p) - g_base;
}

int version_detect(void)
{
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(g_base + ((IMAGE_DOS_HEADER *)g_base)->e_lfanew);
    u32 text_size = 0;
    IMAGE_SECTION_HEADER *s = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++)
        if (!memcmp(s[i].Name, ".text", 6))
            text_size = s[i].Misc.VirtualSize;

    g_code_delta = g_data_delta = 0;
    if (!anchors_match()) {
        /* Search +-64 KB. Make sure every probe stays inside .text. */
        int found = 0;
        for (s64 d = -0x10000; d <= 0x10000 && !found; d++) {
            g_code_delta = d;
            if (0x1000 + d < 0 || H_menu_mtx.rva + d + 32 > text_size + 0x1000u)
                continue;
            if (anchors_match())
                found = 1;
        }
        if (!found) {
            g_code_delta = 0;
            logf_("unsupported game build: code layout not recognised");
            return 0;
        }
    }

    /* Data offset: from three references into different parts of the data
     * (model pool, save pointer, party array), which must all agree. */
    {
        s64 d1 = riprel_target(0x28594c, 0) - 0x0d2ada0;   /* lea rax,[spmem] */
        s64 d2 = riprel_target(0x291f47, 0) - 0x2d37614;   /* party_init: lea rax,[entity pool+0x374] */
        s64 d3 = riprel_target(0x3a154d, 0) - RVA_SAVE;     /* partyswap: mov rax,[save] */
        if (d1 != d2 || d1 != d3) {
            logf_("unsupported game build: data layout not recognised (%llx %llx %llx)",
                  (long long)d1, (long long)d2, (long long)d3);
            return 0;
        }
        g_data_delta = d1;
    }
    if (g_code_delta || g_data_delta)
        logf_("non-global game build: code offset %+lld, data offset %+lld",
              (long long)g_code_delta, (long long)g_data_delta);
    return 1;
}
