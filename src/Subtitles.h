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

#include <glm/vec2.hpp>

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
// Each frame, over everything but a movie.
void Draw();
} // namespace openblack::subtitles
