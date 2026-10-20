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
#include <glm/vec2.hpp>

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
// `variant` 0-3 loads that variant animation set instead of the base one.
bool Load(const std::filesystem::path& path, std::string_view segment, Poses& poses, int variant = -1);
// The mesh's bones (relative to their parents) in `pose` at `frame` (fractional frames blend between the two
// either side). With `rootMoves` false the root keeps its rest position.
std::vector<glm::mat4> LocalBones(const graphics::L3DMesh& mesh, const morph::Animation& pose, float frame, bool rootMoves);
// Adds a layer's difference from one of its own frames to bones made by CreatureBones, as the original's
// CAnim::ModifyBufferByDifferenceFromFrame: each turned bone turns on by the layer's turn at `frame` less its turn at
// `reference`, about the model's axes, and each moved bone moves by the difference in its position.
void AddDifference(const graphics::L3DMesh& mesh, std::vector<glm::mat4>& local, const morph::Animation& layer, float frame,
                   size_t reference);
// A creature's bones (relative to their parents) at a frame, as the original's CAnim::FillBuffer makes them:
// each turn is about the model's axes at rest, and a bone the pose leaves alone takes standing's first frame.
std::vector<glm::mat4> CreatureBones(const graphics::L3DMesh& mesh, const morph::Animation& pose, float frame,
                                     const morph::Animation* stand);
// Several poses at once, each with its weight (summing to one), as the original's LH3DCreature::UpdateBuffers
// mixes its cycle animations: each posed, taken down the hierarchy into the model, and the matrices summed by weight.
struct Layer
{
	const morph::Animation* pose;
	float frame;
	float weight;
};
std::vector<glm::mat4> BlendedCreatureBones(const graphics::L3DMesh& mesh, const std::vector<Layer>& layers,
                                            const morph::Animation* stand);
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
// The hand leaning as the original's HandStateNormal::Update leans it: pose `name` at `frame`, with its "_lr" and
// "_fb" layers added by how far the hand trails behind the pointer across and up the screen (`lean`, in the
// original's screen pixels, -80 to 80; 0 leaves them out).
std::vector<glm::mat4> LeaningBones(const graphics::L3DMesh& mesh, std::string_view name, float frame, glm::vec2 lean);
// Where pose `name` is `seconds` after it started: a cyclic one goes round over its length, one played once
// stops on its last frame.
float FrameAt(std::string_view name, float seconds);
// How many frames pose `name` has (0 if none).
int FrameCount(std::string_view name);
} // namespace openblack::handposes
