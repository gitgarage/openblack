# openblack for the Switch, built from openblack's own CMake, unchanged. build.sh configures openblack's
# own source tree and injects this file with CMAKE_PROJECT_INCLUDE, so it runs at the end of
# openblack's first project() call - openblack stays the top level, and its ${CMAKE_SOURCE_DIR}-relative
# asset and shader paths stay right (a wrapper project with add_subdirectory broke both). It supplies what
# vcpkg would on a desktop: devkitPro's portlibs (SDL2, OpenAL, Bullet, glm, zlib, minizip) plus the
# pinned checkouts in deps/ (see deps.txt), bgfx configured the way switch-spike proved on the
# console, and the laptop-built shaderc from build-host-tools.sh.

set(OB_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)
include_guard(GLOBAL)
set(DEPS ${CMAKE_CURRENT_LIST_DIR}/deps)
set(PORTLIBS $ENV{DEVKITPRO}/portlibs/switch)

# --- bgfx, exactly as switch-spike (commit fb370b0) ran it on the console ---------------------------
add_compile_definitions(
  __NX__=1
  BGFX_CONFIG_RENDERER_NVN=0
  BGFX_CONFIG_RENDERER_VULKAN=0
  BGFX_CONFIG_RENDERER_OPENGL=0
  BGFX_CONFIG_RENDERER_WEBGPU=0
  # Named explicitly or bgfx zeroes it with the others - the Noop-renderer black screen.
  BGFX_CONFIG_RENDERER_OPENGLES=31
  _GNU_SOURCE=1
)
add_compile_options("-D__is_target_os(x)=0")
# Minimal graphics on the Switch: the water's reflection of models drawn only for those near the sea (the land's bump
# maps are on, 2026-10-23). Off with -DOPENBLACK_SWITCH_MINIMAL_GRAPHICS=OFF.
option(OPENBLACK_SWITCH_MINIMAL_GRAPHICS "Switch: lighter water reflections" ON)
if(OPENBLACK_SWITCH_MINIMAL_GRAPHICS)
  add_compile_definitions(OPENBLACK_SWITCH_MINIMAL_GRAPHICS=1)
endif()
# openblack calls std::memcpy in a few files without <cstring>; desktop standard libraries pull it in
# through other headers, newlib's libstdc++ does not.
add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include cstring>")
include_directories(BEFORE SYSTEM ${OB_ROOT}/switch-spike/compat)

# The laptop's shaderc/bin2c, imported before bgfx.cmake's tool utilities look for them.
add_executable(bgfx::shaderc IMPORTED GLOBAL)
set_target_properties(bgfx::shaderc PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_LIST_DIR}/host-tools/cmake/bgfx/shaderc)
add_executable(bgfx::bin2c IMPORTED GLOBAL)
set_target_properties(bgfx::bin2c PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_LIST_DIR}/host-tools/cmake/bx/bin2c)

# Where shaderc finds bgfx_shader.sh (mul, u_view, ...). bgfx.cmake only sets this when it builds the
# tools itself, which this build leaves to build-host-tools.sh.
set(BGFX_SHADER_INCLUDE_PATH ${OB_ROOT}/switch-spike/deps/bgfx.cmake/bgfx/src)
set(BGFX_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BGFX_INSTALL OFF CACHE BOOL "" FORCE)
set(BGFX_CUSTOM_TARGETS OFF CACHE BOOL "" FORCE)
set(BGFX_CONFIG_VIDEO OFF CACHE BOOL "" FORCE)
# Single-threaded, as switch-spike ran: bgfx renders on the calling thread, where SDL's GLES context
# is current (it is made there by Sdl2WindowingSystem and handed to bgfx).
set(BGFX_CONFIG_MULTITHREADED OFF CACHE BOOL "" FORCE)
add_subdirectory(${OB_ROOT}/switch-spike/deps/bgfx.cmake bgfx EXCLUDE_FROM_ALL)
target_include_directories(bgfx BEFORE PRIVATE ${PORTLIBS}/include) # Mesa's EGL headers, not bgfx's
# Imported rather than ALIAS: openblack adds bgfx::bx to bgfx::bgfx's links, which an alias refuses.
add_library(bgfx::bgfx INTERFACE IMPORTED GLOBAL)
set_target_properties(bgfx::bgfx PROPERTIES INTERFACE_LINK_LIBRARIES "bgfx;bimg;bx") # openblack includes bimg too
add_library(bgfx::bx ALIAS bx)
add_library(bgfx::bimg ALIAS bimg)
target_link_libraries(bgfx INTERFACE GLESv2 EGL glapi drm_nouveau)

# --- pinned source dependencies ---------------------------------------------------------------------
# openblack uses fmt directly, so spdlog must share it rather than build its bundled copy.
add_subdirectory(${DEPS}/fmt fmt EXCLUDE_FROM_ALL)
set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "" FORCE)
# newlib's struct tm has no tm_gmtoff, which spdlog reads for "%z" unless told there is none.
add_compile_definitions(SPDLOG_NO_TZ_OFFSET)
add_subdirectory(${DEPS}/spdlog spdlog EXCLUDE_FROM_ALL)
add_subdirectory(${DEPS}/entt entt EXCLUDE_FROM_ALL)

add_library(imgui STATIC
  ${DEPS}/imgui/imgui.cpp ${DEPS}/imgui/imgui_draw.cpp ${DEPS}/imgui/imgui_tables.cpp
  ${DEPS}/imgui/imgui_widgets.cpp ${DEPS}/imgui/imgui_demo.cpp ${DEPS}/imgui/misc/cpp/imgui_stdlib.cpp)
