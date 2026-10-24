#!/bin/sh
# Builds the DLL and packs the OpenKH Mod Manager archive dist/KH1-Full-Party-Epic.zip
# (mod.yml, README, CHANGELOG, icon, preview, dll/kh1party.dll, plus the source).
set -e
cd "$(dirname "$0")"
./build.sh
rm -rf dist && mkdir -p dist
python3 - <<'PY'
import os, zipfile
files = ['mod.yml', 'README.md', 'CHANGELOG.md', 'icon.png', 'preview.png', 'build.sh']
with zipfile.ZipFile('dist/KH1-Full-Party-Epic.zip', 'w', zipfile.ZIP_DEFLATED) as z:
    for f in files:
        z.write(f, f)
    z.write('build/kh1party.dll', 'dll/kh1party.dll')
    for d in ('src', 'tests', 'tools'):
        for root, _, names in os.walk(d):
            for n in sorted(names):
                if n != 'harness' and '__pycache__' not in root:
                    p = os.path.join(root, n)
                    z.write(p, p)
PY
echo packed dist/KH1-Full-Party-Epic.zip
