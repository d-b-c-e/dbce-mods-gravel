#!/usr/bin/env bash
# Build dinput8.dll (x64). Needs MSYS2 MinGW64 - see README.
set -euo pipefail
cd "$(dirname "$0")"
export PATH="/e/msys64/mingw64/bin:$PATH"
mkdir -p build
[ -f lib/toolkit/include/forza_packet.h ] || { echo "lib/toolkit is missing - run tools/Sync-Toolkit.ps1 first" >&2; exit 1; }
x86_64-w64-mingw32-g++.exe -std=c++17 -O2 -s -shared -static -static-libgcc -static-libstdc++ \
  -Wall -Wno-unused-function -Wno-stringop-truncation -Ilib/toolkit/include \
  -o build/dinput8.dll src/proxy.cpp src/fmod_tap.cpp src/ue4.cpp src/telemetry.cpp src/recording.cpp src/triple.cpp src/dinput8.def \
  -ldxguid -luuid -lole32 -lws2_32
file build/dinput8.dll
