/******************************************************************************
 * Copyright (c) 2018-2026 openblack developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/openblack/openblack
 *
 * openblack is licensed under the GNU General Public License version 3.
 *******************************************************************************/

#include <set>
#include <memory>
#include <chrono>
#include <cstdlib>
#include <pthread.h>
#include <mutex>
#include <deque>
#include <condition_variable>
#define LOCATOR_IMPLEMENTATIONS

#include <cstdint>

#include <SDL_video.h>
#include <bgfx/platform.h>
#include <bimg/bimg.h>
#include <bx/file.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/transform.hpp>
#include <spdlog/spdlog.h>

#include "3D/L3DAnim.h"
#include "3D/L3DMesh.h"
#include "3D/L3DSubMesh.h"
#include "3D/LandBlock.h"
#include "3D/LandIslandInterface.h"
#include "3D/OceanInterface.h"
#include "3D/SkyInterface.h"
#include "Camera/Camera.h"
#include "ECS/Components/Mesh.h"
#include "ECS/Components/Sprite.h"
#include "PSys/PSys.h"
#include "ECS/Registry.h"
#include "ECS/Systems/RenderingSystemInterface.h"
#include "EngineConfig.h"
#include "Graphics/DebugLines.h"
#include "Graphics/FrameBuffer.h"
#include "Graphics/GraphicsHandleBgfx.h"
#include "Graphics/IndexBuffer.h"
#include "Graphics/Primitive.h"
#include "Graphics/ShaderManager.h"
#include "Graphics/VertexBuffer.h"
#include "Locator.h"
#include "Profiler.h"
#include "Renderer.h"

#include <unordered_map>
#include "Resources/ResourceManager.h"
#include "Resources/ResourcesInterface.h"
#include "Windowing/WindowingInterface.h"

using namespace openblack;
using namespace openblack::graphics;
using namespace openblack::ecs::systems;

namespace openblack
{
// clang-format off
constexpr auto k_BgfxDefaultStateInvertedZ = 0 \
                                     | BGFX_STATE_WRITE_RGB \
                                     | BGFX_STATE_WRITE_A \
                                     | BGFX_STATE_WRITE_Z \
                                     | BGFX_STATE_CULL_CW \
                                     | BGFX_STATE_DEPTH_TEST_GREATER \
                                     | BGFX_STATE_MSAA;
// clang-format on

// Writes screenshots on its own thread. Holds at most two waiting; more are dropped, not queued, so a slow
// SD card never builds up memory or delays the game.
struct ShotJob
{
	std::string path;
	uint32_t width = 0;
	uint32_t height = 0;
	bool yflip = false;
	std::vector<uint32_t> pixels;
};

class ShotWriter
{
public:
	static ShotWriter& Instance()
	{
		// Never destroyed: its thread waits on these members until the process ends, and destroying them
		// under it at exit hung the program.
		static auto* writer = new ShotWriter();
		return *writer;
	}

	void Push(ShotJob job)
	{
		if (!_started)
		{
			return;
		}
		{
			std::lock_guard lock(_mutex);
			if (_jobs.size() >= 2)
			{
				SPDLOG_LOGGER_WARN(spdlog::get("graphics"), "Screenshot {} dropped: still writing earlier ones", job.path);
				return;
			}
			_jobs.push_back(std::move(job));
		}
		_wake.notify_one();
	}

private:
	ShotWriter()
	{
		// PNG encoding wants a large stack: a default-sized thread crashed on the Switch at the first shot.
		pthread_attr_t attributes;
		pthread_attr_init(&attributes);
		pthread_attr_setstacksize(&attributes, 1024 * 1024);
		pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
		_started = pthread_create(
		               &_thread, &attributes,
		               [](void* self) -> void* {
			               static_cast<ShotWriter*>(self)->Run();
			               return nullptr;
		               },
		               this) == 0;
		pthread_attr_destroy(&attributes);
		if (!_started)
		{
			SPDLOG_LOGGER_ERROR(spdlog::get("graphics"), "Could not start the screenshot writer thread: screenshots are off");
		}
	}

	void Run()
	{
		for (;;)
		{
			ShotJob job;
			{
				std::unique_lock lock(_mutex);
				_wake.wait(lock, [this]() { return !_jobs.empty(); });
				job = std::move(_jobs.front());
				_jobs.pop_front();
			}
			bx::FileWriter writer;
			bx::Error err;
			if (bx::open(&writer, job.path.c_str(), false, &err))
			{
				bimg::imageWritePng(&writer, job.width, job.height, job.width * 4, job.pixels.data(),
				                    bimg::TextureFormat::BGRA8, job.yflip, &err);
				bx::close(&writer);
				SPDLOG_LOGGER_INFO(spdlog::get("graphics"), "Screenshot ({}x{}) saved at {}", job.width, job.height, job.path);
			}
			else
			{
				SPDLOG_LOGGER_ERROR(spdlog::get("graphics"), "Failed to save screenshot at {}: {}", job.path,
				                    std::string(err.getMessage().getCPtr(), err.getMessage().getLength()));
			}
		}
	}

	std::mutex _mutex;
	std::condition_variable _wake;
	std::deque<ShotJob> _jobs;
	pthread_t _thread {};
	bool _started = false;
};

struct BgfxCallback: public bgfx::CallbackI
{
	constexpr static std::array<std::string_view, bgfx::Fatal::Count> k_CodeLookup = {
	    "DebugCheck",            //
	    "InvalidShader",         //
	    "UnableToInitialize",    //
	    "UnableToCreateTexture", //
	    "DeviceLost",            //
	};

	~BgfxCallback() override = default;

