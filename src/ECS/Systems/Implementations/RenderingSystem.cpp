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
#include <chrono>
#include <array>
#include <limits>
#include <unordered_map>
#include <unordered_set>

#include <glm/gtx/transform.hpp>
#include <spdlog/spdlog.h>

#include "3D/L3DAnim.h"
#include "3D/L3DMesh.h"
#include "ECS/Components/Construction.h"
#include "ECS/Components/ShownAsShadow.h"
#include "ECS/Components/Asleep.h"
#include "ECS/Components/Feature.h"
#include "Graphics/Renderer.h"
#include "ECS/Components/Mesh.h"
#include "ECS/Components/MorphWithTerrain.h"
#include "ECS/Components/Stream.h"
#include "ECS/Components/Temple.h"
#include "ECS/Components/Transform.h"
#include "ECS/Registry.h"
#include "3D/LandIslandInterface.h"
#include "Graphics/DebugLines.h"
#include "Graphics/GraphicsHandleBgfx.h"
#include "Graphics/ShaderManager.h"
#include "Camera/Camera.h"
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
	                                           entt::exclude<MorphWithTerrain, TempleInteriorPart, Animated, Construction>);
	uint32_t constructionCount = 0;
	registry.Each<const Mesh, const Transform, const Construction>(
	    [&constructionCount](const Mesh&, const Transform&, const Construction&) { constructionCount += 2; },
	    entt::exclude<Animated>);
	instanceCount += constructionCount;
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

	// Whether a thing shows in the water: the sea (land under height 0) within its height above the sea of where it
	// stands (Mike, 2026-10-17: reflect only what is near the water and tall enough to be in the reflection). Kept
	// per thing, looked at again once it has moved 2 or more.
	_renderContext.reflectedDrawDescs.clear();
	static std::unordered_map<entt::entity, std::pair<glm::vec3, bool>> s_reflects;
	auto reflects = [&meshes](entt::entity entity, const Mesh& mesh, const Transform& transform) {
		auto& cached = s_reflects[entity];
		if (glm::distance(cached.first, transform.position) < 2.0f && cached.first != glm::vec3(0.0f))
		{
			return cached.second;
		}
		cached.first = transform.position;
		const auto& terrain = Locator::terrainSystem::value();
		const float top = transform.position.y + meshes.Handle(mesh.id)->GetBoundingBox().maxima.y * transform.scale.y;
		bool near = false;
		if (top > 0.5f)
		{
			const auto at = glm::vec2(transform.position.x, transform.position.z);
			near = terrain.GetHeightAt(at) < 0.0f;
			const float reach = std::min(top, 80.0f);
			const float step = std::max(reach / 6.0f, 2.0f);
			for (float r = step; !near && r <= reach; r += step)
			{
				for (int d = 0; d < 8 && !near; ++d)
				{
					const float a = static_cast<float>(d) * glm::quarter_pi<float>();
					near = terrain.GetHeightAt(at + glm::vec2(std::cos(a), std::sin(a)) * r) < 0.0f;
				}
			}
		}
		cached.second = near;
		return near;
	};

	// What is drawn: only what is in the camera's view; cow-sized and smaller only while big enough there to see
	// (under ~3 pixels across it is left out). Mike, 2026-10-18: draw nothing far off or off camera. A bounding sphere each, tested against the four
	// sides of the view (Gribb-Hartmann planes from view x projection) and by its size over its distance.
	const auto& camera = Locator::camera::value();
	const auto viewProjection = camera.GetViewProjectionMatrix();
	std::array<glm::vec4, 4> sides;
	for (int i = 0; i < 2; ++i)
	{
		const glm::vec4 row(viewProjection[0][i], viewProjection[1][i], viewProjection[2][i], viewProjection[3][i]);
		const glm::vec4 w(viewProjection[0][3], viewProjection[1][3], viewProjection[2][3], viewProjection[3][3]);
		sides.at(static_cast<size_t>(i * 2)) = w + row;
		sides.at(static_cast<size_t>(i * 2 + 1)) = w - row;
	}
	for (auto& side : sides)
	{
		side /= std::max(glm::length(glm::vec3(side)), 1e-6f);
	}
	const auto eye = camera.GetOrigin();
	constexpr float k_SmallestSeen = 0.0025f; // radius over distance: about 3 pixels across at 720 lines
	auto inView = [&](entt::id_type meshId, const Transform& transform) {
		if (!meshes.Contains(meshId))
		{
			return true;
		}
		const auto box = meshes.Handle(meshId)->GetBoundingBox();
		const float scale = std::max({transform.scale.x, transform.scale.y, transform.scale.z});
		const auto middle = transform.position + transform.rotation * (box.Center() * transform.scale);
		const float radius = glm::length(box.Size()) * 0.5f * scale;
		for (const auto& side : sides)
		{
			if (glm::dot(glm::vec3(side), middle) + side.w < -radius)
			{
				return false; // wholly off one side of the view
			}
		}
		// Bigger than a cow (a large rock, a house, a tree), it is drawn however far off, while in view; cows, people and
		// smaller drop out once too small to see (Mike, 2026-10-18). The cow measured from its own mesh.
		static const float s_cowRadius = [&meshes]() {
			const auto cow = resources::HashIdentifier(MeshId::AnimalCow1);
			return meshes.Contains(cow) ? glm::length(meshes.Handle(cow)->GetBoundingBox().Size()) * 0.5f : 2.0f;
		}();
		// Never too small: what the player looks for - scrolls (the miniscrolls, gold, silver, bronze), the miracle
		// bubble and its icon models (Mike, 2026-10-18: the gold scroll was gone).
		static const std::unordered_set<entt::id_type> s_alwaysSeen = []() {
			std::unordered_set<entt::id_type> ids;
			for (int m = static_cast<int>(MeshId::I_Blast); m <= static_cast<int>(MeshId::I_Wall); ++m)
			{
				ids.insert(resources::HashIdentifier(static_cast<MeshId>(m)));
			}
			for (int m = static_cast<int>(MeshId::SpellHealMesh); m <= static_cast<int>(MeshId::SpellPhileSmall); ++m)
			{
				ids.insert(resources::HashIdentifier(static_cast<MeshId>(m)));
			}
			ids.insert(entt::hashed_string("spell/seed").value());
			return ids;
		}();
		return radius > s_cowRadius * 1.05f || s_alwaysSeen.contains(meshId) || radius >= k_SmallestSeen * glm::distance(eye, middle);
	};

	// Set transforms for instanced draw at offsets
	registry.Each<const Mesh, const Transform>(
	    [this, &registry, &uniformOffsets, drawBoundingBox, &defaultPoseRow, &reflects, &inView](entt::entity entity, const Mesh& mesh,
	                                                                                            const Transform& transform) {
		    // Far-off trees are drawn as just their shadows (ShownAsShadow, set by CHLApi's shadows).
		    if (!inView(mesh.id, transform) || registry.AllOf<ShownAsShadow>(entity))
		    {
			    return;
		    }
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
		    if (reflects(entity, mesh, transform))
		    {
			    _renderContext.reflectedDrawDescs.push_back({mesh.id, idx, false, desc->second.morphWithTerrain});
		    }
	    },
	    entt::exclude<TempleInteriorPart, Animated, Construction>);
	// Each mesh's draw: as many as are in view, from the start of its slots.
	uint32_t drawn = 0;
	uint32_t slots = 0;
	for (auto& [meshId, desc] : _renderContext.instancedDrawDescs)
	{
		slots += desc.count;
		const auto counted = uniformOffsets.find(meshId);
		desc.count = counted != uniformOffsets.end() ? counted->second : 0;
		drawn += desc.count;
	}
	{
		static auto s_said = std::chrono::steady_clock::now();
		if (std::chrono::steady_clock::now() - s_said > std::chrono::seconds(10))
		{
			s_said = std::chrono::steady_clock::now();
			SPDLOG_LOGGER_INFO(spdlog::get("graphics"), "Culling: {} of {} models drawn (the rest off camera or too small to see)",
			                   drawn, slots);
		}
	}


	// Animated entities: each posed from its animation's frame. The animation gives each bone relative to its
	// parent; the mesh wants them composed down the hierarchy (as the mesh viewer does).
	_renderContext.animatedDrawDescs.clear();
	uint32_t animatedIndex = _renderContext.animatedOffset;
	std::vector<glm::mat4> bones;
	registry.Each<const Mesh, const Transform, const Animated>(
	    [&](entt::entity entity, const Mesh& mesh, const Transform& transform, const Animated& animated) {
		    // Far-off villagers and animals are drawn as just their shadows (ShownAsShadow, set by CHLApi's shadows).
		    // And villagers asleep in their houses (Asleep) aren't seen at all.
		    if (!inView(mesh.id, transform) || registry.AnyOf<ShownAsShadow, Asleep>(entity))
		    {
			    return; // not drawn: no pose worked out either
		    }
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
		    if (reflects(entity, mesh, transform))
		    {
			    _renderContext.reflectedDrawDescs.push_back({mesh.id, idx, true, false});
		    }
	    },
	    entt::exclude<TempleInteriorPart>);

	// Buildings under construction: the building fills in from the bottom up as its scaffold comes down.
	_renderContext.constructionDrawDescs.clear();
	uint32_t constructionIndex = animatedIndex; // after the animated entities' uniforms
	registry.Each<const Mesh, const Transform, const Construction>(
	    [&](entt::entity entity, const Mesh& mesh, const Transform& transform, const Construction& construction) {
		    const auto l3dMesh = meshes.Handle(mesh.id);
		    // As the original's: for the first half the building and its scaffold go up together, layer by layer
		    // from the bottom; for the second half the building is whole and the scaffold comes down from the top.
		    const auto box = l3dMesh->GetBoundingBox();
		    const float bottom = transform.position.y + box.minima.y * transform.scale.y;
		    const float height = box.Size().y * transform.scale.y;
		    const float built = construction.drawnFrom + construction.built * (1.0f - construction.drawnFrom);
		    const float firstHalf = std::min(built * 2.0f, 1.0f);
		    const float secondHalf = std::max(built * 2.0f - 1.0f, 0.0f);
		    graphics::SetScaffoldShown(*l3dMesh, built < 1.0f ? 1.0f : 0.0f);
		    graphics::SetBuiltHeight(*l3dMesh, bottom + firstHalf * height + (secondHalf > 0.0f ? 1.0f : 0.0f));
		    // The scaffold on an inverse bell: up fast to keep ahead of the building, easing to a stop at the top as
		    // the building is finished; then down slowly at first, as fast as it went up by the end.
		    float scaffoldShare = secondHalf > 0.0f ? std::cos(secondHalf * glm::half_pi<float>())
		                                       : std::sin(firstHalf * glm::half_pi<float>());
		    // Land 1's Ark stands in its whole frame from the start (Mike's photo of the original, 2026-10-18).
		    if (registry.AllOf<Feature>(entity) && construction.built < 1.0f)
		    {
			    scaffoldShare = 1.0f;
		    }
		    graphics::SetScaffoldHeight(*l3dMesh, bottom + scaffoldShare * height);
		    auto modelMatrix = glm::mat4(transform.rotation);
		    modelMatrix = glm::translate(modelMatrix, transform.position * transform.rotation);
		    modelMatrix = glm::scale(modelMatrix, transform.scale);
		    modelMatrix[0][3] = static_cast<float>(defaultPoseRow(mesh.id));
		    const auto buildingMatrix = modelMatrix;
		    const uint32_t building = constructionIndex++;
		    const uint32_t scaffold = constructionIndex++;
		    _renderContext.instanceUniforms[building] = buildingMatrix;
		    _renderContext.instanceUniforms[scaffold] = modelMatrix;
		    _renderContext.constructionDrawDescs.push_back({mesh.id, building, scaffold});
	    },
	    entt::exclude<Animated>);

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
