#!/usr/bin/env bash
# Builds openblack.nro inside the devkita64 container. Needs host-tools/ (build-host-tools.sh) and the
# pinned checkouts listed in deps.txt under deps/. Output: build/openblack.nro
set -euo pipefail
cd "$(dirname "$0")/.."
USBLINK="$(cd ../atlas-usb-link && pwd)"
# bgfx patches for the Switch (present through the app's EGL surface), applied once.
for p in switch-port/patches/*.patch; do
  git -C switch-spike/deps/bgfx.cmake/bgfx apply --check "$PWD/$p" 2>/dev/null && git -C switch-spike/deps/bgfx.cmake/bgfx apply "$PWD/$p" && echo "applied $p"
done
docker run --rm -e KEEP_GOING="${KEEP_GOING:-}" --user "$(id -u):$(id -g)" -e HOME=/tmp -v "$PWD":/work -v "$USBLINK":/atlas-usb-link -w /work devkitpro/devkita64:latest bash -c '
  set -e
  export DEVKITPRO=/opt/devkitpro PATH=/opt/devkitpro/devkitA64/bin:/opt/devkitpro/tools/bin:$PATH
  cmake -S . -B switch-port/build -DCMAKE_PROJECT_INCLUDE=/work/switch-port/switch-setup.cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/Switch.cmake \
    -DCMAKE_BUILD_TYPE=Release -DATLAS_USB_LINK_DIR=/atlas-usb-link
  cmake --build switch-port/build --target openblack openblack_nro -j"$(nproc)" -- ${KEEP_GOING:+-k 0} 2>&1
'