	void fatal(const char* filePath, uint16_t line, bgfx::Fatal::Enum code, const char* str) override
	{
		const auto* codeStr = k_CodeLookup.at(code).data();
		SPDLOG_LOGGER_CRITICAL(spdlog::get("graphics"), "bgfx: {}:{}: FATAL ({}): {}", filePath, line, codeStr, str);

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_CRITICAL
		spdlog::get("graphics")
		    ->log(spdlog::source_loc {filePath, line, SPDLOG_FUNCTION}, spdlog::level::critical, "FATAL ({}): {}", codeStr,
		          str);
#endif

		// Must terminate, continuing will cause crash anyway.
		throw std::runtime_error(std::string("bgfx: ") + filePath + ":" + std::to_string(line) + ": FATAL (" + codeStr +
		                         "): " + str);
	}

	void traceVargs([[maybe_unused]] const char* filePath, [[maybe_unused]] uint16_t line, const char* format,
	                va_list argList) override
	{
		std::array<char, 0x2000> temp;
		char* out = temp.data();
		int32_t len = vsnprintf(out, temp.size(), format, argList);
		if (static_cast<int32_t>(temp.size()) < len)
		{
			out = reinterpret_cast<char*>(alloca(len + 1));
			len = vsnprintf(out, len, format, argList);
		}
		if (len > 0)
		{
			out[len] = '\0';
			if (len > 0 && out[len - 1] == '\n')
			{
				out[len - 1] = '\0';
			}
// TODO(bwrsandman): change level to trace
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_DEBUG
			spdlog::get("graphics")->log(spdlog::source_loc {filePath, line, SPDLOG_FUNCTION}, spdlog::level::debug, out);
#endif
		}
		else
		{
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_ERROR
			spdlog::get("graphics")
			    ->log(spdlog::source_loc {filePath, line, SPDLOG_FUNCTION}, spdlog::level::err,
			          "bgfx: failed to format message: {}", format);
#endif
		}
	}
	void profilerBegin([[maybe_unused]] const char* name, [[maybe_unused]] uint32_t abgr, [[maybe_unused]] const char* filePath,
	                   [[maybe_unused]] uint16_t line) override
	{
	}
	void profilerBeginLiteral([[maybe_unused]] const char* name, [[maybe_unused]] uint32_t abgr,
	                          [[maybe_unused]] const char* filePath, [[maybe_unused]] uint16_t line) override
	{
	}
	void profilerEnd() override {}
	// Reading and writing to shader cache
	uint32_t cacheReadSize([[maybe_unused]] uint64_t id) override { return 0; }
	bool cacheRead([[maybe_unused]] uint64_t id, [[maybe_unused]] void* data, [[maybe_unused]] uint32_t size) override
	{
		return false;
	}
	void cacheWrite([[maybe_unused]] uint64_t id, [[maybe_unused]] const void* data, [[maybe_unused]] uint32_t size) override {}
	// Saving a screenshot. Only the copy happens here, on the render thread: an opaque (and, with
	// OPENBLACK_SHOT_SCALE=n, 1/n size) copy of the frame. Encoding the PNG and writing it happen on a
	// background thread, so a screenshot doesn't stall the game (on the Switch that took most of a second).
	void screenShot(const char* filePath, uint32_t width, uint32_t height, uint32_t pitch, const void* data,
	                [[maybe_unused]] uint32_t size, bool yflip) override
	{
		const auto ext = std::filesystem::path(filePath).extension();
		if (ext != ".png")
		{
			SPDLOG_LOGGER_WARN(spdlog::get("graphics"), "Not Implemented: {} screenshot ({}x{}) requested at {}", ext.string(),
			                   width, height, filePath);
			return;
		}
		static const uint32_t scale = []() {
			const char* value = std::getenv("OPENBLACK_SHOT_SCALE");
			return value != nullptr ? std::max(1, std::atoi(value)) : 1;
		}();
		ShotJob job;
		job.path = filePath;
		job.width = width / scale;
		job.height = height / scale;
		job.yflip = yflip;
		job.pixels.resize(static_cast<size_t>(job.width) * job.height);
		const auto* source = static_cast<const uint8_t*>(data);
		for (uint32_t y = 0; y < job.height; ++y)
		{
			const auto* row = reinterpret_cast<const uint32_t*>(source + static_cast<size_t>(y * scale) * pitch);
			for (uint32_t x = 0; x < job.width; ++x)
			{
				job.pixels[static_cast<size_t>(y) * job.width + x] = row[x * scale] | 0xFF000000; // no alpha
			}
		}
		ShotWriter::Instance().Push(std::move(job));
	}
	// Saving a video
	void captureBegin(uint32_t width, uint32_t height, [[maybe_unused]] uint32_t pitch,
	                  [[maybe_unused]] bgfx::TextureFormat::Enum format, [[maybe_unused]] bool yflip) override
	{
		SPDLOG_LOGGER_WARN(spdlog::get("graphics"), "Not Implemented: Video Capture Begin ({}x{}) requested", width, height);
	}
	void captureEnd() override { SPDLOG_LOGGER_WARN(spdlog::get("graphics"), "Not Implemented: Video Capture End requested"); }
	void captureFrame([[maybe_unused]] const void* data, [[maybe_unused]] uint32_t size) override
	{
		SPDLOG_LOGGER_WARN(spdlog::get("graphics"), "Not Implemented: Video Capture Frame requested");
	}
};

} // namespace openblack

