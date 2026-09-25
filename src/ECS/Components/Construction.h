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
// and each game turn it spends woodPerStep of that for one of its steps (the original's woodRequiredPerBuild and
// timeToBuild). Its BuiltPercentage (GET_PROPERTY 22) is built, 1 when all the steps are done.
struct Construction
{
	float woodStock;
	float woodPerStep;
	float steps;
	float built = 0.0f;
	float turnTime = 0.0f;
};

} // namespace openblack::ecs::components
