#!/usr/bin/env bash
# Builds the spike inside the devkitA64 container (the host's devkitPro compilers need a newer glibc
# than this laptop has). Output: build/bgfx_spike.nro
set -euo pipefail
cd "$(dirname "$0")"
docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp -v "$PWD":/work -w /work devkitpro/devkita64:latest bash -c '
  set -e
  export DEVKITPRO=/opt/devkitpro
  export PATH=$DEVKITPRO/devkitA64/bin:$DEVKITPRO/tools/bin:$PATH
  cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/Switch.cmake -DCMAKE_BUILD_TYPE=Release
  cmake --build build --target bgfx_spike_nro 2>&1 || cmake --build build 2>&1
'
