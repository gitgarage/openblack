/*******************************************************************************
 * Copyright (c) 2018-2026 openblack developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/openblack/openblack
 *
 * openblack is licensed under the GNU General Public License version 3.
 *******************************************************************************/

#include "HelpSpirits.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include <MorphFile.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <spdlog/spdlog.h>

#include "3D/L3DMesh.h"
#include "Camera/Camera.h"
#include "ECS/Components/Mesh.h"
#include "ECS/Components/Sprite.h"
#include "ECS/Components/Transform.h"
#include "ECS/Registry.h"
#include "EngineConfig.h"
#include "FileSystem/FileSystemInterface.h"
#include <bgfx/bgfx.h>

#include "Graphics/Texture2D.h"
#include "HandPoses.h"
#include "Locator.h"
#include "Resources/ResourcesInterface.h"

namespace openblack::helpspirits
{
namespace
{
// HelpDudeAnim.h's numbers for the ones used here.
enum Anim : size_t
{
	k_Stand = 0,
	k_Hover = 1,
	k_HoverL = 2,
	k_HoverR = 3,
	k_Wings = 4,
	k_PointLIntro = 27,
	k_PointL = 28,
	k_PointRIntro = 29,
	k_PointR = 30,
	k_HoverStable = 31,
};

// The original's Zoomer: a value going to a destination over a time, starting at the speed it had and arriving at
// rest (a cubic through both ends and both speeds).
struct Zoomer
{
	float value = 0.0f;
	float speed = 0.0f;
	float start = 0.0f;
	float startSpeed = 0.0f;
	float destination = 0.0f;
	float time = 0.0f;
	float duration = 0.0f;

	void SetPosition(float v)
	{
		value = start = destination = v;
		speed = startSpeed = 0.0f;
		time = duration = 0.0f;
	}
	void SetDestination(float v, float seconds)
	{
		if (seconds < 0.001f)
		{
			SetPosition(v);
			return;
		}
		start = value;
		startSpeed = speed;
		destination = v;
		time = 0.0f;
		duration = seconds;
	}
	[[nodiscard]] bool Arrived() const { return time >= duration; }
	void Update(float dt)
	{
		time += dt;
		if (time >= duration)
		{
			time = duration;
			value = destination;
			speed = 0.0f;
			return;
		}
		// Hermite: from (start, startSpeed) to (destination, 0) over duration.
		const float d = duration;
		const float s = time / d;
		const float h00 = 2 * s * s * s - 3 * s * s + 1;
		const float h10 = s * s * s - 2 * s * s + s;
		const float h01 = -2 * s * s * s + 3 * s * s;
		value = h00 * start + h10 * d * startSpeed + h01 * destination;
		const float d00 = (6 * s * s - 6 * s) / d;
		const float d10 = 3 * s * s - 4 * s + 1;
		const float d01 = (-6 * s * s + 6 * s) / d;
		speed = d00 * start + d10 * startSpeed + d01 * destination;
	}
};

struct Dude
{
	bool loaded = false;
	std::string meshName;
	std::vector<std::optional<morph::Animation>> anims;
	float depth = 8.0f;
	float size = 0.025f;
	float tilt = 0.0f;
	float talkDepth = 0.0f; // its depth while talking
	bool talking = false;
	float zoomBlend = 0.0f;
	float glowSize = 0.0f; // the Yogi's glow beneath him (0 for none)
	glm::vec3 glowOffset {0.0f};
	bool evil = false;

