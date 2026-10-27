/*******************************************************************************
 * Copyright (c) 2018-2026 openblack developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/openblack/openblack
 *
 * openblack is licensed under the GNU General Public License version 3.
 *******************************************************************************/

// The particle systems' drawing, sounds and world: what each atom is drawn as (Particle3DSprite::DrawAt with
// LH3DSprite::Draw, LH3DMist, a mesh), batched into one draw a sheet; their sounds from the spell bank's animation sound
// table; the land, the hand and the objects they are about.

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <unordered_map>

#include <bgfx/bgfx.h>
#include <entt/core/hashed_string.hpp>
#include <glm/geometric.hpp>
#include <spdlog/spdlog.h>

#include "3D/LandIslandInterface.h"
#include "Audio/AudioManagerInterface.h"
#include "Camera/Camera.h"
#include "ECS/Components/Mesh.h"
#include "ECS/Components/Transform.h"
#include "ECS/Registry.h"
#include "FileSystem/FileSystemInterface.h"
#include "Locator.h"
#include "PSys/PSysCore.h"
#include "Resources/ResourcesInterface.h"
#include "3D/L3DMesh.h"
#include "3D/AllMeshes.h"
#include "Resources/ResourceManager.h"
#include <LNDFile.h>

namespace openblack::psys::core
{
namespace
{
// ------------------------------------------------------------------------------------------------------------ sheets

struct Sheet
{
	bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
};

// Data/Textures/<name>.raw (256 x 256 RGB) and its alpha <name>a.raw; never freed (see the systems' map).
const Sheet& SheetOf(const std::string& path)
{
	static auto& s_sheets = *new std::unordered_map<std::string, Sheet>();
	// ".\Data\Textures\S_SpriteSheet1.raw": its name.
	auto name = path;
	std::replace(name.begin(), name.end(), '\\', '/');
	if (const auto slash = name.find_last_of('/'); slash != std::string::npos)
	{
		name = name.substr(slash + 1);
	}
	if (const auto dot = name.find_last_of('.'); dot != std::string::npos)
	{
		name = name.substr(0, dot);
	}
	auto [it, made] = s_sheets.try_emplace(name);
	if (!made)
	{
		return it->second;
	}
	try
	{
		auto& fs = Locator::filesystem::value();
		const auto textures = fs.GetPath<filesystem::Path::Data>() / "Textures";
		const auto rgb = fs.ReadAll(textures / (name + ".raw"));
		std::vector<uint8_t> alpha;
		if (fs.Exists(textures / (name + "a.raw")))
		{
			alpha = fs.ReadAll(textures / (name + "a.raw"));
		}
		constexpr size_t k_Texels = 256 * 256;
		if (rgb.size() >= k_Texels * 3)
		{
			std::vector<uint8_t> pixels(k_Texels * 4);
			for (size_t t = 0; t < k_Texels; ++t)
			{
				pixels[t * 4 + 0] = rgb[t * 3 + 0];
				pixels[t * 4 + 1] = rgb[t * 3 + 1];
				pixels[t * 4 + 2] = rgb[t * 3 + 2];
				pixels[t * 4 + 3] = alpha.size() >= k_Texels ? alpha[t] : 255;
			}
			it->second.texture = bgfx::createTexture2D(256, 256, false, 1, bgfx::TextureFormat::RGBA8,
			                                           BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
			                                           bgfx::copy(pixels.data(), static_cast<uint32_t>(pixels.size())));
			SPDLOG_LOGGER_INFO(spdlog::get("game"), "Effects: sheet {}", name);
		}
	}
	catch (const std::exception& e)
	{
		SPDLOG_LOGGER_WARN(spdlog::get("game"), "Effects: sheet {}: {}", name, e.what());
	}
	return it->second;
}

// ----------------------------------------------------------------------------------------------------------- batches

struct Vertex
{
	float x, y, z;
	uint32_t abgr;
	float u, v;
};

struct BatchKey
{
	uint16_t texture;
	bool additive;
	bool operator<(const BatchKey& other) const
	{
		return texture != other.texture ? texture < other.texture : additive < other.additive;
	}
};

std::map<BatchKey, std::vector<Vertex>>& Batches()
{
	static auto& s_batches = *new std::map<BatchKey, std::vector<Vertex>>();
	return s_batches;
}

uint32_t Abgr(uint32_t argb, uint8_t alpha)
{
	const uint32_t r = (argb >> 16) & 0xff;
	const uint32_t g = (argb >> 8) & 0xff;
	const uint32_t b = argb & 0xff;
	return (static_cast<uint32_t>(alpha) << 24) | (b << 16) | (g << 8) | r;
}

// A quad turned to the camera: centred at `centre`, `right` and `up` its half extents, the sheet's cell from uv0 to uv1.
void AddQuad(bgfx::TextureHandle texture, bool additive, const glm::vec3& centre, const glm::vec3& right, const glm::vec3& up,
             glm::vec2 uv0, glm::vec2 uv1, uint32_t abgr)
{
	if (!bgfx::isValid(texture))
	{
		return;
	}
	auto& vertices = Batches()[BatchKey {texture.idx, additive}];
	const glm::vec3 corners[4] = {centre - right + up, centre + right + up, centre + right - up, centre - right - up};
	const glm::vec2 uvs[4] = {{uv0.x, uv0.y}, {uv1.x, uv0.y}, {uv1.x, uv1.y}, {uv0.x, uv1.y}};
	for (const int i : {0, 1, 2, 0, 2, 3})
	{
		vertices.push_back({corners[i].x, corners[i].y, corners[i].z, abgr, uvs[i].x, uvs[i].y});
	}
}

glm::vec3 Lerp(const glm::vec3& a, const glm::vec3& b, float t)
{
	return a + (b - a) * t;
}

// The mesh entities drawn for atoms, by atom.
std::unordered_map<const Atom*, entt::entity>& MeshEntities()
{
	static auto& s_entities = *new std::unordered_map<const Atom*, entt::entity>();
	return s_entities;
}
} // namespace

void DrawAtom(const Atom& atom, float interpolation)
{
	const auto& camera = Locator::camera::value();
	const auto& particle = atom.particle;
	auto pos = Lerp(atom.lastDrawPos, atom.drawPos, interpolation);
	if (atom.followsHand && atom.collection != nullptr && atom.collection->inGlobalCoords)
	{
		pos = HandPos(); // DrawOffsetLT: where the hand is now
	}
	const float scale = std::max(atom.lastDrawScale + (atom.drawScale - atom.lastDrawScale) * interpolation, 0.0001f);
	const auto alpha = static_cast<uint8_t>((static_cast<uint32_t>(atom.drawAlpha) * particle.alpha) / 255u);
	switch (particle.kind)
	{
	case Particle::Kind::Sprite:
	{
		// Particle3DSprite::DrawAt (BW1W120 0067ae80) and LH3DSprite::Draw (BW1W120 00840530): a quad facing the camera,
		// its half width the atom's scale and half height that by its stretch; turned on the screen by the atom's yaw;
		// moved by its origin, up by half its height if CentreAtBase; its frame from the sheet, after FileOffset.
		if (particle.texture == nullptr)
		{
			break;
		}
		const float stretch = particle.stretched
		                          ? particle.previousStretch + (particle.stretch - particle.previousStretch) * interpolation
		                          : 1.0f;
		const float halfWidth = scale;
		const float halfHeight = scale * stretch;
		auto right = camera.GetRight();
		auto up = camera.GetUp();
		if (!particle.ignoreRotation)
		{
			const auto& axis = atom.drawMatrix[0];
			const float angle = std::atan2(axis.z, axis.x);
			const float c = std::cos(angle);
			const float s = std::sin(angle);
			const auto r = right * c + up * s;
			const auto u = up * c - right * s;
			right = r;
			up = u;
		}
		auto centre = pos - right * (particle.originX * halfWidth) - up * (particle.originY * halfHeight);
		if (particle.centreAtBase)
		{
			centre.y += halfHeight * 0.5f;
		}
		const int frame = (particle.frameOffset + static_cast<int>(std::floor(atom.frameTime))) & 63;
		const int perRow = std::max(particle.perRow, 1);
		const float cell = 1.0f / static_cast<float>(perRow);
		const glm::vec2 uv0(static_cast<float>(frame % perRow) * cell, static_cast<float>(frame / perRow) * cell);
		AddQuad(SheetOf(*particle.texture).texture, particle.additive, centre, right * halfWidth, up * halfHeight, uv0,
		        uv0 + glm::vec2(cell), Abgr(atom.colour, alpha));
		break;
	}
	case Particle::Kind::Mist:
	{
		// LH3DMist (BW1W120 007f9560, drawn at 007fa300): a frame of the smoke sheet's rows 2 and 3 - its counter from a
		// random 0-15 on at 0.255 a millisecond, wrapping at 900, over 20 - 20 units across at scale 1, flattened by its
		// ratio seen from the side.
		constexpr float k_MistRadius = 20.0f;
		const float counter = std::fmod(particle.mistCounter + atom.Age() * 255.0f, 900.0f);
		const int frame = static_cast<int>(counter / 20.0f) & 15;
		const float radius = k_MistRadius * scale;
		const auto toEye = glm::normalize(camera.GetOrigin() - pos);
		const float height = radius / (1.0f + (particle.mistRatio - 1.0f) * (1.0f - std::abs(toEye.y)));
		const glm::vec2 uv0 = glm::vec2(static_cast<float>(frame % 8) * 0.125f, 0.25f + static_cast<float>(frame / 8) * 0.125f) +
		                      glm::vec2(0.002f);
		AddQuad(SheetOf("smoke").texture, false, pos, camera.GetRight() * radius, camera.GetUp() * height, uv0,
		        uv0 + glm::vec2(0.121f), Abgr(atom.colour, alpha));
		break;
	}
	case Particle::Kind::Mesh:
	{
		auto& registry = Locator::entitiesRegistry::value();
		auto& entity = MeshEntities()[&atom];
		if (entity == entt::entity {} || !registry.Valid(entity))
		{
			entity = registry.Create();
			registry.Assign<ecs::components::Transform>(entity, pos, glm::mat3(1.0f), glm::vec3(1.0f));
			registry.Assign<ecs::components::Mesh>(entity, resources::HashIdentifier(static_cast<MeshId>(particle.mesh)),
			                                       static_cast<int8_t>(0), static_cast<int8_t>(-1));
		}
		auto& transform = registry.Get<ecs::components::Transform>(entity);
		const auto& m = atom.drawMatrix;
		const glm::vec3 scales(glm::length(m[0]), glm::length(m[1]), glm::length(m[2]));
		transform.position = pos;
		transform.scale = scales;
		transform.rotation = glm::mat3(m[0] / std::max(scales.x, 1e-6f), m[1] / std::max(scales.y, 1e-6f), m[2] / std::max(scales.z, 1e-6f));
		break;
	}
	case Particle::Kind::None:
		break;
	}
}

void ForgetAtom(Atom& atom)
{
	auto& entities = MeshEntities();
	const auto it = entities.find(&atom);
	if (it == entities.end())
	{
		return;
	}
	auto& registry = Locator::entitiesRegistry::value();
	if (registry.Valid(it->second))
	{
		registry.Destroy(it->second);
	}
	entities.erase(it);
}

void FlushDrawing(uint16_t viewId, uint16_t program)
{
	static bgfx::VertexLayout s_layout;
	static bool s_layoutMade = false;
	if (!s_layoutMade)
	{
		s_layoutMade = true;
		s_layout.begin()
		    .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
		    .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
		    .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
		    .end();
	}
	static const bgfx::UniformHandle s_sampler = bgfx::createUniform("s_diffuse", bgfx::UniformType::Sampler);
	const bgfx::ProgramHandle handle {program};
	for (auto& [key, vertices] : Batches())
	{
		if (vertices.empty() || !bgfx::isValid(handle))
		{
			vertices.clear();
			continue;
		}
		const auto count = static_cast<uint32_t>(vertices.size());
		if (bgfx::getAvailTransientVertexBuffer(count, s_layout) < count)
		{
			vertices.clear();
			continue;
		}
		bgfx::TransientVertexBuffer buffer;
		bgfx::allocTransientVertexBuffer(&buffer, count, s_layout);
		std::memcpy(buffer.data, vertices.data(), count * sizeof(Vertex));
		bgfx::setVertexBuffer(0, &buffer);
		bgfx::setTexture(0, s_sampler, bgfx::TextureHandle {key.texture});
		// Drawn over what is behind, not into the depth (MaterialUpdateZBuffer off), the depth inverted as the world's.
		bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_GREATER |
		               (key.additive ? BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE) : BGFX_STATE_BLEND_ALPHA));
		bgfx::submit(viewId, handle);
		vertices.clear();
	}
}