std::unique_ptr<RendererInterface> RendererInterface::Create(GraphicsBackend backend, bool vsync) noexcept
{
	bgfx::Init init {};
	switch (backend)
	{
	case GraphicsBackend::Noop:
		init.type = bgfx::RendererType::Noop;
		break;
	case GraphicsBackend::Direct3D12:
		init.type = bgfx::RendererType::Direct3D12;
		break;
	case GraphicsBackend::Metal:
		init.type = bgfx::RendererType::Metal;
		break;
	case GraphicsBackend::Vulkan:
		init.type = bgfx::RendererType::Vulkan;
		break;
	case GraphicsBackend::OpenGL:
		init.type = bgfx::RendererType::OpenGL;
		break;
	default:
		SPDLOG_LOGGER_CRITICAL(spdlog::get("graphics"), "Got impossible graphics backend.");
		return nullptr;
	}

	// Get render area size
	glm::uvec2 drawableSize;
	if (backend != GraphicsBackend::Noop)
	{
		const auto& window = Locator::windowing::value();

		drawableSize = static_cast<glm::uvec2>(window.GetSize());
		init.resolution.width = static_cast<uint32_t>(drawableSize.x);
		init.resolution.height = static_cast<uint32_t>(drawableSize.y);

		// Get Native Handles from SDL window
		const auto handles = window.GetNativeHandles();
		init.platformData.nwh = handles.nativeWindow;
		init.platformData.ndt = handles.nativeDisplay;
#if defined(__SWITCH__)
		init.platformData.context = SDL_GL_GetCurrentContext(); // made by Sdl2WindowingSystem
#endif
	}

	uint32_t bgfxReset = BGFX_RESET_NONE;
	auto bgfxCallback = std::make_unique<BgfxCallback>();
	if (vsync)
	{
		bgfxReset |= BGFX_RESET_VSYNC;
	}
	init.resolution.reset = bgfxReset;
	init.callback = dynamic_cast<bgfx::CallbackI*>(bgfxCallback.get());

	if (!bgfx::init(init))
	{
		SPDLOG_LOGGER_CRITICAL(spdlog::get("graphics"), "Failed to initialize bgfx.");
		return nullptr;
	}

	const bgfx::Caps* caps = bgfx::getCaps();
	SPDLOG_LOGGER_INFO(spdlog::get("graphics"), "bgfx renderer: {} (max uniforms {})", bgfx::getRendererName(bgfx::getRendererType()),
	                   caps->limits.maxUniforms);
	if ((caps->supported & BGFX_CAPS_TEXTURE_2D_ARRAY) == 0 || caps->limits.maxTextureLayers < 9)
	{
		SPDLOG_LOGGER_CRITICAL(spdlog::get("graphics"), "Graphics device must support texture layers.");
		return nullptr;
	}

	return std::make_unique<Renderer>(bgfxReset, std::move(bgfxCallback));
}

Renderer::Renderer(uint32_t bgfxReset, std::unique_ptr<BgfxCallback>&& bgfxCallback) noexcept
    : _shaderManager(std::make_unique<ShaderManager>())
    , _bgfxCallback(std::move(bgfxCallback))
    , _bgfxReset(bgfxReset)
{
	_shaderManager->LoadShaders();
	_plane = Primitive::CreatePlane();

	// give debug names to views
	// TODO (#749) use std::views::enumerate
	for (bgfx::ViewId i = 0; const auto& name : k_RenderPassNames)
	{
		bgfx::setViewName(i, name.data());
		++i;
	}
	// The world drawn in the order DrawPass submits it - sky, sea, land, models, sprites - as the original's
	// GLandscape::Draw: the sea, writing no depth, lies under whatever is drawn after it (a shark's body, a swimmer's).
	bgfx::setViewMode(static_cast<bgfx::ViewId>(graphics::RenderPass::Main), bgfx::ViewMode::Sequential);
}

Renderer::~Renderer() noexcept
{
	_plane.reset();
	_shaderManager.reset();
	bgfx::frame();
	bgfx::shutdown();
}

void Renderer::ConfigureView(graphics::RenderPass viewId, glm::u16vec2 resolution, uint32_t clearColor) const noexcept
{
	bgfx::setViewClear(static_cast<bgfx::ViewId>(viewId), BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, clearColor, 0.0f, 0);
	bgfx::setViewRect(static_cast<bgfx::ViewId>(viewId), 0, 0, resolution.x, resolution.y);
}

void Renderer::Reset(glm::u16vec2 resolution) const noexcept
{
	bgfx::reset(resolution.x, resolution.y, _bgfxReset);
}

graphics::ShaderManager& Renderer::GetShaderManager() const noexcept
{
	return *_shaderManager;
}

const Texture2D* GetTexture(uint32_t skinID, const std::unordered_map<SkinId, std::unique_ptr<graphics::Texture2D>>& meshSkins)
{
	const auto& textureManager = Locator::resources::value().GetTextures();

	const Texture2D* texture = nullptr;

	if (skinID != 0xFFFFFFFF)
	{
		if (meshSkins.find(skinID) != meshSkins.end())
		{
			texture = meshSkins.at(skinID).get();
		}
		else if (textureManager.Contains(skinID))
		{
			texture = &*textureManager.Handle(skinID);
		}
		else if (const auto* shared = L3DMesh::FindLoadedSkin(skinID); shared != nullptr)
		{
			texture = shared; // a skin another mesh carries
		}
		else
		{
			SPDLOG_LOGGER_ERROR(spdlog::get("graphics"), "Could not find the texture");
		}
	}

	return texture;
}

namespace
{
std::unordered_map<const openblack::graphics::L3DMesh*, float> g_scaffoldShown;
} // namespace

std::unordered_map<const openblack::graphics::L3DMesh*, float> g_builtHeight;
std::unordered_map<const openblack::graphics::L3DMesh*, float> g_scaffoldHeight;

void openblack::graphics::SetScaffoldHeight(const L3DMesh& mesh, float worldHeight)
{
	g_scaffoldHeight[&mesh] = worldHeight;
}

void openblack::graphics::SetScaffoldShown(const L3DMesh& mesh, float fraction)
{
	g_scaffoldShown[&mesh] = glm::clamp(fraction, 0.0f, 1.0f);
}

void openblack::graphics::SetBuiltHeight(const L3DMesh& mesh, float worldHeight)
{
	g_builtHeight[&mesh] = worldHeight;
}

