/******************************************************************************
 * Copyright (c) 2018-2026 openblack developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/openblack/openblack
 *
 * openblack is licensed under the GNU General Public License version 3.
 *******************************************************************************/

#pragma once

#include <string>

#include <entt/entity/entity.hpp>
#include <entt/fwd.hpp>
#include <glm/fwd.hpp>

#include "Enums.h"

namespace openblack::ecs::archetypes
{
class VillagerArchetype
{
public:
	static entt::entity Create(const glm::vec3& abodePosition, const glm::vec3& position, VillagerInfo type, uint32_t age,
	                           entt::entity hint = entt::null);
	// The body for a villager of this type and age: children have their own in the info.
	static entt::id_type MeshResource(VillagerInfo type, uint32_t age);
	// A child until its type's teen age (info.dat's, 13 for Land 1's people), as the original's Villager.
	static bool IsChild(VillagerInfo type, uint32_t age);
	// How big it is drawn at this age (Villager::InitialiseScale and SetScaleForAge): a child its type's ageToScale for
	// its age - 0.41 at birth to 0.97 at 12 - plus a random part of the way to the next year's; from its teens 0.95-1.05.
	static float ScaleForAge(VillagerInfo type, uint32_t age);
	VillagerArchetype() = delete;
};
} // namespace openblack::ecs::archetypes
