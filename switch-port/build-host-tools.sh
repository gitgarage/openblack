#!/usr/bin/env bash
# Builds bgfx's shaderc and bin2c for the laptop (x86_64), inside the devkita64 container, from the
# same bgfx.cmake the spike proved on the console. The Switch build imports them as bgfx::shaderc /
# bgfx::bin2c to compile openblack's shaders (the essl/300_es profile is what GLES uses).
set -euo pipefail
cd "$(dirname "$0")/.."
# Runs as root to apt-get the X11/GL headers bgfx's native Linux configure insists on (shaderc itself
# never uses them), then hands the output back to this user.
docker run --rm -e HOME=/tmp -e UID_GID="$(id -u):$(id -g)" -v "$PWD":/work -w /work devkitpro/devkita64:latest bash -c '
  set -e
  apt-get update -qq >/dev/null && apt-get install -y -qq libx11-dev libgl-dev >/dev/null
  cmake -S switch-spike/deps/bgfx.cmake -B switch-port/host-tools -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DBGFX_BUILD_TOOLS=ON -DBGFX_BUILD_TOOLS_SHADER=ON -DBGFX_BUILD_TOOLS_BIN2C=ON \
    -DBGFX_BUILD_TOOLS_GEOMETRY=OFF -DBGFX_BUILD_TOOLS_TEXTURE=OFF -DBGFX_BUILD_EXAMPLES=OFF -DBGFX_INSTALL=OFF
  cmake --build switch-port/host-tools --target shaderc bin2c -j"$(nproc)"
  chown -R "$UID_GID" switch-port/host-tools
'
ls -la switch-port/host-tools/cmake/bgfx/shaderc switch-port/host-tools/cmake/bx/bin2c 2>/dev/null || find switch-port/host-tools -maxdepth 4 -type f \( -name shaderc -o -name bin2c \)
