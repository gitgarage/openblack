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
#include <cstring>
#include <sys/iosupport.h>
#include <vector>

extern "C" int __real_main(int argc, char** argv);

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
} // namespace

extern "C" int __wrap_main(int argc, char** argv)
{
	// The log file is opened before stdout is redirected, so its own writes go to the SD card, not
	// back through the redirect.
	g_logFile = fopen(k_LogPath, "w");
	// 12s: when the Switch is host, the tablet drops off the bus and comes back as an accessory, which
	// took longer than the 5s this first waited (the log said "link down", the tablet then opened it).
	bool link = atlasLinkInit(12000);
	devoptab_list[STD_OUT] = &g_teeDevoptab;
	devoptab_list[STD_ERR] = &g_teeDevoptab;
	setvbuf(stdout, nullptr, _IOLBF, 0);
	setvbuf(stderr, nullptr, _IONBF, 0);
	printf("openblack for Switch: usb link %s, game path %s\n", atlasLinkMode(), k_GamePath);
	(void)link;

	std::vector<char*> args(argv, argv + argc);
	const char* extra[] = {"--game-path", k_GamePath, "--log-file", "stdout", "--width", "1280", "--height", "720"};
	for (const char* a : extra)
	{
		args.push_back(const_cast<char*>(a));
	}
	args.push_back(nullptr);

	int result = __real_main(static_cast<int>(args.size()) - 1, args.data());
	printf("openblack exited with %d\n", result);
	atlasLinkExit();
	if (g_logFile != nullptr)
	{
		fclose(g_logFile);
	}
	return result;
}
