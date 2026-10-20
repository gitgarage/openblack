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
bool Load(const std::filesystem::path& path, std::string_view segmentName, Poses& poses, int variant)
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
		// The file keeps a pose only for some of the spec's names, in the spec's order. A variant set (the original's
		// AnimSets 1-4, for the evil/good and thin/fat looks) is taken to keep the same ones as the base.
		const auto& animations = variant < 0 ? file.GetBaseAnimationSet() : file.GetVariantAnimationSet(static_cast<uint32_t>(variant));
		if (variant < 0)
		{
			for (uint32_t v = 0; v < 4; ++v)
			{
				SPDLOG_LOGGER_INFO(spdlog::get("game"), "Poses in {}: variant {} ({}) has {} animations (base {})",
				                   path.filename().string(), v, file.GetHeader().variantMeshNames[v].data(),
				                   file.GetVariantAnimationSet(v).size(), animations.size());
			}
		}
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
namespace
{
// LHMatrix::SetYXZMatrixOnly(a, b, c) (BW1W120 007fac10), in the original's row-vector layout, which is glm's
// memory order: m[r] is the original's row r. FillBuffer calls it with (y, x, z) of a frame's angles.
glm::mat3 SetYXZMatrixOnly(float a, float b, float c)
{
	const float ca = std::cos(a), sa = std::sin(a);
	const float cb = std::cos(b), sb = std::sin(b);
	const float cc = std::cos(c), sc = std::sin(c);
	glm::mat3 m;
	m[0] = {cc * ca - sc * sb * sa, -sc * cb, cc * sa + sc * sb * ca};
	m[1] = {sc * ca + cc * sb * sa, cc * cb, sc * sa - cc * sb * ca};
	m[2] = {-cb * sa, sb, cb * ca};
	return m;
}

glm::mat3 FrameTurn(const std::array<float, 3>& angles)
{
	return SetYXZMatrixOnly(angles[1], angles[0], angles[2]);
}

// Where a bone sits in a pose's list of turned (or moved) bones.
std::optional<size_t> Slot(const std::vector<uint32_t>& indices, size_t bone)
{
	const auto it = std::find(indices.begin(), indices.end(), static_cast<uint32_t>(bone));
	return it == indices.end() ? std::nullopt : std::optional<size_t>(static_cast<size_t>(it - indices.begin()));
}
} // namespace

std::vector<glm::mat4> CreatureBones(const graphics::L3DMesh& mesh, const morph::Animation& pose, float frame,
                                     const morph::Animation* stand)
{
	if (pose.keyframes.empty())
	{
		return {};
	}
	const auto& base = mesh.GetBoneMatrices(); // the original's base matrices: rest, down the hierarchy
	const auto& parents = mesh.GetBoneParents();
	const auto count = pose.keyframes.size();
	// A cyclic pose (header 0x4) runs from its last frame back into its first.
	const bool cyclic = pose.header.unknown0x4 != 0;
	const float last = static_cast<float>(count - 1);
	const float at = cyclic ? std::fmod(std::max(frame, 0.0f), static_cast<float>(count)) : glm::clamp(frame, 0.0f, last);
	const auto a = std::min(static_cast<size_t>(at), count - 1);
	const auto b = a + 1 < count ? a + 1 : (cyclic ? 0 : a);
	const float t = at - static_cast<float>(a);
	const bool standing = stand != nullptr && !stand->keyframes.empty();

	std::vector<glm::mat4> local(base.size());
	for (size_t i = 0; i < base.size(); ++i)
	{
		const bool root = parents[i] == std::numeric_limits<uint32_t>::max();
		// At rest, relative to its parent: the translation stays unless the pose (or standing) moves it.
		local[i] = root ? base[i] : glm::inverse(base[parents[i]]) * base[i];
		std::optional<glm::mat3> turn;
		if (const auto j = Slot(pose.rotatedJointIndices, i))
		{
			// Each frame's turn made whole, the two blended entry by entry, then each row brought back to unit
			// length (LHMatrix::NormaliseMatrixOnly).
			const auto ma = FrameTurn(pose.keyframes[a].eulerAngles[*j]);
			const auto mb = FrameTurn(pose.keyframes[b].eulerAngles[*j]);
			glm::mat3 m;
			for (int r = 0; r < 3; ++r)
			{
				m[r] = glm::normalize(glm::mix(ma[r], mb[r], t));
			}
			turn = m;
		}
		else if (standing)
		{
			if (const auto k = Slot(stand->rotatedJointIndices, i))
			{
				turn = FrameTurn(stand->keyframes[0].eulerAngles[*k]);
			}
		}
		if (turn)
		{
			// The original's Base[i] x turn x Base[parent]^-1 (row vectors), its 3x3 only: the turn is about
			// the model's axes, not the bone's own.
			const glm::mat3 rotation = root ? *turn * glm::mat3(base[i])
			                                : glm::mat3(glm::inverse(base[parents[i]])) * *turn * glm::mat3(base[i]);
			const glm::vec4 position = local[i][3];
			local[i] = glm::mat4(rotation);
			local[i][3] = position;
		}
		if (const auto j = Slot(pose.translatedJointIndices, i))
		{
			const auto& pa = pose.keyframes[a].translations[*j];
			const auto& pb = pose.keyframes[b].translations[*j];
			local[i][3] = glm::vec4(glm::mix(glm::vec3(pa[0], pa[1], pa[2]), glm::vec3(pb[0], pb[1], pb[2]), t), 1.0f);
		}
		else if (standing)
		{
			if (const auto k = Slot(stand->translatedJointIndices, i))
			{
				const auto& p = stand->keyframes[0].translations[*k];
				local[i][3] = glm::vec4(p[0], p[1], p[2], 1.0f);
			}
		}
	}
	return local;
}

