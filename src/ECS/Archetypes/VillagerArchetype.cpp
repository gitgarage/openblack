/******************************************************************************
 * Copyright (c) 2018-2026 openblack developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/openblack/openblack
 *
 * openblack is licensed under the GNU General Public License version 3.
 *******************************************************************************/

#include "VillagerArchetype.h"

#include <glm/gtx/euler_angles.hpp>
#include <glm/vec3.hpp>

#include "Common/RandomNumberManager.h"
#include "ECS/Components/LivingAction.h"
#include "ECS/Components/Mesh.h"
#include "ECS/Components/Mobile.h"
#include "ECS/Components/Transform.h"
#include "ECS/Components/Villager.h"
#include "ECS/Components/WallHug.h"
#include "ECS/Registry.h"
#include "ECS/Systems/TownSystemInterface.h"
#include "InfoConstants.h"
#include "Locator.h"
#include "Resources/ResourceManager.h"

using namespace openblack;
using namespace openblack::ecs::archetypes;
using namespace openblack::ecs::components;
using namespace openblack::ecs::systems;

entt::entity VillagerArchetype::Create([[maybe_unused]] const glm::vec3& abodePosition, const glm::vec3& position,
                                       VillagerInfo type, uint32_t age, entt::entity hint)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = hint == entt::null ? registry.Create() : registry.Create(hint);

	const auto& info = Locator::infoConstants::value().villager.at(static_cast<size_t>(type));

	registry.Assign<Transform>(entity, position, glm::eulerAngleY(glm::radians(180.0f)), glm::vec3(1.0));
	registry.Assign<Mobile>(entity);
	const float life = info.life;
	// Villager::Villager: fed to a random point above hungry (bw1-decomp).
	const float food = std::min(Locator::rng::value().NextValue(0.0f, 0.6f) + info.hungryForFood, 1.0f);

	const auto lifeStage = age < 18 ? Villager::LifeStage::Child : Villager::LifeStage::Adult;
	const auto sex = info.villagerNumber == VillagerNumber::Housewife ? Villager::Sex::FEMALE : Villager::Sex::MALE;
	const auto task = Villager::Task::IDLE;

	// TODO(bwrsandman): Might be better to make a FindClosestAbode
	const entt::entity town = Locator::townSystem::value().FindClosestTown(abodePosition);
	entt::entity abode = entt::null;
	if (town != entt::null)
	{
		abode = Locator::townSystem::value().FindAbodeWithSpace(town);
	}

	registry.Assign<Villager>(entity, life, static_cast<uint32_t>(age), food, lifeStage, sex, info.tribeType,
	                          info.villagerNumber, task, town, abode);
	// The pathfinder steps a living by its WallHug speed once a game turn (100 ms), and info.dat's speed is metres a
	// second: a tenth of it a turn. Given whole, villagers about the town went ten times their pace (Mike,
	// 2026-10-17: much faster than the builders, whose walk is right).
	constexpr float k_TurnSeconds = 0.1f;
	registry.Assign<WallHug>(entity, glm::vec2(), glm::vec2(), 0.0f, GetSpeedStateSpeed(info.speedGroup.speedDefault) * k_TurnSeconds);
	registry.Assign<Mesh>(entity, MeshResource(type, age), static_cast<int8_t>(0), static_cast<int8_t>(0));
	auto turnsSinceStateChange = Locator::rng::value().NextValue<uint16_t>(1, 500);
	registry.Assign<LivingAction>(entity, VillagerStates::Created, turnsSinceStateChange);

	return entity;
}

entt::id_type VillagerArchetype::MeshResource(VillagerInfo type, uint32_t age)
{
	const auto& info = Locator::infoConstants::value().villager.at(static_cast<size_t>(type));
	const bool child = age < 18 && info.childMeshHigh != MeshId::Dummy;
	return resources::HashIdentifier(child ? info.childMeshHigh : info.highDetail);
}
