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
#include <filesystem>
#include <vector>

// The original's Bink movies (Data/INTRO.bik...), played over the whole screen with their sound. Decoded by
// ffmpeg where the build has it (OPENBLACK_HAVE_FFMPEG: the Switch's portlibs); without it nothing plays and
// the script carries straight on.
namespace openblack::video
{
// Starts the movie; false if it can't be played (no file, no decoder).
bool StartMovie(const std::filesystem::path& path);
// While true, the game waits: no script turns, input only skips.
bool MoviePlaying();
// Decodes up to now and puts the frame due on the screen's texture; ends the movie at its end.
void UpdateMovie();
void StopMovie();
// Over everything, from the debug GUI's frame: black, with the movie fitted in the middle.
void DrawMovie();
// Picture `index` (from 0) of a movie, as RGBA: the loading screen's tip from Data/tips.bik, one picture a tip.
// False without a decoder or such a picture.
bool DecodeMovieFrame(const std::filesystem::path& path, int index, int& width, int& height, std::vector<uint8_t>& rgba);
} // namespace openblack::video
