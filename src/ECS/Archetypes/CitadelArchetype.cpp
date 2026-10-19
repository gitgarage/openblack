/******************************************************************************
 * Copyright (c) 2018-2026 openblack developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/openblack/openblack
 *
 * openblack is licensed under the GNU General Public License version 3.
 *******************************************************************************/

#include "CitadelArchetype.h"

#include <entt/fwd.hpp>

#include "ECS/Components/Construction.h"
#include "ECS/Components/Mesh.h"
#include "ECS/Components/Temple.h"
#include "ECS/Components/Transform.h"
#include "ECS/Registry.h"
#include "InfoConstants.h"
#include "Locator.h"

#include <algorithm>

using namespace openblack;
using namespace openblack::ecs::archetypes;
using namespace openblack::ecs::components;

entt::entity CitadelArchetype::Create(const glm::vec3& position, PlayerNames playerOwner, const glm::mat4& rotation,
                                      const glm::vec3& size)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = registry.Create();
	registry.Assign<Transform>(entity, position, rotation, size);
	registry.Assign<Temple>(entity, playerOwner);
	// The original builds its Temple from B_TEMPLE<height><shape>: height 0-2 by its size, shape 0 (evil: curved feet,
	// arches) to 4 (good) by alignment (runblack.exe 0x882b10). Neither is tracked here yet: the smallest, neutral
	// (2), with B_FIRST_TEMPLE's skin, which the shapes refer to (Mike, 2026-10-18; B_FIRST_TEMPLE is shape 0).
	const auto meshId = entt::hashed_string("temple/b_temple02_l3d");
	registry.Assign<Mesh>(entity, meshId, static_cast<int8_t>(0), static_cast<int8_t>(0));
	return entity;
}

entt::entity CitadelArchetype::CreatePlan(int32_t /*townId*/, const glm::vec3& position, PlayerNames playerOwner,
                                          const glm::mat4& rotation, const glm::vec3& size)
{
	// A planned Citadel is built from the wood given to it, at the original Citadel heart's rate.
	const auto entity = Create(position, playerOwner, rotation, size);
	const auto& heart = Locator::infoConstants::value().citadelHeart;
	Locator::entitiesRegistry::value().Assign<Construction>(entity, 0.0f, static_cast<float>(heart.woodRequiredPerBuild),
	                                                        static_cast<float>(std::max(heart.timeToBuild, 1u)));
	return entity;
}
