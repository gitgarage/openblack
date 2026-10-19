/*******************************************************************************
 * Copyright (c) 2018-2026 openblack developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/openblack/openblack
 *
 * openblack is licensed under the GNU General Public License version 3.
 *******************************************************************************/

#pragma once

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "Graphics/GraphicsHandle.h"

namespace openblack::ecs::components
{
struct Sprite
{
	graphics::TextureHandle texture;
	glm::vec2 uvMin;
	glm::vec2 uvExtent;
	glm::vec4 tint;
	bool darkens = false; // drawn over what is behind (a shadow), not added to it as light (glints, sparkles)
	// Its texture's own colours and alpha (the miracle bubble), not one channel read as both (glows, shadows).
	bool fullColour = false;
	// Lying flat in its Transform's XZ plane (shadows on the ground), not turned to face the camera.
	bool flat = false;
};

} // namespace openblack::ecs::components
