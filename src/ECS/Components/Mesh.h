/******************************************************************************
 * Copyright (c) 2018-2026 openblack developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/openblack/openblack
 *
 * openblack is licensed under the GNU General Public License version 3.
 *******************************************************************************/

#pragma once

#include <vector>

#include <entt/fwd.hpp>
#include <glm/mat4x4.hpp>

namespace openblack::ecs::components
{

struct Mesh
{
	entt::id_type id;
	int8_t submeshId;
	int8_t bbSubmeshId;
};

// A mesh posed by an animation from AllAnims.anm instead of its default pose. `time` is in the
// animation's own units (keyframe times); entities with this are drawn one by one, not instanced.
struct Animated
{
	uint32_t animation; // AnimId
	float time;
	// When not empty, the bones (relative to their parents) to draw instead of the animation's: the player's
	// hand, posed from its own pose file (see HandPoses.h).
	std::vector<glm::mat4> localPose {};
};

} // namespace openblack::ecs::components
