/*******************************************************************************
 * Copyright (c) 2018-2026 openblack developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/openblack/openblack
 *
 * openblack is licensed under the GNU General Public License version 3.
 *******************************************************************************/

#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include <entt/core/hashed_string.hpp>
#include <glm/mat4x4.hpp>

#include "Extent.h"

namespace openblack
{
class LandBlock;
namespace graphics
{
class FrameBuffer;
class Texture2D;
} // namespace graphics
namespace lnd
{
struct LNDCell;
struct LNDCountry;
} // namespace lnd
class LandIslandInterface
{
public:
	static const uint8_t k_CellCount;
	static const float k_HeightUnit;
	// The original's LAND_CELL_SEA_ALTITUDE: land points this low are drawn, and stood on, at sea level (0); and its
	// LAND_SEA_LEVEL: GetAltitude flattens them only in cells this low (LH3DIsland::GetAltitude's caca_jc_bodge).
	static constexpr uint8_t k_SeaAltitude = 3;
	static constexpr uint8_t k_SeaLevel = 4;
	// The original's LAND_CELL_SHORE_ALTITUDE: land points this low are see-through, where the sea is drawn.
	static constexpr uint8_t k_ShoreAltitude = 1;
	static const float k_CellSize;
	static constexpr entt::hashed_string k_SmallBumpTextureId = entt::hashed_string("raw/smallbumpa");

	[[nodiscard]] virtual float GetHeightAt(glm::vec2) const = 0;
	[[nodiscard]] virtual glm::vec3 GetNormalAt(glm::vec2) const = 0;
	[[nodiscard]] virtual const lnd::LNDCell& GetCell(const glm::u16vec2& coordinates) const = 0;

	// Debug
	virtual void DumpTextures() const = 0;
	virtual void DumpMaps() const = 0;

	[[nodiscard]] virtual std::vector<LandBlock>& GetBlocks() = 0;
	[[nodiscard]] virtual const std::vector<LandBlock>& GetBlocks() const = 0;
	[[nodiscard]] virtual const std::vector<lnd::LNDCountry>& GetCountries() const = 0;

	[[nodiscard]] virtual const graphics::Texture2D& GetAlbedoArray() const = 0;
	[[nodiscard]] virtual const graphics::Texture2D& GetBump() const = 0;
	[[nodiscard]] virtual const graphics::Texture2D& GetHeightMap() const = 0;
	[[nodiscard]] virtual const graphics::FrameBuffer& GetFootprintFramebuffer() const = 0;

	[[nodiscard]] virtual U16Extent2 GetIndexExtent() const = 0;
	[[nodiscard]] virtual glm::mat4 GetOrthoView() const = 0;
	[[nodiscard]] virtual glm::mat4 GetOrthoProj() const = 0;
	[[nodiscard]] virtual Extent2 GetExtent() const = 0;
	virtual uint8_t GetNoise(glm::u8vec2 pos) = 0;
};
} // namespace openblack