	entt::entity entity = entt::null;
	entt::entity glow = entt::null;
	Zoomer hoverX;
	Zoomer hoverY;
	float visibility = 0.0f;
	float targetVisibility = 0.0f;
	bool homing = false; // going off its edge; hidden once there
	float wingTime = 0.0f;
	float wingsBlend = 0.0f;
	// Pointing: at a place in the world, how far into the point it is (0-1).
	bool pointing = false;
	glm::vec3 pointAt {0.0f};
	float pointBlend = 0.0f;
	int pointSide = 0; // 1: to the place's left (HELPDUDESTATE_POINT_LEFT), 2: to its right; 0 not pointing
	// An animation played once.
	int playing = -1;
	float playSpeed = 1.0f;
	float playTime = 0.0f;
	float scale = 1.0f; // the file's Scale (its mesh's): how big it is on screen, with Size and Depth
	float pointX = 0.0f; // where what it points at is on screen, in hover units
	float pointY = 0.0f;
};

std::array<Dude, 2> g_dudes;
std::mt19937 g_random(1234);

Dude& Of(Spirit spirit)
{
	return g_dudes.at(static_cast<size_t>(spirit));
}

float Random(float range)
{
	return std::uniform_real_distribution<float>(0.0f, range)(g_random);
}

// The screen's width over its height, from the camera's projection.
float Aspect()
{
	const auto& projection = Locator::camera::value().GetProjectionMatrix();
	return projection[0][0] != 0.0f ? projection[1][1] / projection[0][0] : 16.0f / 9.0f;
}

// The original's hover position: x -1 (left edge) to 1 (right), y down the screen in half-widths from the middle.
// The point in front of the camera there, `depth` along the camera's view.
glm::vec3 HoverTo3D(float x, float y, float depth)
{
	const auto& camera = Locator::camera::value();
	const glm::vec2 screen((x + 1.0f) * 0.5f, 0.5f + y * Aspect() * 0.5f);
	glm::vec3 origin;
	glm::vec3 direction;
	camera.DeprojectScreenToWorld(screen, origin, direction);
	glm::vec3 centreOrigin;
	glm::vec3 centre;
	camera.DeprojectScreenToWorld(glm::vec2(0.5f), centreOrigin, centre);
	const float along = glm::dot(direction, centre);
	return camera.GetOrigin() + direction * (depth / (along > 0.01f ? along : 1.0f));
}

// A place in the world as a hover position (false if behind the camera).
bool WorldToHover(const glm::vec3& position, float& x, float& y)
{
	const auto clip = Locator::camera::value().GetViewProjectionMatrix() * glm::vec4(position, 1.0f);
	if (clip.w <= 0.001f)
	{
		return false;
	}
	x = clip.x / clip.w;
	y = -clip.y / clip.w / Aspect();
	return true;
}

// The original's HelpDude::Sethoverx/Sethovery with clamp: kept to 0.75 of the way to the sides, 0.6 up and down.
void FlyToHover(Dude& dude, float x, float y, float seconds, bool clamp)
{
	if (clamp)
	{
		x = glm::clamp(x, -0.75f, 0.75f);
		y = glm::clamp(y, -0.6f, 0.6f);
	}
	dude.hoverX.SetDestination(x, seconds);
	dude.hoverY.SetDestination(y, seconds);
}

// Its home, off its edge of the screen (the original's GetHomeIdx): the Demon's the left, the Yogi's the right.
glm::vec2 Home(const Dude& dude)
{
	return {dude.evil ? -2.0f : 2.0f, 0.0f};
}

// Data/HelpSprite/<file>: "LiOnHeAd", then the "helpdude" segment (bw1-decomp HelpDude::Load).
bool LoadDude(Dude& dude, const std::string& file, const std::string& meshName)
{
	auto& fileSystem = Locator::filesystem::value();
	const auto path = fileSystem.GetPath<filesystem::Path::Data>() / "HelpSprite" / file;
	try
	{
		const auto segment = handposes::Segment(fileSystem.ReadAll(path), "helpdude");
		size_t at = 0;
		const auto need = [&](size_t n) {
			if (at + n > segment.size())
			{
				throw std::runtime_error("ends early");
			}
		};
		const auto read = [&](auto& value) {
			need(sizeof(value));
			std::memcpy(&value, segment.data() + at, sizeof(value));
			at += sizeof(value);
		};
		constexpr size_t k_FileName = 128;
		int32_t boneCount = 0;
		int32_t animCount = 0;
		float scale = 0.0f;
		read(boneCount);
		read(animCount);
		read(scale);
		dude.scale = scale;
		need(k_FileName * (1 + static_cast<size_t>(animCount)));
		// Which animations it has: those with a file name (HelpDude::LoadAnim keeps nothing for the rest).
		std::vector<bool> named(static_cast<size_t>(animCount));
		for (size_t i = 0; i < named.size(); ++i)
		{
			named[i] = segment[at + k_FileName * (1 + i)] != 0;
		}
		at += k_FileName * (1 + static_cast<size_t>(animCount));
		int32_t withMesh = 0;
		read(withMesh);
		if (withMesh == 0)
		{
			throw std::runtime_error("no mesh in it");
		}
		uint32_t meshSize = 0;
		read(meshSize);
		need(meshSize);
		const std::vector<uint8_t> mesh(segment.begin() + static_cast<std::ptrdiff_t>(at),
		                                segment.begin() + static_cast<std::ptrdiff_t>(at + meshSize));
		at += meshSize;
		Locator::resources::value().GetMeshes().Load(meshName, resources::L3DLoader::FromBufferTag {}, meshName, mesh);

		int32_t stored = 0;
		read(stored);
		dude.anims.assign(static_cast<size_t>(std::max(stored, 0)), std::nullopt);
		for (size_t index = 0; index < dude.anims.size(); ++index)
		{
			auto& anim = dude.anims[index];
			if (index >= named.size() || !named[index])
			{
				continue;
			}
			int32_t size = 0;
			read(size);
			if (size == 0)
			{
				continue;
			}
			const size_t end = at + static_cast<size_t>(size);
			auto& animation = anim.emplace();
			read(animation.header);
			const auto ints = [&](std::vector<uint32_t>& out, uint32_t n) {
				out.resize(n);
				need(n * sizeof(uint32_t));
				std::memcpy(out.data(), segment.data() + at, n * sizeof(uint32_t));
				at += n * sizeof(uint32_t);
			};
			ints(animation.rotatedJointIndices, animation.header.rotatedJointCount);
			ints(animation.translatedJointIndices, animation.header.translatedJointCount);
			animation.keyframes.resize(animation.header.frameCount);
			for (auto& frame : animation.keyframes)
			{
				frame.eulerAngles.resize(animation.header.rotatedJointCount);
				frame.translations.resize(animation.header.translatedJointCount);
				const size_t r = frame.eulerAngles.size() * sizeof(frame.eulerAngles[0]);
				const size_t t = frame.translations.size() * sizeof(frame.translations[0]);
				need(r + t);
				std::memcpy(frame.eulerAngles.data(), segment.data() + at, r);
				std::memcpy(frame.translations.data(), segment.data() + at + r, t);
				at += r + t;
			}
			// The size counts itself (HelpDude::SaveAnim: GetWriteSize() + 4).
			at = std::max(at, end - 4);
		}
		int32_t bones[7]; // HelpDudeBones: the eyes' bones and the head's
		int32_t pupilBone = 0;
		float pupil[3];
		int32_t emotion = 0;
		int32_t emotionSize = 0;
		read(bones);
		read(pupilBone);
		read(pupil);
		read(emotion);
		read(emotionSize);
		need(static_cast<size_t>(emotionSize));
		at += static_cast<size_t>(emotionSize);
		read(dude.depth);
		read(dude.size);
		read(dude.tilt);
		int32_t infoCount = 0;
		read(infoCount);
		for (int32_t i = 0; i < infoCount; ++i)
		{
			int32_t sounds = 0;
			read(sounds);
			constexpr size_t k_Sound = 12; // HelpDudeSound: sample, optional, time
			need(static_cast<size_t>(sounds) * k_Sound + 8);
			at += static_cast<size_t>(sounds) * k_Sound + 8; // and its LoopStart, LoopEnd
		}
		float finger[3]; // FingerSide, FingerForward, TalkDepth
		read(finger);
		dude.talkDepth = finger[2];
		need(static_cast<size_t>(animCount));
		at += static_cast<size_t>(animCount); // LipSyncAnimFlags
		read(dude.glowSize);
		read(dude.glowOffset);
		dude.meshName = meshName;
		dude.loaded = true;

		SPDLOG_LOGGER_INFO(spdlog::get("game"), "Advisor {}: {} bones, {} animations, depth {:.3f} (talking {:.3f}), size {:.5f}, tilt {:.3f}",
		                   file, boneCount, stored, dude.depth, dude.talkDepth, dude.size, dude.tilt);
		return true;
	}
	catch (const std::exception& e)
	{
		SPDLOG_LOGGER_ERROR(spdlog::get("game"), "Advisor {}: {}", file, e.what());
		return false;
	}
}

// The sprite sheet LH3DSprite draws by default, Data/Textures/smoke.raw (alpha smokea.raw), whose rows 0-1 are the 16
// frames of a soft white puff. Made once, never freed (as CHLApi's sheets).
graphics::Texture2D* PuffSheet()
{
	static graphics::Texture2D* s_sheet = nullptr;
	static bool s_tried = false;
	if (s_tried)
	{
		return s_sheet;
	}
	s_tried = true;
	try
	{
		auto& fs = Locator::filesystem::value();
		const auto textures = fs.GetPath<filesystem::Path::Data>() / "Textures";
		const auto rgb = fs.ReadAll(textures / "smoke.raw");
		const auto alpha = fs.ReadAll(textures / "smokea.raw");
		constexpr size_t k_Texels = 256 * 256;
		if (rgb.size() >= k_Texels * 3 && alpha.size() >= k_Texels)
		{
			std::vector<uint8_t> pixels(k_Texels * 4);
			for (size_t t = 0; t < k_Texels; ++t)
			{
				pixels[t * 4 + 0] = rgb[t * 3 + 0];
				pixels[t * 4 + 1] = rgb[t * 3 + 1];
				pixels[t * 4 + 2] = rgb[t * 3 + 2];
				pixels[t * 4 + 3] = alpha[t];
			}
			s_sheet = new graphics::Texture2D("advisor glow");
			s_sheet->Create(256, 256, 1, graphics::TextureFormat::RGBA8, graphics::Wrapping::ClampEdge, graphics::Filter::Linear,
			                bgfx::copy(pixels.data(), static_cast<uint32_t>(pixels.size())));
		}
	}
	catch (const std::exception& e)
	{
		SPDLOG_LOGGER_WARN(spdlog::get("game"), "Advisor glow sheet: {}", e.what());
	}
	return s_sheet;
}

// HelpDude::Draw's glow (the Yogi's): a puff from the sheet squashed to 0.3 of its width, GlowSize x his scale x 100
// across, at GlowOffset in his model, stepping a frame each 200 ms, as bright as he is visible.
void PlaceGlow(Dude& dude, const glm::vec3& position, const glm::mat3& rotation, float scale, float seconds)
{
	auto& registry = Locator::entitiesRegistry::value();
	auto* sheet = PuffSheet();
	if (dude.glowSize <= 0.0f || sheet == nullptr)
	{
		return;
	}
	if (!registry.Valid(dude.glow) || !registry.AllOf<ecs::components::Sprite>(dude.glow) ||
	    registry.Get<ecs::components::Sprite>(dude.glow).texture.id != sheet->GetNativeHandle().id)
	{
		dude.glow = registry.Create();
		auto& sprite = registry.Assign<ecs::components::Sprite>(dude.glow, sheet->GetNativeHandle(), glm::vec2(0.0f),
		                                                         glm::vec2(0.125f), glm::vec4(1.0f));
		sprite.fullColour = true;
		sprite.darkens = true; // blended over what is behind: added as light it burnt out to a white blob
		registry.Assign<ecs::components::Transform>(dude.glow, glm::vec3(0.0f), glm::mat3(1.0f), glm::vec3(0.0f));
	}
	static float s_glowTime = 0.0f;
	s_glowTime += seconds;
	const int frame = static_cast<int>(s_glowTime * 1000.0f / 200.0f) & 15;
	auto& sprite = registry.Get<ecs::components::Sprite>(dude.glow);
	sprite.uvMin = glm::vec2(static_cast<float>(frame % 8) * 0.125f, static_cast<float>(frame / 8) * 0.125f) + glm::vec2(0.002f);
	sprite.uvExtent = glm::vec2(0.121f);
	sprite.tint = glm::vec4(1.0f, 1.0f, 1.0f, 0.6f * dude.visibility);
	auto& transform = registry.Get<ecs::components::Transform>(dude.glow);
	// (Its size and place both in hundreds of the model's units: GlowOffset's -0.28 is at his seat.)
	const float halfWidth = dude.glowSize * scale * 100.0f;
	transform.position = position + rotation * (dude.glowOffset * 100.0f * scale);
	transform.scale = dude.visibility > 0.0f ? glm::vec3(halfWidth, halfWidth * 0.3f, 1.0f) : glm::vec3(0.0f);
}

// A place on screen, a circle round it with full strength inside InnerRadius, fading to none at OuterRadius
// (HoverZone::Feel).
float ZoneFeel(float x, float y, float zoneX, float zoneY, float inner, float outer, float strength)
{
	const float dx = x - zoneX;
	const float dy = y - zoneY;
	const float distance = std::sqrt(dx * dx + dy * dy);
	if (strength == 0.0f || distance > outer)
	{
		return 0.0f;
	}
	return distance < inner ? strength : (1.0f - (distance - inner) / (outer - inner)) * strength;
}

// How much it likes being at a hover place (HelpDude::Feel with UpdateHoverZones' zones): not off the screen's
// edges (nearer the middle up and down in a widescreen cutscene), not over the middle of it, not over the other
// advisor, and, pointing, near what it points at but not on it.
float Feel(const Dude& dude, const Dude& other, float x, float y)
{
	const float maxY = Locator::camera::value().IsScriptControlled() ? 0.6f * 0.75f : 0.6f;
	float edge = 0.0f;
	const float ax = std::abs(x);
	const float ay = std::abs(y) * (4.0f / 3.0f);
	if (ax > 0.75f)
	{
		edge += ax - 0.75f;
	}
	if (ay > maxY)
	{
		edge += (ay - maxY) * 2.0f;
	}
	float feel = edge * edge * -250.0f;
	if (other.visibility > 0.0f && other.pointBlend == 0.0f)
	{
		const float outer = other.size * other.scale / other.depth * 2.0f;
		feel += ZoneFeel(x, y, other.hoverX.value, other.hoverY.value, outer * 0.1f, outer, -0.5f);
	}
	if (dude.pointing)
	{
		feel += ZoneFeel(x, y, dude.pointX, dude.pointY, 0.2f, 0.6f, 4.0f);
		feel += ZoneFeel(x, y, dude.pointX, dude.pointY, 0.08f, 0.16f, -1.0f);
	}
	feel += ZoneFeel(x, y, 0.0f, 0.0f, 0.0f, 0.6f, -4.0f);
	return feel;
}

// HelpDude::UpdateHoverPos: once it has got where it was going, a try somewhere near (nearer the better it feels
// where it is), gone to at a chance of how much better that feels times `rate` (1 hovering, a third pointing); where
// it feels very bad, the best of 25 tries further afield, at once.
void UpdateHoverPos(Dude& dude, const Dude& other, float rate)
{
	if (!dude.hoverX.Arrived() || !dude.hoverY.Arrived())
	{
		return;
	}
	const float x = dude.hoverX.value;
	const float y = dude.hoverY.value;
	const float feel = Feel(dude, other, x, y);
	float range = (std::abs(feel) + 1.0f) * 0.3f;
	int tries = 1;
	const bool desperate = feel < -1.0f;
	if (desperate)
	{
		range = 2.0f;
		tries = 25;
	}
	float bestFeel = feel;
	float bestX = x;
	float bestY = y;
	for (int i = 0; i < tries; ++i)
	{
		const float nx = glm::clamp(x + Random(range * 2.0f) - range, -1.0f, 1.0f);
		const float ny = glm::clamp(y + Random(range * 2.0f) - range, -0.75f, 0.75f);
		const float newFeel = Feel(dude, other, nx, ny);
		if (newFeel > bestFeel)
		{
			bestFeel = newFeel;
			bestX = nx;
			bestY = ny;
		}
	}
	const float chance = desperate ? 1.0f : (bestFeel > feel ? rate * (bestFeel - feel) : 0.0f);
	if (desperate || (chance > 0.0f && Random(1.0f) <= chance))
	{
		FlyToHover(dude, bestX, bestY, 1.0f, true);
	}
}

const morph::Animation* AnimOf(const Dude& dude, size_t anim)
{
	return anim < dude.anims.size() && dude.anims[anim] && !dude.anims[anim]->keyframes.empty() ? &*dude.anims[anim] : nullptr;
}

// Where `seconds` into a cyclic animation is, in its keyframes (its header's duration is in milliseconds).
float CycleFrame(const morph::Animation& anim, float seconds)
{
	const float duration = std::max(static_cast<float>(anim.header.unknown0x0), 1.0f) / 1000.0f;
	return std::fmod(seconds / duration, 1.0f) * static_cast<float>(anim.keyframes.size());
}

// A once-through animation `t` (0-1) of the way through, in its keyframes.
float OnceFrame(const morph::Animation& anim, float t)
{
	return glm::clamp(t, 0.0f, 1.0f) * static_cast<float>(anim.keyframes.size() - 1);
}

void Pose(Dude& dude, float dt)
{
	auto& meshes = Locator::resources::value().GetMeshes();
	if (!meshes.Contains(entt::hashed_string(dude.meshName.c_str()).value()))
	{
		return;
	}
	const auto& mesh = *meshes.Handle(entt::hashed_string(dude.meshName.c_str()).value());
	const auto* stand = AnimOf(dude, k_Stand);
	const auto* hover = AnimOf(dude, k_Hover);
	const auto* stable = AnimOf(dude, k_HoverStable);
	if (hover == nullptr)
	{
		return;
	}
	// HelpDude::ApplyWingsHover: hovering about, its Hover cycle; busy (pointing, playing), its HoverStable one,
	// blended between over a second. Its wings flap on top (Wings, against its own first frame).
	const bool busy = dude.pointing || dude.playing >= 0;
	dude.wingsBlend = glm::clamp(dude.wingsBlend + (busy ? dt : -dt), 0.0f, 1.0f);
	std::vector<glm::mat4> local;
	if (dude.wingsBlend <= 0.0f || stable == nullptr)
	{
		local = morphposes::CreatureBones(mesh, *hover, CycleFrame(*hover, dude.wingTime), stand);
	}
	else if (dude.wingsBlend >= 1.0f)
	{
		local = morphposes::CreatureBones(mesh, *stable, CycleFrame(*stable, dude.wingTime), stand);
	}
	else
	{
		local = morphposes::BlendedCreatureBones(mesh,
		                                         {{hover, CycleFrame(*hover, dude.wingTime), 1.0f - dude.wingsBlend},
		                                          {stable, CycleFrame(*stable, dude.wingTime), dude.wingsBlend}},
		                                         stand);
	}
	if (const auto* wings = AnimOf(dude, k_Wings))
	{
		morphposes::AddDifference(mesh, local, *wings, CycleFrame(*wings, dude.wingTime), 0);
	}
	// Its flap: a little quicker for the Demon (HelpDude::ApplyWingsHover).
	dude.wingTime += (dude.evil ? 1.01f : 0.99981135f) * dt;

	// Pointing (HelpDude::Update1, HELPDUDESTATE_POINT_*): the arm raised over a quarter second (PointL/R In), then
	// aimed by the PointL/R animation, taken to the place's direction from the hand across the screen.
	dude.pointBlend = glm::clamp(dude.pointBlend + (dude.pointing ? dt * 4.0f : -dt * 4.0f), 0.0f, 1.0f);
	if (dude.pointBlend > 0.0f)
	{
		float px = 0.0f;
		float py = 0.0f;
		// Off the screen (or behind the camera) it is pointed at where it leaves the screen, and not gone to.
		const bool onScreen = WorldToHover(dude.pointAt, px, py) && std::abs(px) <= 1.0f && std::abs(py) <= 0.75f;
		if (!onScreen)
		{
			px = glm::clamp(px, -1.0f, 1.0f);
			py = glm::clamp(py, -0.75f, 0.75f);
		}
		dude.pointX = px;
		dude.pointY = py;
		// Which side of the place it points from (HelpDude::SetState, HELPDUDESTATE_POINT): its left (state LEFT, the
		// PointL animations) when it is left of the place or the place is right of 0.25, its right when it is right of
		// it or the place is left of -0.25 - that one winning - and kept while it points. (Taken the other way round,
		// with the y below upside down, they pointed away: Mike, 2026-10-22.)
		if (dude.pointSide == 0 && dude.pointing)
		{
			dude.pointSide = 1;
			if (dude.hoverX.value > px || px < -0.25f)
			{
				dude.pointSide = 2;
			}
		}
		const bool left = dude.pointSide != 2;
		if (const auto* intro = AnimOf(dude, left ? k_PointLIntro : k_PointRIntro))
		{
			morphposes::AddDifference(mesh, local, *intro, OnceFrame(*intro, dude.pointBlend), 0);
		}
		if (const auto* aim = AnimOf(dude, left ? k_PointL : k_PointR))
		{
			// HelpDude::Update2: from the place to its hand on the screen (here its hover place), x turned about for
			// the left side, an angle of 100 degrees across the animation either way of its middle.
			float dx = dude.hoverX.value - px;
			const float dy = dude.hoverY.value - py;
			if (left)
			{
				dx = -dx;
			}
			const float unclamped = dude.pointBlend * std::atan2(dy, dx) / glm::radians(100.0f) + 0.5f;
			const float blend = glm::clamp(unclamped, 0.0f, 1.0f);
			const auto middle = static_cast<size_t>(static_cast<float>(aim->keyframes.size() - 1) * 0.5f);
			morphposes::AddDifference(mesh, local, *aim, OnceFrame(*aim, blend), middle);
			// It goes to point: to the place's height when its arm can't reach that far up or down, and a fifth of the
			// screen to its side when nearer than 0.16 or further than 0.3 across.
			if (dude.pointing && onScreen)
			{
				if (unclamped != blend)
				{
					dude.hoverY.SetDestination(glm::clamp(py, -0.6f, 0.6f), 1.0f);
				}
				const float across = dude.hoverX.value - px;
				if (std::abs(across) < 0.16f || std::abs(across) > 0.3f)
				{
					dude.hoverX.SetDestination(glm::clamp(across < 0.0f ? px - 0.2f : px + 0.2f, -0.75f, 0.75f), 1.0f);
				}
			}
		}
	}
	// A played animation (HELPDUDESTATE_ANIM), against its own first frame.
	if (dude.playing >= 0)
	{
		if (const auto* anim = AnimOf(dude, static_cast<size_t>(dude.playing)))
		{
			const float duration = std::max(static_cast<float>(anim->header.unknown0x0), 1.0f) / 1000.0f;
			const float t = dude.playSpeed * dude.playTime / duration;
			morphposes::AddDifference(mesh, local, *anim, OnceFrame(*anim, t), 0);
			dude.playTime += dt;
			if (t > 1.0f)
			{
				dude.playing = -1;
			}
		}
		else
		{
			dude.playing = -1;
		}
	}
	// Leaning into its sideways speed (Hover left / Hover right), less when busy.
	float lean = dude.hoverX.speed / std::max(dude.size, 0.0001f) / 40.0f;
	lean *= 1.0f - dude.wingsBlend * 0.8f;
	if (std::abs(lean) > 0.01f && dude.pointBlend == 0.0f)
	{
		if (const auto* side = AnimOf(dude, lean < 0.0f ? k_HoverL : k_HoverR))
		{
			morphposes::AddDifference(mesh, local, *side, OnceFrame(*side, std::abs(lean)), 0);
		}
	}

	// HelpDude::Draw: lit by a sun moved to the screen's left edge, 10 behind the camera, whatever the time of day.
	meshes.Handle(entt::hashed_string(dude.meshName.c_str()).value())->SetOwnLight(HoverTo3D(-1.0f, 0.0f, -10.0f));

	auto& registry = Locator::entitiesRegistry::value();
	const auto meshId = entt::hashed_string(dude.meshName.c_str()).value();
	// (A new land empties the registry, and its number may have gone to something else since.)
	if (!registry.Valid(dude.entity) || !registry.AllOf<ecs::components::Mesh>(dude.entity) ||
	    registry.Get<ecs::components::Mesh>(dude.entity).id != meshId)
	{
		dude.entity = registry.Create();
		registry.Assign<ecs::components::Transform>(dude.entity, glm::vec3(0.0f), glm::mat3(1.0f), glm::vec3(0.0f));
		registry.Assign<ecs::components::Mesh>(dude.entity, meshId, static_cast<int8_t>(0), static_cast<int8_t>(0));
	}
	// HelpDude::Update1: the camera's turn undone (facing it), scaled by Size, tilted by Tilt, turned toward the
	// middle by 0.8 of its hover x, at the hover position Depth in front of the camera.
	// (The camera's own axes, taken from the middle of the screen, made exactly square: the renderer places a model
	// assuming its turn is a pure one.)
	// The original draws them over the world. Seen through the camera, one k times as near and k times as small looks
	// just the same, so they are drawn that much nearer - in front of the sand and trees a low camera has nearer
	// than their Depth - and smaller.
	// Three times the near plane's distance (Game.cpp's: 0.1 while a script films, else the config's).
	const auto& camera = Locator::camera::value();
	const float nearClip = camera.IsScriptControlled() ? std::min(Locator::config::value().cameraNearClip, 0.1f)
	                                                   : Locator::config::value().cameraNearClip;
	// Talking, nearer: in at 2 a second, back at 1.5, eased (HelpDude::Smoothify), to TalkDepth.
	dude.zoomBlend = glm::clamp(dude.zoomBlend + (dude.talking ? dt * 2.0f : -dt * 1.5f), 0.0f, 1.0f);
	const float eased = (1.0f - std::cos(dude.zoomBlend * glm::pi<float>())) * 0.5f;
	const float ownDepth = dude.talkDepth > 0.0f ? glm::mix(dude.depth, dude.talkDepth, eased) : dude.depth;
	const float nearer = std::min(nearClip * 3.0f / ownDepth, 1.0f);
	const float depth = ownDepth * nearer;
	const auto centre = HoverTo3D(dude.hoverX.value, dude.hoverY.value, depth);
	const auto middle = HoverTo3D(0.0f, 0.0f, depth);
	const auto forward = glm::normalize(middle - Locator::camera::value().GetOrigin());
	auto right = HoverTo3D(0.1f, 0.0f, depth) - middle;
	right = glm::normalize(right - forward * glm::dot(right, forward));
	auto up = HoverTo3D(0.0f, -0.1f, depth) - middle;
	up = glm::normalize(up - forward * glm::dot(up, forward) - right * glm::dot(up, right));
	glm::mat3 facing(right, up, forward);
	// (LH3D's turns go the other way about each axis from glm's: its RotateY(-0.8 x) is glm's 0.8 x.)
	facing = facing * glm::mat3(glm::rotate(glm::mat4(1.0f), -dude.tilt, glm::vec3(1.0f, 0.0f, 0.0f))) *
	         glm::mat3(glm::rotate(glm::mat4(1.0f), dude.hoverX.value * 0.8f, glm::vec3(0.0f, 1.0f, 0.0f)));
	auto& transform = registry.Get<ecs::components::Transform>(dude.entity);
	transform.position = centre;
	transform.rotation = facing;
	// Faded in or out: shown from half way (the renderer has no fading for one model).
	transform.scale = glm::vec3(dude.visibility >= 0.5f ? dude.size * nearer : 0.0f);
	PlaceGlow(dude, centre, facing, dude.size * nearer, dt);
	// Test hook: OPENBLACK_TEST_ADVISOR_REST draws them unposed, to see the model as it is.
	static const bool s_rest = std::getenv("OPENBLACK_TEST_ADVISOR_REST") != nullptr;
	// Moved and posed: the draw lists made again (as the hand's are).
	registry.SetDirty();
	if (s_rest)
	{
		registry.Remove<ecs::components::Animated>(dude.entity);
		return;
	}
	registry.AssignOrReplace<ecs::components::Animated>(dude.entity, 0u, 0.0f, std::move(local));
}
} // namespace

bool Load()
{
	g_dudes[0].evil = false;
	g_dudes[1].evil = true;
	const bool good = LoadDude(g_dudes[0], "markgood.hd", "helpdude_good");
	const bool evil = LoadDude(g_dudes[1], "markevil.hd", "helpdude_evil");
	for (auto& dude : g_dudes)
	{
		const auto home = Home(dude);
		dude.hoverX.SetPosition(home.x);
		dude.hoverY.SetPosition(home.y);
	}
	return good && evil;
}

void Eject(Spirit spirit)
{
	// fn_005C32C0: from its home, to a random place in the middle half of the screen, over a second.
	// One already out stays where it is.
	auto& dude = Of(spirit);
	if (dude.targetVisibility > 0.0f && !dude.homing)
	{
		return;
	}
	const auto home = Home(dude);
	dude.hoverX.SetPosition(home.x);
	dude.hoverY.SetPosition(home.y);
	dude.homing = false;
	dude.targetVisibility = 1.0f;
	const float aspect = Aspect();
	const float x = 0.25f + Random(0.5f);
	const float y = 0.25f + Random(0.5f);
	FlyToHover(dude, x * 2.0f - 1.0f, (y * 2.0f - 1.0f) / aspect, 1.0f, true);
	SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "Advisor {} ejected, to ({:.2f}, {:.2f})", dude.evil ? "evil" : "good", x, y);
}