void Renderer::DrawSubMesh(const graphics::L3DMesh& mesh, const graphics::L3DSubMesh& subMesh, const L3DMeshSubmitDesc& desc,
                           bool preserveState) const
{
	assert(&subMesh.GetMesh());
	// We don't draw physics meshes, we haven't implemented statuses (building and graves) and modern GPUs can handle high lod
	// Status 1 is a building's scaffold: drawn on its own for a building under construction, up to the height
	// it stands to (see L3DMeshSubmitDesc::Scaffold).
	using Scaffold = L3DMeshSubmitDesc::Scaffold;
	float scaffoldShown = 1.0f;
	const bool isScaffold = subMesh.GetFlags().status == 1 && !subMesh.IsPhysics();
	if (!desc.drawAll && desc.scaffold == Scaffold::Only)
	{
		const auto it = g_scaffoldShown.find(&mesh);
		scaffoldShown = it != g_scaffoldShown.end() ? it->second : 0.0f;
		if (!isScaffold || scaffoldShown <= 0.0f)
		{
			return;
		}
	}
	else if (!desc.drawAll && (subMesh.IsPhysics() || subMesh.GetFlags().status != 0 || (subMesh.GetFlags().lodMask & 1) != 1))
	{
		return;
	}

	const auto& island = Locator::terrainSystem::value();

	auto extent = island.GetExtent();
	auto islandExtent = glm::vec4(extent.minimum, extent.maximum);
	const auto& heightMap = island.GetHeightMap();

	auto const& skins = mesh.GetSkins();
	bool lastPreserveState = false;
	const auto& primitives = subMesh.GetPrimitives();
	for (auto it = primitives.begin(); it != primitives.end(); ++it)
	{
		const auto& prim = *it;

		const bool hasNext = std::next(it) != primitives.end();

		const Texture2D* texture = GetTexture(prim.skinID, skins);
		const Texture2D* nextTexture = !hasNext ? nullptr : GetTexture(std::next(it)->skinID, skins);

		const bool primitivePreserveState = texture != nullptr && texture == nextTexture && (preserveState || hasNext);

		uint32_t skip = Mesh::SkipState::SkipNone;
		if (!lastPreserveState)
		{
			if (desc.modelMatrices != nullptr && desc.matrixCount > 0)
			{
				bgfx::setTransform(desc.modelMatrices, desc.matrixCount);
			}
			if (texture != nullptr)
			{
				desc.program->SetTextureSampler("s_diffuse", 0, *texture);
			}
			if (desc.instanceDesc)
			{
				const auto& renderCtx = Locator::rendereringSystem::value().GetContext();
				const glm::vec4 bonesSize = {ecs::systems::RenderContext::k_BonesPerRow * 4.0f,
				                             static_cast<float>(ecs::systems::RenderContext::k_BoneRows), 0.0f, 0.0f};
				desc.program->SetTextureSampler("s_bones", 2, renderCtx.boneTexture); // vs
				desc.program->SetUniformValue("u_bonesSize", &bonesSize);             // vs
			}
			if (desc.morphWithTerrain)
			{
				desc.program->SetTextureSampler("s_heightmap", 1, heightMap);   // vs
				desc.program->SetUniformValue("u_islandExtent", &islandExtent); // vs
			}
			if (!desc.isSky)
			{
				// z/w: a building under construction is drawn up to the height it is built to.
				const auto& heights = desc.scaffold == L3DMeshSubmitDesc::Scaffold::Only ? g_scaffoldHeight : g_builtHeight;
				const auto built = desc.scaffold != L3DMeshSubmitDesc::Scaffold::Hidden ? heights.find(&mesh) : heights.end();
				const glm::vec4 u_skyAlphaThreshold = {
				    Locator::skySystem::value().GetCurrentSkyType(),
				    prim.thresholdAlpha ? prim.alphaCutoutThreshold : 0.0f,
				    built != heights.end() ? 1.0f : 0.0f,
				    built != heights.end() ? built->second : 0.0f,
				};
				desc.program->SetUniformValue("u_skyAlphaThreshold", &u_skyAlphaThreshold);
				const auto& ownLight = mesh.GetOwnLight();
				const glm::vec4 u_ownLight = ownLight ? glm::vec4(*ownLight, 1.0f) : glm::vec4(0.0f);
				desc.program->SetUniformValue("u_ownLight", &u_ownLight);
			}
		}
		else
		{
			skip |= Mesh::SkipState::SkipRenderState;
			skip |= Mesh::SkipState::SkipVertexBuffer;
		}

		{
			if (desc.instanceDesc != nullptr && (skip & Mesh::SkipState::SkipInstanceBuffer) == 0)
			{
				bgfx::setInstanceDataBuffer(toBgfx(desc.instanceDesc->GetRawHandle()), desc.instanceDesc->GetStart(),
				                            desc.instanceDesc->GetCount());
			}
			if (subMesh.GetMesh().IsIndexed() && (skip & Mesh::SkipState::SkipIndexBuffer) == 0)
			{
				subMesh.GetMesh().GetIndexBuffer().Bind(prim.indicesCount, prim.indicesOffset);
			}
			if ((skip & Mesh::SkipState::SkipVertexBuffer) == 0)
			{
				subMesh.GetMesh().GetVertexBuffer().Bind();
			}
			if ((skip & Mesh::SkipState::SkipRenderState) == 0)
			{
				bgfx::setState(desc.state, desc.rgba);
			}

			bgfx::submit(static_cast<bgfx::ViewId>(desc.viewId), toBgfx(desc.program->GetRawHandle()), 0,
			             primitivePreserveState ? BGFX_DISCARD_NONE : BGFX_DISCARD_ALL);
		}
		lastPreserveState = primitivePreserveState;
	}
}

