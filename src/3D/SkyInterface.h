/*******************************************************************************
 * Copyright (c) 2018-2026 openblack developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/openblack/openblack
 *
 * openblack is licensed under the GNU General Public License version 3.
 *******************************************************************************/

#pragma once

namespace openblack
{

namespace graphics
{
class L3DMesh;
class Texture2D;
} // namespace graphics

class SkyInterface
{
public:
	[[nodiscard]] virtual float GetCurrentSkyType() const noexcept = 0;
	[[nodiscard]] virtual graphics::L3DMesh& GetMesh() const noexcept = 0;
	[[nodiscard]] virtual graphics::Texture2D& GetTexture() const noexcept = 0;
	virtual void SetTime(float time) noexcept = 0;
	[[nodiscard]] virtual float GetTime() const noexcept = 0; // the hour of the game's day, 0-24
	/// Draw broad daylight whatever the time is; the time itself, and everything scripts read of it, is unchanged.
	virtual void SetForceDaylight(bool force) noexcept = 0;
	[[nodiscard]] virtual bool GetForceDaylight() const noexcept = 0;
};

} // namespace openblack
