#!/bin/sh
# Builds kh1party.dll with llvm-mingw (https://github.com/mstorsjo/llvm-mingw,
# release 20250114, x86_64 UCRT). Set CC to its x86_64-w64-mingw32-clang.
set -e
cd "$(dirname "$0")"
CC=${CC:-x86_64-w64-mingw32-clang}
mkdir -p build
$CC -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -std=c11 \
    -fno-strict-aliasing -shared -o build/kh1party.dll src/*.c \
    -Wl,--subsystem,windows,--exclude-all-symbols -s -static-libgcc
echo built build/kh1party.dll