void Renderer::DrawMesh(const graphics::L3DMesh& mesh, const L3DMeshSubmitDesc& desc, uint8_t subMeshIndex) const noexcept
{
	if (mesh.GetNumSubMeshes() == 0)
	{
		// Once each: Land 1's SingingStoneCenter, every frame, was hundreds of thousands of lines.
		static std::set<std::string> s_warned;
		if (s_warned.emplace(mesh.GetDebugName()).second)
		{
			SPDLOG_LOGGER_WARN(spdlog::get("graphics"), "Mesh {} has no submeshes to draw", mesh.GetDebugName());
		}
		return;
	}

	const auto& subMeshes = mesh.GetSubMeshes();

	if (subMeshIndex != std::numeric_limits<uint8_t>::max())
	{
		if (subMeshIndex >= mesh.GetNumSubMeshes())
		{
			SPDLOG_LOGGER_WARN(spdlog::get("graphics"), "tried to draw submesh out of range ({}/{})", subMeshIndex,
			                   mesh.GetNumSubMeshes());
		}

		DrawSubMesh(mesh, *subMeshes[subMeshIndex], desc, false);
		return;
	}

	for (auto it = subMeshes.begin(); it != subMeshes.end(); ++it)
	{
		const L3DSubMesh& subMesh = **it;
		DrawSubMesh(mesh, subMesh, desc, std::next(it) != subMeshes.end());
	}
}

void Renderer::DrawFootprintPass(const DrawSceneDesc& drawDesc) const
{
	const auto viewId = graphics::RenderPass::Footprint;
	auto section = Locator::profiler::value().BeginScoped(Profiler::Stage::FootprintPass);
	if (drawDesc.drawIsland)
	{
		const auto& island = Locator::terrainSystem::value();
		island.GetFootprintFramebuffer().Bind(viewId);

		// This dummy draw call is here to make sure that view is cleared if no
		// other draw calls are submitted to view
		bgfx::touch(static_cast<bgfx::ViewId>(viewId));

		// _shaderManager->SetCamera(viewId, *drawDesc.camera); // TODO

		auto view = island.GetOrthoView();
		auto proj = island.GetOrthoProj();
		bgfx::setViewTransform(static_cast<bgfx::ViewId>(viewId), &view, &proj);

		const auto& meshManager = Locator::resources::value().GetMeshes();
		const auto& renderCtx = Locator::rendereringSystem::value().GetContext();
		const auto* footprintShaderInstanced = _shaderManager->GetShader("FootprintInstanced");
		// Buildings under construction have their ground marked too: the footprint shows where to bring wood.
		std::vector<std::tuple<entt::id_type, uint32_t, uint32_t>> footprinted;
		for (const auto& [meshId, placers] : renderCtx.instancedDrawDescs)
		{
			footprinted.emplace_back(meshId, placers.offset, placers.count);
		}
		for (const auto& construction : renderCtx.constructionDrawDescs)
		{
			footprinted.emplace_back(construction.meshId, construction.scaffoldOffset, 1u);
		}
		for (const auto& [meshId, offset, count] : footprinted)
		{
			auto mesh = meshManager.Handle(meshId);
			if (!mesh->ContainsLandscapeFeature() || mesh->GetFootprints().empty())
			{
				continue;
			}
			const auto& footprint = mesh->GetFootprints()[0];
			footprintShaderInstanced->SetTextureSampler("s_footprint", 0, *footprint.texture);
			footprint.mesh->GetVertexBuffer().Bind();
			bgfx::setInstanceDataBuffer(toBgfx(renderCtx.instanceUniformBuffer), offset, count);
			const uint64_t state = 0u                       //
			                       | BGFX_STATE_WRITE_RGB   //
			                       | BGFX_STATE_WRITE_A     //
			                       | BGFX_STATE_BLEND_ALPHA //
			                       | BGFX_STATE_CULL_CW     //
			                       | BGFX_STATE_MSAA;
			bgfx::setState(state);
			bgfx::submit(static_cast<bgfx::ViewId>(viewId), toBgfx(footprintShaderInstanced->GetRawHandle()));
		}
	}
}

void Renderer::DrawScene(const DrawSceneDesc& drawDesc) const noexcept
{
	// TODO(bwrsandman): Footprint framebuffer doesn't need to be updated each frame
	DrawFootprintPass(drawDesc);
	// Reflection Pass
	{
		auto section = Locator::profiler::value().BeginScoped(Profiler::Stage::ReflectionPass);
		// The sea shows the sky only, as the original's does with its land reflection off (LH3DIsland::g_b_draw_reflection:
		// GLandscape::Draw draws the sky, then only with it set LH3DIsland::DrawUnderWater the land again, mirrored at
		// half its brightness, before the sea goes over them). It was off in the original captured on 2026-10-21 - no
		// land, model or creature is mirrored in any of its shots - so the reflection pass draws the sky alone. The
		// near-the-sea models (reflectedDrawDescs) are there for if the original's option is wanted: drawIsland and
		// drawEntities true below.
		if (drawDesc.drawWater)
		{
			DrawSceneDesc drawPassDesc = drawDesc;

			const auto& frameBuffer = Locator::oceanSystem::value().GetReflectionFramebuffer();
			auto reflectionCamera = drawDesc.camera->Reflect();

			drawPassDesc.viewId = graphics::RenderPass::Reflection;
			drawPassDesc.camera = reflectionCamera.get();
			drawPassDesc.frameBuffer = &frameBuffer;
			drawPassDesc.drawWater = false;
			drawPassDesc.drawBoundingBoxes = false;
			drawPassDesc.cullBack = true;
			drawPassDesc.drawIsland = false;
			drawPassDesc.drawEntities = false;
			drawPassDesc.drawSprites = false;

			DrawPass(drawPassDesc);
		}
	}

	// Main Draw Pass
	{
		auto section = Locator::profiler::value().BeginScoped(Profiler::Stage::MainPass);
		DrawPass(drawDesc);
	}
}

