/******************************************************************************
 * Copyright (c) 2018-2026 openblack developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/openblack/openblack
 *
 * openblack is licensed under the GNU General Public License version 3.
 *******************************************************************************/

#pragma once

#include "Enums.h"

namespace openblack::ecs::components
{

// A land or sky animal: a cow, a horse, a dove. Standing where the land put it, playing its type's default animation.
struct Animal
{
	AnimalInfo type;
};

} // namespace openblack::ecs::components
