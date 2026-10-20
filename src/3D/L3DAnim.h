/******************************************************************************
 * Copyright (c) 2018-2026 openblack developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/openblack/openblack
 *
 * openblack is licensed under the GNU General Public License version 3.
 *******************************************************************************/

#pragma once

#include <array>

#include <cstdint>

#include <filesystem>
#include <vector>

#include <glm/fwd.hpp>

namespace openblack
{
namespace anm
{
class ANMFile;
} // namespace anm

namespace debug::gui
{
class MeshViewer;
}

class L3DAnim
{
public:
	struct Frame
	{
		uint32_t time;
		std::vector<glm::mat4> bones;
	};

	L3DAnim() noexcept = default;
	virtual ~L3DAnim() noexcept = default;

	void Load(const anm::ANMFile& anm) noexcept;
	bool LoadFromFilesystem(const std::filesystem::path& path) noexcept;
	bool LoadFromFile(const std::filesystem::path& path) noexcept;
	bool LoadFromBuffer(const std::vector<uint8_t>& data) noexcept;

	[[nodiscard]] const std::string& GetName() const noexcept { return _name; }
	[[nodiscard]] uint32_t GetDuration() const noexcept { return _duration; }
	[[nodiscard]] const std::vector<Frame>& GetFrames() const noexcept { return _frames; }
	[[nodiscard]] std::vector<glm::mat4> GetBoneMatrices(uint32_t time) const noexcept;
	/// How far one play of the animation carries its owner: the header's float at 0x28 (walk 1.16, man's
	/// run 2.46, 0 for animations done on the spot). Unconfirmed from the original's code; inferred from
	/// the values across the locomotion animations.
	[[nodiscard]] float GetDistancePerPlay() const noexcept { return _unknown_0x28; }
	/// The header's words at 0x20 and 0x50 (bw1-decomp's LH3DAnim: 0x50 holds the flags, cyclic 0x100 and time-based
	/// 0x200 - a walk not time-based is played by the distance walked), for inspection.
	[[nodiscard]] uint32_t GetHeaderWord20() const noexcept { return _unknown_0x20; }
	[[nodiscard]] uint32_t GetHeaderWord50() const noexcept { return _unknown_0x50; }
	/// The header's undecoded floats at 0x24..0x34, for inspection.
	[[nodiscard]] std::array<float, 5> GetHeaderFloats() const noexcept
	{
		return {_unknown_0x24, _unknown_0x28, _unknown_0x2C, _unknown_0x30, _unknown_0x34};
	}

private:
	std::string _name;
	uint32_t _unknown_0x20; // TODO(#471): Seems to be a uint16_t padded
	float _unknown_0x24;    // TODO(#471)
	float _unknown_0x28;    // TODO(#471)
	float _unknown_0x2C;    // TODO(#471)
	float _unknown_0x30;    // TODO(#471)
	float _unknown_0x34;    // TODO(#471)
	uint32_t _unknown_0x3C; // TODO(#471): Always 1 in Body Block, a count
	uint32_t _duration;
	uint32_t _unknown_0x44; // TODO(#471): Always 1 in Body Block
	uint32_t _unknown_0x48; // TODO(#471): Always 0 in Body Block
	uint32_t _unknown_0x50; // TODO(#471): Seems to be a uint16_t padded

	std::vector<Frame> _frames;

	friend debug::gui::MeshViewer; // TODO(#471): Remove me once the unknowns are known and replace with getters
};

} // namespace openblack
