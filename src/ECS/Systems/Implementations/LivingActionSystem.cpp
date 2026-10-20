/******************************************************************************
 * Copyright (c) 2018-2026 openblack developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/openblack/openblack
 *
 * openblack is licensed under the GNU General Public License version 3.
 *******************************************************************************/

#define LOCATOR_IMPLEMENTATIONS

#include "LivingActionSystem.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <string>
#include <optional>
#include <unordered_map>

#include <entt/entity/entity.hpp>
#include <glm/common.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtx/vec_swizzle.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec2.hpp>
#include <spdlog/spdlog.h>

#include "Common/RandomNumberManager.h"
#include "3D/SkyInterface.h"
#include "CHLApi.h"
#include "InfoConstants.h"
#include "ECS/Components/Abode.h"
#include "ECS/Components/Asleep.h"
#include "ECS/Components/Fixed.h"
#include "ECS/Components/StoragePit.h"
#include "ECS/Components/LivingAction.h"
#include "ECS/Components/Town.h"
#include "ECS/Components/Transform.h"
#include "ECS/Components/Villager.h"
#include "ECS/Components/WallHug.h"
#include "ECS/Registry.h"
#include "Enums.h"
#include "Locator.h"
#include "Resources/ResourcesInterface.h"
#include "3D/AllMeshes.h"

using namespace openblack;
using namespace openblack::ecs::components;
using namespace openblack::ecs::systems;

uint32_t VillagerInvalidState(LivingAction& action)
{
	SPDLOG_LOGGER_ERROR(spdlog::get("ai"), "Villager #{}: Stuck in an invalid state",
	                    static_cast<uint32_t>(Locator::entitiesRegistry::value().ToEntity(action)));
	assert(false);
	return 0;
}

uint32_t VillagerCreated(LivingAction& action)
{
	if (action.turnsUntilStateChange > 0)
	{
		--action.turnsUntilStateChange;
	}
	else
	{
		Locator::livingActionSystem::value().VillagerSetState(action, LivingAction::Index::Top, VillagerStates::DecideWhatToDo,
		                                                      false);
	}
	return 0;
}

// Keep goals clear of the map edges so the pathfinder's neighbouring-cell lookups stay in
// bounds (the movement grid is MapInterface::k_GridSize cells, each 10 world units wide).
static constexpr float k_WanderWorldBoundMin = 30.0f;
static constexpr float k_WanderWorldBoundMax = 5090.0f;

// A villager's day as the original runs it (bw1-decomp: VillagerHome.cpp, VillagerFood.cpp, VillagerStates.cpp,
// VillagerCivic.cpp, Villager.cpp; and from runblack.exe 1.20 what it has not decompiled yet: Villager::DecideWhatToDo,
// TownDesire::Process/CallDesireFunction/CheckVillagerNeededForTownDesire and the town's desire functions for sleep,
// relaxation and food). Every number is info.dat's: the town's bed time, each state's life drain, a villager type's
// rest and thresholds.
//
// The town wants things (TOWN_DESIRE_INFO, 17 of them), each a function of the town clamped to -1..1 and sorted; a
// villager deciding what to do goes down that list and acts on the first desire above its trigger. Stage 1 (Mike,
// 2026-10-19) has three of them - sleep and relaxation, from the time of day, and food, from the store - and the
// others count 0 until their stages (work, building, food). A villager's own needs - life (worn down by work, rested
// at home) and food - come next, then nothing to do: home, outside home, or sitting about the town.
namespace
{
enum TownDesireType : size_t
{
	k_DesireFood = 0,
	k_DesireWood = 1,
	k_DesireToBuild = 9,
	k_DesireRelaxation = 15,
	k_DesireSleep = 16,
	k_DesireCount = 17,
};
// The desires a child acts on too (the desire function table's +0x60 in runblack.exe 1.20).
constexpr std::array<bool, k_DesireCount> k_ChildDesire {false, false, true, true, true, false, false, false,
                                                          false, false, false, false, false, false, false, true, true};

struct TownDesires
{
	std::array<float, k_DesireCount> value {};
	float rawSleep = 0.0f; // before the stage 1 rule below, for the log
	std::array<size_t, k_DesireCount> order {}; // most wanted first
	size_t Top() const { return order[0]; }
};
std::unordered_map<entt::entity, TownDesires> g_townDesires;

// The hours of the game's day; the sky's own times (Sky::SetDayNightTimes, LH3DSky's in the original).
constexpr float k_NightFull = 4.5f;
constexpr float k_DuskStart = 7.0f;
constexpr float k_DuskEnd = 7.5f;
constexpr float k_DayFull = 8.25f;

// LH3DSky::Time2SkyType: 2 at night, 1 at dusk, 0 by day (openblack's sky index the other way up), the clock folded
// about noon.
float SkyDarkness(float hour)
{
	const float time = hour > 12.0f ? 24.0f - hour : hour;
	if (time < k_NightFull)
	{
		return 2.0f;
	}
	if (time < k_DuskStart)
	{
		return 2.0f - (time - k_NightFull) / (k_DuskStart - k_NightFull);
	}
	if (time < k_DuskEnd)
	{
		return 1.0f;
	}
	if (time < k_DayFull)
	{
		return 1.0f - (time - k_DuskEnd) / (k_DayFull - k_DuskEnd);
	}
	return 0.0f;
}

// runblack.exe 1.20's 0x557ae0 (GGameInfo): 0 by day, rising over `span` hours to 1 once the day has `late` hours or
// fewer to go before its full day ends, 1 till midnight.
float LateInTheDay(float hour, float span, float late)
{
	const float left = 24.0f - hour;
	const float start = k_DayFull + late;
	if (left < start)
	{
		return 1.0f;
	}
	if (start + span <= left)
	{
		return 0.0f;
	}
	return 1.0f - (left - start) / span;
}

const GVillagerInfo& InfoOf(const Villager& villager)
{
	return Locator::infoConstants::value().villager.at(static_cast<size_t>(GVillagerInfo::Find(villager.tribe, villager.number)));
}

const GVillagerStateTableInfo& StateInfo(VillagerStates state)
{
	return Locator::infoConstants::value().villagerStateTable.at(static_cast<size_t>(state));
}

// The town's desires this turn (TownDesire::Process / CallDesireFunction).
void UpdateTownDesires()
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto& info = Locator::infoConstants::value();
	// Test hook: OPENBLACK_TEST_HOUR=<h> has the towns want as at that hour (the sky's own clock left alone).
	static const char* s_testHour = std::getenv("OPENBLACK_TEST_HOUR");
	const float hour = s_testHour != nullptr ? static_cast<float>(std::atof(s_testHour)) : Locator::skySystem::value().GetTime();
	const float dark = SkyDarkness(hour);
	std::unordered_map<entt::entity, float> dinners; // the food a town's people eat at a dinner
	std::unordered_map<entt::entity, float> adults;
	std::unordered_map<entt::entity, uint32_t> woodCarried; // what one of its villagers carries (maxWoodCarried)
	std::unordered_map<entt::entity, uint32_t> foodCarried; // (maxFoodCarried)
	registry.Each<const Villager>([&](const Villager& villager) {
		if (villager.town != entt::null)
		{
			const auto& own = InfoOf(villager);
			dinners[villager.town] += static_cast<float>(own.foodReqiredForDinner);
			adults[villager.town] += villager.lifeStage == Villager::LifeStage::Child ? 0.0f : 1.0f;
			woodCarried[villager.town] = own.maxWoodCarried;
			foodCarried[villager.town] = own.maxFoodCarried;
		}
	});
	std::unordered_map<uint32_t, float> buildings; // by town id (Town::CalculateActualNumberOfBuildings)
	registry.Each<const Abode>([&](const Abode& abode) { buildings[abode.townId] += 1.0f; });
	g_townDesires.clear();
	registry.Each<const Town>([&](entt::entity town, const Town&) {
		auto& desires = g_townDesires[town];
		// Sleep (0x748960): the dark and the late hours over the town's bed time, squared.
		const float sleep = std::max(dark + LateInTheDay(hour, 1.0f, 0.0f) - info.town.bedTimeMod, 0.0f);
		desires.value[k_DesireSleep] = sleep * sleep;
		// Relaxation (0x7488c0): late in the day while it is still light; never under a tenth.
		const float half = info.town.relaxationMod * 0.5f;
		desires.value[k_DesireRelaxation] = std::clamp(LateInTheDay(hour, half, half) * std::max(1.0f - dark, 0.0f), 0.1f, 1.0f);
		// Food (Town::CalculateDesireForFood): what the store lacks of foodWantedMultiplier dinners for everyone.
		// Test hook: OPENBLACK_TEST_NO_FOOD=1 has the towns want food as if their stores had none.
		static const bool s_noFood = std::getenv("OPENBLACK_TEST_NO_FOOD") != nullptr;
		const float stored = s_noFood ? 0.0f : chlapi::TownStoreAmount(static_cast<uint32_t>(town), false);
		const float wanted = info.town.foodWantedMultiplier * dinners[town];
		desires.value[k_DesireFood] = std::clamp(1.0f - (stored + 1e-4f) / (wanted + 1e-4f), 0.0f, 1.0f);
		// Its villager modification (0x7462a0): the grain already being brought counts as there.
		{
			const float coming = static_cast<float>(foodCarried[town]) * static_cast<float>(chlapi::FarmerCount(static_cast<uint32_t>(town)));
			desires.value[k_DesireFood] *= 1.0f - std::min((coming + 1e-4f + stored) / (wanted + 1e-4f), 1.0f);
		}
		// Wood (0x747ff0): what the store lacks of the wood a town of its size wants - minimum- and
		// maximumWoodForDesire, for numOfBuildingsForDesiredWood buildings or more. (Its building sites' own needs,
		// a term the town keeps statistics for, are not in yet.)
		{
			// Test hook: OPENBLACK_TEST_NO_WOOD=1 has the towns want wood as if their stores had none.
			static const bool s_noWood = std::getenv("OPENBLACK_TEST_NO_WOOD") != nullptr;
			const float wood = s_noWood ? 0.0f : chlapi::TownStoreAmount(static_cast<uint32_t>(town), true);
			const float scale = std::min(buildings[registry.Get<Town>(town).id] / std::max(info.town.numOfBuildingsForDesiredWood, 1.0f), 1.0f);
			const float least = info.town.minimumWoodForDesire * scale;
			const float most = info.town.maximumWoodForDesire * scale;
			const float sites = 0.001f / (adults[town] + 0.001f);
			const float underLeast = 1.0f - std::min(least > 0.0f ? wood / least : 1.0f, 1.0f);
			const float underMost = 1.0f - std::min(most > 0.0f ? wood / most : 1.0f, 1.0f);
			float desire = std::clamp(underMost * (underLeast + sites), 0.0f, 1.0f);
			// Its villager modification (0x746350): the wood already being fetched counts as there.
			const float coming = static_cast<float>(woodCarried[town]) * static_cast<float>(chlapi::ForesterCount(static_cast<uint32_t>(town)));
			desire *= 1.0f - std::min((coming + 1e-4f + wood) / (most + 1e-4f), 1.0f);
			desires.value[k_DesireWood] = desire;
		}
		for (size_t d = 0; d < k_DesireCount; d++)
		{
			// CallDesireFunction: times the tribe's multiplier, clamped (the villagers' own modification, not yet).
			desires.value[d] = std::clamp(desires.value[d] * info.townDesire.at(d).tribeMultiplier.at(0), -1.0f, 1.0f);
			desires.order[d] = d;
		}
		// To build (0x748640): its building sites' want of builders.
		desires.value[k_DesireToBuild] = chlapi::TownBuildDesire(static_cast<uint32_t>(town));
		// Stage 1: 14 of the 17 desires (wood, building, children...) aren't there yet, and the original's sleep at
		// part strength - a quarter in the dawn and dusk half light - loses to them; with them missing, everyone went
		// back to bed in the morning. Until they come, sleep counts only at full strength (from about 16:00 to 05:45),
		// when it heads the original's list whatever the others are.
		desires.rawSleep = desires.value[k_DesireSleep];
		if (desires.value[k_DesireSleep] < 1.0f)
		{
			desires.value[k_DesireSleep] = 0.0f;
		}
		std::stable_sort(desires.order.begin(), desires.order.end(),
		                 [&](size_t a, size_t b) { return desires.value[a] > desires.value[b]; });
	});
}

