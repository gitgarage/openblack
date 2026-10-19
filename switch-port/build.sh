#!/usr/bin/env bash
# Builds openblack.nro inside the devkita64 container. Needs host-tools/ (build-host-tools.sh) and the
# pinned checkouts listed in deps.txt under deps/.
#   With ../atlas-usb-link present: the development build (live USB log, crash reports, update.py), into
#   switch-port/build/src/openblack.nro.
#   RELEASE=1, or without it: the release build, none of that in it, into switch-port/build-release/src/openblack.nro.
set -euo pipefail
cd "$(dirname "$0")/.."
if [ "${RELEASE:-0}" != 1 ] && [ -d ../atlas-usb-link ]; then
  USBLINK="$(cd ../atlas-usb-link && pwd)"
  BUILD=switch-port/build
  DEVLINK=ON
  MOUNT=(-v "$USBLINK":/atlas-usb-link)
else
  BUILD=switch-port/build-release
  DEVLINK=OFF
  MOUNT=()
fi
echo "building $( [ $DEVLINK = ON ] && echo development || echo release ) into $BUILD"
# bgfx patches for the Switch (present through the app's EGL surface), applied once.
for p in switch-port/patches/*.patch; do
  git -C switch-spike/deps/bgfx.cmake/bgfx apply --check "$PWD/$p" 2>/dev/null && git -C switch-spike/deps/bgfx.cmake/bgfx apply "$PWD/$p" && echo "applied $p"
done
# Always regenerate the shaders: the container's timestamps left edited .sc files looking older than their
# compiled headers, so shader changes silently never reached the Switch.
rm -rf "$BUILD"/include/generated/shaders
docker run --rm -e KEEP_GOING="${KEEP_GOING:-}" -e BUILD="$BUILD" -e DEVLINK="$DEVLINK" --user "$(id -u):$(id -g)" -e HOME=/tmp \
  -v "$PWD":/work "${MOUNT[@]}" -w /work devkitpro/devkita64:latest bash -c '
  set -e
  export DEVKITPRO=/opt/devkitpro PATH=/opt/devkitpro/devkitA64/bin:/opt/devkitpro/tools/bin:$PATH
  cmake -S . -B "$BUILD" -DCMAKE_PROJECT_INCLUDE=/work/switch-port/switch-setup.cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/Switch.cmake \
    -DCMAKE_BUILD_TYPE=Release -DOPENBLACK_SWITCH_DEV_LINK=$DEVLINK -DATLAS_USB_LINK_DIR=/atlas-usb-link
  cmake --build "$BUILD" --target openblack openblack_nro -j"$(nproc)" -- ${KEEP_GOING:+-k 0} 2>&1
'
