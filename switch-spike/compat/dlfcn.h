// Empty on purpose. bx/src/os.cpp includes <dlfcn.h> on every POSIX platform, but newlib has none, and on
// BX_PLATFORM_NX bx never calls dlopen/dlclose (it returns NULL), so nothing from it is needed.