const TownDesires* DesiresOf(const Villager& villager)
{
	const auto it = g_townDesires.find(villager.town);
	return it != g_townDesires.end() ? &it->second : nullptr;
}

// Villager::GetDesireForFood: POWER(food) = 1 - food^3.
float FoodDesire(float food)
{
	const float fed = std::min(food, 1.0f);
	return 1.0f - fed * fed * fed;
}

// Villager::GetLifeDesireFromLife.
float LifeDesire(const GVillagerInfo& info, float life)
{
	const float least = std::min(info.damageThresholdToGoHome, life);
	const float t = (life - least) / (1.0f - info.damageThresholdToGoHome);
	return 1.0f - t * t;
}

bool IsChild(const Villager& villager)
{
	return villager.lifeStage == Villager::LifeStage::Child;
}

bool AtHome(entt::entity entity)
{
	return Locator::entitiesRegistry::value().AllOf<Asleep>(entity); // inside its house (the original's Flags & 4)
}

// Walk to the front of a house: the edge of its footprint nearest the villager (its middle is inside the building,
// which the pathfinder can't reach).
glm::vec2 HouseDoor(entt::entity abode, glm::vec2 from)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto& at = registry.Get<Transform>(abode).position;
	glm::vec2 centre(at.x, at.z);
	float radius = 3.0f;
	if (const auto* fixed = registry.TryGet<Fixed>(abode))
	{
		centre = fixed->boundingCenter;
		radius = fixed->boundingRadius;
	}
	const auto away = from - centre;
	const auto way = glm::length(away) > 1e-3f ? away / glm::length(away) : glm::vec2(1.0f, 0.0f);
	return centre + way * (radius + 1.0f);
}

bool HasHome(const Villager& villager)
{
	auto& registry = Locator::entitiesRegistry::value();
	return villager.abode != entt::null && registry.Valid(villager.abode) && registry.AllOf<Transform>(villager.abode);
}

// Villager::SetStateSpeed: the speed info.dat gives the state it is going to (its SpeedId), slowed when wounded, and
// by how much the town needs (its average desire), times the villager's own (SetSpeed: one villager to the next by
// its creation, the hungry and the hale a little slower, women slower, the young and the old slower). In map units
// a turn (speed x 100/65536 metres a second; a turn is a tenth of one).
float StateSpeed(entt::entity entity, const Villager& villager, VillagerStates state)
{
	const auto& info = InfoOf(villager);
	const auto& speeds = info.speedGroup;
	const std::array<SpeedState, 6> table {speeds.speedDefault, speeds.speedFleeing, speeds.speed2, speeds.speed3, speeds.speed4, speeds.speed5};
	const auto speedOf = [&](uint32_t id) { return static_cast<float>(table.at(std::min<size_t>(id, table.size() - 1))); };
	auto& rng = Locator::rng::value();
	float speed;
	if (villager.life <= info.lifeWhenCrawlsWounded)
	{
		speed = (rng.NextValue(0.0f, 0.2f) + 0.4f) * speedOf(4); // SPEED_ID_WANDER
	}
	else if (villager.life <= info.lifeWhenWalksWounded)
	{
		speed = (rng.NextValue(0.0f, 0.25f) + 0.5f) * speedOf(0);
	}
	else
	{
		float townNeeds = 1.0f;
		if (const auto* desires = DesiresOf(villager))
		{
			// TownDesire::CalculateAverageTownNeeds: a tenth of the work desires' sum (food, wood, protection,
			// mercy, abodes, civic, worship, build, repair, workshop), at most 1.
			float sum = 0.0f;
			for (const size_t d : {0u, 1u, 3u, 4u, 5u, 6u, 7u, 9u, 12u, 13u})
			{
				sum += desires->value.at(d);
			}
			const float average = std::clamp(sum * 0.1f, 0.0f, 1.0f);
			townNeeds = info.baseForTownNeedsSpeedMod + std::clamp(average / info.divisorForTownNeedsSpeedMod, 0.0f, 0.5f);
		}
		speed = speedOf(StateInfo(state).field0x24) * townNeeds; // nothing carried yet: the load factors are 1
	}
	// Villager::SetSpeed's own factor.
	float factor = 1.0f + static_cast<float>(static_cast<int>(static_cast<uint32_t>(entity) * 47u % 31u) - 16) / 100.0f;
	const auto age = static_cast<float>(villager.age);
	const auto teen = static_cast<float>(info.grownUpAge); // the original's TeenAge and MiddleAge, taken as these
	const auto middle = static_cast<float>(info.oldAge);
	if (age < teen)
	{
		factor -= std::min((teen - age) / 5.0f / 10.0f, 0.4f);
	}
	else if (age > middle)
	{
		factor -= std::min((age - middle) / 5.0f / 10.0f, 0.4f);
	}
	else
	{
		factor -= FoodDesire(villager.food) / 10.0f;
		factor -= villager.life / 10.0f;
		if (villager.sex == Villager::Sex::FEMALE)
		{
			factor -= 0.2f;
		}
	}
	return std::clamp(speed * factor, 0.0f, 65535.0f) * 100.0f / 65536.0f * 0.1f;
}

// SetupMoveToWithHug: walk there at the speed for what it is going to, then be in `then`.
void MoveTo(LivingAction& action, glm::vec2 goal, VillagerStates then)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = registry.ToEntity(action);
	auto& wallHug = registry.Get<WallHug>(entity);
	wallHug.goal = glm::clamp(goal, glm::vec2(k_WanderWorldBoundMin), glm::vec2(k_WanderWorldBoundMax));
	wallHug.step = glm::vec2(0.0f); // force a fresh step to be computed on the next pathfinding turn
	wallHug.speed = StateSpeed(entity, registry.Get<Villager>(entity), then);
	registry.Remove<MoveStateLinearTag, MoveStateOrbitTag, MoveStateExitCircleTag, MoveStateStepThroughTag,
	                MoveStateFinalStepTag, MoveStateArrivedTag>(entity);
	registry.Remove<WallHugObjectReference>(entity);
	registry.Assign<MoveStateLinearTag>(entity);
	auto& system = Locator::livingActionSystem::value();
	system.VillagerSetState(action, LivingAction::Index::Final, then, true);
	system.VillagerSetState(action, LivingAction::Index::Top, VillagerStates::MoveToPos, true);
}

void SetState(LivingAction& action, VillagerStates state)
{
	Locator::livingActionSystem::value().VillagerSetState(action, LivingAction::Index::Top, state, false);
}

bool CheckNeededForSomething(LivingAction& action);

// Villager::CheckSatisfySleep: to bed if home, home if it has one.
bool CheckSatisfySleep(LivingAction& action)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = registry.ToEntity(action);
	if (AtHome(entity))
	{
		SetState(action, VillagerStates::GotoBedAtHome);
		return true;
	}
	if (HasHome(registry.Get<Villager>(entity)))
	{
		SetState(action, VillagerStates::GoHome);
		return true;
	}
	return Locator::livingActionSystem::value().VillagerGetState(action, LivingAction::Index::Top) == VillagerStates::SleepInTent;
}

bool CheckSatisfyOwnFoodDesire(LivingAction& action);

// Villager::CheckSatisfyOwnDesire: the greater of its hunger and its tiredness first.
bool CheckSatisfyOwnDesire(LivingAction& action, float threshold)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto& villager = registry.Get<Villager>(registry.ToEntity(action));
	const auto& info = InfoOf(villager);
	const float food = FoodDesire(villager.food) - threshold;
	const float life = LifeDesire(info, villager.life) - threshold;
	if (food > life && food > 0.0f)
	{
		if (CheckSatisfyOwnFoodDesire(action))
		{
			return true;
		}
		return life > 0.0f && CheckSatisfySleep(action);
	}
	if (life > 0.0f)
	{
		if (CheckSatisfySleep(action))
		{
			return true;
		}
		return food > 0.0f && CheckSatisfyOwnFoodDesire(action);
	}
	return false;
}

// Villager::GetAmountOfFoodToEat: a dinner's worth for how hungry it is, a little less when its town wants food.
uint32_t AmountOfFoodToEat(const Villager& villager)
{
	const auto& info = InfoOf(villager);
	const float wanted = FoodDesire(villager.food) * static_cast<float>(info.foodReqiredForDinner);
	const auto* desires = DesiresOf(villager);
	const float scarcity = desires != nullptr ? std::clamp(desires->value[k_DesireFood], 0.0f, 1.0f) : 0.0f;
	return static_cast<uint32_t>((1.0f - scarcity * 0.3f) * wanted);
}

// Villager::ChangeStateToFindFoodToEat: what it carries if enough; else home, if its house has enough; else the store.
bool ChangeStateToFindFoodToEat(LivingAction& action)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = registry.ToEntity(action);
	const auto& villager = registry.Get<Villager>(entity);
	const int required = static_cast<int>(AmountOfFoodToEat(villager)) - static_cast<int>(villager.foodHeld);
	const auto eat = AtHome(entity) ? VillagerStates::EatFoodAtHome : VillagerStates::EatFood;
	if (required <= 0)
	{
		SetState(action, eat);
		return true;
	}
	if (HasHome(villager))
	{
		if (const auto* abode = registry.TryGet<Abode>(villager.abode);
		    abode != nullptr && static_cast<int>(abode->foodAmount) + villager.foodHeld >= required)
		{
			SetState(action, AtHome(entity) ? VillagerStates::EatFoodAtHome : VillagerStates::GoHome);
			return true;
		}
	}
	if (villager.town != entt::null && chlapi::TownStoreAmount(static_cast<uint32_t>(villager.town), false) >= static_cast<float>(required))
	{
		SetState(action, VillagerStates::GotoStoragePitForFood);
		return true;
	}
	if (villager.foodHeld > 0)
	{
		SetState(action, eat);
		return true;
	}
	return false;
}

// Villager::CheckSatisfyOwnFoodDesire.
bool CheckSatisfyOwnFoodDesire(LivingAction& action)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto& villager = registry.Get<Villager>(registry.ToEntity(action));
	return villager.food < InfoOf(villager).hungryForFood && ChangeStateToFindFoodToEat(action);
}

