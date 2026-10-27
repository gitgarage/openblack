/*******************************************************************************
 * Copyright (c) 2018-2026 openblack developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/openblack/openblack
 *
 * openblack is licensed under the GNU General Public License version 3.
 *******************************************************************************/

#include "LoadingScreen.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <vector>

#include <bgfx/bgfx.h>
#include <imgui.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include "Debug/DebugGuiInterface.h"
#include "Debug/ImGuiUtils.h"
#include "FileSystem/FileSystemInterface.h"
#include "Locator.h"
#include "Subtitles.h"
#include "Video/MoviePlayer.h"

namespace openblack::loading
{
namespace
{
// The tip: n of the 34 (help text 5151 + n, tips.bik's picture n). The original shows a player with no profile tip 0
// ("These are 'Tips of the Day'...") and anyone else one of 1-34 by the clock, never 29 or 32; the port has no profiles,
// so it shows the others'.
constexpr int k_Tips = 34;
constexpr int k_TipTextBase = 5151;
// One light sweep then one dark across the bar in this long (the original's LoadingBarDuration), its time going on by
// at most 350 ms a drawing, so a long wait doesn't leap it on.
constexpr float k_BarMs = 15000.0f;
constexpr auto k_DrawEvery = std::chrono::milliseconds(150);
constexpr float k_MostStepMs = 350.0f;

struct Picture
{
	bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
	int width = 0;
	int height = 0;
};

struct State
{
	bool started = false;
	int tip = 0;
	std::u16string words;
	Picture sharp;   // the tip's picture
	Picture blurred; // the same, a twelfth the size, drawn over the whole screen (the original's BlurMaterial)
	Picture frame;   // Data/Textures/Front_end_buttons.raw (the original's ButtonMaterial), for the panel's shadow
	std::chrono::steady_clock::time_point last;
	float ms = 0.0f;
};
State g_state;

ImTextureID Id(const Picture& picture)
{
	union
	{
		struct
		{
			bgfx::TextureHandle handle;
			uint8_t flags;
			uint8_t mip;
		} s;
		ImTextureID ptr;
	} texture {};
	texture.s.handle = picture.texture;
	texture.s.flags = IMGUI_FLAGS_ALPHA_BLEND;
	return texture.ptr;
}

Picture MakePicture(int width, int height, const std::vector<uint8_t>& rgba)
{
	Picture picture;
	picture.width = width;
	picture.height = height;
	picture.texture = bgfx::createTexture2D(static_cast<uint16_t>(width), static_cast<uint16_t>(height), false, 1,
	                                        bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
	                                        bgfx::copy(rgba.data(), static_cast<uint32_t>(rgba.size())));
	return picture;
}

void Start()
{
	auto& state = g_state;
	state.started = true;
	const auto now = std::chrono::steady_clock::now();
	const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
	state.tip = static_cast<int>((ms >> 4) % k_Tips) + 1;
	while (state.tip == 29 || state.tip == 32)
	{
		state.tip = state.tip % k_Tips + 1;
	}
	state.words = subtitles::TextOf(k_TipTextBase + state.tip);
	state.last = now;

	auto& fs = Locator::filesystem::value();
	int width = 0;
	int height = 0;
	std::vector<uint8_t> rgba;
	bool have = video::DecodeMovieFrame(fs.GetPath<filesystem::Path::Data>(true) / "tips.bik", state.tip, width, height, rgba);
	// Test hook: a builds without a movie decoder can be given the picture as raw RGBA, 768 x 512.
	if (const char* test = std::getenv("OPENBLACK_TEST_TIP_RGBA"); !have && test != nullptr)
	{
		std::ifstream file(test, std::ios::binary);
		rgba.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
		width = 768;
		height = 512;
		have = rgba.size() == static_cast<size_t>(width * height * 4);
	}
	if (have)
	{
		state.sharp = MakePicture(width, height, rgba);
		constexpr int k_Down = 12;
		const int smallWidth = std::max(width / k_Down, 1);
		const int smallHeight = std::max(height / k_Down, 1);
		std::vector<uint8_t> small(static_cast<size_t>(smallWidth * smallHeight * 4), 255);
		for (int y = 0; y < smallHeight; ++y)
		{
			for (int x = 0; x < smallWidth; ++x)
			{
				for (int c = 0; c < 3; ++c)
				{
					int sum = 0;
					for (int dy = 0; dy < k_Down; ++dy)
					{
						for (int dx = 0; dx < k_Down; ++dx)
						{
							sum += rgba[static_cast<size_t>(((y * k_Down + dy) * width + x * k_Down + dx) * 4 + c)];
						}
					}
					small[static_cast<size_t>((y * smallWidth + x) * 4 + c)] = static_cast<uint8_t>(sum / (k_Down * k_Down));
				}
			}
		}
		state.blurred = MakePicture(smallWidth, smallHeight, small);
	}
	else
	{
		SPDLOG_LOGGER_WARN(spdlog::get("game"), "Loading screen: no picture for tip {} (no movie decoder?)", state.tip);
	}
	try
	{
		const auto textures = fs.GetPath<filesystem::Path::Data>() / "Textures";
		const auto colour = fs.ReadAll(textures / "Front_end_buttons.raw");
		const auto alpha = fs.ReadAll(textures / "Front_end_buttonsa.raw");
		constexpr size_t k_Texels = 256 * 256;
		if (colour.size() >= k_Texels * 3 && alpha.size() >= k_Texels)
		{
			std::vector<uint8_t> pixels(k_Texels * 4);
			for (size_t t = 0; t < k_Texels; ++t)
			{
				pixels[t * 4 + 0] = colour[t * 3 + 0];
				pixels[t * 4 + 1] = colour[t * 3 + 1];
				pixels[t * 4 + 2] = colour[t * 3 + 2];
				pixels[t * 4 + 3] = alpha[t];
			}
			state.frame = MakePicture(256, 256, pixels);
		}
	}
	catch (const std::exception& e)
	{
		SPDLOG_LOGGER_WARN(spdlog::get("game"), "Loading screen: {}", e.what());
	}
	SPDLOG_LOGGER_INFO(spdlog::get("game"), "Loading screen: tip {}", state.tip);
}

ImU32 Colour(uint32_t argb, float alpha = 1.0f)
{
	const auto a = static_cast<uint32_t>(static_cast<float>(argb >> 24) * alpha);
	return IM_COL32((argb >> 16) & 0xff, (argb >> 8) & 0xff, argb & 0xff, a);
}

// DrawLoading (fade 1): everything in the original's places and colours. It was laid out on a 4:3 screen: here its
// sizes go by the screen's height, the picture keeps its own 3:2 and the panel sits in the middle.
void DrawFrame(float progress)
{
	const auto& state = g_state;
	auto& draw = *ImGui::GetBackgroundDrawList();
	const auto& io = ImGui::GetIO();
	const float w = io.DisplaySize.x;
	const float h = io.DisplaySize.y;
	const float u = h / 480.0f; // the original's 640 x 480 units
	draw.AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(w, h), IM_COL32(0, 0, 0, 255));

