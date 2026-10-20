/*******************************************************************************
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
// A villager inside its house (LivingActionSystem: at home, in bed, asleep - the original's Flags & 4): not drawn, and
// nothing of it to be seen or picked up till it comes out.
struct Asleep
{
};
} // namespace openblack::ecs::components
