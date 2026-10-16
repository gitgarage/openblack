/******************************************************************************
 * Copyright (c) 2018-2026 openblack developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/openblack/openblack
 *
 * openblack is licensed under the GNU General Public License version 3.
 *******************************************************************************/

#include "HandPoses.h"

#include <cstring>
#include <limits>
#include <map>
#include <string>

#include <MorphFile.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <spdlog/spdlog.h>

#include "3D/L3DMesh.h"
#include "FileSystem/FileSystemInterface.h"
#include "Locator.h"

namespace openblack::handposes
{
namespace
{
morphposes::Poses g_poses;
} // namespace

// The segment named `name` of a Lionhead segmented file ("LiOnHeAd", then name[32], size, data...).
std::vector<uint8_t> Segment(const std::vector<uint8_t>& file, std::string_view name)
{
	size_t at = 8;
	while (at + 36 <= file.size())
	{
		const std::string_view segment(reinterpret_cast<const char*>(file.data() + at),
		                               strnlen(reinterpret_cast<const char*>(file.data() + at), 32));
		uint32_t size = 0;
		std::memcpy(&size, file.data() + at + 32, sizeof(size));
		if (at + 36 + size > file.size())
		{
			break;
		}
		if (segment == name)
		{
			return {file.begin() + static_cast<std::ptrdiff_t>(at + 36),
			        file.begin() + static_cast<std::ptrdiff_t>(at + 36 + size)};
		}
		at += 36 + size;
	}
	return {};
}
} // namespace openblack::handposes

namespace openblack::morphposes
{
bool Load(const std::filesystem::path& path, std::string_view segmentName, Poses& poses)
{
	auto& fileSystem = Locator::filesystem::value();
	try
	{
		const auto segment = handposes::Segment(fileSystem.ReadAll(path), segmentName);
		morph::MorphFile file;
		const auto result = file.Open(segment, fileSystem.GetPath<filesystem::Path::Data>(true));
		if (result != morph::MorphResult::Success)
		{
			SPDLOG_LOGGER_ERROR(spdlog::get("game"), "Poses in {}: {}", path.filename().string(), morph::ResultToStr(result));
			return false;
		}
		// The file keeps a pose only for some of the spec's names, in the spec's order.
		const auto& animations = file.GetBaseAnimationSet();
		size_t next = 0;
		std::vector<std::string> names;
		for (const auto& set : file.GetAnimationSpecs().animationSets)
		{
			for (const auto& animation : set.animations)
			{
				names.push_back(animation.name);
			}
		}
		// Which names are kept is the non-zero offsets straight after the header.
		std::vector<uint32_t> offsets(names.size());
		std::memcpy(offsets.data(), segment.data() + sizeof(morph::MorphHeader),
		            std::min(segment.size() - sizeof(morph::MorphHeader), offsets.size() * sizeof(uint32_t)));
		for (size_t i = 0; i < names.size() && next < animations.size(); ++i)
		{
			if (offsets[i] != 0)
			{
				poses.emplace(names[i], animations[next++]);
			}
		}
		SPDLOG_LOGGER_INFO(spdlog::get("game"), "Poses in {}: {} loaded", path.filename().string(), poses.size());
		return true;
	}
	catch (const std::exception& e)
	{
		SPDLOG_LOGGER_ERROR(spdlog::get("game"), "Poses in {}: {}", path.filename().string(), e.what());
		return false;
	}
}

std::vector<glm::mat4> LocalBones(const graphics::L3DMesh& mesh, const morph::Animation& pose, float frame, bool rootMoves)
{
	if (pose.keyframes.empty())
	{
		return {};
	}
	const auto& composed = mesh.GetBoneMatrices();
	const auto& parents = mesh.GetBoneParents();
	std::vector<glm::mat4> local(composed.size());
	std::vector<glm::vec3> euler(composed.size(), glm::vec3(0.0f));
	std::vector<glm::vec3> position(composed.size());
	for (size_t i = 0; i < composed.size(); ++i)
	{
		local[i] = parents[i] != std::numeric_limits<uint32_t>::max() ? glm::inverse(composed[parents[i]]) * composed[i]
		                                                               : composed[i];
		position[i] = glm::vec3(local[i][3]);
	}

	const float last = static_cast<float>(pose.keyframes.size() - 1);
	const float clamped = glm::clamp(frame, 0.0f, last);
	const auto a = static_cast<size_t>(clamped);
	const auto b = std::min(a + 1, pose.keyframes.size() - 1);
	const float t = clamped - static_cast<float>(a);
	for (size_t j = 0; j < pose.rotatedJointIndices.size(); ++j)
	{
		const auto bone = pose.rotatedJointIndices[j];
		if (bone < euler.size())
		{
			const auto& ea = pose.keyframes[a].eulerAngles[j];
			const auto& eb = pose.keyframes[b].eulerAngles[j];
			euler[bone] = glm::mix(glm::vec3(ea[0], ea[1], ea[2]), glm::vec3(eb[0], eb[1], eb[2]), t);
		}
	}
	for (size_t j = 0; j < pose.translatedJointIndices.size(); ++j)
	{
		const auto bone = pose.translatedJointIndices[j];
		// The hand's root stays put: its translation lifts the whole hand clear of what it holds, and here the
		// hand stays at the pointer with the held thing hanging under it.
		if (bone < position.size() && (rootMoves || parents[bone] != std::numeric_limits<uint32_t>::max()))
		{
			const auto& pa = pose.keyframes[a].translations[j];
			const auto& pb = pose.keyframes[b].translations[j];
			position[bone] = glm::mix(glm::vec3(pa[0], pa[1], pa[2]), glm::vec3(pb[0], pb[1], pb[2]), t);
		}
	}
	for (size_t i = 0; i < local.size(); ++i)
	{
		// The file's angles are the original's row-vector (DirectX) rotation: transposed for glm, turning each
		// bone from its rest orientation ("point" then points its index finger with the others curled in).
		const auto turn = glm::transpose(glm::eulerAngleXYZ(euler[i].x, euler[i].y, euler[i].z));
		local[i] = glm::translate(glm::mat4(1.0f), position[i]) * glm::mat4(glm::mat3(local[i])) * turn;
	}
	return local;
}
} // namespace openblack::morphposes

namespace openblack::handposes
{
bool Load()
{
	const auto path = Locator::filesystem::value().GetPath<filesystem::Path::Data>() / "CTR" / "hh.HBN";
	return morphposes::Load(path, "Hand", g_poses);
}

int FrameCount(std::string_view name)
{
	const auto it = g_poses.find(name);
	return it == g_poses.end() ? 0 : static_cast<int>(it->second.keyframes.size());
}

std::vector<glm::mat4> LocalBones(const graphics::L3DMesh& mesh, std::string_view name, float frame)
{
	const auto it = g_poses.find(name);
	return it == g_poses.end() ? std::vector<glm::mat4> {} : morphposes::LocalBones(mesh, it->second, frame, false);
}
} // namespace openblack::handposes
