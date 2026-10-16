// Self-update over the USB-C link: the laptop (switch-port/update.py) sends a new openblack.nro down the same
// link the log comes up, through the Pixel Tablet's port 10810; openblack writes it to the SD card next to the
// running one, swaps it in and starts it - no RCM, no SD mode, no reboot.
//
// What the laptop sends, all as text lines starting "ATLAS-UPDATE" (anything else on the link is ignored):
//   ATLAS-UPDATE BEGIN <nonce> <size> <crc32>                   -> "[update] <nonce> ready"
//   ATLAS-UPDATE CHUNK <nonce> <offset> <len> <crc32>, <len> bytes -> "[update] <nonce> ack <received>" or
//                                                                    "[update] <nonce> nak <received>"
//   ATLAS-UPDATE END <nonce>                                     -> "[update] <nonce> installed" then a restart,
//                                                                    or "[update] <nonce> failed <why>"
// The replies are log lines, so they reach the laptop the way the log does; the nonce tells this transfer's
// replies from old ones. Each chunk is checked on its own, so a bad one is sent again rather than written.
// A transfer cut off mid-chunk leaves this reader waiting for the rest of that chunk: the laptop starts every
// transfer with more blank lines than a chunk holds, which fill it (it then fails its check and is dropped)
// and are ignored after.
#include "atlas_usb_link.h"