// ------------------------------------------------------------------------------------------------------------ sounds

namespace
{
// A bank's animation sound table (LHAudioAnimArrayTable: per entry its level, two ranges, surface, action and where
// its samples are in LHAudioWaveNumTable - a count then that many sample ids; 0x0fff0000 any).
struct AnimSound
{
	uint32_t level;
	uint32_t surface;
	int action;
	std::vector<int> samples;
};

struct Bank
{
	std::string file; // its file name, as its samples are known by ("spells.sad/<id>")
	std::vector<AnimSound> sounds;
};

const Bank& SpellBank()
{
	static auto& s_bank = *new Bank();
	static bool s_read = false;
	if (s_read)
	{
		return s_bank;
	}
	s_read = true;
	try
	{
		auto& fs = Locator::filesystem::value();
		std::filesystem::path found;
		fs.Iterate(fs.GetPath<filesystem::Path::Audio>(), true, [&found](const std::filesystem::path& f) {
			auto name = f.filename().string();
			std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
			if (name == "spells.sad")
			{
				found = f;
			}
		});
		if (found.empty())
		{
			return s_bank;
		}
		s_bank.file = found.filename().string();
		const auto bytes = fs.ReadAll(found);
		std::map<std::string, std::pair<size_t, size_t>> blocks; // name: offset, size
		for (size_t o = 8; o + 36 <= bytes.size();)
		{
			const std::string name(reinterpret_cast<const char*>(bytes.data() + o), strnlen(reinterpret_cast<const char*>(bytes.data() + o), 32));
			uint32_t size = 0;
			std::memcpy(&size, bytes.data() + o + 32, 4);
			blocks[name] = {o + 36, size};
			o += 36 + size;
		}
		const auto table = blocks.find("LHAudioAnimArrayTable");
		const auto waves = blocks.find("LHAudioWaveNumTable");
		if (table == blocks.end() || waves == blocks.end())
		{
			return s_bank;
		}
		const auto u32 = [&bytes](size_t at) {
			uint32_t v = 0;
			std::memcpy(&v, bytes.data() + at, 4);
			return v;
		};
		const auto [tableAt, tableSize] = table->second;
		const auto [wavesAt, wavesSize] = waves->second;
		const uint32_t count = u32(tableAt);
		for (uint32_t i = 0; i < count && 8 + (i + 1) * 24 <= tableSize; ++i)
		{
			const size_t at = tableAt + 8 + i * 24;
			AnimSound sound {u32(at), u32(at + 12), static_cast<int>(u32(at + 16)), {}};
			const uint32_t offset = u32(at + 20);
			if ((offset + 1) * 4 <= wavesSize)
			{
				const uint32_t n = u32(wavesAt + offset * 4);
				for (uint32_t k = 0; k < n && (offset + 2 + k) * 4 <= wavesSize; ++k)
				{
					sound.samples.push_back(static_cast<int>(u32(wavesAt + (offset + 1 + k) * 4)));
				}
			}
			s_bank.sounds.push_back(std::move(sound));
		}
		SPDLOG_LOGGER_INFO(spdlog::get("audio"), "Effects: {} animation sounds in {}", s_bank.sounds.size(), s_bank.file);
	}
	catch (const std::exception& e)
	{
		SPDLOG_LOGGER_WARN(spdlog::get("audio"), "Effects: spell sounds: {}", e.what());
	}
	return s_bank;
}
} // namespace

