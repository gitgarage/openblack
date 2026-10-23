/*******************************************************************************
 * Copyright (c) 2018-2026 openblack developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/openblack/openblack
 *
 * openblack is licensed under the GNU General Public License version 3.
 *******************************************************************************/

#include "Subtitles.h"

#include <array>
#include <chrono>
#include <cstring>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

#include <bgfx/bgfx.h>
#include <imgui.h>
#include <spdlog/spdlog.h>

#include "Debug/ImGuiUtils.h"
#include "FileSystem/FileSystemInterface.h"
#include "Locator.h"

namespace openblack::subtitles
{
namespace
{
enum class Speaker : uint8_t
{
	Villager,
	Good,
	Evil,
};

struct Text
{
	Speaker speaker = Speaker::Villager;
	std::u16string words;
};

struct Glyph
{
	ImVec2 uv0;
	ImVec2 uv1;
	float width = 0.0f;   // its bitmap, in the halved font's pixels
	float left = 0.0f;    // drawn this far on from the pen
	float advance = 0.0f; // and the pen moved on this far
};

struct Font
{
	bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
	float lineHeight = 0.0f;
	std::unordered_map<char16_t, Glyph> glyphs;
};

struct Line
{
	const Text* text;
	std::chrono::steady_clock::time_point shown;
};

// How long a line stays (the original's opening: one 6.2 seconds old still shown, none 7.5 seconds old) and how many.
constexpr auto k_LineLife = std::chrono::milliseconds(7000);
constexpr size_t k_MostLines = 4;

std::vector<Text> g_texts; // by number (0 is "Invalid text string.")
bool g_textsLoaded = false;
std::array<Font, 3> g_fonts; // by Speaker
bool g_fontsLoaded = false;
std::deque<Line> g_lines; // newest first
std::optional<glm::vec2> g_demoHand;
std::chrono::steady_clock::time_point g_demoHandSet {};
bgfx::TextureHandle g_mouseSheet = BGFX_INVALID_HANDLE; // 4 x 4 mice, the first with no button pressed
bool g_mouseSheetLoaded = false;

void LoadMouseSheet()
{
	g_mouseSheetLoaded = true;
	try
	{
		auto& fs = Locator::filesystem::value();
		const auto textures = fs.GetPath<filesystem::Path::Data>() / "Textures";
		const auto rgb = fs.ReadAll(textures / "mousehelp.raw");
		const auto alpha = fs.ReadAll(textures / "mousehelpa.raw");
		constexpr size_t k_Texels = 256 * 256;
		if (rgb.size() < k_Texels * 3 || alpha.size() < k_Texels)
		{
			return;
		}
		std::vector<uint8_t> pixels(k_Texels * 4);
		for (size_t t = 0; t < k_Texels; ++t)
		{
			pixels[t * 4 + 0] = rgb[t * 3 + 0];
			pixels[t * 4 + 1] = rgb[t * 3 + 1];
			pixels[t * 4 + 2] = rgb[t * 3 + 2];
			pixels[t * 4 + 3] = alpha[t];
		}
		g_mouseSheet = bgfx::createTexture2D(256, 256, false, 1, bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
		                                     bgfx::copy(pixels.data(), static_cast<uint32_t>(pixels.size())));
	}
	catch (const std::exception& e)
	{
		SPDLOG_LOGGER_ERROR(spdlog::get("game"), "Subtitles: mouse help: {}", e.what());
	}
}

// Scripts/InfoScript2.txt: UTF-16 lines `ADD_TEXT( 1, HELP_TEXT_NARRATOR_<WHO>, "<NAME>", "<text>")`.
void LoadTexts()
{
	g_textsLoaded = true;
	try
	{
		auto& fs = Locator::filesystem::value();
		const auto bytes = fs.ReadAll(fs.GetPath<filesystem::Path::Scripts>() / "InfoScript2.txt");
		std::u16string all(bytes.size() / 2, u'\0');
		std::memcpy(all.data(), bytes.data(), all.size() * 2);
		size_t at = 0;
		while ((at = all.find(u"ADD_TEXT(", at)) != std::u16string::npos)
		{
			const size_t end = all.find(u'\n', at);
			const auto line = all.substr(at, end == std::u16string::npos ? std::u16string::npos : end - at);
			at = end == std::u16string::npos ? all.size() : end;
			Text text;
			if (line.find(u"NARRATOR_EVIL_SPIRIT") != std::u16string::npos)
			{
				text.speaker = Speaker::Evil;
			}
			else if (line.find(u"NARRATOR_GOOD_SPIRIT") != std::u16string::npos)
			{
				text.speaker = Speaker::Good;
			}
			// The words: the second quoted string.
			size_t quote = line.find(u'"');
			for (int skip = 0; skip < 2 && quote != std::u16string::npos; ++skip)
			{
				quote = line.find(u'"', quote + 1);
			}
			const size_t close = quote == std::u16string::npos ? quote : line.rfind(u'"');
			if (quote != std::u16string::npos && close != std::u16string::npos && close > quote)
			{
				text.words = line.substr(quote + 1, close - quote - 1);
			}
			g_texts.push_back(std::move(text));
		}
		SPDLOG_LOGGER_INFO(spdlog::get("game"), "Subtitles: {} lines of text", g_texts.size());
	}
	catch (const std::exception& e)
	{
		SPDLOG_LOGGER_ERROR(spdlog::get("game"), "Subtitles: {}", e.what());
	}
}

// Data/<name>.met (its line height at twice the size, its name, then per glyph its character, bitmap width, the space
// before, over and after it, and where its bitmap is in the .fnt) and Data/<name>.fnt (each bitmap a row after row of
// runs, a byte each - 255 then two bytes for a long one - alternately blank and ink, blank first). Halved into one
// texture, each 2x2 of its pixels one (FontCache::AddToCacheWork).
Font LoadFont(const std::string& name)
{
	Font font;
	try
	{
		auto& fs = Locator::filesystem::value();
		const auto met = fs.ReadAll(fs.GetPath<filesystem::Path::Data>() / (name + ".met"));
		const auto fnt = fs.ReadAll(fs.GetPath<filesystem::Path::Data>() / (name + ".fnt"));
		const auto read32 = [&met](size_t at) {
			int32_t value = 0;
			std::memcpy(&value, met.data() + at, sizeof(value));
			return value;
		};
		const int height = read32(0);
		const int count = read32(0x104);
		if (height <= 0 || met.size() < 0x108 + static_cast<size_t>(count) * 28)
		{
			throw std::runtime_error("bad metrics");
		}
		const int halfHeight = height / 2;
		constexpr int k_AtlasWidth = 1024;
		// Packed in rows, each a halved line high.
		int x = 0;
		int rows = 1;
		for (int i = 0; i < count; ++i)
		{
			uint16_t width = 0;
			std::memcpy(&width, met.data() + 0x108 + i * 28 + 2, 2);
			const int halfWidth = (width + 1) / 2 + 1;
			if (x + halfWidth > k_AtlasWidth)
			{
				x = 0;
				++rows;
			}
			x += halfWidth;
		}
		int atlasHeight = 1;
		while (atlasHeight < rows * (halfHeight + 1))
		{
			atlasHeight *= 2;
		}
		std::vector<uint8_t> atlas(static_cast<size_t>(k_AtlasWidth * atlasHeight * 4), 0);
		for (size_t p = 0; p < atlas.size(); p += 4)
		{
			atlas[p] = atlas[p + 1] = atlas[p + 2] = 255;
		}
		x = 0;
		int y = 0;
		std::vector<uint8_t> bits;
		for (int i = 0; i < count; ++i)
		{
			const size_t record = 0x108 + static_cast<size_t>(i) * 28;
			uint16_t code = 0;
			uint16_t width = 0;
			std::array<float, 3> spacing {};
			uint32_t offset = 0;
			uint32_t size = 0;
			std::memcpy(&code, met.data() + record, 2);
			std::memcpy(&width, met.data() + record + 2, 2);
			std::memcpy(spacing.data(), met.data() + record + 8, 12);
			std::memcpy(&offset, met.data() + record + 0x14, 4);
			std::memcpy(&size, met.data() + record + 0x18, 4);
			const int halfWidth = (width + 1) / 2 + 1;
			if (x + halfWidth > k_AtlasWidth)
			{
				x = 0;
				y += halfHeight + 1;
			}
			// Its bitmap, at twice the size.
			bits.assign(static_cast<size_t>(width) * height, 0);
			size_t out = 0;
			uint8_t ink = 0;
			for (size_t at = offset; at < offset + size && at < fnt.size() && out < bits.size();)
			{
				size_t run = fnt[at++];
				if (run == 0xff && at + 2 <= fnt.size())
				{
					run = static_cast<size_t>(fnt[at] | (fnt[at + 1] << 8));
					at += 2;
				}
				for (size_t r = 0; r < run && out < bits.size(); ++r)
				{
					bits[out++] = ink;
				}
				ink ^= 1;
			}
			for (int gy = 0; gy < halfHeight; ++gy)
			{
				for (int gx = 0; gx < (width + 1) / 2; ++gx)
				{
					int sum = 0;
					for (int dy = 0; dy < 2; ++dy)
					{
						for (int dx = 0; dx < 2; ++dx)
						{
							const int sx = gx * 2 + dx;
							const int sy = gy * 2 + dy;
							if (sx < width && sy < height)
							{
								sum += bits[static_cast<size_t>(sy) * width + sx];
							}
						}
					}
					atlas[(static_cast<size_t>(y + gy) * k_AtlasWidth + x + gx) * 4 + 3] = static_cast<uint8_t>(sum * 255 / 4);
				}
			}
			Glyph glyph;
			glyph.uv0 = ImVec2(static_cast<float>(x) / k_AtlasWidth, static_cast<float>(y) / static_cast<float>(atlasHeight));
			glyph.uv1 = ImVec2(static_cast<float>(x + (width + 1) / 2) / k_AtlasWidth,
			                   static_cast<float>(y + halfHeight) / static_cast<float>(atlasHeight));
			glyph.width = static_cast<float>((width + 1) / 2);
			glyph.left = spacing[0] * 0.5f;
			glyph.advance = (spacing[0] + spacing[1] + spacing[2]) * 0.5f;
			font.glyphs[static_cast<char16_t>(code)] = glyph;
			x += halfWidth;
		}
		font.lineHeight = static_cast<float>(halfHeight);
		font.texture = bgfx::createTexture2D(static_cast<uint16_t>(k_AtlasWidth), static_cast<uint16_t>(atlasHeight), false, 1,
		                                     bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
		                                     bgfx::copy(atlas.data(), static_cast<uint32_t>(atlas.size())));
		SPDLOG_LOGGER_INFO(spdlog::get("game"), "Subtitles: font {} ({} glyphs)", name, count);
	}
	catch (const std::exception& e)
	{
		SPDLOG_LOGGER_ERROR(spdlog::get("game"), "Subtitles: font {}: {}", name, e.what());
	}
	return font;
}

ImTextureID TextureId(bgfx::TextureHandle handle)
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
	texture.s.handle = handle;
	texture.s.flags = IMGUI_FLAGS_ALPHA_BLEND;
	return texture.ptr;
}

float Width(const Font& font, const std::u16string& words)
{
	float width = 0.0f;
	for (const auto c : words)
	{
		if (const auto glyph = font.glyphs.find(c); glyph != font.glyphs.end())
		{
			width += glyph->second.advance;
		}
	}
	return width;
}

void DrawLine(ImDrawList& draw, const Font& font, const std::u16string& words, float centreX, float centreY, float scale,
              ImU32 colour)
{
	if (!bgfx::isValid(font.texture))
	{
		return;
	}
	const auto texture = TextureId(font.texture);
	float x = centreX - Width(font, words) * scale * 0.5f;
	const float top = centreY - font.lineHeight * scale * 0.5f;
	for (const auto c : words)
	{
		const auto glyph = font.glyphs.find(c);
		if (glyph == font.glyphs.end())
		{
			continue;
		}
		const auto& g = glyph->second;
		const float left = x + g.left * scale;
		draw.AddImage(texture, ImVec2(left, top), ImVec2(left + g.width * scale, top + font.lineHeight * scale), g.uv0, g.uv1,
		              colour);
		x += g.advance * scale;
	}
}
} // namespace

