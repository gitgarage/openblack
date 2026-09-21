cmake_minimum_required(VERSION 3.20)
project(bgfx_spike LANGUAGES C CXX)

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

add_executable(bgfx_spike main.cpp)
target_compile_features(bgfx_spike PRIVATE cxx_std_20)
target_link_libraries(bgfx_spike PRIVATE bgfx bx bimg EGL glapi drm_nouveau nx)
nx_create_nro(bgfx_spike)
