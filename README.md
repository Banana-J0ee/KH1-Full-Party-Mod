# KH1 Full Party

Lets Sora's whole party fight at the same time in *Kingdom Hearts Final Mix*
(PC, HD 1.5+2.5 ReMIX): both friends plus the reserve member, so with a world
guest you have Donald, Goofy and the guest together.

It is a code mod: one DLL (`kh1party.dll`) that patches the running game in
memory. No game files are changed.

## Install (OpenKH Mod Manager)

1. In the Mod Manager choose *Kingdom Hearts 1*, then *Mods > Install a new mod
   > Select and install Mod Archive* and pick `KH1-Full-Party.zip`.
2. Tick the mod in the list.
3. *Mod Loader > Build Only*, then start the game (from the Mod Manager or
   from Steam).

It needs the Mod Loader (Panacea), which the Mod Manager's setup wizard
installs for the PC games. "Build and Patch" does not work for this mod: a DLL
cannot be patched into the game packages. To turn it off, untick it and run
*Build Only* again.

Supported game builds: the Steam global release. Other releases whose code is
the same but sits at different addresses (such as the Japanese Steam release)
are detected automatically; if a build is not recognised the mod logs it and
leaves the game unmodified.

## Files the mod reads and writes

They can go in the game folder (next to `KINGDOM HEARTS FINAL MIX.exe`) or in
`openkh/mod/kh1/dll/` (next to the DLL). The Mod Manager clears the second
folder on every build, so the game folder is the place for files you want to
keep.

| File | Meaning |
|---|---|
| `kh1party_log.txt` | Written by the mod next to the DLL: install result, warnings, and a record of any crash. Please attach it to bug reports. |
| `kh1party_debug.txt` | If present: extra logging and the debug command channel (`kh1party_cmd.txt`, see `src/debug.c`). |
| `kh1party_off.txt` | If present: the mod does nothing. |

## Known limitations

- Cutscenes and scripted scenes only know about two friends. While a scene
  moves or hides the friends, the extra member is taken out of it and comes
  back afterwards. After reordering the party in the menu without leaving the
  room, the wrong character can be taken out of a scene.
- Changing the reserve member in the party menu while the extra member is not
  spawned leaves the party at three until the next room change.
- The save-point heal sparkle shows on two friends only (the heal itself
  covers everyone).
- The extra member stands between the two friends when a room loads.

## Building from source

The mod is plain C (`src/`), built with [llvm-mingw](https://github.com/mstorsjo/llvm-mingw)
(release 20250114, x86_64 UCRT, on Linux or Windows):

    CC=/path/to/llvm-mingw/bin/x86_64-w64-mingw32-clang ./build.sh

The output is `build/kh1party.dll`.

`./package.sh` builds the DLL and packs the Mod Manager archive
`dist/KH1-Full-Party.zip` (needs `python3`).

`tests/harness.c` checks the install against a copy of the game exe without
running the game (Linux, gcc): it maps the exe the way Windows would, runs the
full install and reports every patch site that does not match.

    cd tests
    gcc -O1 -w -I shim -include shim/windows.h -o harness harness.c ../src/[^d]*.c
    ./harness "/path/to/KINGDOM HEARTS FINAL MIX.exe"

## How it works (short version)

See the comment at the top of `src/kh1party.h`. In brief: the game already has
a four-entry party list but only spawns three bodies; the mod spawns the
fourth in party slot 3 and widens every game table that was sized for three
(model memory, weapon slots, voice banks and voice channels, party arrays,
HUD gauges, level-up prompts, the item pop-up, party menu figures). The fourth
member runs a second copy of the friend AI, swapped in around each AI call.
