#!/bin/sh
# Regenerates src/addrmap.c (Steam RVA -> Epic RVA table).
# Usage: tools/epic_map/run.sh STEAM_EXE EPIC_EXE
# Needs: python3, objdump (binutils), gcc. Writes intermediate files to ./work.
set -e
here=$(cd "$(dirname "$0")" && pwd); root=$(cd "$here/../.." && pwd)
work=${EPICMAP_WORK:-$root/work/epic_map}; mkdir -p "$work"; export EPICMAP_WORK="$work"
cp "$1" "$work/steam.exe"; cp "$2" "$work/epic.exe"
objdump -d -M intel --no-show-raw-insn "$work/steam.exe" > "$work/steam.asm"
objdump -d -M intel --no-show-raw-insn "$work/epic.exe" > "$work/epic.asm"
# every hex literal in the source that could be an address
python3 - "$root/src" "$work/lits.txt" <<'PY'
import re, glob, sys, os
out = {}
for f in sorted(glob.glob(sys.argv[1] + '/*.[ch]')):
    if f.endswith('addrmap.c'): continue
    for ln, line in enumerate(open(f), 1):
        for m in re.finditer(r'0x([0-9a-fA-F]+)', line.split('//')[0]):
            v = int(m.group(1), 16)
            if 0x1000 <= v < 0x2f90000: out.setdefault(v, '%s:%d' % (os.path.basename(f), ln))
open(sys.argv[2], 'w').write(''.join('%x %s\n' % (v, c) for v, c in sorted(out.items())))
PY
# Two passes: build the table from the source literals, then trace every
# address the install touches on the Epic build and rebuild the table with
# those too (computed addresses such as table base + offset).
cd "$here"
python3 match.py
python3 regions.py > /dev/null
rm -f "$work/a_calls.txt"
for pass in 1 2 3; do
    python3 gen.py "$root/src/addrmap.c" > "$work/gen_report_$pass.txt"
    (cd "$root/tests" && gcc -O1 -w -DADDRLOG -I shim -include shim/windows.h -o "$work/h_log" harness.c \
        $(ls ../src/*.c | grep -v -e dllmain.c -e debug.c))
    "$work/h_log" "$work/epic.exe" 2> "$work/a_calls.new" | tail -1
    sort -u "$work/a_calls.new" > "$work/a_calls.sorted"
    if [ -f "$work/a_calls.txt" ] && cmp -s "$work/a_calls.sorted" "$work/a_calls.txt"; then break; fi
    mv "$work/a_calls.sorted" "$work/a_calls.txt"
done
tail -3 "$work/gen_report_$pass.txt"
