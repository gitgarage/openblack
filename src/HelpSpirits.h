/*******************************************************************************
 * Copyright (c) 2018-2026 openblack developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/openblack/openblack
 *
 * openblack is licensed under the GNU General Public License version 3.
 *******************************************************************************/

#pragma once

#include <glm/vec3.hpp>

// The advisors: the good spirit (the bearded Yogi, Data/HelpSprite/markgood.hd) and the evil one (the Demon,
// markevil.hd), as the original's HelpDude draws them: a model held in front of the camera at the file's Depth,
// placed by a "hover" position on the screen, flapping (Hover and Wings animations), turning toward the middle of the
// screen as it goes aside, and fading in and out. Each .hd keeps its own mesh and its animations, already made from
// standing (bw1-decomp helpdude.cpp, HelpDude::Load).
namespace openblack::helpspirits
{
enum class Spirit
{
	Good,
	Evil,
};

bool Load();

// SPIRIT_EJECT: in from its edge of the screen, flying (over a second) to a place in the middle half of it.
void Eject(Spirit spirit);
// SPIRIT_HOME: back off its edge, fading away.
void Home(Spirit spirit);
// SPIRIT_APPEAR / SPIRIT_DISAPPEAR: fade in or out where it is.
void Appear(Spirit spirit);
void Disappear(Spirit spirit);
// FLY_SPIRIT: to `x`, `y` across and down the screen (0-1), over a second.
void FlyTo(Spirit spirit, float x, float y);
// SPIRIT_POINT_POS / SPIRIT_POINT_GAME_THING: point at a place in the world, until StopPointing.
void PointAt(Spirit spirit, const glm::vec3& position);
void StopPointing(Spirit spirit);
// PLAY_SPIRIT_ANIM: one of the HelpDudeAnim.h animations once, at `speed`.
void PlayAnim(Spirit spirit, int anim, float speed);
// While its line is being said: it comes nearer, to its TalkDepth (HelpDude::Update1's ZoomBlend).
void SetTalking(Spirit spirit, bool talking);
[[nodiscard]] bool Playing(Spirit spirit);
[[nodiscard]] bool Shown(Spirit spirit);

// Each frame, once the camera has moved: where both are and how they are posed.
void Update(float seconds);
} // namespace openblack::helpspirits
