/******************************************************************************
 * Copyright (c) 2018-2026 openblack developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/openblack/openblack
 *
 * openblack is licensed under the GNU General Public License version 3.
 *******************************************************************************/

#pragma once

namespace openblack::ecs::components
{

// A building still being built (a planned Citadel: CREATE_PLANNED_CITADEL). Wood given to it goes to its stock,
// which its builders turn into progress (see UpdateBuilders in CHLApi.cpp); it is done after woodPerStep x steps
// wood (the original's woodRequiredPerBuild and timeToBuild). Its BuiltPercentage (GET_PROPERTY 22) is built.
struct Construction
{
	float woodStock;
	float woodPerStep;
	float steps;
	float built = 0.0f;
	// Villagers take a site up once it has had the town's totalWoodNeededToStartBuilding (600) given to it.
	bool started = false;
};

} // namespace openblack::ecs::components
