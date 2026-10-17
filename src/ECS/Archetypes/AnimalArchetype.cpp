/******************************************************************************
 * Copyright (c) 2018-2026 openblack developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/openblack/openblack
 *
 * openblack is licensed under the GNU General Public License version 3.
 *******************************************************************************/

#include "AnimalArchetype.h"

#include <cstdlib>

#include <glm/gtx/euler_angles.hpp>
#include <spdlog/spdlog.h>

#include "3D/LandIslandInterface.h"
#include "ECS/Components/Animal.h"
#include "ECS/Components/Mesh.h"
#include "ECS/Components/Mobile.h"
#include "ECS/Components/Transform.h"
#include "ECS/Registry.h"
#include "InfoConstants.h"
#include "Locator.h"
#include "Resources/ResourceManager.h"
#include "Resources/ResourcesInterface.h"

using namespace openblack;
using namespace openblack::ecs::archetypes;
using namespace openblack::ecs::components;

entt::entity AnimalArchetype::Create(const glm::vec3& position, AnimalInfo type)
{
	const auto index = static_cast<size_t>(type);
	const auto& infos = Locator::infoConstants::value().animal;
	if (index >= infos.size())
	{
		return entt::null;
	}
	const auto& info = infos.at(index);
	const auto mesh = resources::HashIdentifier(info.high);
	if (!Locator::resources::value().GetMeshes().Contains(mesh))
	{
		SPDLOG_LOGGER_WARN(spdlog::get("game"), "Animal {} has no mesh {}", static_cast<int>(type), static_cast<int>(info.high));
		return entt::null;
	}
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = registry.Create();
	const float ground = Locator::terrainSystem::value().GetHeightAt(glm::vec2(position.x, position.z));
	const float facing = glm::radians(static_cast<float>(std::rand() % 360));
	registry.Assign<Transform>(entity, glm::vec3(position.x, ground, position.z), glm::eulerAngleY(facing), glm::vec3(1.0f));
	registry.Assign<Mesh>(entity, mesh, static_cast<int8_t>(0), static_cast<int8_t>(0));
	registry.Assign<Animal>(entity, type);
	registry.Assign<Mobile>(entity); // the hand can pick it up, put it down and throw it, like a villager
	// Started at a random point of its animation, so a herd doesn't move in step.
	const auto& animations = Locator::resources::value().GetAnimations();
	const auto animation = resources::HashIdentifier(static_cast<uint32_t>(info.defaultAnim));
	const float duration = animations.Contains(animation) ? static_cast<float>(animations.Handle(animation)->GetDuration()) : 0.0f;
	registry.Assign<Animated>(entity, static_cast<uint32_t>(info.defaultAnim),
	                          duration * static_cast<float>(std::rand() % 1000) / 1000.0f);
	return entity;
}
