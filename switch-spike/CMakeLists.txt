cmake_minimum_required(VERSION 3.20)
project(bgfx_spike LANGUAGES C CXX)
set(ATLAS_USB_LINK_DIR /atlas-usb-link CACHE PATH "atlas-usb-link checkout")

# Feasibility spike: does bgfx run on homebrew Switch at all? bgfx only knows the Switch as
# BX_PLATFORM_NX (Nintendo's official SDK, keyed on __NX__). That platform is already static-EGL
# and no-dlopen, which is exactly what homebrew needs, so claim it here and switch off the two
# renderers homebrew has no access to (NVN, Vulkan). OpenGL ES 3 comes from Mesa's EGL.
add_compile_definitions(
  __NX__=1
  BGFX_CONFIG_RENDERER_NVN=0
  BGFX_CONFIG_RENDERER_VULKAN=0
  BGFX_CONFIG_RENDERER_OPENGL=0
  BGFX_CONFIG_RENDERER_WEBGPU=0
  # Must be named: once any BGFX_CONFIG_RENDERER_* is defined, bgfx's config.h stops picking
  # platform defaults and sets every unnamed renderer to 0 - GLES included. Without this line the
  # first console run silently got the Noop renderer and a black screen (2026-09-22). 31 = ES 3.1.
  BGFX_CONFIG_RENDERER_OPENGLES=31
  # newlib hides POSIX functions (ftello, ...) under strict -std=c++NN unless asked.
  _GNU_SOURCE=1
)

# bx probes for visionOS with a Clang-only builtin that GCC's preprocessor cannot even parse. Must be
# an option, not a definition: add_compile_definitions silently drops function-like macros.
add_compile_options("-D__is_target_os(x)=0")
include_directories(BEFORE SYSTEM ${CMAKE_CURRENT_SOURCE_DIR}/compat) # empty dlfcn.h, see compat/

set(BGFX_BUILD_TOOLS OFF CACHE BOOL "" FORCE)      # shaderc etc. run on the laptop, not the Switch
set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BGFX_INSTALL OFF CACHE BOOL "" FORCE)
set(BGFX_CUSTOM_TARGETS OFF CACHE BOOL "" FORCE)
set(BGFX_CONFIG_MULTITHREADED OFF CACHE BOOL "" FORCE) # no render thread: one less thing to go wrong
set(BGFX_CONFIG_VIDEO OFF CACHE BOOL "" FORCE)
add_subdirectory(deps/bgfx.cmake EXCLUDE_FROM_ALL)
# bgfx bundles Khronos EGL headers whose eglplatform.h stops at "Platform not recognized" for the
# Switch; devkitPro's Mesa headers (portlibs) have the generic fallback. Search those first.
target_include_directories(bgfx BEFORE PRIVATE $ENV{DEVKITPRO}/portlibs/switch/include)

# Live log over the USB-C cable to the Pixel Tablet - ../../atlas-usb-link, mounted at /atlas-usb-link
# by build.sh.
add_executable(bgfx_spike main.cpp compat/posix_memalign.c ${ATLAS_USB_LINK_DIR}/source/atlas_usb_link.c)
target_include_directories(bgfx_spike PRIVATE ${ATLAS_USB_LINK_DIR}/source)
target_compile_features(bgfx_spike PRIVATE cxx_std_20)
target_link_libraries(bgfx_spike PRIVATE bgfx bx bimg GLESv2 EGL glapi drm_nouveau nx)
nx_create_nro(bgfx_spike)
