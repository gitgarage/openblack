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
	// The original's growing (bw1-decomp's Field, from runblack.exe 1.20's Field::Process / PlantCrop / RemoveFood):
	// its field type (info.dat's GFieldTypeInfo), how many of its timesToSow crops are planted, and how far it has
	// grown (to ageGrowth it is ripe enough to harvest, to ageRecolt fully grown, full of its totalFoodInField).
	uint32_t fieldType = 0;
	uint8_t crops = 0;
	float growth = 0.0f;
};

} // namespace openblack::ecs::components