void AddDifference(const graphics::L3DMesh& mesh, std::vector<glm::mat4>& local, const morph::Animation& layer, float frame,
                   size_t reference)
{
	if (layer.keyframes.empty() || local.size() != mesh.GetBoneMatrices().size())
	{
		return;
	}
	const auto& base = mesh.GetBoneMatrices();
	const auto& parents = mesh.GetBoneParents();
	const auto count = layer.keyframes.size();
	reference = std::min(reference, count - 1);
	const float at = glm::clamp(frame, 0.0f, static_cast<float>(count - 1));
	const auto a = std::min(static_cast<size_t>(at), count - 1);
	const auto b = std::min(a + 1, count - 1);
	const float t = at - static_cast<float>(a);
	for (size_t j = 0; j < layer.rotatedJointIndices.size(); ++j)
	{
		const auto i = layer.rotatedJointIndices[j];
		if (i >= local.size())
		{
			continue;
		}
		const auto ma = FrameTurn(layer.keyframes[a].eulerAngles[j]);
		const auto mb = FrameTurn(layer.keyframes[b].eulerAngles[j]);
		glm::mat3 m;
		for (int r = 0; r < 3; ++r)
		{
			m[r] = glm::normalize(glm::mix(ma[r], mb[r], t));
		}
		const auto undo = glm::transpose(FrameTurn(layer.keyframes[reference].eulerAngles[j])); // its turn at the reference, undone
		// The original's L x Base[parent] x reference^-1 x turn x Base[parent]^-1 (row vectors), its 3x3 only.
		const bool root = parents[i] == std::numeric_limits<uint32_t>::max();
		const glm::mat3 rotation = root ? m * undo * glm::mat3(local[i])
		                                : glm::mat3(glm::inverse(base[parents[i]])) * m * undo * glm::mat3(base[parents[i]]) *
		                                      glm::mat3(local[i]);
		const glm::vec4 position = local[i][3];
		local[i] = glm::mat4(rotation);
		local[i][3] = position;
	}
	for (size_t j = 0; j < layer.translatedJointIndices.size(); ++j)
	{
		const auto i = layer.translatedJointIndices[j];
		if (i >= local.size())
		{
			continue;
		}
		const auto& pa = layer.keyframes[a].translations[j];
		const auto& pb = layer.keyframes[b].translations[j];
		const auto& pr = layer.keyframes[reference].translations[j];
		const auto moved = glm::mix(glm::vec3(pa[0], pa[1], pa[2]), glm::vec3(pb[0], pb[1], pb[2]), t) - glm::vec3(pr[0], pr[1], pr[2]);
		local[i][3] += glm::vec4(moved, 0.0f);
	}
}

