#!/usr/bin/env bash
# Crossbuild von Linux/macOS aus mit Zig (pip install ziglang).
# Erzeugt bin/PaintClone.exe (Windows x64, statisch gelinkt).
set -euo pipefail
cd "$(dirname "$0")"
ZIG="${ZIG:-python3 -m ziglang}"
mkdir -p build/zig bin
$ZIG rc /y /fo build/zig/PaintClone.res src/PaintClone.rc
$ZIG c++ -target x86_64-windows-gnu -std=c++17 -O2 -municode \
  -DUNICODE -D_UNICODE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
  -Wno-nullability-completeness -Wno-macro-redefined \
  src/main.cpp src/canvas.cpp src/panels.cpp src/image.cpp src/shapes.cpp src/dialogs.cpp \
  build/zig/PaintClone.res \
  -lgdiplus -lcomctl32 -lcomdlg32 -lgdi32 -luser32 -lshell32 -lshlwapi -lole32 -luuid -ladvapi32 \
  -Wl,--subsystem,windows -s -o bin/PaintClone.exe
echo "OK: bin/PaintClone.exe"