// Villager::GetOwnDesiresTrigger: how much a villager's own needs raise the bar for the town's.
float OwnDesiresTrigger(const Villager& villager)
{
	const auto& info = InfoOf(villager);
	const float food = villager.food < info.hungryForFood ? FoodDesire(villager.food) : 0.0f;
	const float life = std::max(LifeDesire(info, villager.life) - info.ownDesireThreshold, 0.0f);
	float trigger = std::max(food, life) + std::min(food, life) * 0.5f;
	if (IsChild(villager) && trigger <= 0.11f)
	{
		trigger = 0.11f;
	}
	return std::min(trigger, 1.0f);
}

// TownDesire::CheckVillagerNeededForTownDesire: down the town's wants while each is over the bar.
bool CheckNeededForTownDesire(LivingAction& action)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto& villager = registry.Get<Villager>(registry.ToEntity(action));
	const auto* desires = DesiresOf(villager);
	if (desires == nullptr)
	{
		return false;
	}
	const auto& infos = Locator::infoConstants::value().townDesire;
	float trigger = OwnDesiresTrigger(villager);
	if (trigger == 0.0f)
	{
		trigger = 0.001f;
	}
	for (const auto d : desires->order)
	{
		const float bar = std::min(trigger + infos.at(d).desireTriggersVillagerAction, 1.0f);
		if (desires->value[d] <= bar)
		{
			return false; // sorted: the rest are lower
		}
		if (IsChild(villager) && !k_ChildDesire.at(d))
		{
			continue;
		}
		if (d == k_DesireSleep && CheckSatisfySleep(action))
		{
			return true;
		}
		// Villager::CheckSatisfyWoodDesire: off to a forest (the felling and carrying: CHLApi's foresters).
		if (d == k_DesireWood)
		{
			const auto entity = registry.ToEntity(action);
			const float metresPerSecond = StateSpeed(entity, villager, VillagerStates::ForesterMoveToForest) * 10.0f;
			if (chlapi::StartForester(static_cast<uint32_t>(entity), metresPerSecond))
			{
				return true;
			}
		}
		// Villager::CheckSatisfyFoodDesire: to its town's best field (the sowing, harvesting and carrying: CHLApi's
		// farmers; shepherding and fishing, its other ways, not yet).
		if (d == k_DesireFood && !IsChild(villager))
		{
			const auto entity = registry.ToEntity(action);
			const float metresPerSecond = StateSpeed(entity, villager, VillagerStates::FarmerArrivesAtFarm) * 10.0f;
			if (chlapi::StartFarmer(static_cast<uint32_t>(entity), metresPerSecond))
			{
				return true;
			}
		}
		// Villager::CheckSatisfyToBuild: to the town's best building site (the fetching and building: CHLApi's builders).
		if (d == k_DesireToBuild && !IsChild(villager))
		{
			const auto entity = registry.ToEntity(action);
			const float metresPerSecond = StateSpeed(entity, villager, VillagerStates::ArrivesAtBuildingSite) * 10.0f;
			if (chlapi::StartBuilder(static_cast<uint32_t>(entity), metresPerSecond))
			{
				return true;
			}
		}
		// Relaxation (the town's activities): later.
	}
	return false;
}

// Villager::CheckNeededForSomething (CheckNeededForSpecial: worship not yet; the town's desires; its own needs).
bool CheckNeededForSomething(LivingAction& action)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto& villager = registry.Get<Villager>(registry.ToEntity(action));
	if (CheckNeededForTownDesire(action))
	{
		return true;
	}
	return CheckSatisfyOwnDesire(action, InfoOf(villager).ownDesireThreshold);
}

// Villager::GetChillOutPos: somewhere round the town's meeting place, on this villager's side of it.
std::optional<glm::vec2> ChillOutPos(entt::entity entity, const Villager& villager)
{
	auto& registry = Locator::entitiesRegistry::value();
	if (villager.town == entt::null || !registry.Valid(villager.town) || !registry.AllOf<Transform>(villager.town))
	{
		return std::nullopt;
	}
	const auto& centre = registry.Get<Transform>(villager.town).position;
	const auto& at = registry.Get<Transform>(entity).position;
	auto& rng = Locator::rng::value();
	const float radius = Locator::infoConstants::value().town.maxDistanceFromCongreationPosThatPeopleChillOut / 10.0f;
	const float angle = rng.NextValue(0.0f, 0.7853982f) - 0.39269909f + std::atan2(at.z - centre.z, at.x - centre.x);
	const float distance = rng.NextValue(0.0f, radius * 9.0f) + radius;
	return glm::vec2(centre.x, centre.z) + glm::vec2(std::cos(angle), std::sin(angle)) * distance;
}

// Villager::GetPosOutsideMyHouse: a step or so outside its house.
std::optional<glm::vec2> OutsideHomePos(entt::entity entity, const Villager& villager)
{
	if (!HasHome(villager))
	{
		return std::nullopt;
	}
	auto& rng = Locator::rng::value();
	const float distance = Locator::infoConstants::value().town.maxDistanceFromHouseThatPeopleChillOut * 0.5f;
	const auto& at = Locator::entitiesRegistry::value().Get<Transform>(entity).position;
	const auto door = HouseDoor(villager.abode, glm::vec2(at.x, at.z));
	const float angle = rng.NextValue(0.0f, glm::two_pi<float>());
	return door + glm::vec2(std::cos(angle), std::sin(angle)) * (3.0f + rng.NextValue(0.0f, distance));
}

// Villager::SetupNothingToDo.
void SetupNothingToDo(LivingAction& action)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = registry.ToEntity(action);
	const auto& villager = registry.Get<Villager>(entity);
	auto& rng = Locator::rng::value();
	switch (rng.NextValue<int>(0, 8))
	{
	case 0:
		if (HasHome(villager) || rng.NextValue<int>(0, 99) < 10)
		{
			SetState(action, VillagerStates::GoHome);
			return;
		}
		[[fallthrough]];
	case 1:
	case 2:
	case 3:
		if (HasHome(villager))
		{
			SetState(action, VillagerStates::GoAndChilloutOutsideHome);
			return;
		}
		[[fallthrough]];
	default:
		if (const auto pos = ChillOutPos(entity, villager))
		{
			MoveTo(action, *pos, VillagerStates::SitAndChillout);
			return;
		}
		SetState(action, VillagerStates::GoHome);
	}
}

uint32_t VillagerDecideWhatToDo(LivingAction& action)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = registry.ToEntity(action);
	if (registry.AllOf<ScriptMove>(entity))
	{
		return 0; // held by a script (or a builder's job), which decides where it goes
	}
	if (CheckNeededForSomething(action))
	{
		return 0;
	}
	SetupNothingToDo(action); // (taking what it carries to the store: stage 2)
	return 0;
}

// Wait until the PathfindingSystem has walked us to the goal, then be in the state the walk was for.
uint32_t VillagerMoveToPos(LivingAction& action)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = registry.ToEntity(action);

	// The PathfindingSystem removes the "in transit" move-state tags once the goal is reached
	// (leaving only a FinalStep tag). When none of them remain, we've arrived.
	const bool stillMoving =
	    registry.AnyOf<MoveStateLinearTag, MoveStateOrbitTag, MoveStateExitCircleTag, MoveStateStepThroughTag>(entity);
	if (auto* scriptMove = registry.TryGet<ScriptMove>(entity))
	{
		// Scripts compare positions exactly, and the pathfinder can get stuck on scenery: land on the
		// target when the walk ends or after ~30 s of trying, then stand still until told otherwise.
		constexpr uint32_t k_ScriptMoveGiveUpTurns = 300;
		if (!scriptMove->arrived && (!stillMoving || ++scriptMove->turns > k_ScriptMoveGiveUpTurns))
		{
			registry.Remove<MoveStateLinearTag, MoveStateOrbitTag, MoveStateExitCircleTag, MoveStateStepThroughTag,
			                MoveStateFinalStepTag, MoveStateArrivedTag, WallHugObjectReference>(entity);
			registry.Get<Transform>(entity).position = scriptMove->target;
			scriptMove->arrived = true;
		}
		return 0;
	}
	if (!stillMoving)
	{
		registry.Remove<MoveStateFinalStepTag, MoveStateArrivedTag>(entity);
		auto& system = Locator::livingActionSystem::value();
		const auto then = system.VillagerGetState(action, LivingAction::Index::Final);
		system.VillagerSetState(action, LivingAction::Index::Final, VillagerStates::InvalidState, true);
		SetState(action, then == VillagerStates::InvalidState ? VillagerStates::DecideWhatToDo : then);
	}
	return 0;
}

// Villager::GoHome / DoGoingHome: to its house; without one, to a sleeping spot by the town.
uint32_t VillagerGoHome(LivingAction& action)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = registry.ToEntity(action);
	if (AtHome(entity))
	{
		SetState(action, VillagerStates::AtHome);
		return 0;
	}
	const auto& villager = registry.Get<Villager>(entity);
	const auto& at = registry.Get<Transform>(entity).position;
	if (HasHome(villager))
	{
		MoveTo(action, HouseDoor(villager.abode, glm::vec2(at.x, at.z)), VillagerStates::ArrivesHome);
		return 0;
	}
	auto& rng = Locator::rng::value();
	if (villager.town != entt::null && registry.Valid(villager.town) && registry.AllOf<Transform>(villager.town))
	{
		const auto& town = registry.Get<Transform>(villager.town).position;
		const float angle = rng.NextValue(0.0f, glm::two_pi<float>());
		const float distance = rng.NextValue(0.0f, 8.0f) + 2.0f;
		MoveTo(action, glm::vec2(town.x, town.z) + glm::vec2(std::cos(angle), std::sin(angle)) * distance, VillagerStates::SleepInTent);
		return 0;
	}
	SetState(action, VillagerStates::DecideWhatToDo); // vagrant: not yet
	return 0;
}

// Villager::ArrivesHome: in at the door (ArriveHome), and at home.
uint32_t VillagerArrivesHome(LivingAction& action)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = registry.ToEntity(action);
	if (!HasHome(registry.Get<Villager>(entity)))
	{
		SetState(action, VillagerStates::DecideWhatToDo);
		return 0;
	}
	registry.AssignOrReplace<Asleep>(entity); // inside: not seen
	SetState(action, VillagerStates::AtHome);
	return 0;
}

bool DoSleeping(LivingAction& action);
bool HousewifeAtHome(LivingAction& action); // below, with the housewives

// Villager::HomeDecideWhatToDo (CheckNeedsAtHome, then anything wanted of it, then HomeNothingToDo).
uint32_t HomeDecideWhatToDo(LivingAction& action)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = registry.ToEntity(action);
	const auto& villager = registry.Get<Villager>(entity);
	const auto& info = InfoOf(villager);
	if (HousewifeAtHome(action)) // CheckNeedsAtHome: a woman's own work first (WomanSpecial)
	{
		return 0;
	}
	const float desire = std::max(LifeDesire(info, info.damageThresholdToSleepUntil), FoodDesire(info.hungryForFood));
	if (CheckSatisfyOwnDesire(action, desire * 0.9f) || CheckNeededForSomething(action))
	{
		return 0;
	}
	if (AtHome(entity) && Locator::rng::value().NextValue<int>(0, 3) == 0)
	{
		action.turnsUntilStateChange = 0;
		SetState(action, VillagerStates::GotoBedAtHome);
		return 0;
	}
	SetupNothingToDo(action);
	return 0;
}

uint32_t VillagerAtHome(LivingAction& action)
{
	return HomeDecideWhatToDo(action);
}

