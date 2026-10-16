/******************************************************************************
 * Copyright (c) 2018-2026 openblack developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/openblack/openblack
 *
 * openblack is licensed under the GNU General Public License version 3.
 *******************************************************************************/

#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <MorphFile.h>
#include <glm/mat4x4.hpp>

namespace openblack::graphics
{
class L3DMesh;
}

// Named bone animations from the morph segment of a Lionhead body file: the hand's ("Hand" in Data/CTR/hh.HBN,
// names in Data/hndspec5.txt) and each creature's ("Creature" in Data/CTR/<species>.CBN, names in
// Data/ctrspec27.txt). The names drop the spec's one-letter prefix: "stand", not "Cstand".
namespace openblack::morphposes
{
using Poses = std::map<std::string, morph::Animation, std::less<>>;
bool Load(const std::filesystem::path& path, std::string_view segment, Poses& poses);
// The mesh's bones (relative to their parents) in `pose` at `frame` (fractional frames blend between the two
// either side). With `rootMoves` false the root keeps its rest position.
std::vector<glm::mat4> LocalBones(const graphics::L3DMesh& mesh, const morph::Animation& pose, float frame, bool rootMoves);
} // namespace openblack::morphposes

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
