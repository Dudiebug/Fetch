#!/bin/bash
# Cross-compiles Fetch as dsound.dll (a load-time import of the game exe) with llvm-mingw, and the clonetrain program.
# Usage: ./build.sh [Release|Debug]   Output: build/dsound.dll, build/clonetrain.exe
set -euo pipefail
cd "$(dirname "$0")"
TC=${LLVM_MINGW:-$HOME/.local/opt/llvm-mingw-20260922-ucrt-ubuntu-22.04-x86_64}
CXX="$TC/bin/x86_64-w64-mingw32-clang++"
CC="$TC/bin/x86_64-w64-mingw32-clang"
CFG=${1:-Release}
OPT=$([ "$CFG" = Debug ] && echo "-O0 -g" || echo "-O2")
mkdir -p build/obj

COMMON="$OPT -Wall -Wextra -D_WIN32_WINNT=0x0A00 -DWIN32_LEAN_AND_MEAN -DNOMINMAX -fstack-protector-strong"
INC="-Isrc -Ishared -Ishared/protocol -Ithird_party/minhook/include"
JOBS=${BUILD_JOBS:-$(( ($(nproc 2>/dev/null || echo 2) + 1) / 2 ))}

objs=()
compile() {
  local o=$1; shift
  while [ "$(jobs -rp | wc -l)" -ge "$JOBS" ]; do wait -n || true; done
  rm -f "$o.status"
  ( if "$@" -o "$o" >"$o.log" 2>&1; then echo 0; else echo 1; fi >"$o.status" ) &
  objs+=("$o")
}
for f in third_party/minhook/src/{buffer,hook,trampoline}.c third_party/minhook/src/hde/hde64.c; do
  compile "build/obj/$(basename "${f%.c}").o" "$CC" $COMMON -w -Ithird_party/minhook/include -c "$f"
done
for f in src/*.cpp src/ue/*.cpp src/gameplay/*.cpp src/ui/*.cpp; do
  compile "build/obj/$(echo "${f%.cpp}" | tr '/' '_').o" "$CXX" -std=c++20 $COMMON -Werror $INC -c "$f"
done
wait
failed=0
for o in "${objs[@]}"; do
  cat "$o.log" >&2
  [ "$(cat "$o.status" 2>/dev/null)" = 0 ] || { echo "build: compile failed for $o" >&2; failed=1; }
done
[ "$failed" -eq 0 ] || exit 1
LINK_FLAGS="-fstack-protector-strong -s -Wl,-Map,build/dsound.map"
echo 0 > build/guard_cf
if "$CXX" -c -x c++ /dev/null -o /dev/null -mguard=cf 2>/dev/null; then
  LINK_FLAGS="$LINK_FLAGS -mguard=cf"
  echo 1 > build/guard_cf
fi
"$CXX" -shared -o build/dsound.dll "${objs[@]}" src/dsound.def $LINK_FLAGS -static -static-libgcc -static-libstdc++ -Wl,--kill-at -lws2_32 -lwinmm
"$CXX" -std=c++20 -O2 -D_USE_MATH_DEFINES -Ishared -Ishared/protocol -static -static-libgcc -static-libstdc++ tools/clonetrain.cpp -o build/clonetrain.exe
echo "built build/dsound.dll and build/clonetrain.exe ($CFG)"