void Show(int32_t textId)
{
	if (!g_textsLoaded)
	{
		LoadTexts();
	}
	if (textId < 1 || static_cast<size_t>(textId) >= g_texts.size() || g_texts[static_cast<size_t>(textId)].words.empty())
	{
		return;
	}
	g_lines.push_front({&g_texts[static_cast<size_t>(textId)], std::chrono::steady_clock::now()});
	while (g_lines.size() > k_MostLines)
	{
		g_lines.pop_back();
	}
}

int SpeakerOf(int32_t textId)
{
	if (!g_textsLoaded)
	{
		LoadTexts();
	}
	if (textId < 0 || static_cast<size_t>(textId) >= g_texts.size())
	{
		return 0;
	}
	switch (g_texts[static_cast<size_t>(textId)].speaker)
	{
	case Speaker::Good:
		return 1;
	case Speaker::Evil:
		return 2;
	default:
		return 0;
	}
}

void Clear()
{
	g_lines.clear();
}

void SetDemoHand(const std::optional<glm::vec2>& screen)
{
	g_demoHand = screen;
	g_demoHandSet = std::chrono::steady_clock::now();
}

namespace
{
void LoadFonts()
{
	if (!g_fontsLoaded)
	{
		g_fontsLoaded = true;
		g_fonts[static_cast<size_t>(Speaker::Villager)] = LoadFont("j0");
		g_fonts[static_cast<size_t>(Speaker::Good)] = LoadFont("f1");
		g_fonts[static_cast<size_t>(Speaker::Evil)] = LoadFont("f3");
	}
}

// Measured on the original at 1920 x 1080: the mouse 60 to the right of the hand, 28 across on a soft white glow,
// "Demo" from 80 to the right, red.
void DrawDemoHand(ImDrawList& draw, const ImVec2& display, float unit)
{
	// (Not told this frame or the last few: no longer there.)
	if (!g_demoHand || std::chrono::steady_clock::now() - g_demoHandSet > std::chrono::milliseconds(200))
	{
		return;
	}
	if (!g_mouseSheetLoaded)
	{
		LoadMouseSheet();
	}
	LoadFonts();
	const ImVec2 hand(g_demoHand->x * display.x, g_demoHand->y * display.y);
	const ImVec2 mouse(hand.x + 60.0f * unit, hand.y);
	draw.AddCircleFilled(mouse, 19.0f * unit, IM_COL32(255, 255, 240, 70), 24);
	draw.AddCircleFilled(mouse, 15.0f * unit, IM_COL32(255, 255, 240, 70), 24);
	if (bgfx::isValid(g_mouseSheet))
	{
		const float half = 14.0f * unit;
		draw.AddImage(TextureId(g_mouseSheet), ImVec2(mouse.x - half, mouse.y - half), ImVec2(mouse.x + half, mouse.y + half),
		              ImVec2(0.0f, 0.0f), ImVec2(0.25f, 0.25f));
	}
	const auto& font = g_fonts.at(static_cast<size_t>(Speaker::Evil));
	const std::u16string demo = u"Demo";
	const float scale = 0.5f * unit;
	DrawLine(draw, font, demo, hand.x + 80.0f * unit + Width(font, demo) * scale * 0.5f, hand.y, scale, IM_COL32(215, 25, 15, 255));
}
} // namespace

