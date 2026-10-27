/*******************************************************************************
 * Copyright (c) 2018-2026 openblack developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/openblack/openblack
 *
 * openblack is licensed under the GNU General Public License version 3.
 *******************************************************************************/

#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <glm/vec2.hpp>

struct ImDrawList;

// The original's spoken lines on screen: a dark band along the bottom with the newest line on top and the few before
// it beneath, smaller. Each line is its text from Scripts/InfoScript2.txt (ADD_TEXT, numbered in order from 0), in its
// speaker's font and colour - the evil spirit's Orange LET in pink, the good spirit's Footlight in cream, everyone
// else's Ocean Sans in white - the fonts' own glyphs from Data/<f3|f1|j0>.fnt (drawn at twice the size, 1 bit, run
// length coded; halved as the original's FontCache does).
namespace openblack::subtitles
{
// RUN_TEXT: line `textId` said.
void Show(int32_t textId);
// Who says line `textId`: 1 the good spirit, 2 the evil one, 0 anyone else.
int SpeakerOf(int32_t textId);
// GAME_CLEAR_DIALOGUE: the band emptied.
void Clear();
// Each frame while a tutorial's hand demo plays: where its hand is on the screen (0-1), or none. The original marks it
// with a mouse (Data/Textures/mousehelp.raw) and "Demo" in red.
void SetDemoHand(const std::optional<glm::vec2>& screen);
// Each frame, over everything but a movie. A line's "$m<n>" codes are drawn as the original's pictures of what does
// action n (a mouse with its button lit, or a key cap; on the Switch, the Switch's control on a key cap).
void Draw();
// The words of text `textId` with their "$" and "\\" markers blanked out, as the original's tip of the day takes them
// (LoadingScreen's StartTipOfTheDayText); empty if there is none.
std::u16string TextOf(int32_t textId);
// Words wrapped into the box (left, top) to (right, bottom) in everyone else's font (the original's game font), each
// line centred and the lines in the box's middle, at the largest size from `size` (pixels a line) down by 2 that fits,
// with a shadow 2 to the right and down (the loading screen's tip).
void DrawWrapped(ImDrawList& draw, const std::u16string& words, float left, float top, float right, float bottom, float size,
                 uint32_t colour, uint32_t shadow);
} // namespace openblack::subtitles