// Villager::GotoBedAtHome.
uint32_t VillagerGotoBedAtHome(LivingAction& action)
{
	auto& registry = Locator::entitiesRegistry::value();
	SetState(action, VillagerStates::SleepingAtHome);
	action.turnsUntilStateChange = static_cast<uint16_t>(InfoOf(registry.Get<Villager>(registry.ToEntity(action))).restAtHomeTime);
	return 0;
}

// Villager::DoSleeping: rested a little; on while the town sleeps or it is still tired.
bool DoSleeping(LivingAction& action)
{
	auto& registry = Locator::entitiesRegistry::value();
	auto& villager = registry.Get<Villager>(registry.ToEntity(action));
	const auto& info = InfoOf(villager);
	if (villager.life < info.life)
	{
		villager.life = std::min(villager.life + info.restAtHomeRestoresLifeBy, info.life);
	}
	const auto* desires = DesiresOf(villager);
	if ((desires == nullptr || desires->Top() != k_DesireSleep) && villager.life >= info.damageThresholdToSleepUntil)
	{
		return false;
	}
	action.turnsUntilStateChange = static_cast<uint16_t>(info.restAtHomeTime);
	return true;
}

// Villager::SleepingAtHome.
uint32_t VillagerSleepingAtHome(LivingAction& action)
{
	if (action.turnsUntilStateChange > 0 && --action.turnsUntilStateChange == 0 && !DoSleeping(action))
	{
		SetState(action, VillagerStates::AtHome);
	}
	return 0;
}

uint32_t VillagerWakeUpAtHome(LivingAction& action)
{
	return VillagerGoHome(action);
}

// Villager::SleepInTent: the homeless sleep out by the town.
uint32_t VillagerSleepInTent(LivingAction& action)
{
	if (action.turnsUntilStateChange == 0)
	{
		if (DoSleeping(action))
		{
			return 0;
		}
		HomeDecideWhatToDo(action);
		if (Locator::livingActionSystem::value().VillagerGetState(action, LivingAction::Index::Top) != VillagerStates::SleepInTent)
		{
			return 0;
		}
		auto& registry = Locator::entitiesRegistry::value();
		action.turnsUntilStateChange = static_cast<uint16_t>(InfoOf(registry.Get<Villager>(registry.ToEntity(action))).restAtHomeTime);
	}
	--action.turnsUntilStateChange;
	return 0;
}

// Villager::GoAndChilloutOutsideHome (a stub in bw1-decomp): out to just outside its house, to sit.
uint32_t VillagerGoAndChilloutOutsideHome(LivingAction& action)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = registry.ToEntity(action);
	if (const auto pos = OutsideHomePos(entity, registry.Get<Villager>(entity)))
	{
		MoveTo(action, *pos, VillagerStates::SitAndChillout);
		return 0;
	}
	SetState(action, VillagerStates::DecideWhatToDo);
	return 0;
}

// Villager::GoAndChilloutInTown.
uint32_t VillagerGoAndChilloutInTown(LivingAction& action)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = registry.ToEntity(action);
	if (const auto pos = ChillOutPos(entity, registry.Get<Villager>(entity)))
	{
		MoveTo(action, *pos, VillagerStates::SitAndChillout);
		return 0;
	}
	SetState(action, VillagerStates::DecideWhatToDo);
	return 0;
}

// Villager::EnterSitAndChillOut.
bool VillagerEnterSitAndChillout(LivingAction& action, VillagerStates, VillagerStates)
{
	auto& registry = Locator::entitiesRegistry::value();
	action.turnsUntilStateChange = InfoOf(registry.Get<Villager>(registry.ToEntity(action))).initialChillOutTime;
	return false;
}

// Villager::SitAndChillout: sits a while, then now and then thinks again.
uint32_t VillagerSitAndChillout(LivingAction& action)
{
	if (action.turnsUntilStateChange > 0)
	{
		--action.turnsUntilStateChange;
		return 0;
	}
	if (CheckNeededForSomething(action))
	{
		return 0;
	}
	if (Locator::rng::value().NextValue<int>(0, 9) == 0)
	{
		SetupNothingToDo(action);
		return 0;
	}
	auto& registry = Locator::entitiesRegistry::value();
	action.turnsUntilStateChange = InfoOf(registry.Get<Villager>(registry.ToEntity(action))).subsequentChillOutTime;
	return 0;
}

// Villager::GotoStoragePitForFood.
uint32_t VillagerGotoStoragePitForFood(LivingAction& action)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = registry.ToEntity(action);
	const auto& villager = registry.Get<Villager>(entity);
	glm::vec3 store {0.0f};
	bool found = false;
	float best = 300.0f;
	if (villager.town != entt::null && registry.Valid(villager.town) && registry.AllOf<Transform>(villager.town))
	{
		const auto centre = registry.Get<Transform>(villager.town).position;
		registry.Each<const StoragePit, const Transform>([&](const StoragePit&, const Transform& where) {
			const float d = glm::distance(where.position, centre);
			if (d < best)
			{
				best = d;
				store = where.position;
				found = true;
			}
		});
	}
	if (!found)
	{
		SetState(action, VillagerStates::DecideWhatToDo);
		return 0;
	}
	MoveTo(action, glm::vec2(store.x, store.z), VillagerStates::ArrivesAtStoragePitForFood);
	return 0;
}

// Villager::ArrivesAtStoragePitForFood (ArrivesAtStoragePitForResource): a meal's worth from the store, then on.
uint32_t VillagerArrivesAtStoragePitForFood(LivingAction& action)
{
	auto& registry = Locator::entitiesRegistry::value();
	auto& villager = registry.Get<Villager>(registry.ToEntity(action));
	const int required = static_cast<int>(AmountOfFoodToEat(villager)) - static_cast<int>(villager.foodHeld);
	if (required > 0 && villager.town != entt::null)
	{
		villager.foodHeld = static_cast<uint16_t>(villager.foodHeld +
		                                          chlapi::TakeFromTownStore(static_cast<uint32_t>(villager.town), false, static_cast<float>(required)));
	}
	SetState(action, VillagerStates::DecideWhatToDo);
	return 0;
}

// Villager::EatFoodHeld: what it carries, up to a meal, nourishing it by foodNurishmentMultiplier.
void EatFoodHeld(Villager& villager)
{
	const auto& info = InfoOf(villager);
	const float toEat = static_cast<float>(std::max(AmountOfFoodToEat(villager), 1u));
	const float eaten = std::min(toEat, static_cast<float>(villager.foodHeld));
	villager.foodHeld = static_cast<uint16_t>(villager.foodHeld - static_cast<uint16_t>(eaten));
	villager.food = std::clamp(villager.food + eaten / toEat * info.foodNurishmentMultiplier, 0.0f, 1.0f);
}

// The turns a state's animation plays for once (its duration, in ms, over the 100 ms turn).
uint16_t AnimationTurns(AnimId animation)
{
	const auto& animations = Locator::resources::value().GetAnimations();
	const auto id = resources::HashIdentifier(animation);
	if (!animations.Contains(id))
	{
		return 20;
	}
	return static_cast<uint16_t>(std::max(animations.Handle(id)->GetHeaderWord20() / 100u, 1u));
}

// Villager::EatFood: eating what it carries (PEat, once), then deciding again.
bool VillagerEnterEatFood(LivingAction& action, VillagerStates, VillagerStates)
{
	action.turnsUntilStateChange = AnimationTurns(AnimId::PEat);
	return false;
}

uint32_t VillagerEatFood(LivingAction& action)
{
	if (action.turnsUntilStateChange > 0)
	{
		--action.turnsUntilStateChange;
		return 0;
	}
	auto& registry = Locator::entitiesRegistry::value();
	EatFoodHeld(registry.Get<Villager>(registry.ToEntity(action)));
	SetState(action, VillagerStates::DecideWhatToDo);
	return 0;
}

// Villager::EatFoodAtHome: what the house has, then at home again.
uint32_t VillagerEatFoodAtHome(LivingAction& action)
{
	auto& registry = Locator::entitiesRegistry::value();
	auto& villager = registry.Get<Villager>(registry.ToEntity(action));
	const int required = static_cast<int>(AmountOfFoodToEat(villager)) - static_cast<int>(villager.foodHeld);
	if (required > 0 && HasHome(villager))
	{
		if (auto* abode = registry.TryGet<Abode>(villager.abode))
		{
			const auto take = std::min<uint32_t>(abode->foodAmount, static_cast<uint32_t>(required));
			abode->foodAmount -= take;
			villager.foodHeld = static_cast<uint16_t>(villager.foodHeld + take);
		}
	}
	EatFoodHeld(villager);
	SetState(action, VillagerStates::AtHome);
	return 0;
}

// Villager::CheckHungry, every ProcessChecksEvery turns: food used up as it goes (more running); hungry, its life
// goes too; starved, it dies.
void CheckHungry(entt::entity entity, Villager& villager, LivingAction& action, uint32_t turns)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto& info = InfoOf(villager);
	float drop = static_cast<float>(turns) * info.gameTurnReducesFoodInBellyBy;
	if (const auto* wallHug = registry.TryGet<WallHug>(entity))
	{
		const float pace = wallHug->speed * 10.0f / std::max(static_cast<float>(info.speedGroup.speedDefault) * 100.0f / 65536.0f, 0.01f);
		const bool moving = registry.AnyOf<MoveStateLinearTag, MoveStateOrbitTag, MoveStateExitCircleTag, MoveStateStepThroughTag>(entity);
		if (pace > 1.0f && moving)
		{
			drop *= pace;
		}
	}
	villager.food = std::max(villager.food - drop, 0.0f);
	if (villager.food < info.hungryForFood)
	{
		const float hunger = std::min(1.0f - villager.food / info.hungryForFood, 1.0f);
		villager.life = std::max(villager.life - hunger * info.hungerToLifeMultiplier, 0.0f);
		const auto top = Locator::livingActionSystem::value().VillagerGetState(action, LivingAction::Index::Top);
		const bool eating = top == VillagerStates::EatFood || top == VillagerStates::EatFoodAtHome ||
		                    top == VillagerStates::GotoStoragePitForFood || top == VillagerStates::ArrivesAtStoragePitForFood;
		if (villager.food < info.starvingForFood && !eating && !registry.AllOf<ScriptMove>(entity))
		{
			ChangeStateToFindFoodToEat(action);
		}
	}
	if (villager.life <= 0.0f)
	{
		chlapi::VillagerDiesOf(static_cast<uint32_t>(entity), "starved");
	}
}

// --- Housewives and dinner (bw1-decomp's VillagerHousewife.cpp) ---------------------------------------------------

bool IsHousewife(const Villager& villager)
{
	return villager.number == VillagerNumber::Housewife && !IsChild(villager);
}

// Abode::CalculateFoodNeededForDinner: a dinner for everyone in the house.
uint32_t FoodNeededForDinner(entt::entity abode)
{
	auto& registry = Locator::entitiesRegistry::value();
	uint32_t total = 0;
	if (const auto* home = registry.TryGet<Abode>(abode))
	{
		for (const auto person : home->inhabitants)
		{
			if (const auto* villager = registry.Valid(person) ? registry.TryGet<Villager>(person) : nullptr)
			{
				total += InfoOf(*villager).foodReqiredForDinner;
			}
		}
	}
	return total;
}

