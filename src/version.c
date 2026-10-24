/* Game build detection (Epic Games Store port).
 *
 * The mod's RVAs come from the Steam global release; game_addr() translates
 * them into the Epic image (addrmap.c). At start-up the mod checks that it is
 * running on that Epic build: a set of hooked sites must hold their expected
 * bytes at the translated addresses, and three data references must point
 * where the table says. Every patch site is checked again before anything is
 * written; if anything does not line up, nothing is applied. */
#include <windows.h>
#include "kh1party.h"

static const HookSite *const anchors[] = {
    &H_party_init, &H_friendfilter, &H_slotassign, &H_ai_tick, &H_ai_pre,
    &H_hud_create, &H_lvlprompt, &H_ev_orders, &H_menu_mtx, &H_partyswap,
};

/* Target of the rel32 at site (+extra bytes after it), as a pointer. */
static u8 *riprel_target(u32 site, u8 extra)
{
    u8 *p = A(site);
    return p + 4 + extra + *(s32 *)p;
}

int version_detect(void)
{
    for (size_t i = 0; i < sizeof anchors / sizeof anchors[0]; i++)
        if (!site_matches(anchors[i])) {
            logf_("unsupported game build: this is the Epic Games Store version of the mod "
                  "(code at %s does not match)", anchors[i]->name);
            return 0;
        }
    if (riprel_target(0x28594c, 0) != A(0x0d2ada0) ||   /* lea rax,[spmem] */
        riprel_target(0x291f47, 0) != A(0x2d37614) ||   /* party_init: lea rax,[entity pool+0x374] */
        riprel_target(0x3a154d, 0) != A(RVA_SAVE)) {    /* partyswap: mov rax,[save] */
        logf_("unsupported game build: data layout not recognised");
        return 0;
    }
    if (g_unmapped) {
        logf_("unsupported game build: %d addresses have no translation", g_unmapped);
        return 0;
    }
    return 1;
}