	const float videoHeight = h * 0.64f;
	const float videoWidth = videoHeight * 1.5f;
	const float videoX = (w - videoWidth) * 0.5f;
	const float videoY = h * 0.05f;
	if (bgfx::isValid(state.blurred.texture))
	{
		draw.AddImage(Id(state.blurred), ImVec2(0.0f, 0.0f), ImVec2(w, h), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
		              IM_COL32(200, 200, 200, 255));
	}
	// The lower half shaded down to black.
	draw.AddRectFilledMultiColor(ImVec2(0.0f, h * 0.5f), ImVec2(w, h), 0, 0, IM_COL32(0, 0, 0, 255), IM_COL32(0, 0, 0, 255));
	if (bgfx::isValid(state.sharp.texture))
	{
		draw.AddImage(Id(state.sharp), ImVec2(videoX, videoY), ImVec2(videoX + videoWidth, videoY + videoHeight));
	}

	// The tip on its grey band (0xA0808080), faded in over its first and out over its last fifth.
	const ImU32 band = Colour(0xA0808080);
	const float bandTop = h * 0.8f;
	const float bandBottom = h * 0.95f;
	draw.AddRectFilledMultiColor(ImVec2(0.0f, bandTop), ImVec2(w * 0.2f, bandBottom), 0, band, band, 0);
	draw.AddRectFilled(ImVec2(w * 0.2f, bandTop), ImVec2(w * 0.8f, bandBottom), band);
	draw.AddRectFilledMultiColor(ImVec2(w * 0.8f, bandTop), ImVec2(w, bandBottom), band, 0, 0, band);
	subtitles::DrawWrapped(draw, state.words, w * 0.125f, bandTop, w * 0.875f, bandBottom, 30.0f * u, IM_COL32(255, 255, 255, 255),
	                       IM_COL32(0, 0, 0, 128));

	float left = videoX - 1.0f;
	float top = videoY - 1.0f;
	float right = videoX + videoWidth + 1.0f;
	float bottom = videoY + videoHeight + 1.0f;
	const float border = 12.8f * u; // a fiftieth of the screen's width