void Home(Spirit spirit)
{
	auto& dude = Of(spirit);
	const auto home = Home(dude);
	FlyToHover(dude, home.x, home.y, 1.0f, false);
	dude.homing = true;
	dude.pointing = false;
	dude.pointSide = 0;
}

void Appear(Spirit spirit)
{
	Of(spirit).targetVisibility = 1.0f;
	Of(spirit).homing = false;
}

void Disappear(Spirit spirit)
{
	Of(spirit).targetVisibility = 0.0f;
}

void FlyTo(Spirit spirit, float x, float y)
{
	// HelpSystem's fly (fn_005C4DA0 -> fn_005C3250): to x, y of the screen as pixels, HelpDude::FlyTo2D over a
	// second, clamped.
	// (Back to hovering: no longer pointing.)
	auto& dude = Of(spirit);
	dude.pointing = false;
	dude.pointSide = 0;
	FlyToHover(dude, x * 2.0f - 1.0f, (y * 2.0f - 1.0f) / Aspect(), 1.0f, true);
	SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "Advisor {} flies to ({:.2f}, {:.2f})", dude.evil ? "evil" : "good", x, y);
}

void PointAt(Spirit spirit, const glm::vec3& position)
{
	auto& dude = Of(spirit);
	if (!dude.pointing || glm::distance(dude.pointAt, position) > 1.0f)
	{
		dude.pointSide = 0; // a new place: its side chosen again
	}
	dude.pointing = true;
	dude.pointAt = position;
}