void Renderer::DrawPass(const DrawSceneDesc& desc) const
{
	const auto& meshManager = Locator::resources::value().GetMeshes();
	auto& profiler = Locator::profiler::value();

	if (desc.frameBuffer != nullptr)
	{
		desc.frameBuffer->Bind(desc.viewId);
	}
	// This dummy draw call is here to make sure that view is cleared if no
	// other draw calls are submitted to view
	bgfx::touch(static_cast<bgfx::ViewId>(desc.viewId));

	_shaderManager->SetCamera(desc.viewId, *desc.camera);

	const auto* skyShader = _shaderManager->GetShader("Sky");
	const auto* waterShader = _shaderManager->GetShader("Water");
	const auto* terrainShader = _shaderManager->GetShader("Terrain");
	const auto* debugShader = _shaderManager->GetShader("DebugLine");
	const auto* spriteShader = _shaderManager->GetShader("Sprite");
	const auto* debugShaderInstanced = _shaderManager->GetShader("DebugLineInstanced");
	const auto* objectShaderInstanced = _shaderManager->GetShader("ObjectInstanced");
	const auto* objectShaderHeightMapInstanced = _shaderManager->GetShader("ObjectHeightMapInstanced");

	const auto skyType = Locator::skySystem::value().GetCurrentSkyType();

	{
		auto section = profiler.BeginScoped(desc.viewId == RenderPass::Reflection ? Profiler::Stage::ReflectionDrawSky
		                                                                          : Profiler::Stage::MainPassDrawSky);
		if (desc.drawSky)
		{
			const auto modelMatrix = glm::mat4(1.0f);
			const glm::vec4 u_typeAlignment = {skyType, Locator::config::value().skyAlignment + 1.0f, 0.0f, 0.0f};

			skyShader->SetTextureSampler("s_diffuse", 0, Locator::skySystem::value().GetTexture());
			skyShader->SetUniformValue("u_typeAlignment", &u_typeAlignment);

			L3DMeshSubmitDesc submitDesc = {};
			submitDesc.viewId = desc.viewId;
			submitDesc.program = skyShader;
			submitDesc.state = k_BgfxDefaultStateInvertedZ;
			if (!desc.cullBack)
			{
				submitDesc.state &= ~BGFX_STATE_CULL_MASK;
				submitDesc.state |= BGFX_STATE_CULL_CCW;
			}
			submitDesc.modelMatrices = &modelMatrix;
			submitDesc.matrixCount = 1;
			submitDesc.isSky = true;

			DrawMesh(Locator::skySystem::value().GetMesh(), submitDesc, 0);
		}
	}

	{
		auto section = profiler.BeginScoped(desc.viewId == RenderPass::Reflection ? Profiler::Stage::ReflectionDrawWater
		                                                                          : Profiler::Stage::MainPassDrawWater);
		if (desc.drawWater)
		{
			const auto& ocean = Locator::oceanSystem::value();
			const auto& mesh = ocean.GetMesh();
			mesh.GetIndexBuffer().Bind(mesh.GetIndexBuffer().GetCount(), 0);
			mesh.GetVertexBuffer().Bind();
			// Writing no depth, as the original's sea: what is drawn after it under the surface - a shark's body, a
			// swimmer's - shows whole, as in its shots of the sharks (87-93 s) and the boy in the sea (174-177 s).
			bgfx::setState(k_BgfxDefaultStateInvertedZ & ~BGFX_STATE_WRITE_Z);
			auto diffuse = Locator::resources::value().GetTextures().Handle(ocean.GetDiffuseTexture());
			auto alpha = Locator::resources::value().GetTextures().Handle(ocean.GetAlphaTexture());
			waterShader->SetTextureSampler("s_diffuse", 0, *diffuse);
			waterShader->SetTextureSampler("s_alpha", 1, *alpha);
			waterShader->SetTextureSampler("s_reflection", 2, ocean.GetReflectionFramebuffer().GetColorAttachment());
			const glm::vec4 u_sky = {skyType, 0.0f, 0.0f, 0.0f};
			waterShader->SetUniformValue("u_sky", &u_sky); // fs
			bgfx::submit(static_cast<bgfx::ViewId>(desc.viewId), toBgfx(waterShader->GetRawHandle()));
		}
	}

	{
		auto section = profiler.BeginScoped(desc.viewId == RenderPass::Reflection ? Profiler::Stage::ReflectionDrawIsland
		                                                                          : Profiler::Stage::MainPassDrawIsland);
		if (desc.drawIsland)
		{
			auto& island = Locator::terrainSystem::value();
			auto islandExtent = glm::vec4(island.GetExtent().minimum, island.GetExtent().maximum);

			auto texture = Locator::resources::value().GetTextures().Handle(LandIslandInterface::k_SmallBumpTextureId);
			// The land's bump detail is for near the ground: with the camera high over the land (Mike, 2026-10-23) it fades
			// out from 200 to 300 above it, and the shader skips it at 0. The small bump already stops 200 from the camera.
			const auto eye = desc.camera->GetOrigin();
			const float above = eye.y - std::max(island.GetHeightAt(glm::vec2(eye.x, eye.z)), 0.0f);
			const float nearGround = 1.0f - glm::clamp((above - 200.0f) / 100.0f, 0.0f, 1.0f);
			const glm::vec4 u_skyAndBump = {skyType, desc.bumpMapStrength * nearGround, desc.smallBumpMapStrength, 0.0f};

			terrainShader->SetTextureSampler("s0_materials", 0, island.GetAlbedoArray());
			terrainShader->SetTextureSampler("s1_bump", 1, island.GetBump());
			terrainShader->SetTextureSampler("s2_smallBump", 2, *texture);
			terrainShader->SetTextureSampler("s3_footprints", 3, island.GetFootprintFramebuffer().GetColorAttachment());

			terrainShader->SetUniformValue("u_skyAndBump", &u_skyAndBump);
			terrainShader->SetUniformValue("u_islandExtent", &islandExtent);

			// clang-format off
			constexpr auto defaultState = 0u
				| BGFX_STATE_WRITE_MASK
				| BGFX_STATE_DEPTH_TEST_GREATER
				| BGFX_STATE_BLEND_ALPHA
				| BGFX_STATE_MSAA
			;

			constexpr auto discard = 0u
				| BGFX_DISCARD_INSTANCE_DATA
				| BGFX_DISCARD_INDEX_BUFFER
				| BGFX_DISCARD_TRANSFORM
				| BGFX_DISCARD_VERTEX_STREAMS
				| BGFX_DISCARD_STATE
			;
			// clang-format on

			for (const auto& block : island.GetBlocks())
			{
				// pack uniforms
				const glm::vec4 mapPositionAndSize = glm::vec4(block.GetMapPosition(), 160.0f, 160.0f);
				terrainShader->SetUniformValue("u_blockPositionAndSize", &mapPositionAndSize);

				block.GetMesh().GetVertexBuffer().Bind();

				bgfx::setState(defaultState | (desc.cullBack ? BGFX_STATE_CULL_CCW : BGFX_STATE_CULL_CW), 0);
				bgfx::submit(static_cast<bgfx::ViewId>(desc.viewId), toBgfx(terrainShader->GetRawHandle()), 0, discard);
			}
			bgfx::discard(BGFX_DISCARD_BINDINGS);
		}
	}

	{
		auto section = profiler.BeginScoped(desc.viewId == RenderPass::Reflection ? Profiler::Stage::ReflectionDrawModels
		                                                                          : Profiler::Stage::MainPassDrawModels);
		if (desc.drawEntities)
		{
			L3DMeshSubmitDesc submitDesc = {};
			submitDesc.viewId = desc.viewId;
			submitDesc.program = objectShaderInstanced;
			submitDesc.state = 0u                              //
			                   | BGFX_STATE_WRITE_MASK         //
			                   | BGFX_STATE_DEPTH_TEST_GREATER //
			                   | BGFX_STATE_MSAA               //
			    ;
			const auto& renderCtx = Locator::rendereringSystem::value().GetContext();

#if defined(OPENBLACK_SWITCH_MINIMAL_GRAPHICS)
			// The Switch's reflection: only what stands near enough to the sea to show in it, one draw each.
			const bool reflectionOnly = desc.viewId == RenderPass::Reflection;
			if (reflectionOnly)
			{
				for (const auto& reflected : renderCtx.reflectedDrawDescs)
				{
					auto mesh = meshManager.Handle(reflected.meshId);
					submitDesc.instanceDesc =
					    std::make_unique<graphics::InstanceDesc>(renderCtx.instanceUniformBuffer, reflected.offset, 1);
					const static auto identity = glm::mat4(1.0f);
					if (!reflected.animated && mesh->IsBoned())
					{
						submitDesc.modelMatrices = mesh->GetBoneMatrices().data();
						submitDesc.matrixCount = static_cast<uint8_t>(mesh->GetBoneMatrices().size());
					}
					else
					{
						submitDesc.modelMatrices = &identity;
						submitDesc.matrixCount = 1;
					}
					submitDesc.isSky = false;
					submitDesc.morphWithTerrain = reflected.morphWithTerrain;
					submitDesc.program = submitDesc.morphWithTerrain ? objectShaderHeightMapInstanced : objectShaderInstanced;
					DrawMesh(*mesh, submitDesc, std::numeric_limits<uint8_t>::max());
				}
			}
#else
			constexpr bool reflectionOnly = false;
#endif

			// Instance meshes
			for (const auto& [meshId, placers] : renderCtx.instancedDrawDescs)
			{
				if (reflectionOnly)
				{
					break;
				}
				auto mesh = meshManager.Handle(meshId);

				submitDesc.instanceDesc =
				    std::make_unique<graphics::InstanceDesc>(renderCtx.instanceUniformBuffer, placers.offset, placers.count);
				if (mesh->IsBoned())
				{
					submitDesc.modelMatrices = mesh->GetBoneMatrices().data();
					submitDesc.matrixCount = static_cast<uint8_t>(mesh->GetBoneMatrices().size());
					// TODO(bwrsandman): Get animation frame instead of default
				}
				else
				{
					const static auto identity = glm::mat4(1.0f);
					submitDesc.modelMatrices = &identity;
					submitDesc.matrixCount = 1;
				}
				submitDesc.isSky = false;
				submitDesc.morphWithTerrain = placers.morphWithTerrain;
				submitDesc.program = submitDesc.morphWithTerrain ? objectShaderHeightMapInstanced : objectShaderInstanced;

				// TODO(bwrsandman): choose the correct LOD
				DrawMesh(*mesh, submitDesc, std::numeric_limits<uint8_t>::max());
			}

			// Animated meshes: one draw each (their instance transform carries their own bone-texture row).
			for (const auto& animated : renderCtx.animatedDrawDescs)
			{
				if (reflectionOnly)
				{
					break;
				}
				auto mesh = meshManager.Handle(animated.meshId);
				submitDesc.instanceDesc =
				    std::make_unique<graphics::InstanceDesc>(renderCtx.instanceUniformBuffer, animated.offset, 1);
				// The pose is in the bone texture (row in the instance transform, see RenderContext::boneTexture).
				const static auto identity = glm::mat4(1.0f);
				submitDesc.modelMatrices = &identity;
				submitDesc.matrixCount = 1;
				submitDesc.isSky = false;
				submitDesc.morphWithTerrain = false;
				submitDesc.program = objectShaderInstanced;
				DrawMesh(*mesh, submitDesc, std::numeric_limits<uint8_t>::max());
			}

			// Buildings under construction: the building, then its scaffold, each with its own transform.
			for (const auto& construction : renderCtx.constructionDrawDescs)
			{
				auto mesh = meshManager.Handle(construction.meshId);
				const static auto identity = glm::mat4(1.0f);
				submitDesc.modelMatrices = &identity;
				submitDesc.matrixCount = 1;
				submitDesc.isSky = false;
				submitDesc.morphWithTerrain = false;
				submitDesc.program = objectShaderInstanced;
				for (const auto& [offset, part] : {std::pair {construction.buildingOffset, L3DMeshSubmitDesc::Scaffold::Without},
				                                   std::pair {construction.scaffoldOffset, L3DMeshSubmitDesc::Scaffold::Only}})
				{
					submitDesc.instanceDesc =
					    std::make_unique<graphics::InstanceDesc>(renderCtx.instanceUniformBuffer, offset, 1);
					submitDesc.scaffold = part;
					DrawMesh(*mesh, submitDesc, std::numeric_limits<uint8_t>::max());
				}
				submitDesc.scaffold = L3DMeshSubmitDesc::Scaffold::Hidden;
			}

			// Debug
			if (desc.viewId == graphics::RenderPass::Main)
			{
				for (const auto& [meshId, placers] : renderCtx.instancedDrawDescs)
				{
					auto mesh = meshManager.Handle(meshId);
					if (!mesh->ContainsLandscapeFeature() || mesh->GetFootprints().empty())
					{
						continue;
					}
				}
				if (renderCtx.boundingBox)
				{
					const auto boundBoxOffset = static_cast<uint32_t>(renderCtx.instanceUniforms.size() / 2);
					const auto boundBoxCount = static_cast<uint32_t>(renderCtx.instanceUniforms.size() / 2);
					renderCtx.boundingBox->GetVertexBuffer().Bind();
					bgfx::setInstanceDataBuffer(toBgfx(renderCtx.instanceUniformBuffer), boundBoxOffset, boundBoxCount);
					bgfx::setState(k_BgfxDefaultStateInvertedZ | BGFX_STATE_PT_LINES);
					bgfx::submit(static_cast<bgfx::ViewId>(desc.viewId), toBgfx(debugShaderInstanced->GetRawHandle()));
				}
				if (renderCtx.footpaths)
				{
					renderCtx.footpaths->GetVertexBuffer().Bind();
					bgfx::setState(k_BgfxDefaultStateInvertedZ | BGFX_STATE_PT_LINES);
					bgfx::submit(static_cast<bgfx::ViewId>(desc.viewId), toBgfx(debugShader->GetRawHandle()));
				}
				if (renderCtx.streams)
				{
					renderCtx.streams->GetVertexBuffer().Bind();
					bgfx::setState(k_BgfxDefaultStateInvertedZ | BGFX_STATE_PT_LINES);
					bgfx::submit(static_cast<bgfx::ViewId>(desc.viewId), toBgfx(debugShader->GetRawHandle()));
				}
			}
		}

		{
			auto subSection =
			    profiler.BeginScoped(desc.viewId == RenderPass::Reflection ? Profiler::Stage::ReflectionDrawSprites
			                                                               : Profiler::Stage::MainPassDrawSprites);

			if (desc.drawSprites)
			{
				using namespace ecs::components;

				auto& registry = Locator::entitiesRegistry::value();
				registry.Each<const Sprite, const Transform>(
				    [this, &spriteShader, &desc](const Sprite& sprite, const Transform& transform) {
					    glm::mat4 modelMatrix = glm::mat4(1.0f);
					    modelMatrix = glm::translate(modelMatrix, transform.position);
					    modelMatrix *= glm::mat4(transform.rotation);
					    modelMatrix = glm::scale(modelMatrix, transform.scale);

					    glm::vec4 u_sampleRect(sprite.uvExtent, sprite.uvMin);

					    bgfx::setTransform(glm::value_ptr(modelMatrix));
					    spriteShader->SetUniformValue("u_sampleRect", glm::value_ptr(u_sampleRect));
					    spriteShader->SetUniformValue("u_tint", glm::value_ptr(sprite.tint));
					    const glm::vec4 u_spriteMode(sprite.fullColour ? 1.0f : 0.0f, sprite.flat ? 1.0f : 0.0f, 0.0f, 0.0f);
					    spriteShader->SetUniformValue("u_spriteMode", glm::value_ptr(u_spriteMode));
					    spriteShader->SetTextureSampler("s_diffuse", 0, sprite.texture);

					    _plane->GetVertexBuffer().Bind();

					    bgfx::setState(0 | (sprite.onTop ? BGFX_STATE_DEPTH_TEST_ALWAYS : BGFX_STATE_DEPTH_TEST_GREATER) |
					                   BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
					                   (sprite.darkens ? BGFX_STATE_BLEND_ALPHA
					                                   : BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE)) |
					                   BGFX_STATE_BLEND_EQUATION(BGFX_STATE_BLEND_EQUATION_ADD));

					    bgfx::submit(static_cast<bgfx::ViewId>(desc.viewId), toBgfx(spriteShader->GetRawHandle()));
				    });
				// The particle systems' atoms (src/PSys), after the sprites, as the original draws its spells after the
				// world's objects; not in the sea's reflection.
				if (desc.viewId == RenderPass::Main)
				{
					if (const auto* particleShader = _shaderManager->GetShader("Particle"); particleShader != nullptr)
					{
						psys::Draw(static_cast<uint16_t>(desc.viewId), toBgfx(particleShader->GetRawHandle()).idx);
					}
				}
			}
		}
	}

	// Enable stats or debug text.
	auto debugMode = BGFX_DEBUG_NONE;
	if (_bgfxDebug)
	{
		debugMode |= BGFX_DEBUG_STATS;
	}
	if (desc.wireframe)
	{
		debugMode |= BGFX_DEBUG_WIREFRAME;
	}
	if (_bgfxProfile)
	{
		debugMode |= BGFX_DEBUG_PROFILER;
	}
	bgfx::setDebug(debugMode);
}

void Renderer::Frame() noexcept
{
	// Advance to next frame. Process submitted rendering primitives.
	bgfx::frame();
}

void Renderer::RequestScreenshot(const std::filesystem::path& filepath) noexcept
{
	const bgfx::FrameBufferHandle mainBackbuffer = BGFX_INVALID_HANDLE;
	bgfx::requestScreenShot(mainBackbuffer, filepath.string().c_str());
}
