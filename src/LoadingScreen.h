/*******************************************************************************
 * Copyright (c) 2018-2026 openblack developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/openblack/openblack
 *
 * openblack is licensed under the GNU General Public License version 3.
 *******************************************************************************/

#pragma once

// The original's loading screen (runblack.exe's LoadingScreen.cpp, reconstructed in bw1-decomp): a tip of the day -
// its picture from Data/tips.bik, blurred behind everything and sharp in a framed panel, its words from
// Scripts/InfoScript2.txt on a grey band - over a bar whose light and dark sweeps run across it for as long as it loads.
namespace openblack::loading
{
// Draws it, if a moment has passed since it last was (at most every 150 ms, as the original's RenderLoadingFrame).
void Draw();
// Frees its pictures once the game is loaded.
void Finish();
} // namespace openblack::loading
