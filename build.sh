#!/bin/sh
# Builds d3d8.dll (32-bit) with the portable llvm-mingw toolchain in tools/.
set -e
cd "$(dirname "$0")"
TC=tools/llvm-mingw-20260922-msvcrt-x86_64/bin
mkdir -p build
"$TC/i686-w64-mingw32-clang++" -std=c++17 -O2 -shared -static \
    -fno-exceptions -fno-rtti -Wall -Wno-unused-function \
    -o build/d3d8.dll \
    src/main.cpp src/hooks.cpp src/timescale.cpp src/input.cpp src/game.cpp \
    src/render.cpp src/wheel.cpp src/debug.cpp src/scene.cpp src/gun.cpp src/trace.cpp src/weapon.cpp src/react.cpp src/decal.cpp src/sky.cpp src/rocket.cpp src/explode.cpp src/sound.cpp src/kbm.cpp src/gamefiles.cpp src/carglass.cpp src/tires.cpp src/lab.cpp src/vcover.cpp src/d3d8.def \
    -luser32 -s
echo "built build/d3d8.dll"
