/******************************************************************************
 * Copyright (c) 2018-2026 openblack developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/openblack/openblack
 *
 * openblack is licensed under the GNU General Public License version 3.
 *******************************************************************************/

#pragma once

#include <string_view>
#include <vector>

#include <glm/mat4x4.hpp>

namespace openblack::graphics
{
class L3DMesh;
}

// The player's hand poses, from the "Hand" segment of Data/CTR/hh.HBN (names in Data/hndspec5.txt): wiggle,
// point, hold_above, hold_side, can_pickup, hold_fingers, the reward/punish/camera gestures... Each pose sets some
// bones' rotation (on top of the mesh's rest rotation) and translation over a few frames.
namespace openblack::handposes
{
bool Load();
// The hand mesh's bones (relative to their parents) in pose `name` at `frame` (fractional frames blend
// between the two either side); empty if there is no such pose.
std::vector<glm::mat4> LocalBones(const graphics::L3DMesh& mesh, std::string_view name, float frame);
// How many frames pose `name` has (0 if none).
int FrameCount(std::string_view name);
} // namespace openblack::handposes
