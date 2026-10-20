// Switch entry shim for openblack, linked with -Wl,--wrap=main so openblack's own main.cpp stays
// untouched. Before openblack starts it:
//  - brings up the USB-C dev link to the Pixel Tablet (atlas_usb_link),
//  - sends stdout/stderr down the link and into sdmc:/switch/openblack/openblack.log, so openblack's
//    whole log (it logs to stdout with --log-file stdout) arrives live, and survives on the SD card
//    when the link is down,
//  - supplies the arguments hbmenu can't: the game data folder and the Switch's screen size.
// Crash reports come from atlas_crash.c, linked in alongside.
// All of the link (and the crash reports, new builds over the cable, the script trace and no auto-sleep) is the
// development build's only (OPENBLACK_SWITCH_DEV_LINK): a release logs to the SD card alone.
#if defined(OPENBLACK_SWITCH_DEV_LINK)
#include "atlas_usb_link.h"
#endif

#include <switch.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/iosupport.h>
#include <exception>
#include <vector>

extern "C" int __real_main(int argc, char** argv);
#if defined(OPENBLACK_SWITCH_DEV_LINK)
void startUpdateListener();                // switch_update.cpp
bool stopUpdateListener(unsigned timeoutMs); // switch_update.cpp
#else
// A release has no link: nothing comes down a cable, and Game.cpp's check for a new build is always no.
void atlasLinkWrite(const char*, size_t) {}
bool switchUpdateWantsRestart()
{
	return false;
}
#endif
extern "C" void* __real_malloc(size_t size);
extern "C" void* __real_calloc(size_t count, size_t size);
extern "C" void* __real_realloc(void* ptr, size_t size);
extern "C" void* __real_memalign(size_t alignment, size_t size);

namespace
{
constexpr const char* k_GamePath = "sdmc:/switch/openblack/game";
constexpr const char* k_LogPath = "sdmc:/switch/openblack/openblack.log";

FILE* g_logFile = nullptr;
// Every thread logs (the game, the memory thread, the update listener): one at a time into the SD card's log file.
Mutex g_logMutex;

ssize_t teeWrite(struct _reent*, void*, const char* ptr, size_t len)
{
	atlasLinkWrite(ptr, len);
	mutexLock(&g_logMutex);
	if (g_logFile != nullptr)
	{
		fwrite(ptr, 1, len, g_logFile);
		fflush(g_logFile);
	}
	mutexUnlock(&g_logMutex);
	return static_cast<ssize_t>(len);
}

devoptab_t g_teeDevoptab = [] {
	devoptab_t d {};
	d.name = "openblacktee";
	d.write_r = teeWrite;
	return d;
}();
// This process's memory, in MiB: what it has used and what it is allowed. An applet (launched from
// the Album) gets a small slice of RAM; a title override (hold R on a game) gets nearly all of it.
void memoryMiB(u64& used, u64& total)
{
	used = total = 0;
	svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
	svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
	used >>= 20;
	total >>= 20;
}

// Written straight to the link and the file, with no printf and no allocation, so it is safe to
// call from inside a failing malloc.
void rawLog(const char* text, size_t len)
{
	atlasLinkWrite(text, len);
	if (g_logFile != nullptr)
	{
		fwrite(text, 1, len, g_logFile);
		fflush(g_logFile);
	}
}

void logAllocationFailure(const char* what, size_t size)
{
	static __thread bool inside = false; // a failure while logging a failure must not recurse
	if (inside)
	{
		return;
	}
	inside = true;
	u64 used, total;
	memoryMiB(used, total);
	char line[160];
	int n = snprintf(line, sizeof(line), "[mem] ALLOCATION FAILED: %s(%zu bytes = %zu KiB), process using %lu of %lu MiB\n",
	                 what, size, size >> 10, used, total);
	rawLog(line, static_cast<size_t>(n));
	inside = false;
}

#if defined(OPENBLACK_SWITCH_DEV_LINK)
// The link's own progress: SD log only, since the link is what is failing when these are written.
void linkLog(const char* line)
{
	if (g_logFile != nullptr)
	{
		fputs(line, g_logFile);
		fflush(g_logFile);
	}
}
#endif

// Every 500ms, one line: how full memory is. The last one before a death shows how close it was.
Thread g_memThread;
volatile bool g_memRunning = true;
void memoryThread(void*)
{
	u64 lastUsed = ~0ull;
	while (g_memRunning)
	{
		u64 used, total;
		memoryMiB(used, total);
		if (used != lastUsed) // only when it changes, to keep the log readable
		{
			char line[96];
			int n = snprintf(line, sizeof(line), "[mem] %lu / %lu MiB used\n", used, total);
			rawLog(line, static_cast<size_t>(n));
			lastUsed = used;
		}
		svcSleepThread(500'000'000ULL);
	}
}
} // namespace

extern "C" void* __wrap_malloc(size_t size)
{
	void* p = __real_malloc(size);
	if (p == nullptr && size != 0)
	{
		logAllocationFailure("malloc", size);
	}
	return p;
}

extern "C" void* __wrap_calloc(size_t count, size_t size)
{
	void* p = __real_calloc(count, size);
	if (p == nullptr && count != 0 && size != 0)
	{
		logAllocationFailure("calloc", count * size);
	}
	return p;
}

extern "C" void* __wrap_realloc(void* ptr, size_t size)
{
	void* p = __real_realloc(ptr, size);
	if (p == nullptr && size != 0)
	{
		logAllocationFailure("realloc", size);
	}
	return p;
}

extern "C" void* __wrap_memalign(size_t alignment, size_t size)
{
	void* p = __real_memalign(alignment, size);
	if (p == nullptr && size != 0)
	{
		logAllocationFailure("memalign", size);
	}
	return p;
}

