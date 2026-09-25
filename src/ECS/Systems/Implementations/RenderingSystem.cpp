/******************************************************************************
 * Copyright (c) 2018-2026 openblack developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/openblack/openblack
 *
 * openblack is licensed under the GNU General Public License version 3.
 *******************************************************************************/

#define LOCATOR_IMPLEMENTATIONS

#include "RenderingSystem.h"

#include <algorithm>
#include <limits>
#include <unordered_map>

#include <glm/gtx/transform.hpp>

#include "3D/L3DAnim.h"
#include "3D/L3DMesh.h"
#include "ECS/Components/Mesh.h"
#include "ECS/Components/MorphWithTerrain.h"
#include "ECS/Components/Stream.h"
#include "ECS/Components/Temple.h"
#include "ECS/Components/Transform.h"
#include "ECS/Registry.h"
#include "Graphics/DebugLines.h"
#include "Graphics/GraphicsHandleBgfx.h"
#include "Graphics/ShaderManager.h"
#include "Locator.h"
#include "Resources/ResourceManager.h"
#include "Resources/ResourcesInterface.h"

using namespace openblack::ecs::systems;
using namespace openblack::ecs::components;

RenderingSystem::~RenderingSystem() = default;

void RenderingSystem::PrepareDrawDescs(bool drawBoundingBox)
{
	auto& registry = Locator::entitiesRegistry::value();

	// Count number of instances
	uint32_t instanceCount = 0;
	std::unordered_map<entt::id_type, std::pair<uint32_t, bool>> meshIds;

	auto prep = [&meshIds, &instanceCount](const Mesh& mesh, bool morphWithTerrain) {
		auto count = meshIds.insert(std::make_pair(mesh.id, std::make_pair(mesh.submeshId, morphWithTerrain)));
		count.first->second.first++;
		instanceCount++;
	};

	registry.Each<const Mesh, const Transform>([&prep](const Mesh& mesh, const Transform& /*unused*/) { prep(mesh, false); },
	                                           entt::exclude<MorphWithTerrain, TempleInteriorPart, Animated>);
	uint32_t animatedCount = 0;
	registry.Each<const Mesh, const Transform, const Animated>(
	    [&animatedCount](const Mesh&, const Transform&, const Animated&) { ++animatedCount; },
	    entt::exclude<TempleInteriorPart>);
	instanceCount += animatedCount;
	registry.Each<const Mesh, const Transform, const MorphWithTerrain>(
	    [&prep](const Mesh& mesh, const Transform& /*unused*/, const MorphWithTerrain& /*unused*/) { prep(mesh, true); },
	    entt::exclude<Animated>);

	if (drawBoundingBox)
	{
		instanceCount *= 2;
	}

	// Recreate instancing uniform buffer if it is too small
	if (_renderContext.instanceUniforms.size() < instanceCount)
	{
		if (bgfx::isValid(toBgfx(_renderContext.instanceUniformBuffer)))
		{
			bgfx::destroy(toBgfx(_renderContext.instanceUniformBuffer));
		}
		bgfx::VertexLayout layout;
		layout.begin()
		    .add(bgfx::Attrib::TexCoord7, 4, bgfx::AttribType::Float)
		    .add(bgfx::Attrib::TexCoord6, 4, bgfx::AttribType::Float)
		    .add(bgfx::Attrib::TexCoord5, 4, bgfx::AttribType::Float)
		    .add(bgfx::Attrib::TexCoord4, 4, bgfx::AttribType::Float)
		    .end();
		_renderContext.instanceUniformBuffer = graphics::fromBgfx(bgfx::createDynamicVertexBuffer(instanceCount, layout));
		_renderContext.instanceUniforms.resize(instanceCount);
	}

	// Determine uniform buffer offsets and instance count for draw
	uint32_t offset = 0;
	_renderContext.instancedDrawDescs.clear();
	for (const auto& [meshId, desc] : meshIds)
	{
		_renderContext.instancedDrawDescs.emplace(std::piecewise_construct, std::forward_as_tuple(meshId),
		                                          std::forward_as_tuple(offset, desc.first, desc.second));
		offset += desc.first;
	}
	_renderContext.animatedOffset = offset; // animated entities' uniforms follow the instanced ones
}