bool EnoughFoodForDinner(entt::entity abode)
{
	const auto* home = Locator::entitiesRegistry::value().TryGet<Abode>(abode);
	return home != nullptr && FoodNeededForDinner(abode) <= home->foodAmount;
}

// Villager::HousewifeAtHome (CheckNeededToMakeDinner, CheckNeededForHouseWork): someone hungry at home - dinner (or go
// home to make it); the house short of a dinner - to the store for one; else a little housework.
bool HousewifeAtHome(LivingAction& action)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = registry.ToEntity(action);
	const auto& villager = registry.Get<Villager>(entity);
	if (!IsHousewife(villager) || !HasHome(villager) || !AtHome(entity))
	{
		return false;
	}
	if (const auto* home = registry.TryGet<Abode>(villager.abode))
	{
		for (const auto person : home->inhabitants)
		{
			const auto* member = registry.Valid(person) ? registry.TryGet<Villager>(person) : nullptr;
			if (member != nullptr && member->food < InfoOf(*member).hungryForFood)
			{
				SetState(action, AtHome(person) ? VillagerStates::HousewifeMakeDinner : VillagerStates::GoHome);
				return true;
			}
		}
	}
	if (!EnoughFoodForDinner(villager.abode))
	{
		SetState(action, VillagerStates::HousewifeGotoStoragePit);
		return true;
	}
	// A little housework now and then (one time in four: the original's WomanSpecial, which decides, isn't decompiled
	// yet; every time kept her in the house for ever).
	if (Locator::rng::value().NextValue<int>(0, 3) != 0)
	{
		return false;
	}
	action.turnsUntilStateChange = 5;
	SetState(action, VillagerStates::HousewifeDoesHousework);
	return true;
}

// Villager::HousewifeGotoStoragePit: out to the village store, if the house is short.
uint32_t VillagerHousewifeGotoStoragePit(LivingAction& action)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto& villager = registry.Get<Villager>(registry.ToEntity(action));
	if (!HasHome(villager) || EnoughFoodForDinner(villager.abode))
	{
		SetState(action, VillagerStates::GoHome);
		return 0;
	}
	VillagerGotoStoragePitForFood(action); // the same walk, ending at the store
	if (Locator::livingActionSystem::value().VillagerGetState(action, LivingAction::Index::Final) == VillagerStates::ArrivesAtStoragePitForFood)
	{
		Locator::livingActionSystem::value().VillagerSetState(action, LivingAction::Index::Final, VillagerStates::HousewifeArrivesAtStoragePit, true);
	}
	return 0;
}

// Villager::HousewifeArrivesAtStoragePit: the house's dinner from the store (PPickUpPotOnHead), if it has it.
uint32_t VillagerHousewifeArrivesAtStoragePit(LivingAction& action)
{
	auto& registry = Locator::entitiesRegistry::value();
	auto& villager = registry.Get<Villager>(registry.ToEntity(action));
	const uint32_t amount = HasHome(villager) ? FoodNeededForDinner(villager.abode) : AmountOfFoodToEat(villager);
	if (amount > 0 && villager.town != entt::null &&
	    chlapi::TownStoreAmount(static_cast<uint32_t>(villager.town), false) >= static_cast<float>(amount))
	{
		villager.foodHeld = static_cast<uint16_t>(villager.foodHeld +
		                                          chlapi::TakeFromTownStore(static_cast<uint32_t>(villager.town), false, static_cast<float>(amount)));
		SPDLOG_LOGGER_INFO(spdlog::get("ai"), "Housewife {} takes {} food home from the store for dinner", static_cast<uint32_t>(registry.ToEntity(action)),
		                   villager.foodHeld);
		action.turnsUntilStateChange = AnimationTurns(AnimId::PPickUpPotOnHead);
		SetState(action, VillagerStates::HousewifePickupFromStoragePit);
		return 0;
	}
	SetState(action, VillagerStates::DecideWhatToDo); // (gossiping round the store till there is some: not yet)
	return 0;
}

// Villager::HousewifePickupFromStoragePit: the pot up, then home with it (PCarryingPot).
uint32_t VillagerHousewifePickupFromStoragePit(LivingAction& action)
{
	if (action.turnsUntilStateChange > 0)
	{
		--action.turnsUntilStateChange;
		return 0;
	}
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = registry.ToEntity(action);
	const auto& villager = registry.Get<Villager>(entity);
	if (villager.foodHeld == 0 || !HasHome(villager))
	{
		SetState(action, VillagerStates::DecideWhatToDo);
		return 0;
	}
	const auto& at = registry.Get<Transform>(entity).position;
	MoveTo(action, HouseDoor(villager.abode, glm::vec2(at.x, at.z)), VillagerStates::HousewifeReturnHomeWithFood);
	return 0;
}

// Villager::HousewifeReturnHomeWithFood: the food into the house, and in.
uint32_t VillagerHousewifeReturnHomeWithFood(LivingAction& action)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = registry.ToEntity(action);
	auto& villager = registry.Get<Villager>(entity);
	if (HasHome(villager))
	{
		if (auto* home = registry.TryGet<Abode>(villager.abode))
		{
			home->foodAmount += villager.foodHeld;
			villager.foodHeld = 0;
		}
	}
	return VillagerArrivesHome(action);
}

// Villager::HousewifeMakeDinner / ServesDinner / ClearsAwayDinner: dinner made if the house has it, then she waits
// HousewifeWaits turns while it is eaten, and eats too.
uint32_t VillagerHousewifeMakeDinner(LivingAction& action)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto& villager = registry.Get<Villager>(registry.ToEntity(action));
	SetState(action, HasHome(villager) && EnoughFoodForDinner(villager.abode) ? VillagerStates::HousewifeServesDinner
	                                                                           : VillagerStates::HousewifeGotoStoragePit);
	return 0;
}

uint32_t VillagerHousewifeServesDinner(LivingAction& action)
{
	auto& registry = Locator::entitiesRegistry::value();
	action.turnsUntilStateChange = static_cast<uint16_t>(std::max(InfoOf(registry.Get<Villager>(registry.ToEntity(action))).housewifeWaits, 1u));
	SetState(action, VillagerStates::HousewifeClearsAwayDinner);
	return 0;
}

uint32_t VillagerHousewifeClearsAwayDinner(LivingAction& action)
{
	if (action.turnsUntilStateChange > 0 && --action.turnsUntilStateChange > 0)
	{
		return 0;
	}
	auto& registry = Locator::entitiesRegistry::value();
	auto& villager = registry.Get<Villager>(registry.ToEntity(action));
	// Her own dinner, from the house (EatFoodHeld after taking her share).
	if (HasHome(villager))
	{
		if (auto* home = registry.TryGet<Abode>(villager.abode))
		{
			const auto share = std::min<uint32_t>(home->foodAmount, AmountOfFoodToEat(villager));
			home->foodAmount -= share;
			villager.foodHeld = static_cast<uint16_t>(villager.foodHeld + share);
		}
	}
	EatFoodHeld(villager);
	SetState(action, VillagerStates::AtHome);
	return 0;
}

// Villager::HousewifeDoesHousework: a little while, then at home again.
uint32_t VillagerHousewifeDoesHousework(LivingAction& action)
{
	if (CheckNeededForSomething(action))
	{
		return 0;
	}
	if (action.turnsUntilStateChange > 0 && --action.turnsUntilStateChange > 0)
	{
		return 0;
	}
	SetState(action, VillagerStates::AtHome);
	return 0;
}

bool IsHomeState(VillagerStates state)
{
	return state == VillagerStates::GoHome || state == VillagerStates::ArrivesHome || state == VillagerStates::AtHome ||
	       state == VillagerStates::GotoBedAtHome || state == VillagerStates::SleepingAtHome || state == VillagerStates::WakeUpAtHome ||
	       state == VillagerStates::EatFoodAtHome || state == VillagerStates::HousewifeMakeDinner ||
	       state == VillagerStates::HousewifeServesDinner || state == VillagerStates::HousewifeClearsAwayDinner ||
	       state == VillagerStates::HousewifeDoesHousework || state == VillagerStates::HousewifeReturnHomeWithFood;
}

// Villager::CheckEveryTime, each turn: worn down by what it does; home when hurt (every ProcessChecksEvery turns).
void CheckEveryTime(entt::entity entity, Villager& villager, LivingAction& action)
{
	auto& registry = Locator::entitiesRegistry::value();
	auto& system = Locator::livingActionSystem::value();
	const auto top = system.VillagerGetState(action, LivingAction::Index::Top);
	if (registry.AllOf<ScriptMove>(entity))
	{
		if (AtHome(entity))
		{
			registry.Remove<Asleep>(entity); // a script (or a job) wants it: out of the house, at its door
			if (HasHome(villager))
			{
				auto& transform = registry.Get<Transform>(entity);
				const auto door = HouseDoor(villager.abode, glm::vec2(transform.position.x, transform.position.z));
				transform.position.x = door.x;
				transform.position.z = door.y;
				if (auto* move = registry.TryGet<ScriptMove>(entity))
				{
					move->target = transform.position;
				}
			}
		}
		return; // the original's GAME_THING_WITH_POS_FLAG_CONTROLLED_BY_SCRIPT
	}
	// Out of its house when it is about something else (Villager::ExitAtHome -> LeaveHome): at its door.
	if (AtHome(entity) && !IsHomeState(top))
	{
		registry.Remove<Asleep>(entity);
		if (HasHome(villager))
		{
			auto& transform = registry.Get<Transform>(entity);
			const auto door = HouseDoor(villager.abode, glm::vec2(transform.position.x, transform.position.z));
			transform.position.x = door.x;
			transform.position.z = door.y;
		}
	}
	const auto& stateInfo = StateInfo(top);
	const auto final = system.VillagerGetState(action, LivingAction::Index::Final);
	const auto& drainInfo = stateInfo.field0x14 != 0 || final == VillagerStates::InvalidState ? stateInfo : StateInfo(final); // isMovingForAnimation
	villager.life = std::max(villager.life - drainInfo.field0xf8, 0.0f); // LifeReductionPerTurn
	const auto& info = InfoOf(villager);
	if (stateInfo.field0xe4 != 0 && info.processChecksEvery > 0 && action.turnsSinceStateChange % info.processChecksEvery == 0) // PerformsLivingChecks
	{
		CheckHungry(entity, villager, action, info.processChecksEvery);
		// (Death from exhaustion and old age: later.)
		if (villager.life < info.damageThresholdToGoHome && !AtHome(entity) && stateInfo.field0xe8 != 0 && stateInfo.field0xd8 == 0)
		{
			SetState(action, VillagerStates::GoHome);
		}
	}
}
} // namespace

struct VillagerStateTableEntry
{
	std::function<uint32_t(LivingAction&)> state = nullptr;
	std::function<bool(LivingAction&, VillagerStates, VillagerStates)> entryState = nullptr;
	std::function<bool(LivingAction&)> exitState = nullptr;
	std::function<bool(LivingAction&)> saveState = nullptr;
	std::function<bool(LivingAction&)> loadState = nullptr;
	std::function<bool(LivingAction&)> field0x50 = nullptr;
	std::function<bool(LivingAction&)> field0x60 = nullptr;
	std::function<int(LivingAction&)> transitionAnimation = nullptr;
	std::function<bool(LivingAction&)> validate = nullptr;
};