uint32_t PlaySound(const SoundAction& sound, const glm::vec3& at)
{
	// LHSamplePlayAnimEffect: the table's first entry for the action at its level (or any) - on the surface it is
	// over, if UseSurface - one of its samples at random.
	constexpr uint32_t k_Any = 0x0fff0000;
	const auto& bank = SpellBank();
	const AnimSound* chosen = nullptr;
	for (const auto& entry : bank.sounds)
	{
		if (entry.action != sound.action || entry.samples.empty())
		{
			continue;
		}
		if (entry.level != k_Any && entry.level != static_cast<uint32_t>(sound.level))
		{
			continue;
		}
		if (entry.surface != k_Any && (!sound.useSurface || entry.surface != (IsWater(at) ? 6u : 1u)) && entry.surface != 1u)
		{
			continue;
		}
		chosen = &entry;
		break;
	}
	if (chosen == nullptr)
	{
		return 0;
	}
	const int sample = chosen->samples[static_cast<size_t>(RandomInt(static_cast<int>(chosen->samples.size())))];
	const auto id = entt::hashed_string(fmt::format("{}/{}", bank.file, sample).c_str()).value();
	if (!Locator::resources::value().GetSounds().Contains(id))
	{
		return 0;
	}
	auto& audio = Locator::audio::value();
	const auto emitter = audio.CreateEmitter(id, sound.looping ? audio::PlayType::Repeat : audio::PlayType::Once, at, glm::vec3(0.0f),
	                                         glm::vec2(20.0f, 200.0f), 1.0f, audio::AudioStatus::Playing, false);
	if (emitter == entt::null)
	{
		return 0;
	}
	if (auto& registry = Locator::entitiesRegistry::value(); registry.Valid(emitter) && registry.AllOf<ecs::components::Transform>(emitter))
	{
		registry.Get<ecs::components::Transform>(emitter).position = at;
	}
	audio.PlayEmitter(emitter);
	return static_cast<uint32_t>(emitter) + 1; // 0 is none
}