#include <switch.h>

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{
constexpr const char* k_NroPath = "sdmc:/switch/openblack/openblack.nro";
constexpr const char* k_PartPath = "sdmc:/switch/openblack/openblack.nro.part";
constexpr const char* k_OldPath = "sdmc:/switch/openblack/openblack.nro.old"; // the build before, kept to go back to
constexpr size_t k_MaxChunk = 32 * 1024;

// CRC-32 as zlib's (and Python's zlib.crc32), which the laptop side uses.
unsigned long crc32(unsigned long crc, const u8* data, size_t len)
{
	static const auto table = [] {
		std::array<u32, 256> t {};
		for (u32 i = 0; i < 256; i++)
		{
			u32 c = i;
			for (int k = 0; k < 8; k++)
			{
				c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			}
			t[i] = c;
		}
		return t;
	}();
	u32 c = static_cast<u32>(crc) ^ 0xFFFFFFFFu;
	for (size_t i = 0; i < len; i++)
	{
		c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
	}
	return c ^ 0xFFFFFFFFu;
}

u8 g_in[0x4000];
size_t g_inPos = 0;
size_t g_inLen = 0;

bool fill()
{
	if (g_inPos < g_inLen)
	{
		return true;
	}
	g_inPos = 0;
	g_inLen = atlasLinkRead(g_in, sizeof(g_in));
	return g_inLen > 0;
}

// One line, without its newline; a line too long to be a command is read through and comes back empty.
bool readLine(std::string& line)
{
	line.clear();
	bool tooLong = false;
	while (fill())
	{
		const char c = static_cast<char>(g_in[g_inPos++]);
		if (c == '\n')
		{
			if (tooLong)
			{
				line.clear();
			}
			return true;
		}
		if (c != '\r' && !tooLong)
		{
			line.push_back(c);
			tooLong = line.size() > 200;
		}
	}
	return false;
}

bool readExact(u8* out, size_t size)
{
	while (size > 0)
	{
		if (!fill())
		{
			return false;
		}
		const size_t take = std::min(size, g_inLen - g_inPos);
		std::memcpy(out, g_in + g_inPos, take);
		g_inPos += take;
		out += take;
		size -= take;
	}
	return true;
}

void reply(const std::string& nonce, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void reply(const std::string& nonce, const char* fmt, ...)
{
	char text[200];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(text, sizeof(text), fmt, ap);
	va_end(ap);
	// Through stdout, so it is in the SD log too, between whatever the game was logging.
	printf("[update] %s %s\n", nonce.c_str(), text);
	fflush(stdout);
}

void restart()
{
	if (!envHasNextLoad())
	{
		printf("[update] the loader can't start the new build itself: quit openblack and open it again\n");
		return;
	}
	envSetNextLoad(k_NroPath, k_NroPath);
	printf("[update] restarting into the new build\n");
	fflush(stdout);
	SDL_Event quit {};
	quit.type = SDL_QUIT;
	SDL_PushEvent(&quit); // the game ends as if closed, and the loader starts the new file
}

struct Transfer
{
	std::string nonce;
	unsigned long size = 0;
	unsigned long crc = 0;
	unsigned long received = 0;
	unsigned long runningCrc = 0;
	FILE* file = nullptr;
};

void listen(void*)
{
	while (!atlasLinkIsUp())
	{
		svcSleepThread(500'000'000ULL);
	}
	Transfer t;
	std::vector<u8> chunk(k_MaxChunk);
	std::string line;
	while (true)
	{
		if (!readLine(line))
		{
			// The link went down; wait for it to come back.
			svcSleepThread(500'000'000ULL);
			continue;
		}
		if (line.rfind("ATLAS-UPDATE ", 0) != 0)
		{
			continue;
		}
		char verb[16] = {};
		char nonce[40] = {};
		unsigned long a = 0;
		unsigned long b = 0;
		unsigned long c = 0;
		const int fields = std::sscanf(line.c_str() + 13, "%15s %39s %lu %lu %lx", verb, nonce, &a, &b, &c);
		const std::string v = verb;
		if (v == "BEGIN" && std::sscanf(line.c_str() + 13, "%15s %39s %lu %lx", verb, nonce, &a, &b) == 4)
		{
			if (t.file != nullptr)
			{
				std::fclose(t.file);
			}
			t = {};
			t.nonce = nonce;
			t.size = a;
			t.crc = b;
			t.runningCrc = 0UL;
			t.file = std::fopen(k_PartPath, "wb");
			if (t.file == nullptr)
			{
				reply(t.nonce, "failed can't write %s", k_PartPath);
				t = {};
				continue;
			}
			reply(t.nonce, "ready");
		}
		else if (v == "CHUNK" && fields == 5)
		{
			const unsigned long offset = a;
			const unsigned long len = b;
			if (len > k_MaxChunk)
			{
				continue; // not a chunk of ours: its bytes aren't read, so they come back as lines and are ignored
			}
			if (!readExact(chunk.data(), len))
			{
				continue;
			}
			if (t.file == nullptr || t.nonce != nonce)
			{
				continue;
			}
			const unsigned long crc = crc32(0UL, chunk.data(), len);
			if (offset != t.received || crc != c || t.received + len > t.size ||
			    std::fwrite(chunk.data(), 1, len, t.file) != len)
			{
				reply(t.nonce, "nak %lu", t.received);
				continue;
			}
			t.runningCrc = crc32(t.runningCrc, chunk.data(), len);
			t.received += len;
			reply(t.nonce, "ack %lu", t.received);
		}
		else if (v == "END" && t.file != nullptr && t.nonce == nonce)
		{
			std::fclose(t.file);
			t.file = nullptr;
			if (t.received != t.size || t.runningCrc != t.crc)
			{
				reply(t.nonce, "failed got %lu of %lu bytes, crc %08lx not %08lx", t.received, t.size, t.runningCrc, t.crc);
				std::remove(k_PartPath);
				continue;
			}
			std::remove(k_OldPath);
			std::rename(k_NroPath, k_OldPath);
			if (std::rename(k_PartPath, k_NroPath) != 0)
			{
				std::rename(k_OldPath, k_NroPath); // put the running build back where it was
				reply(t.nonce, "failed can't move the new build into place");
				continue;
			}
			reply(t.nonce, "installed %lu bytes", t.size);
			restart();
		}
	}
}

Thread g_thread;
} // namespace

void startUpdateListener()
{
	if (R_SUCCEEDED(threadCreate(&g_thread, listen, nullptr, nullptr, 0x8000, 0x2C, -2)))
	{
		threadStart(&g_thread);
	}
}