static const VillagerStateTableEntry k_TodoEntry = {
    .state = [](LivingAction& action) -> uint32_t {
	    SPDLOG_LOGGER_WARN(spdlog::get("ai"), "Villager #{}: TODO: Unimplemented state function: {}",
	                       static_cast<uint32_t>(Locator::entitiesRegistry::value().ToEntity(action)),
	                       k_VillagerStateStrings.at(static_cast<size_t>(
	                           Locator::livingActionSystem::value().VillagerGetState(action, LivingAction::Index::Top))));
	    return 0;
    },
    .entryState = [](LivingAction& action, VillagerStates src, VillagerStates dst) -> bool {
	    SPDLOG_LOGGER_WARN(spdlog::get("ai"), "Villager #{}: TODO: Unimplemented entry state function ({} -> {})",
	                       static_cast<uint32_t>(Locator::entitiesRegistry::value().ToEntity(action)),
	                       k_VillagerStateStrings.at(static_cast<size_t>(src)),
	                       k_VillagerStateStrings.at(static_cast<size_t>(dst)));
	    return false;
    },
    .exitState = [](LivingAction& action) -> bool {
	    SPDLOG_LOGGER_WARN(spdlog::get("ai"), "Villager #{}: TODO: Unimplemented exit state function)",
	                       static_cast<uint32_t>(Locator::entitiesRegistry::value().ToEntity(action)));
	    return false;
    },
    .saveState = [](LivingAction& action) -> bool {
	    SPDLOG_LOGGER_WARN(spdlog::get("ai"), "Villager #{}: TODO: Unimplemented save state function: {}",
	                       static_cast<uint32_t>(Locator::entitiesRegistry::value().ToEntity(action)),
	                       k_VillagerStateStrings.at(static_cast<size_t>(
	                           Locator::livingActionSystem::value().VillagerGetState(action, LivingAction::Index::Top))));
	    return false;
    },
    .loadState = [](LivingAction& action) -> bool {
	    SPDLOG_LOGGER_WARN(spdlog::get("ai"), "Villager #{}: TODO: Unimplemented load state function: {}",
	                       static_cast<uint32_t>(Locator::entitiesRegistry::value().ToEntity(action)),
	                       k_VillagerStateStrings.at(static_cast<size_t>(
	                           Locator::livingActionSystem::value().VillagerGetState(action, LivingAction::Index::Top))));
	    return false;
    },
    .field0x50 = [](LivingAction& action) -> bool {
	    SPDLOG_LOGGER_WARN(spdlog::get("ai"), "Villager #{}: TODO: Unimplemented field0x50 state function: {}",
	                       static_cast<uint32_t>(Locator::entitiesRegistry::value().ToEntity(action)),
	                       k_VillagerStateStrings.at(static_cast<size_t>(
	                           Locator::livingActionSystem::value().VillagerGetState(action, LivingAction::Index::Top))));
	    return false;
    },
    .field0x60 = [](LivingAction& action) -> bool {
	    SPDLOG_LOGGER_WARN(spdlog::get("ai"), "Villager #{}: TODO: Unimplemented field0x60 state function: {}",
	                       static_cast<uint32_t>(Locator::entitiesRegistry::value().ToEntity(action)),
	                       k_VillagerStateStrings.at(static_cast<size_t>(
	                           Locator::livingActionSystem::value().VillagerGetState(action, LivingAction::Index::Top))));
	    return false;
    },
    .transitionAnimation = [](LivingAction& action) -> int {
	    SPDLOG_LOGGER_WARN(spdlog::get("ai"), "Villager #{}: TODO: Unimplemented transition animation function: {}",
	                       static_cast<uint32_t>(Locator::entitiesRegistry::value().ToEntity(action)),
	                       k_VillagerStateStrings.at(static_cast<size_t>(
	                           Locator::livingActionSystem::value().VillagerGetState(action, LivingAction::Index::Top))));
	    return -1;
    },
    .validate = [](LivingAction& action) -> bool {
	    SPDLOG_LOGGER_WARN(spdlog::get("ai"), "Villager #{}: TODO: Unimplemented validate function: {}",
	                       static_cast<uint32_t>(Locator::entitiesRegistry::value().ToEntity(action)),
	                       k_VillagerStateStrings.at(static_cast<size_t>(
	                           Locator::livingActionSystem::value().VillagerGetState(action, LivingAction::Index::Top))));
	    return false;
    },
};