void MoveSound(uint32_t emitter, const glm::vec3& at)
{
	if (emitter == 0)
	{
		return;
	}
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = static_cast<entt::entity>(emitter - 1);
	if (registry.Valid(entity) && registry.AllOf<ecs::components::Transform>(entity))
	{
		registry.Get<ecs::components::Transform>(entity).position = at;
	}
}

void StopSound(uint32_t emitter)
{
	if (emitter == 0)
	{
		return;
	}
	auto& audio = Locator::audio::value();
	const auto entity = static_cast<entt::entity>(emitter - 1);
	if (audio.EmitterExists(entity))
	{
		audio.StopEmitter(entity);
	}
}

// ------------------------------------------------------------------------------------------------------------- world

bool IsWater(const glm::vec3& point)
{
	// MapCoords::IsWater: its cell's water flag.
	constexpr float k_Cell = 10.0f;
	constexpr int k_Cells = 512;
	const int x = static_cast<int>(std::floor(point.x / k_Cell));
	const int z = static_cast<int>(std::floor(point.z / k_Cell));
	if (x < 0 || z < 0 || x >= k_Cells || z >= k_Cells)
	{
		return true;
	}
	return Locator::terrainSystem::value().GetCell(glm::u16vec2(x, z)).properties.hasWater;
}