void RenderingSystem::PrepareDrawUploadUniforms(bool drawBoundingBox)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto& meshes = entt::locator<resources::ResourcesInterface>::value().GetMeshes();
	const auto& animations = entt::locator<resources::ResourcesInterface>::value().GetAnimations();
	constexpr auto k_BonesPerRow = RenderContext::k_BonesPerRow;
	constexpr auto k_BoneRows = RenderContext::k_BoneRows;

	// Bone texture: created once, row 0 identity for unskinned meshes (see RenderContext::boneTexture).
	if (!bgfx::isValid(toBgfx(_renderContext.boneTexture)))
	{
		_renderContext.boneTexture = graphics::fromBgfx(
		    bgfx::createTexture2D(k_BonesPerRow * 4, k_BoneRows, false, 1, bgfx::TextureFormat::RGBA32F,
		                          BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP));
		_renderContext.boneData.assign(static_cast<size_t>(k_BonesPerRow) * k_BoneRows, glm::mat4(1.0f));
	}
	uint32_t nextRow = 1;
	uint32_t widestPose = 1;
	// A row holding these bones; 0 (identity) when the texture is full or there are none.
	auto storeRow = [this, &nextRow, &widestPose](const std::vector<glm::mat4>& bones) -> uint32_t {
		if (bones.empty() || nextRow >= k_BoneRows)
		{
			return 0;
		}
		const auto count = std::min<size_t>(bones.size(), k_BonesPerRow);
		std::copy_n(bones.begin(), count, _renderContext.boneData.begin() + static_cast<size_t>(nextRow) * k_BonesPerRow);
		widestPose = std::max(widestPose, static_cast<uint32_t>(count));
		return nextRow++;
	};
	std::unordered_map<entt::id_type, uint32_t> defaultPoseRows;
	auto defaultPoseRow = [&](entt::id_type meshId) -> uint32_t {
		if (const auto it = defaultPoseRows.find(meshId); it != defaultPoseRows.end())
		{
			return it->second;
		}
		const auto l3dMesh = meshes.Handle(meshId);
		const auto row = l3dMesh->IsBoned() ? storeRow(l3dMesh->GetBoneMatrices()) : 0u;
		defaultPoseRows.emplace(meshId, row);
		return row;
	};

	// Store offsets of uniforms for descs
	std::map<entt::id_type, uint32_t> uniformOffsets;

	// Set transforms for instanced draw at offsets
	registry.Each<const Mesh, const Transform>(
	    [this, &uniformOffsets, drawBoundingBox, &defaultPoseRow](const Mesh& mesh, const Transform& transform) {
		    auto offset = uniformOffsets.insert(std::make_pair(mesh.id, 0));
		    auto desc = _renderContext.instancedDrawDescs.find(mesh.id);

		    auto modelMatrix = glm::mat4(transform.rotation);
		    modelMatrix = glm::translate(modelMatrix, transform.position * transform.rotation);
		    modelMatrix = glm::scale(modelMatrix, transform.scale);

		    const uint32_t idx = desc->second.offset + offset.first->second;
		    if (drawBoundingBox)
		    {
			    auto l3dMesh = entt::locator<resources::ResourcesInterface>::value().GetMeshes().Handle(mesh.id);
			    auto box = l3dMesh->GetBoundingBox();
			    auto boxMatrix = modelMatrix * glm::translate(box.Center()) * glm::scale(box.Size());
			    _renderContext.instanceUniforms[idx + _renderContext.instanceUniforms.size() / 2] = boxMatrix;
		    }
		    modelMatrix[0][3] = static_cast<float>(defaultPoseRow(mesh.id)); // bone texture row
		    _renderContext.instanceUniforms[idx] = modelMatrix;
		    offset.first->second++;
	    },
	    entt::exclude<TempleInteriorPart, Animated>);

	// Animated entities: each posed from its animation's frame. The animation gives each bone relative to its
	// parent; the mesh wants them composed down the hierarchy (as the mesh viewer does).
	_renderContext.animatedDrawDescs.clear();
	uint32_t animatedIndex = _renderContext.animatedOffset;
	std::vector<glm::mat4> bones;
	registry.Each<const Mesh, const Transform, const Animated>(
	    [&](const Mesh& mesh, const Transform& transform, const Animated& animated) {
		    auto modelMatrix = glm::mat4(transform.rotation);
		    modelMatrix = glm::translate(modelMatrix, transform.position * transform.rotation);
		    modelMatrix = glm::scale(modelMatrix, transform.scale);
		    const uint32_t idx = animatedIndex++;
		    const auto l3dMesh = meshes.Handle(mesh.id);
		    if (drawBoundingBox)
		    {
			    auto box = l3dMesh->GetBoundingBox();
			    _renderContext.instanceUniforms[idx + _renderContext.instanceUniforms.size() / 2] =
			        modelMatrix * glm::translate(box.Center()) * glm::scale(box.Size());
		    }
		    uint32_t row = 0;
		    if (l3dMesh->IsBoned())
		    {
			    bones = l3dMesh->GetBoneMatrices();
			    const auto animationId = resources::HashIdentifier(animated.animation);
			    if (!animated.localPose.empty() || animations.Contains(animationId))
			    {
				    const auto posed = !animated.localPose.empty()
				                           ? animated.localPose
				                           : animations.Handle(animationId)->GetBoneMatrices(static_cast<uint32_t>(animated.time));
				    const auto& parents = l3dMesh->GetBoneParents();
				    if (posed.size() == bones.size())
				    {
					    for (size_t i = 0; i < posed.size(); ++i)
					    {
						    bones[i] = parents[i] != std::numeric_limits<uint32_t>::max() ? bones[parents[i]] * posed[i]
						                                                                   : posed[i];
					    }
				    }
			    }
			    row = storeRow(bones);
		    }
		    modelMatrix[0][3] = static_cast<float>(row);
		    _renderContext.instanceUniforms[idx] = modelMatrix;
		    _renderContext.animatedDrawDescs.push_back({mesh.id, idx, animated.animation, animated.time});
	    },
	    entt::exclude<TempleInteriorPart>);

	// Upload the rows used this frame, only as wide as the widest pose.
	_renderContext.boneRowsUsed = nextRow;
	const auto rowBytes = static_cast<uint32_t>(k_BonesPerRow * sizeof(glm::mat4));
	bgfx::updateTexture2D(toBgfx(_renderContext.boneTexture), 0, 0, 0, 0, static_cast<uint16_t>(widestPose * 4),
	                      static_cast<uint16_t>(nextRow),
	                      bgfx::copy(_renderContext.boneData.data(), rowBytes * nextRow), static_cast<uint16_t>(rowBytes));

	if (!_renderContext.instanceUniforms.empty())
	{
		const auto size = static_cast<uint32_t>(_renderContext.instanceUniforms.size() * sizeof(glm::mat4));
		bgfx::update(toBgfx(_renderContext.instanceUniformBuffer), 0,
		             bgfx::makeRef(_renderContext.instanceUniforms.data(), size));
	}
}
