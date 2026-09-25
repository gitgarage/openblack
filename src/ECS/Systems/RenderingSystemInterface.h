/*******************************************************************************
 * Copyright (c) 2018-2026 openblack developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/openblack/openblack
 *
 * openblack is licensed under the GNU General Public License version 3.
 *******************************************************************************/

#pragma once

#include <map>

#include <entt/fwd.hpp>
#include <glm/mat4x4.hpp>

#include "Graphics/GraphicsHandle.h"
#include "Graphics/Mesh.h"

namespace openblack::ecs::systems
{
struct RenderContext
{
	RenderContext();
	~RenderContext();
	std::unique_ptr<graphics::Mesh> boundingBox;
	std::unique_ptr<graphics::Mesh> streams;
	std::unique_ptr<graphics::Mesh> footpaths;
	std::unique_ptr<graphics::Mesh> footprints;

	struct InstancedDrawDesc
	{
		InstancedDrawDesc(uint32_t offset, uint32_t count, bool morphWithTerrain)
		    : offset(offset)
		    , count(count)
		    , morphWithTerrain(morphWithTerrain)
		{
		}
		uint32_t offset;
		uint32_t count;
		bool morphWithTerrain;
	};

	/// A list of cpu-side uniforms which is refilled at every \ref PrepareDraw.
	/// This vector will resize to the number of instances it manages
	/// but in practice, it should only grow its reserved memory.
	/// If debug bounding boxes are enabled, it will double in size to fit all
	/// bounding boxes in the second half of the list.
	std::vector<glm::mat4> instanceUniforms;
	/// Stores information for rendering which is prepared at \ref PrepareDraw.
	std::map<entt::id_type, const InstancedDrawDesc> instancedDrawDescs;
	/// Animated entities, drawn one at a time with their own bone pose. Each has one instance uniform, at
	/// `offset`, after those of the instanced draws.
	struct AnimatedDrawDesc
	{
		entt::id_type meshId;
		uint32_t offset;
		uint32_t animation;
		float time;
	};
	std::vector<AnimatedDrawDesc> animatedDrawDescs;
	uint32_t animatedOffset = 0;

	/// Bone matrices for skinned meshes, sampled by the instanced object shader instead of u_model (the
	/// Switch's GLES driver kept only u_model's first matrix). A float texture: one row per pose, four
	/// texels (matrix columns) per bone. Row 0 is all identity (unskinned meshes); each boned mesh type has
	/// a row for its default pose and each animated entity one for its current pose. An instance's row is
	/// carried in its transform's [0][3], which is otherwise 0.
	static constexpr uint16_t k_BonesPerRow = 128;
	static constexpr uint16_t k_BoneRows = 1024;
	graphics::TextureHandle boneTexture;
	std::vector<glm::mat4> boneData; // k_BonesPerRow matrices per row
	uint32_t boneRowsUsed = 0;
	/// Not an actual vertex buffer, but a dynamic general purpose buffer which
	/// stores uniform data as a GPU-side copy of \ref _instanceUniforms and
	/// which is populated in \ref PrepareDraw and consumed in \ref DrawModels.
	/// This buffer will resize if the size of \ref _instanceUniforms exceeds
	/// its allocated size. It will never shrink.
	/// The values stored are a list of uniforms (model matrix) needed for both
	/// the instances of entities and their bounding boxes.
	graphics::DynamicVertexBufferHandle instanceUniformBuffer;

	bool dirty {true};
	bool hasBoundingBoxes {false};
};

class RenderingSystemInterface
{
public:
	virtual void SetDirty() = 0;
	virtual void PrepareDraw(bool drawBoundingBox, bool drawFootpaths, bool drawStreams) = 0;
	virtual const RenderContext& GetContext() = 0;
	inline ~RenderingSystemInterface() = default;
};
} // namespace openblack::ecs::systems
