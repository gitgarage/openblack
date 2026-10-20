/******************************************************************************
 * Copyright (c) 2018-2026 openblack developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/openblack/openblack
 *
 * openblack is licensed under the GNU General Public License version 3.
 *******************************************************************************/

#pragma once

#include <cstdint>

#include <array>
#include <string>
#include <string_view>
#include <tuple>

#include <entt/fwd.hpp>

#include "Enums.h"

namespace openblack::ecs::components
{

struct Villager
{
	/// Originally VillagerTasks
	enum class Task : uint8_t
	{
		IDLE,

		_COUNT
	};
	static constexpr std::array<std::string_view, static_cast<uint8_t>(Task::_COUNT)> k_TaskStrs = {
	    "IDLE", //
	};

	/// Originally VillagerSex
	enum class Sex : uint8_t
	{
		MALE,
		FEMALE,

		_COUNT
	};
	static constexpr std::array<std::string_view, static_cast<uint8_t>(Sex::_COUNT)> k_SexStrs = {
	    "MALE",   //
	    "FEMALE", //
	};

	/// Originally VillagerLifeStage
	enum class LifeStage : uint8_t
	{
		Child,
		Adult,

		_COUNT
	};
	static constexpr std::array<std::string_view, static_cast<uint8_t>(LifeStage::_COUNT)> k_LifeStageStrs = {
	    "Child", //
	    "Adult", //
	};

	using Type = std::tuple<Tribe, Villager::LifeStage, Villager::Sex, VillagerNumber>;

	// Life and food as the original keeps them (bw1-decomp's Villager): 0-1. Life is worn down by what a villager does
	// (each state's LifeReductionPerTurn in info.dat) and by hurts, and rested back up at home; food 1 is fed.
	float life;
	uint32_t age;
	float food;
	LifeStage lifeStage;
	Sex sex;
	Tribe tribe;
	VillagerNumber number;
	Task task;
	entt::entity town;
	entt::entity abode;
	uint16_t foodHeld = 0; // food carried to eat (the original's ResourceHeld[FOOD])
};
} // namespace openblack::ecs::components