bool ObjectPos(uint32_t object, glm::vec3& pos)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = static_cast<entt::entity>(object);
	if (!registry.Valid(entity) || !registry.AllOf<ecs::components::Transform>(entity))
	{
		return false;
	}
	pos = registry.Get<ecs::components::Transform>(entity).position;
	return true;
}

namespace
{
// An object's model's box, at its scale.
bool ObjectBox(uint32_t object, glm::vec3& size)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = static_cast<entt::entity>(object);
	if (!registry.Valid(entity) || !registry.AllOf<ecs::components::Transform, ecs::components::Mesh>(entity))
	{
		return false;
	}
	const auto& mesh = registry.Get<ecs::components::Mesh>(entity);
	auto& meshes = Locator::resources::value().GetMeshes();
	if (!meshes.Contains(mesh.id))
	{
		return false;
	}
	size = meshes.Handle(mesh.id)->GetBoundingBox().Size() * registry.Get<ecs::components::Transform>(entity).scale;
	return true;
}
} // namespace

float ObjectRadius(uint32_t object)
{
	// Object::Get2DRadius: half its width across.
	glm::vec3 size;
	return ObjectBox(object, size) ? 0.5f * std::max(size.x, size.z) : 1.0f;
}

float ObjectHeight(uint32_t object)
{
	glm::vec3 size;
	return ObjectBox(object, size) ? size.y : 2.0f;
}

bool ObjectInHand(uint32_t /*object*/)
{
	return false;
}

void SetObjectSpecular(uint32_t /*object*/, uint32_t /*colour*/) {}
} // namespace openblack::psys::core
