#!/bin/bash
# ECHOFIELD build script
#   ./build.sh host     - desktop x86_64/aarch64 build linked against the system SDL2 (testing)
#   ./build.sh test     - build + run the automated test suite on the host
#   ./build.sh r36s     - cross-compile the R36S binary (aarch64, glibc >= 2.17 ABI)
#   ./build.sh all
# Requirements: clang + lld (any recent version). No SDL2/ALSA headers or aarch64
# sysroot are needed: see src/compat/ and tools/mkstubs.py.
set -euo pipefail
cd "$(dirname "$0")"
SRC="src/util.c src/sample.c src/params.c src/engine.c src/capture.c src/store.c src/audio.c src/draw.c src/vis.c src/ui.c src/diag.c"
CORE="src/util.c src/sample.c src/params.c src/engine.c src/capture.c src/store.c"
CFLAGS_COMMON="-std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -nostdlibinc -fno-stack-protector -ffp-contract=fast -Isrc -Isrc/compat"
SDL_HOST_LIB=$(ls /usr/lib/*-linux-gnu/libSDL2-2.0.so.0 /usr/lib/libSDL2-2.0.so.0 2>/dev/null | head -1 || true)
mkdir -p build

host() {
  [ -n "$SDL_HOST_LIB" ] || { echo "SDL2 runtime library not found on host"; exit 1; }
  clang $CFLAGS_COMMON -g $SRC src/main.c -o build/echofield.host "$SDL_HOST_LIB" -lm
  echo "built build/echofield.host"
}

test_() {
  # sanitizer build with gcc (clang's ASan runtime is often not installed)
  local GI; GI=$(gcc -print-file-name=include)
  gcc -std=c11 -O1 -g -Wall -Wno-unused-parameter -nostdinc -isystem "$GI" -ffreestanding -fno-stack-protector -Isrc -Isrc/compat \
    -fsanitize=address,undefined -fno-omit-frame-pointer $CORE tests/test_main.c -o build/tests "$SDL_HOST_LIB" -lm
  ASAN_OPTIONS=detect_leaks=0 ./build/tests "host $(uname -m), sanitizers on"
  # optimized build of the same tests with clang (catches optimizer-sensitive bugs)
  clang $CFLAGS_COMMON $CORE tests/test_main.c -o build/tests_opt "$SDL_HOST_LIB" -lm
  ./build/tests_opt "host $(uname -m), clang -O2"
}

r36s() {
  local T="--target=aarch64-linux-gnu"
  local OBJ=build/aarch64; rm -rf $OBJ; mkdir -p $OBJ
  for f in $SRC src/main.c; do
    clang $T -mcpu=cortex-a35 -mno-outline-atomics -fPIE $CFLAGS_COMMON -c "$f" -o "$OBJ/$(basename "${f%.c}").o"
  done
  clang $T -c src/crt/start_aarch64.S -o $OBJ/start.o
  # no static constructors may exist (our _start passes init=NULL to __libc_start_main)
  if llvm-readelf -S $OBJ/*.o | grep -q "init_array"; then echo "ERROR: .init_array present"; exit 1; fi
  # externally-defined symbols only: merge objects first so internal references resolve
  ld.lld -r -o $OBJ/merged.o.tmp $OBJ/*.o
  llvm-nm -u $OBJ/merged.o.tmp | awk '{print $NF}' | sort -u > $OBJ/undef.txt
  rm -f $OBJ/merged.o.tmp
  python3 tools/mkstubs.py $OBJ/stubs clang $OBJ/undef.txt
  ld.lld -pie --dynamic-linker=/lib/ld-linux-aarch64.so.1 --hash-style=both -z noexecstack -z relro -z now \
    --as-needed --build-id -o build/echofield.aarch64 $OBJ/start.o $(ls $OBJ/*.o | grep -v start.o) \
    -L$OBJ/stubs -l:libc.so -l:libm.so -l:libSDL2.so
  llvm-strip --strip-debug build/echofield.aarch64
  echo "built build/echofield.aarch64"
  python3 tools/check_elf.py build/echofield.aarch64
}

case "${1:-all}" in
  host) host ;;
  test) test_ ;;
  r36s) r36s ;;
  all) host; test_; r36s ;;
  *) echo "usage: $0 host|test|r36s|all"; exit 1 ;;
esac