target_include_directories(imgui PUBLIC ${DEPS}/imgui ${DEPS}/imgui/misc/cpp)
# imgui's default "open in shell" forks with execvp/waitpid; there is no shell on the Switch.
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS)
add_library(imgui::imgui ALIAS imgui)

# glm from deps/, not portlibs: devkitPro's 0.9.9.7 lacks the sized vector types openblack uses.
add_library(glm INTERFACE)
target_include_directories(glm BEFORE INTERFACE ${DEPS}/glm)
add_library(glm::glm ALIAS glm)

# Cache entries, so openblack's own find_path() calls take them as already found.
set(Stb_INCLUDE_DIR ${DEPS}/stb CACHE PATH "" FORCE)
set(DRLIBS_INCLUDE_DIRS ${DEPS}/dr_libs CACHE PATH "" FORCE)
set(CXXOPTS_INCLUDE_DIRS ${DEPS}/cxxopts/include CACHE PATH "" FORCE)

# minizip and zlib from portlibs, as the target openblack's pkg-config fallback would make.
# Imported, not ALIAS: openblack itself makes minizip::minizip an alias of this.
add_library(unofficial::minizip::minizip INTERFACE IMPORTED GLOBAL)
set_target_properties(unofficial::minizip::minizip PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES ${PORTLIBS}/include
  INTERFACE_LINK_LIBRARIES "${PORTLIBS}/lib/libminizip.a;${PORTLIBS}/lib/libz.a")

# The development link (atlas-usb-link): the live log over USB-C, crash reports, and new builds sent down the cable
# (update.py). Off for a release, which logs to the SD card only and contains none of it.
option(OPENBLACK_SWITCH_DEV_LINK "Switch: build in the atlas-usb-link development link" OFF)
set(ATLAS_USB_LINK_DIR ${OB_ROOT}/../atlas-usb-link CACHE PATH "atlas-usb-link checkout")

# --- find_package: answer for what is supplied above, pass the rest (SDL2, OpenAL, Bullet) through --
set(_SWITCH_SUPPLIED Stb unofficial-minizip glm bgfx imgui spdlog EnTT GTest fmt)
macro(find_package _name)
  if("${_name}" IN_LIST _SWITCH_SUPPLIED)
    set(${_name}_FOUND TRUE)
  else()
    _find_package(${ARGV})
  endif()
endmacro()
# openblack's own find_path calls for header-only libraries: already set above, keep them.
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# bgfx's shader helper picks its platform from the host-OS variables and has no Switch entry. UNIX
# gives LINUX with the 120/300_es/spirv profiles; 300_es (GLSL ES) is what the Switch's GLES runs.
# In openblack, UNIX only adds a wayland-egl lookup (finds nothing here) and desktop install rules.
# Not ANDROID: openblack builds itself as a shared library for Android.
set(UNIX TRUE)

# Once openblack's whole tree is configured: link the USB log and crash reports in, and make the NRO.
function(_switch_finish)
  target_sources(openblack PRIVATE
    ${OB_ROOT}/switch-spike/compat/posix_memalign.c
    ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/switch_main.cpp)
  if(OPENBLACK_SWITCH_DEV_LINK)
    target_sources(openblack PRIVATE
      ${ATLAS_USB_LINK_DIR}/source/atlas_usb_link.c
      ${ATLAS_USB_LINK_DIR}/source/atlas_crash.c
      ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/switch_update.cpp)
    target_compile_definitions(openblack PRIVATE OPENBLACK_SWITCH_DEV_LINK=1)
    target_include_directories(openblack PRIVATE ${ATLAS_USB_LINK_DIR}/source)
  endif()
  # Joy-Con controls; switchInputFrame() is called from Game.cpp and lives in openblack_lib's link.
  target_sources(openblack_lib PRIVATE ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/switch_input.cpp)
  # The original's Bink movies (src/Video/MoviePlayer.cpp): portlibs' ffmpeg has the Bink demuxer and decoders.
  target_compile_definitions(openblack_lib PRIVATE OPENBLACK_HAVE_FFMPEG=1)
  target_include_directories(openblack_lib PRIVATE ${PORTLIBS}/include)
  target_link_libraries(openblack_lib PRIVATE
    ${PORTLIBS}/lib/libavformat.a ${PORTLIBS}/lib/libavcodec.a ${PORTLIBS}/lib/libdav1d.a ${PORTLIBS}/lib/libswscale.a
    ${PORTLIBS}/lib/libswresample.a ${PORTLIBS}/lib/libavutil.a ${PORTLIBS}/lib/libbz2.a ${PORTLIBS}/lib/libz.a)
  # switch_main.cpp's __wrap_main runs first: USB link, stdout to the link, the game path argument.
  target_link_options(openblack PRIVATE -Wl,--wrap=main)
  # Allocation failures are logged (switch_main.cpp): the first console runs died mid-load.
  target_link_options(openblack PRIVATE -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=memalign)
  # main.cpp needs cxxopts too; on a desktop it comes from the system include path.
  target_include_directories(openblack PRIVATE ${CXXOPTS_INCLUDE_DIRS})
  target_link_libraries(openblack PRIVATE glm::glm) # main.cpp's headers need glm; a desktop has it system-wide
  nx_create_nro(openblack)
endfunction()
cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL _switch_finish)