void Draw()
{
	const auto now = std::chrono::steady_clock::now();
	while (!g_lines.empty() && now - g_lines.back().shown > k_LineLife)
	{
		g_lines.pop_back();
	}
	const auto& display = ImGui::GetIO().DisplaySize;
	const float unit = display.y / 1080.0f;
	auto* draw = ImGui::GetForegroundDrawList();
	DrawDemoHand(*draw, display, unit);
	if (g_lines.empty())
	{
		return;
	}
	LoadFonts();
	// Measured on the original at 1920 x 1080: the band from 916 to 1052 down, what is behind it darkened to 0.47; the
	// newest line's middle 946 down, each before it 30 below; the newest a little larger.
	draw->AddRectFilled(ImVec2(0.0f, 916.0f * unit), ImVec2(display.x, 1052.0f * unit), IM_COL32(0, 0, 0, 135));
	constexpr std::array<ImU32, 3> k_Colours {
	    IM_COL32(245, 245, 245, 255), // villagers
	    IM_COL32(225, 220, 165, 255), // the good spirit
	    IM_COL32(246, 170, 170, 255), // the evil spirit
	};
	float y = 946.0f * unit;
	for (size_t i = 0; i < g_lines.size(); ++i)
	{
		const auto& text = *g_lines[i].text;
		const auto speaker = static_cast<size_t>(text.speaker);
		DrawLine(*draw, g_fonts.at(speaker), text.words, display.x * 0.5f, y, (i == 0 ? 1.1f : 0.88f) * unit, k_Colours.at(speaker));
		y += 30.0f * unit;
	}
}
} // namespace openblack::subtitles