void StopPointing(Spirit spirit)
{
	Of(spirit).pointing = false;
}

void PlayAnim(Spirit spirit, int anim, float speed)
{
	auto& dude = Of(spirit);
	dude.playing = anim;
	dude.playSpeed = speed > 0.0f ? speed : 1.0f;
	dude.playTime = 0.0f;
}

void SetTalking(Spirit spirit, bool talking)
{
	Of(spirit).talking = talking;
}

bool Playing(Spirit spirit)
{
	return Of(spirit).playing >= 0;
}

bool Shown(Spirit spirit)
{
	return Of(spirit).visibility > 0.0f;
}

void Update(float seconds)
{
	const float dt = glm::clamp(seconds, 0.0f, 0.1f);
	for (auto& dude : g_dudes)
	{
		if (!dude.loaded)
		{
			continue;
		}
		dude.hoverX.Update(dt);
		dude.hoverY.Update(dt);
		if (dude.homing && dude.hoverX.Arrived())
		{
			dude.targetVisibility = 0.0f;
			dude.visibility = 0.0f;
			dude.homing = false;
		}
		// HelpDude::Draw: in at 3 a second, out at 2.
		if (dude.targetVisibility > dude.visibility)
		{
			dude.visibility = std::min(dude.visibility + dt * 3.0f, dude.targetVisibility);
		}
		else
		{
			dude.visibility = std::max(dude.visibility - dt * 2.0f, dude.targetVisibility);
		}
		// Hovering about where it feels best (each frame, as the original's).
		if (dude.visibility > 0.0f && !dude.homing && dude.playing < 0)
		{
			UpdateHoverPos(dude, g_dudes.at(dude.evil ? 0 : 1), dude.pointing ? 1.0f / 3.0f : 1.0f);
		}
		if (dude.visibility <= 0.0f && dude.targetVisibility <= 0.0f)
		{
			auto& registry = Locator::entitiesRegistry::value();
			if (registry.Valid(dude.entity) && registry.AllOf<ecs::components::Mesh>(dude.entity) &&
			    registry.Get<ecs::components::Mesh>(dude.entity).id == entt::hashed_string(dude.meshName.c_str()).value())
			{
				if (registry.Valid(dude.glow) && registry.AllOf<ecs::components::Transform>(dude.glow))
				{
					registry.Get<ecs::components::Transform>(dude.glow).scale = glm::vec3(0.0f);
				}
				auto& scale = registry.Get<ecs::components::Transform>(dude.entity).scale;
				if (scale != glm::vec3(0.0f))
				{
					scale = glm::vec3(0.0f);
					registry.SetDirty();
				}
			}
			continue;
		}
		Pose(dude, dt);
	}
}
} // namespace openblack::helpspirits
