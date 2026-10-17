/******************************************************************************
 * Copyright (c) 2018-2026 openblack developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/openblack/openblack
 *
 * openblack is licensed under the GNU General Public License version 3.
 *******************************************************************************/

#pragma once

#include <entt/fwd.hpp>
#include <glm/fwd.hpp>

#include "Enums.h"

namespace openblack::ecs::archetypes
{
class AnimalArchetype
{
public:
	// On the ground at `position` (its height is ignored), facing a random way; null for a type with no mesh.
	static entt::entity Create(const glm::vec3& position, AnimalInfo type);
	AnimalArchetype() = delete;
};
} // namespace openblack::ecs::archetypes
