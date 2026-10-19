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

struct Field
{
	int town;
	// The grain in it, which the hand takes as from a store (info.dat's totalFoodInField when full), and how much that
	// is when full. The wheat stands as tall as the share left (FieldArchetype's mesh flattened), and grows back.
	float grain = 0.0f;
	float capacity = 0.0f;
};

} // namespace openblack::ecs::components
