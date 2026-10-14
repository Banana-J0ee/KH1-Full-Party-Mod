/* Entry point. The DLL is loaded by OpenKH Panacea from mod/kh1/dll (Panacea
 * loads every DLL in that folder into every game it starts, so the mod checks
 * that the process really is KINGDOM HEARTS FINAL MIX first).
 *
 * Files the mod looks at (in the DLL's folder, or the game's folder - the
 * DLL's folder is rebuilt by the Mod Manager, so the game folder is the place
 * for files you want to keep):
 *   kh1party_log.txt     written by the mod (next to the DLL)
 *   kh1party_debug.txt   if present: debug logging and the command channel
 *   kh1party_off.txt     if present: the mod does nothing
 */
#include <windows.h>
#include <stdio.h>
#include "kh1party.h"

static char g_gamedir[260];

/* Does `name` exist in the DLL's folder or the game's folder? If so, point
 * g_dir at that folder (used for the debug command files). */
static int flag_file(const char *name, int move_dir)
{
    char p[300];
    snprintf(p, sizeof p, "%s%s", g_dir, name);
    if (GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES)
        return 1;
    snprintf(p, sizeof p, "%s%s", g_gamedir, name);
    if (GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES) {
        if (move_dir)
            strcpy(g_dir, g_gamedir);
        return 1;
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    char exe[MAX_PATH], *s;
    (void)reserved;
    if (reason != DLL_PROCESS_ATTACH)
        return TRUE;
    DisableThreadLibraryCalls(inst);

    GetModuleFileNameA(NULL, exe, sizeof exe);
    s = strrchr(exe, '\\');
    if (_stricmp(s ? s + 1 : exe, "KINGDOM HEARTS FINAL MIX.exe") != 0)
        return TRUE;
    if (s) { s[1] = 0; strcpy(g_gamedir, exe); }

    g_base = (u8 *)GetModuleHandleA(NULL);
    GetModuleFileNameA(inst, g_dir, sizeof g_dir);
    s = strrchr(g_dir, '\\');
    if (s) s[1] = 0;

    log_open();
    logf_("kh1party %s loading; exe base %p", KH1PARTY_VERSION, g_base);
    AddVectoredExceptionHandler(1, (PVECTORED_EXCEPTION_HANDLER)veh);

    if (flag_file("kh1party_off.txt", 0)) {
        logf_("kh1party_off.txt present: patches not applied");
        return TRUE;
    }
    logf_("install %s (errors=%d)", mod_install() ? "OK" : "FAILED", g_patch_errors);
    if (flag_file("kh1party_debug.txt", 1))
        dbg_start();
    return TRUE;
}