std::vector<glm::mat4> BlendedCreatureBones(const graphics::L3DMesh& mesh, const std::vector<Layer>& layers,
                                            const morph::Animation* stand)
{
	const auto& parents = mesh.GetBoneParents();
	std::vector<glm::mat4> blended;
	for (const auto& layer : layers)
	{
		if (layer.pose == nullptr || layer.weight <= 0.0f)
		{
			continue;
		}
		auto model = CreatureBones(mesh, *layer.pose, layer.frame, stand);
		if (model.size() != parents.size())
		{
			continue;
		}
		for (size_t i = 0; i < model.size(); ++i) // LH3DAnim::FinishTransform, parents first
		{
			if (parents[i] != std::numeric_limits<uint32_t>::max())
			{
				model[i] = model[parents[i]] * model[i];
			}
		}
		if (blended.empty())
		{
			blended.assign(model.size(), glm::mat4(0.0f));
		}
		for (size_t i = 0; i < model.size(); ++i) // every entry by its weight (fn_004EECD0), none made unit again
		{
			blended[i] += model[i] * layer.weight;
		}
	}
	if (blended.empty())
	{
		return {};
	}
	for (auto& matrix : blended)
	{
		matrix[0][3] = matrix[1][3] = matrix[2][3] = 0.0f;
		matrix[3][3] = 1.0f;
	}
	// Back to each relative to its parent, which is what is drawn (LH3DAnim::UnFinishTransform).
	std::vector<glm::mat4> local(blended.size());
	for (size_t i = 0; i < blended.size(); ++i)
	{
		local[i] = parents[i] != std::numeric_limits<uint32_t>::max() ? glm::inverse(blended[parents[i]]) * blended[i] : blended[i];
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
	if (it == g_poses.end())
	{
		return {};
	}
	// Posed as the original's CHand is, through CAnim::FillBuffer (morphposes::CreatureBones); its root keeps its
	// rest position, so the hand stays at the pointer with what it holds hanging under it.
	auto local = morphposes::CreatureBones(mesh, it->second, frame, nullptr);
	if (!local.empty())
	{
		local[0][3] = mesh.GetBoneMatrices()[0][3];
	}
	return local;
}

std::vector<glm::mat4> LeaningBones(const graphics::L3DMesh& mesh, std::string_view name, float frame, glm::vec2 lean)
{
	auto local = LocalBones(mesh, name, frame);
	if (local.empty())
	{
		return local;
	}
	// Each layer is played to the point its lean gives - (80 + lean) / 160 of the way through - and added as its
	// difference from its middle frame, so no lean adds nothing. Under 0.0001 the layer is left out altogether.
	constexpr float k_MaxLean = 80.0f;
	const auto add = [&](const char* suffix, float amount) {
		const auto it = g_poses.find(std::string(name) + suffix);
		if (it == g_poses.end() || it->second.keyframes.empty() || std::abs(amount) <= 0.0001f)
		{
			return;
		}
		const auto& layer = it->second;
		const float along = (k_MaxLean + glm::clamp(amount, -k_MaxLean, k_MaxLean)) / (2.0f * k_MaxLean);
		morphposes::AddDifference(mesh, local, layer, along * static_cast<float>(layer.keyframes.size() - 1),
		                          (layer.keyframes.size() - 1) / 2);
	};
	add("_lr", lean.x);
	add("_fb", lean.y);
	// The root keeps its rest position (see LocalBones).
	local[0][3] = mesh.GetBoneMatrices()[0][3];
	return local;
}

float FrameAt(std::string_view name, float seconds)
{
	const auto it = g_poses.find(name);
	if (it == g_poses.end() || it->second.keyframes.empty())
	{
		return 0.0f;
	}
	const auto& pose = it->second;
	const auto count = static_cast<float>(pose.keyframes.size());
	const float durationSeconds = static_cast<float>(pose.header.unknown0x0) / 1000.0f;
	if (pose.header.unknown0x4 != 0) // cyclic: all its frames over its length
	{
		return durationSeconds > 0.0f ? std::fmod(seconds / durationSeconds * count, count) : 0.0f;
	}
	return durationSeconds > 0.0f ? std::min(seconds / durationSeconds * (count - 1.0f), count - 1.0f) : count - 1.0f;
}
} // namespace openblack::handposes