const static std::array<VillagerStateTableEntry, static_cast<size_t>(VillagerStates::_COUNT)> k_VillagerStateTable = {
    /* INVALID_STATE */ VillagerStateTableEntry {
        .state = &VillagerInvalidState,
    },
    /* MOVE_TO_POS */
    VillagerStateTableEntry {
        .state = &VillagerMoveToPos,
    },
    /* MOVE_TO_OBJECT */ k_TodoEntry,
    /* MOVE_ON_STRUCTURE */ k_TodoEntry,
    /* IN_SCRIPT */ k_TodoEntry,
    /* IN_DANCE */ k_TodoEntry,
    /* FLEEING_FROM_OBJECT_REACTION */ k_TodoEntry,
    /* LOOKING_AT_OBJECT_REACTION */ k_TodoEntry,
    /* FOLLOWING_OBJECT_REACTION */ k_TodoEntry,
    /* INSPECT_OBJECT_REACTION */ k_TodoEntry,
    /* FLYING */ k_TodoEntry,
    /* LANDED */ k_TodoEntry,
    /* LOOK_AT_FLYING_OBJECT_REACTION */ k_TodoEntry,
    /* SET_DYING */ k_TodoEntry,
    /* DYING */ k_TodoEntry,
    /* DEAD */ k_TodoEntry,
    /* DROWNING */ k_TodoEntry,
    /* DOWNED */ k_TodoEntry,
    /* BEING_EATEN */ k_TodoEntry,
    /* GOTO_FOOD_REACTION */ k_TodoEntry,
    /* ARRIVES_AT_FOOD_REACTION */ k_TodoEntry,
    /* GOTO_WOOD_REACTION */ k_TodoEntry,
    /* ARRIVES_AT_WOOD_REACTION */ k_TodoEntry,
    /* WAIT_FOR_ANIMATION */ k_TodoEntry,
    /* IN_HAND */ k_TodoEntry,
    /* GOTO_PICKUP_BALL_REACTION */ k_TodoEntry,
    /* ARRIVES_AT_PICKUP_BALL_REACTION */ k_TodoEntry,
    /* MOVE_IN_FLOCK */ k_TodoEntry,
    /* MOVE_ALONG_PATH */ k_TodoEntry,
    /* MOVE_ON_PATH */ k_TodoEntry,
    /* FLEEING_AND_LOOKING_AT_OBJECT_REACTION */ k_TodoEntry,
    /* GOTO_STORAGE_PIT_FOR_DROP_OFF */ k_TodoEntry,
    /* ARRIVES_AT_STORAGE_PIT_FOR_DROP_OFF */ k_TodoEntry,
    /* GOTO_STORAGE_PIT_FOR_FOOD */ VillagerStateTableEntry {
        .state = &VillagerGotoStoragePitForFood,
    },
    /* ARRIVES_AT_STORAGE_PIT_FOR_FOOD */ VillagerStateTableEntry {
        .state = &VillagerArrivesAtStoragePitForFood,
    },
    /* ARRIVES_AT_HOME_WITH_FOOD */ k_TodoEntry,
    /* GO_HOME */ VillagerStateTableEntry {
        .state = &VillagerGoHome,
    },
    /* ARRIVES_HOME */ VillagerStateTableEntry {
        .state = &VillagerArrivesHome,
    },
    /* AT_HOME */ VillagerStateTableEntry {
        .state = &VillagerAtHome,
    },
    /* ARRIVES_AT_STORAGE_PIT_FOR_BUILDING_MATERIALS */ k_TodoEntry,
    /* ARRIVES_AT_BUILDING_SITE */ k_TodoEntry,
    /* BUILDING */ k_TodoEntry,
    /* GOTO_STORAGE_PIT_FOR_WORSHIP_SUPPLIES */ k_TodoEntry,
    /* ARRIVES_AT_STORAGE_PIT_FOR_WORSHIP_SUPPLIES */ k_TodoEntry,
    /* GOTO_WORSHIP_SITE_WITH_SUPPLIES */ k_TodoEntry,
    /* MOVE_TO_WORSHIP_SITE_WITH_SUPPLIES */ k_TodoEntry,
    /* ARRIVES_AT_WORSHIP_SITE_WITH_SUPPLIES */ k_TodoEntry,
    /* FORESTER_MOVE_TO_FOREST */ k_TodoEntry,
    /* FORESTER_GOTO_FOREST */ k_TodoEntry,
    /* FORESTER_ARRIVES_AT_FOREST */ k_TodoEntry,
    /* FORESTER_CHOPS_TREE */ k_TodoEntry,
    /* FORESTER_CHOPS_TREE_FOR_BUILDING */ k_TodoEntry,
    /* FORESTER_FINISHED_FORESTERING */ k_TodoEntry,
    /* ARRIVES_AT_BIG_FOREST */ k_TodoEntry,
    /* ARRIVES_AT_BIG_FOREST_FOR_BUILDING */ k_TodoEntry,
    /* FISHERMAN_ARRIVES_AT_FISHING */ k_TodoEntry,
    /* FISHING */ k_TodoEntry,
    /* WAIT_FOR_COUNTER */ k_TodoEntry,
    /* GOTO_WORSHIP_SITE_FOR_WORSHIP */ k_TodoEntry,
    /* ARRIVES_AT_WORSHIP_SITE_FOR_WORSHIP */ k_TodoEntry,
    /* WORSHIPPING_AT_WORSHIP_SITE */ k_TodoEntry,
    /* GOTO_ALTAR_FOR_REST */ k_TodoEntry,
    /* ARRIVES_AT_ALTAR_FOR_REST */ k_TodoEntry,
    /* AT_ALTAR_REST */ k_TodoEntry,
    /* AT_ALTAR_FINISHED_REST */ k_TodoEntry,
    /* RESTART_WORSHIPPING_AT_WORSHIP_SITE */ k_TodoEntry,
    /* RESTART_WORSHIPPING_CREATURE */ k_TodoEntry,
    /* FARMER_ARRIVES_AT_FARM */ k_TodoEntry,
    /* FARMER_PLANTS_CROP */ k_TodoEntry,
    /* FARMER_DIGS_UP_CROP */ k_TodoEntry,
    /* MOVE_TO_FOOTBALL_PITCH_CONSTRUCTION */ k_TodoEntry,
    /* FOOTBALL_WALK_TO_POSITION */ k_TodoEntry,
    /* FOOTBALL_WAIT_FOR_KICK_OFF */ k_TodoEntry,
    /* FOOTBALL_ATTACKER */ k_TodoEntry,
    /* FOOTBALL_GOALIE */ k_TodoEntry,
    /* FOOTBALL_DEFENDER */ k_TodoEntry,
    /* FOOTBALL_WON_GOAL */ k_TodoEntry,
    /* FOOTBALL_LOST_GOAL */ k_TodoEntry,
    /* START_MOVE_TO_PICK_UP_BALL_FOR_DEAD_BALL */ k_TodoEntry,
    /* ARRIVED_AT_PICK_UP_BALL_FOR_DEAD_BALL */ k_TodoEntry,
    /* ARRIVED_AT_PUT_DOWN_BALL_FOR_DEAD_BALL_START */ k_TodoEntry,
    /* ARRIVED_AT_PUT_DOWN_BALL_FOR_DEAD_BALL_END */ k_TodoEntry,
    /* FOOTBALL_MATCH_PAUSED */ k_TodoEntry,
    /* FOOTBALL_WATCH_MATCH */ k_TodoEntry,
    /* FOOTBALL_MEXICAN_WAVE */ k_TodoEntry,
    /* CREATED */
    VillagerStateTableEntry {
        .state = &VillagerCreated,
        .field0x50 = k_TodoEntry.field0x50,
    },
    /* ARRIVES_IN_ABODE_TO_TRADE */ k_TodoEntry,
    /* ARRIVES_IN_ABODE_TO_PICK_UP_EXCESS */ k_TodoEntry,
    /* MAKE_SCARED_STIFF */ k_TodoEntry,
    /* SCARED_STIFF */ k_TodoEntry,
    /* WORSHIPPING_CREATURE */ k_TodoEntry,
    /* SHEPHERD_LOOK_FOR_FLOCK */ k_TodoEntry,
    /* SHEPHERD_MOVE_FLOCK_TO_WATER */ k_TodoEntry,
    /* SHEPHERD_MOVE_FLOCK_TO_FOOD */ k_TodoEntry,
    /* SHEPHERD_MOVE_FLOCK_BACK */ k_TodoEntry,
    /* SHEPHERD_DECIDE_WHAT_TO_DO_WITH_FLOCK */ k_TodoEntry,
    /* SHEPHERD_WAIT_FOR_FLOCK */ k_TodoEntry,
    /* SHEPHERD_SLAUGHTER_ANIMAL */ k_TodoEntry,
    /* SHEPHERD_FETCH_STRAY */ k_TodoEntry,
    /* SHEPHERD_GOTO_FLOCK */ k_TodoEntry,
    /* HOUSEWIFE_AT_HOME */ k_TodoEntry,
    /* HOUSEWIFE_GOTO_STORAGE_PIT */ VillagerStateTableEntry {
        .state = &VillagerHousewifeGotoStoragePit,
    },
    /* HOUSEWIFE_ARRIVES_AT_STORAGE_PIT */ VillagerStateTableEntry {
        .state = &VillagerHousewifeArrivesAtStoragePit,
    },
    /* HOUSEWIFE_PICKUP_FROM_STORAGE_PIT */ VillagerStateTableEntry {
        .state = &VillagerHousewifePickupFromStoragePit,
    },
    /* HOUSEWIFE_RETURN_HOME_WITH_FOOD */ VillagerStateTableEntry {
        .state = &VillagerHousewifeReturnHomeWithFood,
    },
    /* HOUSEWIFE_MAKE_DINNER */ VillagerStateTableEntry {
        .state = &VillagerHousewifeMakeDinner,
    },
    /* HOUSEWIFE_SERVES_DINNER */ VillagerStateTableEntry {
        .state = &VillagerHousewifeServesDinner,
    },
    /* HOUSEWIFE_CLEARS_AWAY_DINNER */ VillagerStateTableEntry {
        .state = &VillagerHousewifeClearsAwayDinner,
    },
    /* HOUSEWIFE_DOES_HOUSEWORK */ VillagerStateTableEntry {
        .state = &VillagerHousewifeDoesHousework,
    },
    /* HOUSEWIFE_GOSSIPS_AROUND_STORAGE_PIT */ k_TodoEntry,
    /* HOUSEWIFE_STARTS_GIVING_BIRTH */ k_TodoEntry,
    /* HOUSEWIFE_GIVING_BIRTH */ k_TodoEntry,
    /* HOUSEWIFE_GIVEN_BIRTH */ k_TodoEntry,
    /* CHILD_AT_CRECHE */ k_TodoEntry,
    /* CHILD_FOLLOWS_MOTHER */ k_TodoEntry,
    /* CHILD_BECOMES_ADULT */ k_TodoEntry,
    /* SITS_DOWN_TO_DINNER */ k_TodoEntry,
    /* EAT_FOOD */ VillagerStateTableEntry {
        .state = &VillagerEatFood,
        .entryState = &VillagerEnterEatFood,
    },
    /* EAT_FOOD_AT_HOME */ VillagerStateTableEntry {
        .state = &VillagerEatFoodAtHome,
    },
    /* GOTO_BED_AT_HOME */ VillagerStateTableEntry {
        .state = &VillagerGotoBedAtHome,
    },
    /* SLEEPING_AT_HOME */ VillagerStateTableEntry {
        .state = &VillagerSleepingAtHome,
    },
    /* WAKE_UP_AT_HOME */ VillagerStateTableEntry {
        .state = &VillagerWakeUpAtHome,
    },
    /* START_HAVING_SEX */ k_TodoEntry,
    /* HAVING_SEX */ k_TodoEntry,
    /* STOP_HAVING_SEX */ k_TodoEntry,
    /* START_HAVING_SEX_AT_HOME */ k_TodoEntry,
    /* HAVING_SEX_AT_HOME */ k_TodoEntry,
    /* STOP_HAVING_SEX_AT_HOME */ k_TodoEntry,
    /* WAIT_FOR_DINNER */ k_TodoEntry,
    /* HOMELESS_START */ k_TodoEntry,
    /* VAGRANT_START */ k_TodoEntry,
    /* MORN_DEATH */ k_TodoEntry,
    /* PERFORM_INSPECTION_REACTION */ k_TodoEntry,
    /* APPROACH_OBJECT_REACTION */ k_TodoEntry,
    /* INITIALISE_TELL_OTHERS_ABOUT_OBJECT */ k_TodoEntry,
    /* TELL_OTHERS_ABOUT_INTERESTING_OBJECT */ k_TodoEntry,
    /* APPROACH_VILLAGER_TO_TALK_TO */ k_TodoEntry,
    /* TELL_PARTICULAR_VILLAGER_ABOUT_OBJECT */ k_TodoEntry,
    /* INITIALISE_LOOK_AROUND_FOR_VILLAGER_TO_TELL */ k_TodoEntry,
    /* LOOK_AROUND_FOR_VILLAGER_TO_TELL */ k_TodoEntry,
    /* MOVE_TOWARDS_OBJECT_TO_LOOK_AT */ k_TodoEntry,
    /* INITIALISE_IMPRESSED_REACTION */ k_TodoEntry,
    /* PERFORM_IMPRESSED_REACTION */ k_TodoEntry,
    /* INITIALISE_FIGHT_REACTION */ k_TodoEntry,
    /* PERFORM_FIGHT_REACTION */ k_TodoEntry,
    /* HOMELESS_EAT_DINNER */ k_TodoEntry,
    /* INSPECT_CREATURE_REACTION */ k_TodoEntry,
    /* PERFORM_INSPECT_CREATURE_REACTION */ k_TodoEntry,
    /* APPROACH_CREATURE_REACTION */ k_TodoEntry,
    /* INITIALISE_BEWILDERED_BY_MAGIC_TREE_REACTION */ k_TodoEntry,
    /* PERFORM_BEWILDERED_BY_MAGIC_TREE_REACTION */ k_TodoEntry,
    /* TURN_TO_FACE_MAGIC_TREE */ k_TodoEntry,
    /* LOOK_AT_MAGIC_TREE */ k_TodoEntry,
    /* DANCE_FOR_EDITING_PURPOSES */ k_TodoEntry,
    /* MOVE_TO_DANCE_POS */ k_TodoEntry,
    /* INITIALISE_RESPECT_CREATURE_REACTION */ k_TodoEntry,
    /* PERFORM_RESPECT_CREATURE_REACTION */ k_TodoEntry,
    /* FINISH_RESPECT_CREATURE_REACTION */ k_TodoEntry,
    /* APPROACH_HAND_REACTION */ k_TodoEntry,
    /* FLEEING_FROM_CREATURE_REACTION */ k_TodoEntry,
    /* TURN_TO_FACE_CREATURE_REACTION */ k_TodoEntry,
    /* WATCH_FLYING_OBJECT_REACTION */ k_TodoEntry,
    /* POINT_AT_FLYING_OBJECT_REACTION */ k_TodoEntry,
    /* DECIDE_WHAT_TO_DO */
    VillagerStateTableEntry {
        .state = &VillagerDecideWhatToDo,
    },
    /* INTERACT_DECIDE_WHAT_TO_DO */ k_TodoEntry,
    /* EAT_OUTSIDE */ k_TodoEntry,
    /* RUN_AWAY_FROM_OBJECT_REACTION */ k_TodoEntry,
    /* MOVE_TOWARDS_CREATURE_REACTION */ k_TodoEntry,
    /* AMAZED_BY_MAGIC_SHIELD_REACTION */ k_TodoEntry,
    /* VILLAGER_GOSSIPS */ k_TodoEntry,
    /* CHECK_INTERACT_WITH_ANIMAL */ k_TodoEntry,
    /* CHECK_INTERACT_WITH_WORSHIP_SITE */ k_TodoEntry,
    /* CHECK_INTERACT_WITH_ABODE */ k_TodoEntry,
    /* CHECK_INTERACT_WITH_FIELD */ k_TodoEntry,
    /* CHECK_INTERACT_WITH_FISH_FARM */ k_TodoEntry,
    /* CHECK_INTERACT_WITH_TREE */ k_TodoEntry,
    /* CHECK_INTERACT_WITH_BALL */ k_TodoEntry,
    /* CHECK_INTERACT_WITH_POT */ k_TodoEntry,
    /* CHECK_INTERACT_WITH_FOOTBALL */ k_TodoEntry,
    /* CHECK_INTERACT_WITH_VILLAGER */ k_TodoEntry,
    /* CHECK_INTERACT_WITH_MAGIC_LIVING */ k_TodoEntry,
    /* CHECK_INTERACT_WITH_ROCK */ k_TodoEntry,
    /* ARRIVES_AT_ROCK_FOR_WOOD */ k_TodoEntry,
    /* GOT_WOOD_FROM_ROCK */ k_TodoEntry,
    /* REENTER_BUILDING_STATE */ k_TodoEntry,
    /* ARRIVE_AT_PUSH_OBJECT */ k_TodoEntry,
    /* TAKE_WOOD_FROM_TREE */ k_TodoEntry,
    /* TAKE_WOOD_FROM_POT */ k_TodoEntry,
    /* TAKE_WOOD_FROM_TREE_FOR_BUILDING */ k_TodoEntry,
    /* TAKE_WOOD_FROM_POT_FOR_BUILDING */ k_TodoEntry,
    /* SHEPHERD_TAKE_ANIMAL_FOR_SLAUGHTER */ k_TodoEntry,
    /* SHEPHERD_TAKES_CONTROL_OF_FLOCK */ k_TodoEntry,
    /* SHEPHERD_RELEASES_CONTROL_OF_FLOCK */ k_TodoEntry,
    /* DANCE_BUT_NOT_WORSHIP */ k_TodoEntry,
    /* FAINTING_REACTION */ k_TodoEntry,
    /* START_CONFUSED_REACTION */ k_TodoEntry,
    /* CONFUSED_REACTION */ k_TodoEntry,
    /* AFTER_TAP_ON_ABODE */ k_TodoEntry,
    /* WEAK_ON_GROUND */ k_TodoEntry,
    /* SCRIPT_WANDER_AROUND_POSITION */ k_TodoEntry,
    /* SCRIPT_PLAY_ANIM */ k_TodoEntry,
    /* GO_TOWARDS_TELEPORT_REACTION */ k_TodoEntry,
    /* TELEPORT_REACTION */ k_TodoEntry,
    /* DANCE_WHILE_REACTING */ k_TodoEntry,
    /* CONTROLLED_BY_CREATURE */ k_TodoEntry,
    /* POINT_AT_DEAD_PERSON */ k_TodoEntry,
    /* GO_TOWARDS_DEAD_PERSON */ k_TodoEntry,
    /* LOOK_AT_DEAD_PERSON */ k_TodoEntry,
    /* MOURN_DEAD_PERSON */ k_TodoEntry,
    /* NOTHING_TO_DO */ k_TodoEntry,
    /* ARRIVES_AT_WORKSHOP_FOR_DROP_OFF */ k_TodoEntry,
    /* ARRIVES_AT_STORAGE_PIT_FOR_WORKSHOP_MATERIALS */ k_TodoEntry,
    /* SHOW_POISONED */ k_TodoEntry,
    /* HIDING_AT_WORSHIP_SITE */ k_TodoEntry,
    /* CROWD_REACTION */ k_TodoEntry,
    /* REACT_TO_FIRE */ k_TodoEntry,
    /* PUT_OUT_FIRE_BY_BEATING */ k_TodoEntry,
    /* PUT_OUT_FIRE_WITH_WATER */ k_TodoEntry,
    /* GET_WATER_TO_PUT_OUT_FIRE */ k_TodoEntry,
    /* ON_FIRE */ k_TodoEntry,
    /* MOVE_AROUND_FIRE */ k_TodoEntry,
    /* DISCIPLE_NOTHING_TO_DO */ k_TodoEntry,
    /* FOOTBALL_MOVE_TO_BALL */ k_TodoEntry,
    /* ARRIVES_AT_STORAGE_PIT_FOR_TRADER_PICK_UP */ k_TodoEntry,
    /* ARRIVES_AT_STORAGE_PIT_FOR_TRADER_DROP_OFF */ k_TodoEntry,
    /* BREEDER_DISCIPLE */ k_TodoEntry,
    /* MISSIONARY_DISCIPLE */ k_TodoEntry,
    /* REACT_TO_BREEDER */ k_TodoEntry,
    /* SHEPHERD_CHECK_ANIMAL_FOR_SLAUGHTER */ k_TodoEntry,
    /* INTERACT_DECIDE_WHAT_TO_DO_FOR_OTHER_VILLAGER */ k_TodoEntry,
    /* ARTIFACT_DANCE */ k_TodoEntry,
    /* FLEEING_FROM_PREDATOR_REACTION */ k_TodoEntry,
    /* WAIT_FOR_WOOD */ k_TodoEntry,
    /* INSPECT_OBJECT */ k_TodoEntry,
    /* GO_HOME_AND_CHANGE */ k_TodoEntry,
    /* WAIT_FOR_MATE */ k_TodoEntry,
    /* GO_AND_HIDE_IN_NEARBY_BUILDING */ k_TodoEntry,
    /* LOOK_TO_SEE_IF_IT_IS_SAFE */ k_TodoEntry,
    /* SLEEP_IN_TENT */ VillagerStateTableEntry {
        .state = &VillagerSleepInTent,
    },
    /* PAUSE_FOR_A_SECOND */ k_TodoEntry,
    /* PANIC_REACTION */ k_TodoEntry,
    /* GET_FOOD_AT_WORSHIP_SITE */ k_TodoEntry,
    /* GOTO_CONGREGATE_IN_TOWN_AFTER_EMERGENCY */ k_TodoEntry,
    /* CONGREGATE_IN_TOWN_AFTER_EMERGENCY */ k_TodoEntry,
    /* SCRIPT_IN_CROWD */ k_TodoEntry,
    /* GO_AND_CHILLOUT_OUTSIDE_HOME */ VillagerStateTableEntry {
        .state = &VillagerGoAndChilloutOutsideHome,
    },
    /* SIT_AND_CHILLOUT */ VillagerStateTableEntry {
        .state = &VillagerSitAndChillout,
        .entryState = &VillagerEnterSitAndChillout,
    },
    /* SCRIPT_GO_AND_MOVE_ALONG_PATH */ k_TodoEntry,
    /* RESTART_MEETING */ k_TodoEntry,
    /* GOTO_ABODE_BURNING_REACTION */ k_TodoEntry,
    /* ARRIVES_AT_ABODE_BURNING_REACTION */ k_TodoEntry,
    /* REPAIRS_ABODE */ k_TodoEntry,
    /* GO_AND_CHILLOUT_IN_TOWN (252, by Enums.h and bw1-decomp's GStates.h; this list's last names are off) */ VillagerStateTableEntry {
        .state = &VillagerGoAndChilloutInTown,
    },
    /* ARRIVES_AT_BUILDING_SITE_WITH_SCAFFOLD */ k_TodoEntry,
    /* MOVE_SCAFFOLD_TO_BUILDING_SITE */ k_TodoEntry,
};