extern "C" int __wrap_main(int argc, char** argv)
{
	// The log file is opened before stdout is redirected, so its own writes go to the SD card, not
	// back through the redirect.
	g_logFile = fopen(k_LogPath, "w");
	// The link comes up in the background, retried for as long as openblack runs, so a slow or
	// failed handshake never delays or loses anything: lines written before it connects are sent
	// first once it does, and each failed attempt's reason goes to the SD log.
#if defined(OPENBLACK_SWITCH_DEV_LINK)
	atlasLinkStartBackground(linkLog);
	// New builds can come down the same link (switch-port/update.py on the laptop): see switch_update.cpp.
	startUpdateListener();
	// No auto-sleep or screen dimming while openblack runs (Mike, 2026-10-16: development with the Switch sitting
	// still between tests). The system restores both when the app exits.
	appletSetMediaPlaybackState(true);
	appletSetAutoSleepDisabled(true);
#endif
	bool link = false;
	devoptab_list[STD_OUT] = &g_teeDevoptab;
	devoptab_list[STD_ERR] = &g_teeDevoptab;
	setvbuf(stdout, nullptr, _IOLBF, 0);
	setvbuf(stderr, nullptr, _IONBF, 0);
#if defined(OPENBLACK_SWITCH_DEV_LINK)
	printf("openblack for Switch: usb link connecting in the background, game path %s\n", k_GamePath);
#else
	printf("openblack for Switch, game path %s\n", k_GamePath);
#endif
#if defined(OPENBLACK_SWITCH_DEV_LINK)
	// The last crash's report (the development link's crash handler saves it to the SD card as the app dies; the cable
	// is often gone by then): into this run's log, then kept aside so it is reported once.
	if (FILE* crash = fopen("sdmc:/atlas-crash.log", "r"); crash != nullptr)
	{
		printf("=== the last crash (sdmc:/atlas-crash.log) ===\n");
		char line[256];
		while (fgets(line, sizeof(line), crash) != nullptr)
		{
			fputs(line, stdout);
		}
		fclose(crash);
		printf("=== end of the last crash ===\n");
		remove("sdmc:/atlas-crash-reported.log");
		rename("sdmc:/atlas-crash.log", "sdmc:/atlas-crash-reported.log");
	}
#endif
	// A C++ exception nobody caught, or std::terminate: what it was, before the app goes (the crash handler only
	// sees hardware faults).
	std::set_terminate([] {
		const char* what = "no exception";
		try
		{
			if (const auto current = std::current_exception())
			{
				std::rethrow_exception(current);
			}
		}
		catch (const std::exception& e)
		{
			what = e.what();
		}
		catch (...)
		{
			what = "an exception that is not a std::exception";
		}
		printf("[terminate] std::terminate: %s\n", what);
		// Where it was thrown: an uncaught exception is not unwound before terminate, so the frames are still here.
		// Each frame record is {previous fp, return address}. Addresses printed with this handler's own, to work out
		// the code offsets against the .elf afterwards (nm: the terminate lambda's address there).
		printf("[terminate] main at %p\n", reinterpret_cast<void*>(&__wrap_main));
		auto** fp = static_cast<void**>(__builtin_frame_address(0));
		for (int i = 0; i < 24 && fp != nullptr; ++i)
		{
			void* returnTo = fp[1];
			printf("[terminate]   #%d %p\n", i, returnTo);
			auto** next = static_cast<void**>(fp[0]);
			if (next <= fp)
			{
				break;
			}
			fp = next;
		}
		fflush(stdout);
		std::abort();
	});
	(void)link;
	u64 used, total;
	memoryMiB(used, total);
	AppletType appletType = appletGetAppletType();
	printf("[mem] start: %lu / %lu MiB, running as %s (applet type %d)\n", used, total,
	       appletType == AppletType_Application || appletType == AppletType_SystemApplication ? "an application (full memory)"
	                                                                                         : "an applet (limited memory)",
	       static_cast<int>(appletType));
	threadCreate(&g_memThread, memoryThread, nullptr, nullptr, 0x4000, 0x3F, -2);
	threadStart(&g_memThread);

	// No periodic screenshots any more: the live log replaced them, and their writer thread crashed in newlib's
	// write (2026-10-16, ShotWriter::Run -> fwrite -> _write_r, fault at 0x20) - set OPENBLACK_SHOT_EVERY to bring
	// them back.
	setenv("OPENBLACK_SAVEPOINT_DIR", "sdmc:/switch/openblack/savepoints", 0); // D-pad test save points
#if defined(OPENBLACK_SWITCH_DEV_LINK)
	// Every 5 s: the camera and each script's position (what scripts are waiting on), into the log.
	setenv("OPENBLACK_TRACE_VM", "1", 0);
#endif

	std::vector<char*> args(argv, argv + argc);
	const char* extra[] = {"--game-path", k_GamePath, "--log-file", "stdout", "--width", "1280", "--height", "720"};
	for (const char* a : extra)
	{
		args.push_back(const_cast<char*>(a));
	}
	args.push_back(nullptr);

	int result = __real_main(static_cast<int>(args.size()) - 1, args.data());
	printf("openblack exited with %d\n", result);
	g_memRunning = false;
	threadWaitForExit(&g_memThread);
	threadClose(&g_memThread);
	// The update listener must be off the link before it closes (see switch_update.cpp). If it is still blocked,
	// the link is left open rather than hang or crash the exit - the loader can still start the next build.
#if defined(OPENBLACK_SWITCH_DEV_LINK)
	if (stopUpdateListener(5000))
	{
		atlasLinkExit();
	}
	else
	{
		printf("[update] the listener is still waiting on the link: leaving the link open\n");
	}
#endif
	if (g_logFile != nullptr)
	{
		fclose(g_logFile);
	}
	return result;
}
