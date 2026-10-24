/* Offline install test: maps the game exe into memory the way Windows would
 * (at its preferred base), runs mod_install() and reports every patch / hook
 * check (pass the Epic KINGDOM HEARTS FINAL MIX.exe). The optional second
 * argument shifts the sections by N bytes. */
#include <stdio.h>
#include <stdlib.h>
#include "../src/kh1party.h"
#include "shim/windows.h"
int g_debug;
void dbg_frame(void) {}
int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb");
    long shift = argc > 2 ? strtol(argv[2], 0, 0) : 0;
    static u8 file[16 << 20];
    size_t n = fread(file, 1, sizeof file, f);
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(file + ((IMAGE_DOS_HEADER *)file)->e_lfanew);
    IMAGE_SECTION_HEADER *s = IMAGE_FIRST_SECTION(nt);
    u8 *base = VirtualAlloc((void *)0x140000000, nt->OptionalHeader.SizeOfImage + 0x10000, 0, 0);
    (void)n;
    if (!base) { puts("map failed"); return 1; }
    memcpy(base, file, 0x1000);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++)
        memcpy(base + s[i].VirtualAddress + shift, file + s[i].PointerToRawData,
               s[i].SizeOfRawData < s[i].Misc.VirtualSize ? s[i].SizeOfRawData : s[i].Misc.VirtualSize);
    g_base = base;
    strcpy(g_dir, "./");
    log_open();
    size_t img = nt->OptionalHeader.SizeOfImage;
    u8 *snap = malloc(img);
    memcpy(snap, base, img);
    int ok = mod_install();
    {
        size_t diff = 0;
        for (size_t i = 0; i < img; i++) diff += snap[i] != base[i];
        printf("bytes changed in game image: %zu\n", diff);
    }
    printf("RESULT install=%d errors=%d unmapped=%d\n", ok, g_patch_errors, g_unmapped);
    return ok ? 0 : 2;
}