	// The bar: a bevelled box, its light sweep (0xCDFFFFFF) then its dark one running left to right, each ending in a
	// 120 wide fade, a shadow along its inside top and left.
	const float barTop = bottom + border * 1.5f;
	const float barBottom = bottom + border * 2.5f;
	draw.AddRectFilled(ImVec2(left, barTop), ImVec2(right, barBottom), IM_COL32(48, 48, 48, 255));
	draw.AddLine(ImVec2(left, barTop), ImVec2(right, barTop), IM_COL32(16, 16, 16, 255));
	draw.AddLine(ImVec2(left, barTop), ImVec2(left, barBottom), IM_COL32(16, 16, 16, 255));
	draw.AddLine(ImVec2(left, barBottom), ImVec2(right, barBottom), IM_COL32(140, 140, 140, 255));
	draw.AddLine(ImVec2(right, barTop), ImVec2(right, barBottom), IM_COL32(140, 140, 140, 255));
	const float phase = progress - static_cast<float>(static_cast<int>(progress));
	auto sweep = [&](float along, ImU32 colour, float lift) {
		const float span = right - left + 120.0f * u;
		const float from = left + 3.0f;
		const float to = right - 3.0f;
		const float start = std::clamp(along * span + left - 60.0f * u, from, to);
		const float end = std::clamp(start + 120.0f * u, from, to);
		draw.AddRectFilled(ImVec2(from, barTop + 3.0f), ImVec2(start, barBottom - lift), colour);
		draw.AddRectFilledMultiColor(ImVec2(start, barTop + 3.0f), ImVec2(end, barBottom - lift), colour, 0, 0, colour);
	};
	sweep(phase * 2.0f, Colour(0xCDFFFFFF), 4.0f);
	sweep((phase - 0.5f) * 2.0f, IM_COL32(0, 0, 0, 255), 3.0f);
	draw.AddRectFilledMultiColor(ImVec2(left + 3.0f, barTop + 3.0f), ImVec2(right - 4.0f, barTop + 3.0f + border * 0.3f),
	                             IM_COL32(0, 0, 0, 255), IM_COL32(0, 0, 0, 255), 0, 0);
	draw.AddRectFilledMultiColor(ImVec2(left + 3.0f, barTop + 3.0f), ImVec2(left + 3.0f + border * 0.3f, barBottom - 4.0f),
	                             IM_COL32(0, 0, 0, 255), 0, 0, IM_COL32(0, 0, 0, 255));

	// The panel's soft shadow round the picture, a border wide, at half strength, from the buttons' texture (its
	// square at 48-80 across, 0-32 down: 6 texel corners and edges), then a white line round it.
	if (bgfx::isValid(state.frame.texture))
	{
		const auto half = IM_COL32(255, 255, 255, 128);
		auto piece = [&](float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1) {
			draw.AddImage(Id(state.frame), ImVec2(x0, y0), ImVec2(x1, y1), ImVec2(u0 / 256.0f, v0 / 256.0f), ImVec2(u1 / 256.0f, v1 / 256.0f),
			              half);
		};
		const float l = left - border;
		const float t = top - border;
		const float r = right + border;
		const float b = bottom + border;
		piece(l, t, l + border, t + border, 48, 0, 54, 6);
		piece(r - border, t, r, t + border, 74, 0, 80, 6);
		piece(l, b - border, l + border, b, 48, 26, 54, 32);
		piece(r - border, b - border, r, b, 74, 26, 80, 32);
		piece(l + border, t, r - border, t + border, 54, 0, 74, 6);
		piece(l + border, b - border, r - border, b, 54, 26, 74, 32);
		piece(l, t + border, l + border, b - border, 48, 6, 54, 26);
		piece(r - border, t + border, r, b - border, 74, 6, 80, 26);
	}
	draw.AddRect(ImVec2(left, top), ImVec2(right, bottom), IM_COL32(255, 255, 255, 255));
}
} // namespace

void Draw()
{
	if (!Locator::debugGui::has_value() || !Locator::filesystem::has_value())
	{
		return;
	}
	auto& state = g_state;
	if (!state.started)
	{
		Start();
	}
	const auto now = std::chrono::steady_clock::now();
	if (now - state.last < k_DrawEvery && state.ms > 0.0f)
	{
		return;
	}
	state.ms += std::min(std::chrono::duration<float, std::milli>(now - state.last).count(), k_MostStepMs);
	state.last = now;
	bgfx::touch(0);
	Locator::debugGui::value().DrawAlone([&] { DrawFrame(state.ms / k_BarMs); });
	// Test hook: OPENBLACK_SHOT_DIR (the game's screenshots) also takes the loading screen, about a second apart.
	if (static const char* s_shots = std::getenv("OPENBLACK_SHOT_DIR"); s_shots != nullptr)
	{
		static int s_shot = 0;
		static float s_shotMs = -1000.0f;
		if (state.ms - s_shotMs >= 1000.0f)
		{
			s_shotMs = state.ms;
			bgfx::requestScreenShot(BGFX_INVALID_HANDLE, fmt::format("{}/loading_{:02}.png", s_shots, s_shot++).c_str());
		}
	}
	bgfx::frame();
}

void Finish()
{
	auto& state = g_state;
	for (auto* picture : {&state.sharp, &state.blurred, &state.frame})
	{
		if (bgfx::isValid(picture->texture))
		{
			bgfx::destroy(picture->texture);
			picture->texture = BGFX_INVALID_HANDLE;
		}
	}
}
} // namespace openblack::loading
