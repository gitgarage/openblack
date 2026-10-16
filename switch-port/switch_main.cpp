// Switch entry shim for openblack, linked with -Wl,--wrap=main so openblack's own main.cpp stays
// untouched. Before openblack starts it:
//  - brings up the USB-C dev link to the Pixel Tablet (atlas_usb_link),
//  - sends stdout/stderr down the link and into sdmc:/switch/openblack/openblack.log, so openblack's
//    whole log (it logs to stdout with --log-file stdout) arrives live, and survives on the SD card
//    when the link is down,
//  - supplies the arguments hbmenu can't: the game data folder and the Switch's screen size.
// Crash reports come from atlas_crash.c, linked in alongside.
#include "atlas_usb_link.h"

#include <switch.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/iosupport.h>
#include <vector>

extern "C" int __real_main(int argc, char** argv);
void startUpdateListener(); // switch_update.cpp
extern "C" void* __real_malloc(size_t size);
extern "C" void* __real_calloc(size_t count, size_t size);
extern "C" void* __real_realloc(void* ptr, size_t size);
extern "C" void* __real_memalign(size_t alignment, size_t size);

namespace
{
constexpr const char* k_GamePath = "sdmc:/switch/openblack/game";
constexpr const char* k_LogPath = "sdmc:/switch/openblack/openblack.log";

FILE* g_logFile = nullptr;

ssize_t teeWrite(struct _reent*, void*, const char* ptr, size_t len)
{
	atlasLinkWrite(ptr, len);
	if (g_logFile != nullptr)
	{
		fwrite(ptr, 1, len, g_logFile);
		fflush(g_logFile);
	}
	return static_cast<ssize_t>(len);
}

devoptab_t g_teeDevoptab = [] {
	devoptab_t d {};
	d.name = "atlastee";
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

// The link's own progress: SD log only, since the link is what is failing when these are written.
void linkLog(const char* line)
{
	if (g_logFile != nullptr)
	{
		fputs(line, g_logFile);
		fflush(g_logFile);
	}
}

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
	atlasLinkStartBackground(linkLog);
	// New builds can come down the same link (switch-port/update.py on the laptop): see switch_update.cpp.
	startUpdateListener();
	bool link = false;
	devoptab_list[STD_OUT] = &g_teeDevoptab;
	devoptab_list[STD_ERR] = &g_teeDevoptab;
	setvbuf(stdout, nullptr, _IOLBF, 0);
	setvbuf(stderr, nullptr, _IONBF, 0);
	printf("openblack for Switch: usb link connecting in the background, game path %s\n", k_GamePath);
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

	// A screenshot every 20 s into sdmc:/switch/openblack/shots (emptied each run), which deliver.sh
	// fetches, so a run can be looked at on the laptop afterwards.
	setenv("OPENBLACK_SHOT_DIR", "sdmc:/switch/openblack/shots", 0);
	setenv("OPENBLACK_SHOT_EVERY", "20", 0);
	setenv("OPENBLACK_SHOT_SCALE", "2", 0); // 640x360: less to encode and write
	setenv("OPENBLACK_SAVEPOINT_DIR", "sdmc:/switch/openblack/savepoints", 0); // D-pad test save points
	// Every 5 s: the camera and each script's position (what scripts are waiting on), into the log.
	setenv("OPENBLACK_TRACE_VM", "1", 0);

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
	atlasLinkExit();
	if (g_logFile != nullptr)
	{
		fclose(g_logFile);
	}
	return result;
}
