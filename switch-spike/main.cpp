// bgfx-on-Switch feasibility spike: bring bgfx up on Mesa's EGL, clear the screen to a slowly
// changing colour for a few seconds, and write what happened to sdmc:/bgfx-spike.log so the result
// can be read back from the SD card. Press + to quit early.
#include <switch.h>

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>

#include <cstdarg>
#include <cstdio>

static FILE* g_log = nullptr;

static void logf(const char* fmt, ...)
{
	if (!g_log)
		return;
	va_list ap;
	va_start(ap, fmt);
	vfprintf(g_log, fmt, ap);
	va_end(ap);
	fflush(g_log);
}

// bgfx reports its own failures (context creation, missing extensions) through this - without it a
// failed init would only ever say "false".
struct SpikeCallback : bgfx::CallbackI
{
	void fatal(const char* file, uint16_t line, bgfx::Fatal::Enum code, const char* str) override
	{
		logf("bgfx FATAL %s:%u code=%d: %s\n", file, line, (int)code, str);
	}
	void traceVargs(const char* file, uint16_t line, const char* fmt, va_list args) override
	{
		if (!g_log)
			return;
		fprintf(g_log, "bgfx %s:%u ", file, line);
		vfprintf(g_log, fmt, args);
		fflush(g_log);
	}
	void profilerBegin(const char*, uint32_t, const char*, uint16_t) override {}
	void profilerBeginLiteral(const char*, uint32_t, const char*, uint16_t) override {}
	void profilerEnd() override {}
	uint32_t cacheReadSize(uint64_t) override { return 0; }
	bool cacheRead(uint64_t, void*, uint32_t) override { return false; }
	void cacheWrite(uint64_t, const void*, uint32_t) override {}
	void screenShot(const char*, uint32_t, uint32_t, uint32_t, const void*, uint32_t, bool) override {}
	void captureBegin(uint32_t, uint32_t, uint32_t, bgfx::TextureFormat::Enum, bool) override {}
	void captureEnd() override {}
	void captureFrame(const void*, uint32_t) override {}
};

int main(int, char**)
{
	g_log = fopen("sdmc:/bgfx-spike.log", "w");
	logf("bgfx spike starting\n");

	padConfigureInput(1, HidNpadStyleSet_NpadStandard);
	PadState pad;
	padInitializeDefault(&pad);

	NWindow* window = nwindowGetDefault();
	logf("nwindowGetDefault() = %p\n", (void*)window);

	// Single-threaded: calling renderFrame() before init() tells bgfx the calling thread is the
	// render thread, so no second thread is ever created.
	bgfx::renderFrame();

	SpikeCallback callback;
	bgfx::Init init{};
	init.type = bgfx::RendererType::OpenGLES;
	init.platformData.nwh = window;
	init.resolution.width = 1280;
	init.resolution.height = 720;
	init.resolution.reset = BGFX_RESET_VSYNC;
	init.callback = &callback;

	if (!bgfx::init(init))
	{
		logf("bgfx::init FAILED\n");
		if (g_log)
			fclose(g_log);
		return 1;
	}

	const bgfx::Caps* caps = bgfx::getCaps();
	logf("bgfx::init OK. renderer=%s vendorId=0x%04x deviceId=0x%04x\n",
		bgfx::getRendererName(bgfx::getRendererType()), caps->vendorId, caps->deviceId);
	// What openblack itself checks at startup (Renderer.cpp): 2D texture arrays with >= 9 layers.
	logf("texture2DArray=%s maxTextureLayers=%u maxTextureSize=%u maxFBAttachments=%u\n",
		(caps->supported & BGFX_CAPS_TEXTURE_2D_ARRAY) ? "yes" : "NO", caps->limits.maxTextureLayers,
		caps->limits.maxTextureSize, caps->limits.maxFBAttachments);
	logf("instancing=%s compute=%s index32=%s\n", (caps->supported & BGFX_CAPS_INSTANCING) ? "yes" : "NO",
		(caps->supported & BGFX_CAPS_COMPUTE) ? "yes" : "no",
		(caps->supported & BGFX_CAPS_INDEX32) ? "yes" : "NO");

	bgfx::setViewRect(0, 0, 0, 1280, 720);
	for (uint32_t frame = 0; frame < 60 * 6 && appletMainLoop(); frame++)
	{
		padUpdate(&pad);
		if (padGetButtonsDown(&pad) & HidNpadButton_Plus)
			break;

		uint8_t r = (uint8_t)(frame % 256), b = (uint8_t)(255 - frame % 256);
		bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, (r << 24) | (0x40 << 16) | (b << 8) | 0xFF, 1.0f, 0);
		bgfx::touch(0);
		bgfx::frame();
	}

	logf("rendered frames OK\n");
	bgfx::shutdown();
	logf("shutdown OK\n");
	fclose(g_log);
	return 0;
}