void LivingActionSystem::Update()
{
	auto& registry = Locator::entitiesRegistry::value();

	registry.Each<LivingAction>([](LivingAction& action) { ++action.turnsSinceStateChange; });

	UpdateTownDesires();
	registry.Each<Villager, LivingAction>([](entt::entity entity, Villager& villager, LivingAction& action) {
		CheckEveryTime(entity, villager, action);
	});
	// Every 30 s: what the villagers are doing, and what their towns want (the live log, for checking stage 1).
	static uint32_t s_turn = 0;
	if (++s_turn % 300 == 0)
	{
		std::map<std::string_view, int> states;
		float life = 0.0f;
		float food = 0.0f;
		int count = 0;
		registry.Each<const Villager, const LivingAction>([&](const Villager& villager, const LivingAction& action) {
			++states[k_VillagerStateStrings.at(action.states.at(0))];
			life += villager.life;
			food += villager.food;
			++count;
		});
		std::string summary;
		for (const auto& [name, n] : states)
		{
			summary += fmt::format("{}{} {}", summary.empty() ? "" : ", ", n, name);
		}
		std::string wants;
		for (const auto& [town, desires] : g_townDesires)
		{
			wants += fmt::format(" town {}: sleep {:.2f} relax {:.2f} food {:.2f} wood {:.2f} build {:.2f} ({} getting wood, {} farming; top {})", static_cast<uint32_t>(town),
			                     desires.rawSleep, desires.value[k_DesireRelaxation], desires.value[k_DesireFood], desires.value[k_DesireWood], desires.value[k_DesireToBuild],
			                     chlapi::ForesterCount(static_cast<uint32_t>(town)), chlapi::FarmerCount(static_cast<uint32_t>(town)),
			                     desires.Top());
		}
		SPDLOG_LOGGER_INFO(spdlog::get("ai"), "Villager life at {:.1f}h: {} (mean life {:.2f}, food {:.2f});{}", Locator::skySystem::value().GetTime(),
		                   summary, count > 0 ? life / static_cast<float>(count) : 0.0f, count > 0 ? food / static_cast<float>(count) : 0.0f, wants);
	}

	// TODO(#475): process food speedup

	registry.Each<const Villager, LivingAction>([this]([[maybe_unused]] const Villager& villager, LivingAction& action) {
		VillagerCallValidate(action, LivingAction::Index::Top);
	});
	// TODO(#476): same call but for other types of living

	registry.Each<const Villager, LivingAction>([this]([[maybe_unused]] const Villager& villager, LivingAction& action) {
		VillagerCallValidate(action, LivingAction::Index::Final);
	});
	// TODO(#476): same call but for other types of living

	// TODO(bwrsandman): Store result of this call in vector or with tag component
	registry.Each<const Villager, LivingAction>([this]([[maybe_unused]] const Villager& villager, LivingAction& action) {
		VillagerCallState(action, LivingAction::Index::Top);
	});
	// TODO(#476): same call but for other types of living
}

VillagerStates LivingActionSystem::VillagerGetState(const LivingAction& action, LivingAction::Index index) const
{
	return static_cast<VillagerStates>(action.states.at(static_cast<size_t>(index)));
}

void LivingActionSystem::VillagerSetState(LivingAction& action, LivingAction::Index index, VillagerStates state,
                                          bool skipTransition) const
{
	const auto previousState = static_cast<VillagerStates>(action.states.at(static_cast<size_t>(index)));
	if (previousState == state)
	{
		return;
	}

	[[maybe_unused]] auto& registry = Locator::entitiesRegistry::value();
	SPDLOG_LOGGER_TRACE(spdlog::get("ai"), "Villager #{}: Setting state {} -> {}", static_cast<int>(registry.ToEntity(action)),
	                    k_VillagerStateStrings.at(static_cast<size_t>(previousState)),
	                    k_VillagerStateStrings.at(static_cast<size_t>(state)));

	const bool runTransition = index == LivingAction::Index::Top && !skipTransition;

	// Exit the previous state before switching. A truthy return means it isn't ready to be
	// left yet, so abort the transition without changing state.
	if (runTransition && VillagerCallExitState(action, index))
	{
		return;
	}

	if (index == LivingAction::Index::Top)
	{
		action.turnsSinceStateChange = 0;
	}
	action.states.at(static_cast<size_t>(index)) = static_cast<uint8_t>(state);

	if (runTransition)
	{
		VillagerCallEntryState(action, index, previousState, state);
	}
}

uint32_t LivingActionSystem::VillagerCallState(LivingAction& action, LivingAction::Index index) const
{
	const auto state = action.states.at(static_cast<size_t>(index));
	const auto& entry = k_VillagerStateTable.at(static_cast<size_t>(state));
	const auto& callback = entry.state;
	if (!callback)
	{
		return 0;
	}
	return callback(action);
}

bool LivingActionSystem::VillagerCallEntryState(LivingAction& action, LivingAction::Index index, VillagerStates src,
                                                VillagerStates dst) const
{
	const auto state = action.states.at(static_cast<size_t>(index));
	const auto& entry = k_VillagerStateTable.at(static_cast<size_t>(state));
	const auto& callback = entry.entryState;
	if (!callback)
	{
		return false;
	}
	return callback(action, src, dst);
}

bool LivingActionSystem::VillagerCallExitState(LivingAction& action, LivingAction::Index index) const
{
	const auto& state = action.states.at(static_cast<size_t>(index));
	const auto& entry = k_VillagerStateTable.at(static_cast<size_t>(state));
	const auto& callback = entry.exitState;
	if (!callback)
	{
		return false;
	}
	return callback(action);
}

int LivingActionSystem::VillagerCallOutOfAnimation(LivingAction& action, LivingAction::Index index) const
{
	const auto state = action.states.at(static_cast<size_t>(index));
	const auto& entry = k_VillagerStateTable.at(static_cast<size_t>(state));
	const auto& callback = entry.transitionAnimation;
	if (!callback)
	{
		return -1;
	}
	return callback(action);
}

bool LivingActionSystem::VillagerCallValidate(LivingAction& action, LivingAction::Index index) const
{
	const auto state = action.states.at(static_cast<size_t>(index));
	const auto& entry = k_VillagerStateTable.at(static_cast<size_t>(state));
	const auto& callback = entry.validate;
	if (!callback)
	{
		return false;
	}
	return callback(action);
}
