/******************************************************************************
 * Copyright (c) 2018-2026 openblack developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/openblack/openblack
 *
 * openblack is licensed under the GNU General Public License version 3.
 *******************************************************************************/

#include "CHLApi.h"
#include "Video/MoviePlayer.h"
#include "Camera/DefaultWorldCameraModel.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <cctype>
#include <deque>
#include <fstream>
#include <map>
#include <chrono>
#include <ctime>
#include <cmath>
#include <cstdint>

#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <LHVM.h>
#include "Common/RandomNumberManager.h"
#include "Common/RandomNumberManager.h"
#include <SDL.h>
#include <bgfx/bgfx.h>
#include <LHVMTypes.h>
#include <entt/entity/entity.hpp>
#include <entt/entity/fwd.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>
#include <glm/vec3.hpp>
#include <spdlog/spdlog.h>

#include "3D/L3DMesh.h"
#include "PSys/PSys.h"
#include "3D/LandIslandInterface.h"
#include <LNDFile.h>
#include "3D/SkyInterface.h"
#include "3D/TempleInteriorInterface.h"
#include "Input/GameActionMapInterface.h"
#include "Camera/Camera.h"
#include "ECS/Archetypes/AbodeArchetype.h"
#include "ECS/Archetypes/AnimalArchetype.h"
#include "ECS/Archetypes/FeatureArchetype.h"
#include "ECS/Archetypes/CreatureArchetype.h"
#include "ECS/Archetypes/MobileStaticArchetype.h"
#include "ECS/Archetypes/PotArchetype.h"
#include "ECS/Components/Construction.h"
#include "ECS/Components/Pot.h"
#include "ECS/Components/Sprite.h"
#include "Graphics/Texture2D.h"
#include "ECS/Archetypes/TreeArchetype.h"
#include "ECS/Archetypes/VillagerArchetype.h"
#include "ECS/Systems/TownSystemInterface.h"
#include "ECS/Components/Abode.h"
#include "ECS/Components/Fixed.h"
#include "ECS/Components/Mobile.h"
#include "ECS/Systems/DynamicsSystemInterface.h"
#include "ECS/Components/Forest.h"
#include "ECS/Components/Stream.h"
#include "ECS/Components/Footpath.h"
#include "ECS/Components/Hand.h"
#include "ECS/Components/RigidBody.h"
#include "ECS/Components/Animal.h"
#include "ECS/Components/Creature.h"
#include "HandPoses.h"
#include "HelpSpirits.h"
#include "Subtitles.h"
#include "Audio/AudioManagerInterface.h"
#include "Resources/ResourcesInterface.h"
#include "3D/CreatureBody.h"
#include "ECS/Components/Feature.h"
#include "ECS/Components/Field.h"
#include "ECS/Components/Temple.h"
#include "ECS/Components/Town.h"
#include "ECS/Components/Transform.h"
#include "ECS/Components/Tree.h"
#include "ECS/Components/Asleep.h"
#include "ECS/Components/ShownAsShadow.h"
#include "ECS/Components/StoragePit.h"
#include "ECS/Components/Villager.h"
#include "ECS/Components/LivingAction.h"
#include "ECS/Components/Mesh.h"
#include "ECS/Components/WallHug.h"
#include "ECS/Systems/LivingActionSystemInterface.h"
#include "ECS/Registry.h"
#include "ECS/Systems/HandSystemInterface.h"
#include "Enums.h"
#include "FileSystem/FileSystemInterface.h"
#include "InfoConstants.h"
#include "ECS/Components/AnimatedStatic.h"
#include "3D/AllMeshes.h"
#include "Resources/ResourcesInterface.h"
#include "Resources/ResourceManager.h"
#include "Game.h"
#include "Locator.h"
#include "ScriptHeaders/ScriptEnums.h"

namespace openblack::chlapi
{

using namespace openblack::ecs::archetypes;

using openblack::Locator;
using openblack::MobileStaticInfo;
using openblack::ecs::components::Transform;
using openblack::ecs::components::LivingAction;
using openblack::ecs::components::MoveStateArrivedTag;
using openblack::ecs::components::MoveStateExitCircleTag;
using openblack::ecs::components::MoveStateFinalStepTag;
using openblack::ecs::components::MoveStateLinearTag;
using openblack::ecs::components::MoveStateOrbitTag;
using openblack::ecs::components::MoveStateStepThroughTag;
using openblack::ecs::components::ScriptMove;
using openblack::ecs::components::WallHug;
using openblack::ecs::components::WallHugObjectReference;
using openblack::ecs::systems::HandSystemInterface;
using openblack::lhvm::DataType;
using openblack::lhvm::VMValue;
using openblack::script::ObjectType;

#define CREATE_FUNCTION_BINDING(NAME, STACKIN, STACKOUT, FUNCTION)       \
	{                                                                    \
		_functionsTable.emplace_back(FUNCTION, STACKIN, STACKOUT, NAME); \
	}

const std::vector<lhvm::NativeFunction>& CHLApi::GetFunctionsTable()
{
	return _functionsTable;
}

std::unordered_set<std::string> GetUniqueWords(const std::string& strings)
{
	std::unordered_set<std::string> result;
	std::istringstream iss(strings);
	std::string word;
	while (std::getline(iss, word, ' '))
	{
		result.insert(word);
	}
	return result;
}

glm::vec3 PopVec()
{
	auto& lhvm = Locator::vm::value();
	const auto z = lhvm.Popf();
	const auto y = lhvm.Popf();
	const auto x = lhvm.Popf();
	return {x, y, z};
}

void PushVec(const glm::vec3& vec)
{
	auto& lhvm = Locator::vm::value();
	lhvm.Pushv(vec.x);
	lhvm.Pushv(vec.y);
	lhvm.Pushv(vec.z);
}

std::string PopString()
{
	auto& lhvm = Locator::vm::value();
	return lhvm.GetString(lhvm.Pop().intVal);
}

std::vector<float> PopVarArg(const int32_t argc)
{
	std::vector<float> vals;
	vals.resize(argc);
	auto& lhvm = Locator::vm::value();
	for (int i = argc - 1; i >= 0; i--)
	{
		vals[i] = lhvm.Popf();
	}
	return vals;
}

namespace
{
// Objects the scripts created, how to make them again (test save points): script type, subtype, age.
struct ScriptEntityRecord
{
	int32_t type;
	uint32_t subtype;
	uint32_t age;
};
std::map<uint32_t, ScriptEntityRecord> g_scriptEntities;
// A creature's own size on the land (two villagers tall): what a script's Scale property is a multiple of. Land 1's
// guide is set to Scale 2, twice a creature's size, not twice its mesh's.
std::unordered_map<uint32_t, float> g_creatureBaseScale;
// Highlights (CREATE_HIGHLIGHT: the "Did you know" and challenge scrolls on the land), by object: their type.
std::unordered_map<uint32_t, int> g_highlights;
// Scrolls a quest has taken up (SET_ACTIVE): not drawn, not clicked.
std::unordered_set<uint32_t> g_takenHighlights;
// Sparkles: glitter falling through an object, from the original's particle sprite sheet (S_SpriteSheet3, frames 48-63,
// as its ScriptHighlightGlints effects use) - on gold and silver scrolls, and what a script gives SPECIAL_EFFECT_OBJECT
// 34 (Land 1's gate totems). By effect: the object it is on, its tint, and its falling sprites.
struct Sparkler
{
	uint32_t target;
	glm::vec4 tint;
	std::vector<entt::entity> sprites;
	std::vector<float> phases;
};
std::unordered_map<uint32_t, Sparkler> g_sparklers;
// Miracle spheres (script type 30 ONE_SHOT_SPELL, 31 in the hand), by object: their SpellSeedType (3 food, 8 wood...).
std::unordered_map<uint32_t, int> g_oneShotSpells;
// Reward chests (script type Reward: CREATE_REWARD / CREATE_REWARD_IN_TOWN), by object: their REWARD_OBJECT_INFO
// (info.dat's reward table: 5 a large food chest), the town they are for, and whether opened (bw1-decomp's Reward;
// opening is runblack.exe 1.20's Reward::InterfaceTap). Kept once opened, for GET_HELP.
struct RewardChest
{
	int type = 0;
	uint32_t town = 0;
	bool opened = false;
	float falling = 0.0f; // still to fall, from the sky (rewardfromsky)
	float openFor = 0.0f; // seconds since it was opened
};
// The original's reward chest (Reward::CallVirtualFunctionsForCreation): Data/Misc/coffre.l3d, opened by coffre.anm
// ("reward chest_open4", 22 frames over 2.166 s, its header's word 0x20). Its bones `opened` of the way open (0-1).
constexpr float k_ChestOpenSeconds = 2.166f;
constexpr float k_ChestStaysOpenSeconds = 3.0f;
std::vector<glm::mat4> ChestPose(float opened)
{
	const auto& animations = Locator::resources::value().GetAnimations();
	const auto id = entt::hashed_string("coffre").value();
	if (!animations.Contains(id) || animations.Handle(id)->GetFrames().empty())
	{
		return {};
	}
	const auto& anim = *animations.Handle(id);
	const float last = static_cast<float>(anim.GetFrames().back().time);
	return anim.GetBoneMatrices(static_cast<uint32_t>(std::clamp(opened, 0.0f, 1.0f) * last));
}
std::unordered_map<uint32_t, RewardChest> g_rewards;
void OpenReward(uint32_t chest); // below, with the miracles
// A miracle bubble's radius, the original's: its sphere O_Bibble_up.l3d's (2.19 round its centre, at scale 1 for every
// seed - OneOffSpellSeed's MobileObject is made at 1). Its drawn disc is 2.28 (k_BubbleDiscRadius); piling goes by this.
constexpr float k_BubbleRadius = 2.19f;
constexpr float k_BubbleDiscRadius = 2.28f;
// Portals (script type 23): what is let go of over one spirals in and is sent on to the next land.
std::unordered_set<uint32_t> g_vortices;
struct VortexSpiral
{
	uint32_t vortex;
	float angle;
	float radius;
	float height;
	float time;
	glm::vec3 tumbleAxis {0.0f, 1.0f, 0.0f};
};
std::unordered_map<uint32_t, VortexSpiral> g_vortexSpirals;
std::vector<std::string> g_vortexCargo; // what has gone in, for the next land
glm::vec3 g_lastDropAt {};
std::chrono::steady_clock::time_point g_lastDropTime {};
// Creatures dead for good (CREATURE_DO_ACTION 261, DeadForever: Land 1's guide, killed by Nemesis): fading away.
std::unordered_map<uint32_t, float> g_dying;
// Creatures dead for good (CREATURE_DO_ACTION DeadForever): they faint and lie there from then on, where they fell
// (Mike, 2026-10-17: not shrinking away).
std::unordered_set<uint32_t> g_deadForever;
// The pointer's ray this frame (SetHandRay), for what a press is on.
glm::vec3 g_handRayOrigin {};
glm::vec3 g_handRayDirection {};
// Where SET_POSITION last put each object: one still there (Land 1's singing stones snapped into their holes) only
// plays when its turn comes - a click on it is not a click.
std::unordered_map<uint32_t, glm::vec3> g_scriptPlaced;
void PutInHand(uint32_t object); // below, with the hand
int TownOwnerOf(uint32_t town);  // below, with the miracles
// Gates, doors and caves (animated scenery that opens) a script has opened with SET_OPEN_CLOSE; the rest stay shut.
std::unordered_set<uint32_t> g_openedStatics;
} // namespace

entt::entity CreateScriptObjectOn(entt::entity hint, const ObjectType type, uint32_t subtype, const glm::vec3& position,
                                  float altitude, float xAngleRadians, float yAngleRadians, const float zAngleRadians,
                                  const float scale);

entt::entity CreateScriptObject(const ObjectType type, uint32_t subtype, const glm::vec3& position, float altitude,
                                float xAngleRadians, float yAngleRadians, const float zAngleRadians, const float scale)
{
	const auto entity =
	    CreateScriptObjectOn(entt::null, type, subtype, position, altitude, xAngleRadians, yAngleRadians, zAngleRadians, scale);
	if (entity != static_cast<entt::entity>(0) && entity != entt::null)
	{
		const uint32_t age = type == ObjectType::VillagerChild ? 10 : 30;
		g_scriptEntities[static_cast<uint32_t>(entity)] = {static_cast<int32_t>(type), subtype, age};
	}
	return entity;
}

// Makes a script object, on the given identifier when `hint` isn't null (and it's free).
entt::entity CreateScriptObjectOn(entt::entity hint, const ObjectType type, uint32_t subtype, const glm::vec3& position,
                                  float altitude, float xAngleRadians, float yAngleRadians, const float zAngleRadians,
                                  const float scale)
{
	// TODO(Daniels118): handle all types
	switch (type)
	{
	case ObjectType::MobileStatic:
	case ObjectType::Rock: // TODO(Daniels118): add a Rock archetype
		return MobileStaticArchetype::Create(position, static_cast<MobileStaticInfo>(subtype), altitude, xAngleRadians,
		                                     yAngleRadians, zAngleRadians, scale, hint);
	case ObjectType::Highlight:
	{
		// A scroll floating over the land (the original's mini scroll), for the player to find and read.
		auto& registry = Locator::entitiesRegistry::value();
		// A script making one where the same kind already stands replaces it (Land 1's CitadelGuideCinema makes
		// the gold scroll twice; clicks on the first were never seen by the script, which waits on the second).
		for (auto it = g_highlights.begin(); it != g_highlights.end();)
		{
			const auto old = static_cast<entt::entity>(it->first);
			const auto* oldTransform = registry.Valid(old) ? registry.TryGet<Transform>(old) : nullptr;
			if (it->second == static_cast<int>(subtype) && oldTransform != nullptr &&
			    glm::distance(glm::vec2(oldTransform->position.x, oldTransform->position.z), glm::vec2(position.x, position.z)) < 1.0f)
			{
				registry.Destroy(old);
				g_scriptEntities.erase(it->first);
				it = g_highlights.erase(it);
				continue;
			}
			++it;
		}
		const auto entity = hint == entt::null ? registry.Create() : registry.Create(hint);
		// Where the script says, height and all (Land 1's gold scroll hangs on the gate, at 32.3); only a position
		// on or under the ground is lifted to just above it.
		// A signpost (type 1) stands on the ground; a scroll given no height floats a little over it.
		constexpr float k_HighlightHeight = 3.0f;
		const float ground = Locator::terrainSystem::value().GetHeightAt(glm::vec2(position.x, position.z));
		float height = position.y > ground ? position.y : ground + (subtype == 1 ? 0.0f : k_HighlightHeight);
		// A scroll over a building floats above its roof, not in it (Land 1's Pied Piper and lost flock reward
		// scrolls, at the height the script gives, stood inside their houses).
		if (subtype != 1)
		{
			const auto& meshes = Locator::resources::value().GetMeshes();
			float roof = 0.0f;
			registry.Each<const ecs::components::Mesh, const Transform>(
			    [&](entt::entity building, const ecs::components::Mesh& mesh, const Transform& where) {
				    if (!registry.AnyOf<ecs::components::Abode, ecs::components::Feature>(building) || !meshes.Contains(mesh.id))
				    {
					    return;
				    }
				    const auto box = meshes.Handle(mesh.id)->GetBoundingBox();
				    const float reach = std::max({std::abs(box.minima.x), std::abs(box.maxima.x), std::abs(box.minima.z),
				                                  std::abs(box.maxima.z)}) *
				                        where.scale.x;
				    if (glm::distance(glm::vec2(where.position.x, where.position.z), glm::vec2(position.x, position.z)) < reach + 1.0f)
				    {
					    roof = std::max(roof, where.position.y + box.maxima.y * where.scale.y);
				    }
			    });
			if (roof + 1.5f > height)
			{
				SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "Scroll at ({:.0f}, {:.0f}) raised from {:.1f} to {:.1f}, over a roof at {:.1f}",
				                   position.x, position.z, height, roof + 1.5f, roof);
				height = roof + 1.5f;
			}
		}
		registry.Assign<Transform>(entity, glm::vec3(position.x, height, position.z), glm::mat3(1.0f), glm::vec3(1.0f));
		// The script's highlight types index the original's highlight info for their mesh: 1 an info signpost
		// (the "Did you know"s), 2 a Silver Reward Scroll, 3 a Gold Story Scroll.
		const auto& highlights = Locator::infoConstants::value().scriptHighlight;
		const auto mesh = subtype < highlights.size() ? highlights.at(subtype).normal : MeshId::I_Miniscroll;
		registry.Assign<ecs::components::Mesh>(entity, resources::HashIdentifier(mesh), static_cast<int8_t>(0),
		                                       static_cast<int8_t>(0));
		g_highlights[static_cast<uint32_t>(entity)] = static_cast<int>(subtype);
		if (subtype == 2 || subtype == 3)
		{
			const auto tint = subtype == 3 ? glm::vec4(1.0f, 1.0f, 0.31f, 1.0f) : glm::vec4(1.0f); // the gold glints' colour
			g_sparklers[static_cast<uint32_t>(entity)] = {static_cast<uint32_t>(entity), tint, {}, {}};
		}
		return entity;
	}
	case ObjectType::Tree:
		// The subtype is the TREE_INFO number (Land 1's leash lesson makes its tree as 8), full grown, on the ground.
		return TreeArchetype::Create(0, glm::vec3(position.x, Locator::terrainSystem::value().GetHeightAt(glm::vec2(position.x, position.z)), position.z),
		                             static_cast<TreeInfo>(subtype), true, 0.0f, 1.0f, 1.0f);
	case ObjectType::Feature:
	{
		// The subtype is the FEATURE_INFO number: Land 1's Missionaries make their Ark in dry dock (69) and put their
		// scroll on it - without it the scroll stood at the map's corner, out at sea.
		const auto feature = FeatureArchetype::Create(
		    glm::vec3(position.x, Locator::terrainSystem::value().GetHeightAt(glm::vec2(position.x, position.z)), position.z),
		    static_cast<FeatureInfo>(subtype), yAngleRadians, scale);
		if (static_cast<FeatureInfo>(subtype) == FeatureInfo::ArkDryDock)
		{
			// Not built yet: it grows as the Missionaries are brought wood (see ArkWoodTaken), drawn like any building.
			auto& registry = Locator::entitiesRegistry::value();
			auto& construction = registry.Assign<ecs::components::Construction>(feature, 0.0f, 1.0f, 1.0f, 0.0f, false);
			// Drawn from the start as high as the nearest house (the Missionaries' hut): the hull's lower part stands
			// in the dock before any wood comes (Mike, 2026-10-17, as the original's).
			const auto& meshes = Locator::resources::value().GetMeshes();
			const auto& arkTransform = registry.Get<Transform>(feature);
			const auto& arkMesh = registry.Get<ecs::components::Mesh>(feature);
			float nearest = 80.0f;
			float hutTop = 0.0f;
			registry.Each<const ecs::components::Abode, const ecs::components::Mesh, const Transform>(
			    [&](const ecs::components::Abode&, const ecs::components::Mesh& mesh, const Transform& transform) {
				    const float distance = glm::distance(glm::vec2(transform.position.x, transform.position.z),
				                                         glm::vec2(arkTransform.position.x, arkTransform.position.z));
				    if (distance < nearest && meshes.Contains(mesh.id))
				    {
					    nearest = distance;
					    const auto box = meshes.Handle(mesh.id)->GetBoundingBox();
					    hutTop = transform.position.y + box.maxima.y * transform.scale.y;
				    }
			    });
			if (hutTop > 0.0f && meshes.Contains(arkMesh.id))
			{
				const auto box = meshes.Handle(arkMesh.id)->GetBoundingBox();
				const float bottom = arkTransform.position.y + box.minima.y * arkTransform.scale.y;
				const float height = box.Size().y * arkTransform.scale.y;
				// Up to half built, building and scaffold rise together: a share h of the height is built h/2.
				construction.drawnFrom = height > 0.0f ? std::clamp((hutTop - bottom) / height, 0.0f, 0.9f) / 2.0f : 0.0f;
			}
			// No house near (the Missionaries' hut isn't a house to this search): the hull a third up, as in the
			// original before the quest (Mike's photo, 2026-10-18).
			if (construction.drawnFrom <= 0.0f)
			{
				construction.drawnFrom = (1.0f / 3.0f) / 2.0f;
			}
			SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "CREATE ark {}: hut {:.0f} away, top at {:.1f}, so drawn from {:.0f}% built",
			                   static_cast<uint32_t>(feature), nearest, hutTop, construction.drawnFrom * 100.0f);
		}
		return feature;
	}
	case ObjectType::Vortex:
	{
		// The portal (Land 1's LeaveThroughVortexL1): the original's Vortex model, turning; what is let go of over it
		// goes in (see UpdateVortices).
		auto& registry = Locator::entitiesRegistry::value();
		const auto entity = hint == entt::null ? registry.Create() : registry.Create(hint);
		const float ground = Locator::terrainSystem::value().GetHeightAt(glm::vec2(position.x, position.z));
		registry.Assign<Transform>(entity, glm::vec3(position.x, ground, position.z), glm::mat3(1.0f), glm::vec3(1.0f));
		const auto vortexMesh = entt::hashed_string("spell/vortex").value();
		if (Locator::resources::value().GetMeshes().Contains(vortexMesh))
		{
			registry.Assign<ecs::components::Mesh>(entity, vortexMesh, static_cast<int8_t>(0), static_cast<int8_t>(0));
		}
		g_vortices.insert(static_cast<uint32_t>(entity));
		SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "Vortex {} at ({:.1f}, {:.1f}, {:.1f})", static_cast<uint32_t>(entity), position.x,
		                   ground, position.z);
		return entity;
	}
	case ObjectType::OneShotSpell:
	case ObjectType::OneShotSpellInHand:
	{
		// A miracle sphere: floating where the script says (GuideCreateMiracle then raises it 50 with SET_PROPERTY), for
		// the hand to take and let go of over where it should happen.
		auto& registry = Locator::entitiesRegistry::value();
		const auto entity = hint == entt::null ? registry.Create() : registry.Create(hint);
		const float ground = Locator::terrainSystem::value().GetHeightAt(glm::vec2(position.x, position.z));
		registry.Assign<Transform>(entity, glm::vec3(position.x, std::max(position.y, ground + 3.0f), position.z),
		                           glm::mat3(1.0f), glm::vec3(1.0f));
		// The original's sphere model (Data/Spells/Meshes/Seed.L3D) if it loaded, else the spell gem.
		const auto seedMesh = entt::hashed_string("spell/seed").value();
		registry.Assign<ecs::components::Mesh>(entity,
		                                       Locator::resources::value().GetMeshes().Contains(seedMesh)
		                                           ? seedMesh
		                                           : resources::HashIdentifier(MeshId::I_SpellGem),
		                                       static_cast<int8_t>(0), static_cast<int8_t>(0));
		registry.Assign<ecs::components::Mobile>(entity);
		const auto id = static_cast<uint32_t>(entity);
		g_oneShotSpells[id] = static_cast<int>(subtype);
		// No sparkles: the bubble and its turning icon are what a miracle looks like (Mike, 2026-10-18).
		if (type == ObjectType::OneShotSpellInHand)
		{
			PutInHand(id);
		}
		SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "Miracle sphere {} (spell seed {}){} at ({:.1f}, {:.1f}, {:.1f})", id, subtype,
		                   type == ObjectType::OneShotSpellInHand ? " in the hand" : "", position.x, position.y, position.z);
		return entity;
	}
	case ObjectType::Reward:
	{
		// A reward chest, on the ground (or above it, to fall): its reward info's model, else the chest.
		auto& registry = Locator::entitiesRegistry::value();
		const auto entity = hint == entt::null ? registry.Create() : registry.Create(hint);
		const auto& infos = Locator::infoConstants::value().reward;
		const auto* info = subtype < infos.size() ? &infos.at(subtype) : nullptr;
		const float ground = Locator::terrainSystem::value().GetHeightAt(glm::vec2(position.x, position.z));
		const float size = info != nullptr && info->startScale > 0.0f ? info->startScale : 1.0f;
		registry.Assign<Transform>(entity, glm::vec3(position.x, ground, position.z), glm::mat3(glm::eulerAngleY(yAngleRadians)),
		                           glm::vec3(size * scale));
		// The original's chest whatever the reward (its info's model gave a cube with a baby's face on each side - Mike,
		// 2026-10-19), shut.
		const auto chestMesh = entt::hashed_string("coffre").value();
		auto mesh = Locator::resources::value().GetMeshes().Contains(chestMesh) ? chestMesh : resources::HashIdentifier(MeshId::U_Chest);
		registry.Assign<ecs::components::Mesh>(entity, mesh, static_cast<int8_t>(0), static_cast<int8_t>(0));
		if (auto shut = ChestPose(0.0f); !shut.empty() && mesh == chestMesh)
		{
			registry.Assign<ecs::components::Animated>(entity, 0u, 0.0f, std::move(shut));
		}
		g_rewards[static_cast<uint32_t>(entity)] = {static_cast<int>(subtype), 0, false, 0.0f};
		if (Locator::resources::value().GetMeshes().Contains(mesh))
		{
			const auto box = Locator::resources::value().GetMeshes().Handle(mesh)->GetBoundingBox();
			SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "Reward chest model {:.2f} x {:.2f} x {:.2f}, scale {:.2f} (startScale {:.2f})",
			                   box.Size().x, box.Size().y, box.Size().z, size * scale, info != nullptr ? info->startScale : 0.0f);
		}
		SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "Reward chest {} (reward {}: food {:.0f}, wood {}, seed {}) at ({:.1f}, {:.1f}, {:.1f})",
		                   static_cast<uint32_t>(entity), subtype, info != nullptr ? info->foodValue : 0.0f, info != nullptr ? info->woodValue : 0u,
		                   info != nullptr ? static_cast<int>(info->seed) : -1, position.x, ground, position.z);
		return entity;
	}
	case ObjectType::SpellDispenser:
		// The subtype is the ABODE_INFO number (Land 1's rewards make 126, the Norse one), on the ground, in the town
		// nearest. Without it the reward's signpost, placed by the dispenser, stood out in the sea at the map's corner.
		return AbodeArchetype::Create(std::numeric_limits<uint32_t>::max(),
		                              glm::vec3(position.x, Locator::terrainSystem::value().GetHeightAt(glm::vec2(position.x, position.z)),
		                                        position.z),
		                              static_cast<AbodeInfo>(subtype), yAngleRadians, scale, 0, 0);
	case ObjectType::Whale:
	{
		// The sharks of the opening (FollowUs: CREATE 26 5000 at Shark1Pos/Shark2Pos, then WALK_PATH along tracks 21
		// and 20) - the scripts' Whale type, drawn with the original's only sea creature, SharkBoned, swimming. At the
		// sea's surface, its fin out (Mike, 2026-10-18: the shark never appeared - this type was never made).
		auto& registry = Locator::entitiesRegistry::value();
		const auto entity = hint == entt::null ? registry.Create() : registry.Create(hint);
		const float ground = Locator::terrainSystem::value().GetHeightAt(glm::vec2(position.x, position.z));
		// Drawn twice its model's size (Whale::CallVirtualFunctionsForCreation sets its LH3DObject's scale to 2).
		registry.Assign<Transform>(entity, glm::vec3(position.x, std::max(ground, 0.0f), position.z), glm::eulerAngleY(yAngleRadians),
		                           glm::vec3(2.0f));
		registry.Assign<openblack::ecs::components::Mesh>(entity, resources::HashIdentifier(MeshId::SharkBoned), static_cast<int8_t>(0),
		                                                  static_cast<int8_t>(0));
		registry.Assign<openblack::ecs::components::Animated>(entity, static_cast<uint32_t>(AnimId::SharkBonedSwim), 0.0f);
		SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "Shark {} (script type 26 subtype {}) at ({:.1f}, {:.1f}, {:.1f}){}",
		                   static_cast<uint32_t>(entity), subtype, position.x, std::max(ground, 0.0f), position.z,
		                   Locator::resources::value().GetMeshes().Contains(resources::HashIdentifier(MeshId::SharkBoned))
		                       ? ""
		                       : " - SharkBoned not loaded!");
		return entity;
	}
	case ObjectType::Animal:
	{
		const auto animal = AnimalArchetype::Create(position, static_cast<AnimalInfo>(subtype));
		return animal == entt::null ? static_cast<entt::entity>(0) : animal;
	}
	case ObjectType::Marker:
	{
		// A named point for the script: nothing to draw, just somewhere to measure from and move to.
		auto& registry = Locator::entitiesRegistry::value();
		const auto entity = hint == entt::null ? registry.Create() : registry.Create(hint);
		// Near the land it sits on it - the script's height is only approximate, and walks land exactly on it. Well above
		// it, it keeps its height: those are camera points (Land 1's Rotating puts TemplePos 27 above the ground), and on
		// the ground every cutscene ended with the view down in the grass.
		// 10, not 3: FollowUs walks the family to markers 3.4 above openblack's ground, and floating there it waited
		// for ever on their distance to points on the ground.
		constexpr float k_CameraPointHeight = 10.0f;
		const float ground = Locator::terrainSystem::value().GetHeightAt(glm::vec2(position.x, position.z));
		const float height = position.y - ground > k_CameraPointHeight ? position.y : ground;
		registry.Assign<Transform>(entity, glm::vec3(position.x, height, position.z), glm::mat3(1.0f), glm::vec3(1.0f));
		return entity;
	}
	case ObjectType::Creature:
	{
		// Land 1's CreaturesInGlade names what it makes: subtype 0 its CreatureApe, 1 CreatureCow, 2 CreatureTiger -
		// CreatureType's own numbering, with 0 the Ape. Its CreateGuideCreature asks for 11 (the Sheep), 5 (the Lion)
		// or 9 (the Brown Bear), by which of those three is yours.
		struct SpeciesFound
		{
			CreatureType second;
		};
		const auto speciesValue = subtype == 0 ? CreatureType::GiantApe : static_cast<CreatureType>(subtype);
		const std::optional<SpeciesFound> speciesFound =
		    subtype < static_cast<uint32_t>(CreatureType::_COUNT) ? std::optional<SpeciesFound>({speciesValue}) : std::nullopt;
		const auto* species = speciesFound ? &*speciesFound : nullptr;
		if (species == nullptr)
		{
			SPDLOG_LOGGER_WARN(spdlog::get("scripting"), "CREATE: creature subtype {} has no known species", subtype);
			return static_cast<entt::entity>(0);
		}
		// Unowned until a script gives it to a player (CREATURE_SET_PLAYER), standing on the land, two grown villagers
		// tall (Mike, 2026-10-16: half the four it was). A fixed height: measuring against the first villager found
		// sometimes measured a child, and the size changed from load to load.
		constexpr float k_VillagerHeight = 1.63f; // a grown man's mesh
		constexpr float k_VillagersTall = 2.0f;
		const auto& meshes = Locator::resources::value().GetMeshes();
		const float villagerHeight = k_VillagerHeight;
		const auto meshId = openblack::creature::GetIdFromType(species->second, openblack::creature::CreatureBody::Appearance::Base);
		const float meshHeight = meshes.Contains(meshId) ? meshes.Handle(meshId)->GetBoundingBox().Size().y : 0.0f;
		// Land 1's other creature, the sheep, stands five times that (Mike, 2026-10-17: ten times, then half that).
		const float tall = species->second == CreatureType::Sheep ? 5.0f * k_VillagersTall : k_VillagersTall;
		const float size = villagerHeight > 0.0f && meshHeight > 0.0f ? tall * villagerHeight / meshHeight : 0.25f;
		const float ground = Locator::terrainSystem::value().GetHeightAt(glm::vec2(position.x, position.z));
		const auto made = CreatureArchetype::Create(glm::vec3(position.x, ground, position.z), PlayerNames::NEUTRAL,
		                                            species->second, 0, yAngleRadians, scale * size, hint);
		g_creatureBaseScale[static_cast<uint32_t>(made)] = scale * size;
		SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "CREATE creature {} (species {}): mesh {:.1f} tall, a villager {:.2f}, so x{:.3f}",
		                   static_cast<uint32_t>(made), static_cast<int>(species->second), meshHeight, villagerHeight, size);
		return made;
	}
	case ObjectType::Villager:
	case ObjectType::VillagerChild:
	{
		// The script subtype is the VILLAGER_INFO number; the abode position picks the home town.
		const auto villager = VillagerArchetype::Create(position, position, static_cast<VillagerInfo>(subtype),
		                                                type == ObjectType::VillagerChild ? 10 : 30, hint);
		const auto* mesh = Locator::entitiesRegistry::value().TryGet<ecs::components::Mesh>(villager);
		SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "CREATE villager {} (type {}) at ({:.1f}, {:.1f}, {:.1f}), mesh {}{}",
		                   static_cast<uint32_t>(villager), subtype, position.x, position.y, position.z, mesh != nullptr ? mesh->id : 0u,
		                   mesh != nullptr && !Locator::resources::value().GetMeshes().Contains(mesh->id) ? " (not loaded!)" : "");
		return villager;
	}
	default:
	{
		static std::set<std::pair<ObjectType, uint32_t>> warned;
		if (warned.emplace(type, subtype).second)
		{
			SPDLOG_LOGGER_WARN(spdlog::get("scripting"), "CREATE not implemented for script type {} subtype {}",
			                   static_cast<int>(type), subtype);
		}
	}
	}
	return static_cast<entt::entity>(0);
}

VMValue Pop(DataType& type)
{
	auto& lhvm = Locator::vm::value();
	return lhvm.Pop(type);
}

VMValue Pop()
{
	auto& lhvm = Locator::vm::value();
	return lhvm.Pop();
}

float Popf()
{
	auto& lhvm = Locator::vm::value();
	return lhvm.Popf();
}

void Push(VMValue value, DataType type)
{
	auto& lhvm = Locator::vm::value();
	lhvm.Push(value, type);
}

void Pushf(float value)
{
	auto& lhvm = Locator::vm::value();
	lhvm.Pushf(value);
}

void Pushv(float value)
{
	auto& lhvm = Locator::vm::value();
	lhvm.Pushv(value);
}

void Pushi(int32_t value)
{
	auto& lhvm = Locator::vm::value();
	lhvm.Pushi(value);
}

void Pusho(uint32_t value)
{
	auto& lhvm = Locator::vm::value();
	lhvm.Pusho(value);
}

void Pushb(bool value)
{
	auto& lhvm = Locator::vm::value();
	lhvm.Pushb(value);
}

CHLApi::CHLApi()
{
	_functionsTable.reserve(464);
	InitFunctionsTable0();
	InitFunctionsTable1();
	InitFunctionsTable2();
	InitFunctionsTable3();
	InitFunctionsTable4();
}

void None() {} // 000 NONE


namespace
{
// One conversation at a time, as in the original: START_DIALOGUE takes it, END_DIALOGUE gives it back.
bool g_dialogueActive = false;
uint32_t g_dialogueTask = 0; // the script task holding it
// Whether a script holds the camera (START/END_CAMERA_CONTROL): what a hand demo gives the camera back to.
bool g_scriptHasCamera = false;
// What the script's camera follows, as the original's CameraModeScript (a CameraModeFollow): its focus on one thing
// (SET_FOCUS_FOLLOW), its position kept from another (SET_POSITION_FOLLOW, ..._AND_...) at a distance, heading and
// pitch, re-aimed every frame (UpdateCameraFollow). A new camera control starts without either.
struct CameraFollow
{
	uint32_t focus = 0;  // FocusThing
	uint32_t target = 0; // Target: the position follows it, and the focus too unless focus is set
	float heading = 0.0f;
	float pitch = 0.0f;
	float distance = 0.0f;
	float zoomTimeScale = 0.2f;  // how quickly it keeps up: the script mode's, until CAMERA_PROPERTIES
	bool relativeHeading = true; // heading measured from the way a walker faces, not from the world's +z
};
CameraFollow g_cameraFollow;
void CameraFollowFocus(uint32_t thing);
std::optional<glm::vec3> CameraFollowPoint(uint32_t thing);
void CameraFollowPosition(uint32_t thing, std::optional<float> distance, bool snap);
// The line RUN_TEXT last showed, and when. There is no text display yet, so a line counts as read
// once it has been up long enough to read (TEXT_READ).
int g_lastTextId = -1;
std::chrono::steady_clock::time_point g_lastTextShown {};
// Animations scripts play on objects (SET_SCRIPT_ULONG), by object: which, and how many plays are left.
// Villagers otherwise walk, run or stand by how fast they move (see UpdateAnimations).
struct ScriptAnimation
{
	uint32_t animation;
	float playsLeft;
};
std::unordered_map<uint32_t, ScriptAnimation> g_scriptAnimations;
// What a living plays while a script moves it, if the script said (OVERRIDE_STATE_ANIMATION: the opening's son skips
// away with PSkip02, runs to the sea with PRunMan) - else its walk or run for the speed. Until the move ends.
std::unordered_map<uint32_t, uint32_t> g_moveAnimations;

// What a script last told each creature to do (CREATURE_DO_ACTION). The number is the creature action's place in
// info.dat's creature action table (82 LookAtCamera, 161 BeIdle, 17 SleepAtHome...), the table the creature mind
// itself uses: the original passes it straight on to the creature's mind.
struct CreatureOrder
{
	int32_t action;
	uint32_t target;
	uint32_t with;
};
std::unordered_map<uint32_t, CreatureOrder> g_creatureOrders;
// Creatures whose HoldObject order is done: they took the thing up once, and what happens to it after is theirs (and
// the player's) - put down, it isn't grabbed again. Land 1's Pied Piper is handed to your creature like this.
std::unordered_set<uint32_t> g_holdDone;

std::string CreatureActionName(int32_t action)
{
	const auto& table = Locator::infoConstants::value().creatureAction;
	if (action < 0 || static_cast<size_t>(action) >= table.size())
	{
		return "?";
	}
	const auto& name = table.at(static_cast<size_t>(action)).name;
	return {name.data(), strnlen(name.data(), name.size())};
}

std::vector<std::pair<std::string, std::chrono::steady_clock::time_point>> g_pendingShots;
// GET_TOTAL_EVENTS counters, by event type: 25 degrees the player has rotated the camera, 28 degrees
// pitched, 29 metres zoomed (the tutorials ask for 60, 25 and 10-15 more than when they start).
std::unordered_map<int32_t, float> g_totalEvents;
// Script timers (CREATE_TIMER): when each runs out.
std::unordered_map<uint32_t, std::chrono::steady_clock::time_point> g_timers;
uint32_t g_nextTimer = 1;
// What the player's hand holds and is over (see HandPickUp), 0 for nothing.
uint32_t g_heldObject = 0;
uint32_t g_objectUnderHand = 0;
uint32_t g_woodUnderHand = 0;
// Objects scripts have made unpickupable / unmoveable (SET_ID_PICKUPABLE / SET_ID_MOVEABLE false).
std::unordered_set<uint32_t> g_notPickupable;
std::unordered_set<uint32_t> g_scriptPickupable; // what a script has made pickupable (SET_ID_PICKUPABLE true)
// Thrown or dropped objects in flight: velocity, until they reach the ground (see UpdateScriptFrame).
std::unordered_map<uint32_t, glm::vec3> g_flying;
// Where a tree stood in the way of walkers (its Fixed), kept while it is carried or lying, for when it is replanted.
std::unordered_map<uint32_t, openblack::ecs::components::Fixed> g_treeFootprints;
// Knocked off its feet until then (a creature's thrown stone): reads as Flying a while after landing, since Land 1's
// punishment lesson only starts looking once Sable's line before it has been read, after she had already landed.
std::unordered_map<uint32_t, std::chrono::steady_clock::time_point> g_knockedUntil;
// The screen's fade (GScript's, runblack.exe 0x6eb9d0-0x6ebb50): a colour drawn over the picture at an alpha that
// SET_FADE takes from 0 to 255 and SET_FADE_IN from 255 to 0, by 255 / (whole seconds x 10) a game turn
// (ProcessFade), drawn last of each frame (LH3DFade::Draw, from LH3DRender::FinishFrame).
struct ScreenFadeState
{
	std::array<uint8_t, 3> colour {};
	float alpha = 0.0f; // 0-255
	float rate = 0.0f;  // a turn
};
ScreenFadeState g_screenFade;

// The scripts' clock: real time scaled by the play speed (UpdateScriptFrame advances it by the game's frame time),
// so dialogue, fades and script timers keep pace when the game is sped up or slowed down.
std::chrono::steady_clock::time_point g_scriptClock = std::chrono::steady_clock::now();
std::chrono::steady_clock::time_point ScriptNow()
{
	return g_scriptClock;
}

float PlaySpeed()
{
	const auto* game = openblack::Game::Instance();
	return game != nullptr ? game->GetPlaySpeed() : 1.0f;
}

// GScript::SetupScreenFadeTo: none of its seconds (truncated) is the colour at once.
void SetupScreenFadeTo(uint8_t red, uint8_t green, uint8_t blue, int seconds)
{
	g_screenFade.colour = {red, green, blue};
	g_screenFade.alpha = seconds <= 0 ? 255.0f : 0.0f;
	g_screenFade.rate = seconds <= 0 ? 0.0f : 255.0f / (static_cast<float>(seconds) * 10.0f);
}

// GScript::SetupScreenFadeBackToNormal: likewise back to nothing over it.
void SetupScreenFadeBackToNormal(int seconds)
{
	g_screenFade.alpha = seconds <= 0 ? 0.0f : 255.0f;
	g_screenFade.rate = seconds <= 0 ? 0.0f : -255.0f / (static_cast<float>(seconds) * 10.0f);
}
constexpr auto k_TextReadTime = std::chrono::seconds(2);
entt::entity g_textVoice = entt::null; // the voice of the last RUN_TEXT line while it plays
entt::entity PlayTextVoice(int32_t text); // below, with the sounds
// The last object the player clicked, until a GAME_THING_CLICKED for it consumes it.
uint32_t g_clickedThing = 0;
} // namespace

void EndScriptTurn()
{
	g_clickedThing = 0; // a click lasts one script turn
	// GScript::ProcessFade, every turn: to the end, where it stops.
	if (g_screenFade.rate != 0.0f)
	{
		g_screenFade.alpha += g_screenFade.rate;
		if (g_screenFade.alpha > 255.0f || g_screenFade.alpha < 0.0f)
		{
			g_screenFade.alpha = std::clamp(g_screenFade.alpha, 0.0f, 255.0f);
			g_screenFade.rate = 0.0f;
		}
	}
}

std::array<uint8_t, 4> ScreenFade()
{
	const auto& fade = g_screenFade;
	return {fade.colour[0], fade.colour[1], fade.colour[2], static_cast<uint8_t>(fade.alpha)};
}

namespace
{
float ContainerResource(uint32_t container, int32_t resource); // below, with the resources
} // namespace

float TownStoreAmount(uint32_t town, bool wood)
{
	return ContainerResource(town, static_cast<int32_t>(wood ? ResourceType::Wood : ResourceType::Food));
}

void NotifyGameThingClicked(uint32_t entity)
{
	g_clickedThing = entity;
	if (const auto chest = g_rewards.find(entity); chest != g_rewards.end() && !chest->second.opened)
	{
		OpenReward(entity); // Reward::InterfaceTap
	}
	// What it was, so a click that went to the wrong thing can be told from the log.
	auto& registry = Locator::entitiesRegistry::value();
	const auto e = static_cast<entt::entity>(entity);
	std::string what = "object";
	if (registry.Valid(e))
	{
		if (const auto* creature = registry.TryGet<openblack::ecs::components::Creature>(e))
		{
			what = fmt::format("creature (species {})", static_cast<int>(creature->species));
		}
		else if (registry.AllOf<openblack::ecs::components::Villager>(e))
		{
			what = "villager";
		}
		else if (g_highlights.contains(entity))
		{
			what = "scroll";
		}
	}
	if (g_rewards.contains(entity))
	{
		what = "reward chest";
	}
	SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "Clicked {} ({})", entity, what);
}

// The scroll (highlight) the hand is pointing at: the nearest within reach of the pointer's ray. Scrolls float and
// have no physics body for the ray to hit.
uint32_t HighlightOnRay(const glm::vec3& origin, const glm::vec3& direction);
uint32_t CreatureOnRay(const glm::vec3& origin, const glm::vec3& direction);

bool ClickScrollAt(glm::vec2 screen)
{
	glm::vec3 origin;
	glm::vec3 direction;
	Locator::camera::value().DeprojectScreenToWorld(screen, origin, direction);
	const auto scroll = HighlightOnRay(origin, direction);
	if (scroll != 0)
	{
		NotifyGameThingClicked(scroll);
	}
	return scroll != 0;
}

// --- Rewarding and punishing the creature ------------------------------------------------------------------------
// The original's locked interaction: the action button held with the hand on your creature turns the camera to face
// it; a quick swipe across it is a slap, slow movement over it a stroke. Land 1's punishment lesson (Sable) reads
// GET_INTERACTION_MAGNITUDE every 2 s and waits for it to change - below 0 a slap, above 0 a stroke - and
// IS_LOCKED_INTERACTION to prompt you once you face it.
struct CreatureInteraction
{
	uint32_t creature = 0;
	glm::vec2 lastScreen {};
	bool haveLast = false;
	int fastFrames = 0; // frames in a row the hand has moved at slap speed
	float strokeSeconds = 0.0f;
	float sinceSlap = 10.0f;
	glm::vec3 savedOrigin {};
	glm::vec3 savedFocus {};
	bool cameraWasScripted = false;
};
std::optional<CreatureInteraction> g_interaction;
std::unordered_map<uint32_t, float> g_interactionMagnitude; // the last slap (<0) or stroke (>0) each creature had
// What each creature holds, given from the hand (HandGiveToCreature): stroked, it eats it; slapped, it drops it.
std::unordered_map<uint32_t, uint32_t> g_creatureHolding;
// How many times each creature has done each action (GET_ACTION_COUNT): Land 1's LearnToEat waits for one of
// EatAlive (11), EatAfterExamining (12), 276 or EatFromFoodPile (158).
std::map<std::pair<uint32_t, int32_t>, float> g_creatureActionCounts;
void CreatureEatsHeld(uint32_t creature);  // below, with the hand
void CreatureDropsHeld(uint32_t creature); // below, with the hand
// A creature held by another creature is put down (saves and fights don't carry one creature in another's hand).
void ReleaseHeldCreatures()
{
	auto& registry = Locator::entitiesRegistry::value();
	std::vector<uint32_t> holders;
	for (const auto& [holder, held] : g_creatureHolding)
	{
		const auto entity = static_cast<entt::entity>(held);
		if (registry.Valid(entity) && registry.AllOf<openblack::ecs::components::Creature>(entity))
		{
			holders.push_back(holder);
		}
	}
	for (const auto holder : holders)
	{
		SPDLOG_LOGGER_INFO(spdlog::get("game"), "Creature {} puts down the creature {} it held", holder, g_creatureHolding[holder]);
		CreatureDropsHeld(holder);
	}
}
void UpdateCreatureHolding();             // below, with the hand

// Where along the ray it enters the creature's box, and how high on the creature that is (0 its feet, 1 its head).
std::optional<std::pair<float, float>> CreatureRayHit(entt::entity entity, const glm::vec3& origin, const glm::vec3& direction)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto& meshes = Locator::resources::value().GetMeshes();
	const auto* mesh = registry.Valid(entity) ? registry.TryGet<openblack::ecs::components::Mesh>(entity) : nullptr;
	const auto* transform = registry.Valid(entity) ? registry.TryGet<Transform>(entity) : nullptr;
	if (mesh == nullptr || transform == nullptr || !meshes.Contains(mesh->id))
	{
		return std::nullopt;
	}
	const auto box = meshes.Handle(mesh->id)->GetBoundingBox();
	const auto toLocal = glm::transpose(transform->rotation);
	const auto localOrigin = toLocal * (origin - transform->position) / transform->scale;
	const auto localAlong = toLocal * glm::normalize(direction) / transform->scale;
	float enter = 0.0f;
	float leave = std::numeric_limits<float>::max();
	for (int axis = 0; axis < 3; ++axis)
	{
		if (std::abs(localAlong[axis]) < 1e-6f)
		{
			if (localOrigin[axis] < box.minima[axis] || localOrigin[axis] > box.maxima[axis])
			{
				return std::nullopt;
			}
			continue;
		}
		float near = (box.minima[axis] - localOrigin[axis]) / localAlong[axis];
		float far = (box.maxima[axis] - localOrigin[axis]) / localAlong[axis];
		if (near > far)
		{
			std::swap(near, far);
		}
		enter = std::max(enter, near);
		leave = std::min(leave, far);
	}
	if (enter > leave)
	{
		return std::nullopt;
	}
	const float y = (localOrigin + localAlong * enter).y;
	return std::pair {enter, std::clamp((y - box.minima.y) / std::max(box.maxima.y - box.minima.y, 1e-3f), 0.0f, 1.0f)};
}

// The creature player one owns. With none owned but only one creature on the land, that one: test save points
// from before creatures' owners were saved (2026-10-16) remake the chosen creature unowned.
uint32_t PlayersCreature()
{
	uint32_t found = 0;
	uint32_t only = 0;
	int count = 0;
	Locator::entitiesRegistry::value().Each<openblack::ecs::components::Creature>(
	    [&](entt::entity entity, openblack::ecs::components::Creature& creature) {
		    ++count;
		    only = static_cast<uint32_t>(entity);
		    if (found == 0 && creature.owner == PlayerNames::PLAYER_ONE)
		    {
			    found = static_cast<uint32_t>(entity);
		    }
	    });
	if (found == 0 && count == 1)
	{
		auto& registry = Locator::entitiesRegistry::value();
		registry.Get<openblack::ecs::components::Creature>(static_cast<entt::entity>(only)).owner = PlayerNames::PLAYER_ONE;
		SPDLOG_LOGGER_INFO(spdlog::get("game"), "Creature {} is the only one: taken as the player's", only);
		found = only;
	}
	return found;
}

bool BeginCreatureInteraction(const glm::vec3& origin, const glm::vec3& direction)
{
	const auto creature = PlayersCreature();
	const auto entity = static_cast<entt::entity>(creature);
	if (creature == 0 || !CreatureRayHit(entity, origin, direction))
	{
		return false;
	}
	auto& registry = Locator::entitiesRegistry::value();
	auto& camera = Locator::camera::value();
	const auto& transform = registry.Get<Transform>(entity);
	const auto& mesh = registry.Get<openblack::ecs::components::Mesh>(entity);
	const float height = Locator::resources::value().GetMeshes().Handle(mesh.id)->GetBoundingBox().Size().y * transform.scale.y;
	CreatureInteraction interaction;
	interaction.creature = creature;
	interaction.savedOrigin = camera.GetOrigin();
	interaction.savedFocus = camera.GetFocus();
	interaction.cameraWasScripted = camera.IsScriptControlled();
	g_interaction = interaction;
	// In front of it, a little above its middle, looking at it (its front taken as the model's -Z, as everywhere).
	const auto front = transform.rotation * glm::vec3(0.0f, 0.0f, -1.0f);
	camera.SetScriptControl(true);
	camera.SetOrigin(transform.position + front * height * 1.4f + glm::vec3(0.0f, height * 0.6f, 0.0f));
	camera.SetFocus(transform.position + glm::vec3(0.0f, height * 0.5f, 0.0f));
	SPDLOG_LOGGER_INFO(spdlog::get("game"), "Creature interaction with {} begins", creature);
	return true;
}

void HandJumped()
{
	if (g_interaction)
	{
		g_interaction->haveLast = false;
		g_interaction->fastFrames = 0;
	}
}

bool InCreatureInteraction()
{
	return g_interaction.has_value();
}

void UpdateCreatureInteraction(const glm::vec2& screen, const glm::vec3& origin, const glm::vec3& direction,
                               std::chrono::microseconds dt)
{
	if (!g_interaction)
	{
		return;
	}
	constexpr float k_SlapSpeed = 2.5f;     // screen widths a second: faster is a slap
	constexpr float k_StrokeSpeed = 0.05f;  // ...slower than this is the hand at rest
	constexpr float k_SlapEvery = 0.5f;     // seconds between slaps one swipe can make
	constexpr float k_FullStroke = 3.0f;    // seconds of stroking for the most a stroke gives
	constexpr float k_NoStrokeAfterSlap = 1.5f;
	constexpr int k_SlapFrames = 2;        // ...for this many frames in a row: one frame's jump is not a swipe
	auto& interaction = *g_interaction;
	// Real seconds, not game seconds: at half play speed the hand read twice as fast and every move was a slap.
	const float seconds = std::max(std::chrono::duration<float>(dt).count() / PlaySpeed(), 1e-4f);
	interaction.sinceSlap += seconds;
	const float speed = interaction.haveLast ? glm::length(screen - interaction.lastScreen) / seconds : 0.0f;
	interaction.lastScreen = screen;
	interaction.haveLast = true;
	interaction.fastFrames = speed > k_SlapSpeed ? interaction.fastFrames + 1 : 0;
	const auto entity = static_cast<entt::entity>(interaction.creature);
	const auto hit = CreatureRayHit(entity, origin, direction);
	if (!hit)
	{
		return;
	}
	const auto id = interaction.creature;
	if (interaction.fastFrames >= k_SlapFrames && interaction.sinceSlap > k_SlapEvery)
	{
		interaction.sinceSlap = 0.0f;
		interaction.strokeSeconds = 0.0f;
		const float strength = std::min(speed / (k_SlapSpeed * 2.0f), 1.0f);
		g_interactionMagnitude[id] = -strength;
		// The slap where it landed: head, waist or feet (ctrspec27.txt 165/167/169; only the right-hand ones so far).
		const uint32_t animation = hit->second > 0.66f ? 165u : hit->second > 0.33f ? 167u : 169u;
		g_scriptAnimations[id] = {animation, 1.0f};
		CreatureDropsHeld(id); // slapped, it lets go of what it holds
		SPDLOG_LOGGER_INFO(spdlog::get("game"), "Slap on creature {}: {:.2f} ({})", id, -strength,
		                   animation == 165 ? "head" : animation == 167 ? "waist" : "feet");
	}
	// Not straight after a slap: the swipe slowing to its end read as a full stroke, and the lesson, looking every
	// 2 s, saw that instead of the slap.
	else if (speed > k_StrokeSpeed && speed <= k_SlapSpeed * 0.5f && interaction.sinceSlap > k_NoStrokeAfterSlap)
	{
		interaction.strokeSeconds += seconds;
		const float strength = std::min(interaction.strokeSeconds / k_FullStroke, 1.0f);
		// Whole steps, so the lesson's every-2-seconds check sees a change it can act on, not a creep.
		const float stepped = std::ceil(strength * 4.0f) / 4.0f;
		if (g_interactionMagnitude[id] != stepped)
		{
			g_interactionMagnitude[id] = stepped;
			SPDLOG_LOGGER_INFO(spdlog::get("game"), "Stroke on creature {}: {:.2f}", id, stepped);
		}
		// Stroked while it holds something it was given, it eats it (the original's way to teach eating).
		if (g_creatureHolding.contains(id) && interaction.strokeSeconds >= 0.3f)
		{
			CreatureEatsHeld(id);
			return;
		}
		if (g_scriptAnimations.find(id) == g_scriptAnimations.end())
		{
			g_scriptAnimations[id] = {100u, 1.0f}; // stroke
		}
	}
}

void EndCreatureInteraction()
{
	if (!g_interaction)
	{
		return;
	}
	auto& camera = Locator::camera::value();
	camera.SetOrigin(g_interaction->savedOrigin);
	camera.SetFocus(g_interaction->savedFocus);
	camera.SetScriptControl(g_interaction->cameraWasScripted);
	SPDLOG_LOGGER_INFO(spdlog::get("game"), "Creature interaction with {} ends", g_interaction->creature);
	g_interaction.reset();
}

// The villager the ray meets first (0 if none), by its own box like a creature: people have no physics body, so a
// click on one was never seen. Land 1's creature lessons end their wait when you click the gold scroll or Sable.
uint32_t VillagerOnRay(const glm::vec3& origin, const glm::vec3& direction)
{
	uint32_t best = 0;
	float bestEnter = std::numeric_limits<float>::max();
	Locator::entitiesRegistry::value().Each<const openblack::ecs::components::Villager>(
	    [&](entt::entity entity, const openblack::ecs::components::Villager&) {
		    if (const auto hit = CreatureRayHit(entity, origin, direction); hit && hit->first < bestEnter)
		    {
			    bestEnter = hit->first;
			    best = static_cast<uint32_t>(entity);
		    }
	    });
	return best;
}

uint32_t HighlightOnRay(const glm::vec3& origin, const glm::vec3& direction)
{
	constexpr float k_ScrollReach = 5.0f;
	auto& registry = Locator::entitiesRegistry::value();
	const auto along = glm::normalize(direction);
	uint32_t best = 0;
	float bestDistance = std::numeric_limits<float>::max();
	std::vector<uint32_t> clickable;
	for (const auto& [id, type] : g_highlights)
	{
		clickable.push_back(id);
	}
	for (const auto& [id, chest] : g_rewards)
	{
		if (!chest.opened && chest.falling <= 0.0f)
		{
			clickable.push_back(id); // a reward chest is tapped open as a scroll is (Reward::InterfaceValidToTap)
		}
	}
	for (const auto id : clickable)
	{
		const auto entity = static_cast<entt::entity>(id);
		const auto* transform = registry.Valid(entity) ? registry.TryGet<Transform>(entity) : nullptr;
		if (transform == nullptr || g_takenHighlights.contains(id))
		{
			continue;
		}
		// Its middle and size by its shape (a model's origin need not be its middle).
		glm::vec3 middle = transform->position;
		float reach = k_ScrollReach;
		if (const auto* mesh = registry.TryGet<openblack::ecs::components::Mesh>(entity))
		{
			const auto box = Locator::resources::value().GetMeshes().Handle(mesh->id)->GetBoundingBox();
			middle = transform->position + transform->rotation * (box.Center() * transform->scale);
			reach = std::max(reach, glm::length(box.Size() * transform->scale) * 0.5f);
		}
		const auto toScroll = middle - origin;
		const float ahead = glm::dot(toScroll, along);
		if (ahead <= 0.0f)
		{
			continue;
		}
		const float off = glm::length(toScroll - along * ahead);
		if (off < reach && off < bestDistance)
		{
			bestDistance = off;
			best = id;
		}
	}
	return best;
}

// The creature the pointer is on, if any: like a scroll, by its shape's middle and size, since a creature has no
// physics body for the hand's ray to hit (Land 1's glade asks for a click on the Ape, Cow or Tiger to choose one).
uint32_t CreatureOnRay(const glm::vec3& origin, const glm::vec3& direction)
{
	// Of the creatures whose box the ray passes through, the one whose middle it passes closest to, for its size: the
	// boxes cover arms and all, and the nearest box along the ray let the Ape in front take every click meant for the
	// Tiger behind it.
	auto& registry = Locator::entitiesRegistry::value();
	const auto& meshes = Locator::resources::value().GetMeshes();
	const auto along = glm::normalize(direction);
	uint32_t best = 0;
	float bestOff = std::numeric_limits<float>::max();
	registry.Each<const openblack::ecs::components::Creature, const openblack::ecs::components::Mesh, const Transform>(
	    [&](entt::entity entity, const openblack::ecs::components::Creature&, const openblack::ecs::components::Mesh& mesh,
	        const Transform& transform) {
		    if (!meshes.Contains(mesh.id) || !CreatureRayHit(entity, origin, direction))
		    {
			    return;
		    }
		    const auto box = meshes.Handle(mesh.id)->GetBoundingBox();
		    const auto middle = transform.position + transform.rotation * (box.Center() * transform.scale);
		    const auto toMiddle = middle - origin;
		    const float off = glm::length(toMiddle - along * glm::dot(toMiddle, along)) /
		                      std::max(glm::length(box.Size() * transform.scale) * 0.5f, 1e-3f);
		    if (off < bestOff)
		    {
			    bestOff = off;
			    best = static_cast<uint32_t>(entity);
		    }
	    });
	return best;
}


namespace
{
// Black & White's "call <type> [<subtype>] [near <position> [radius <r>]]": the nearest existing
// object of that script object type. Subtype 5000 is how the campaign asks for any subtype (it
// comes with every type), and a negative one is taken the same way.
constexpr int k_AnySubtype = 5000;

// People a script made and hasn't let go of (RELEASE_FROM_SCRIPT): still the script's, so a search excluding scripted
// ones skips them. Land 1's missionaries look for a helper by the ark that way; their own sailors' crew, made by the
// script and held in no variable, was taken as one.
std::unordered_set<uint32_t> g_releasedFromScript;

// Objects a search excluding scripted ones skips (set around the search, see ScriptedObjects).
const std::unordered_set<uint32_t>* g_searchExcludes = nullptr;

// The objects scripts hold: ids in a running task's variables or the globals. A script taking an object as an
// argument keeps it (Land 1's SetupSculptor runs KeepRockScripted on each rock by the sculptor, then asks CALL_NEAR,
// excluding scripted ones, for the next - with this ignored it found the same rock for ever). A task just started
// by RUN still has its arguments on its stack, not yet in its variables, so those count too: without them
// SetupSculptor's loop, which never yields, found the rock handed to the newest KeepRockScripted again and again.
void AddFlockMembers(std::unordered_set<uint32_t>& held); // below, with the flocks
std::unordered_set<uint32_t> ScriptedObjects()
{
	std::unordered_set<uint32_t> held;
	AddFlockMembers(held);
	auto addValue = [&held](openblack::lhvm::DataType type, const openblack::lhvm::VMValue& value) {
		if (type == openblack::lhvm::DataType::Float)
		{
			if (value.floatVal >= 1.0f && value.floatVal < 4294967296.0f && value.floatVal == std::floor(value.floatVal))
			{
				held.insert(static_cast<uint32_t>(value.floatVal));
			}
		}
		else if (type == openblack::lhvm::DataType::Object || type == openblack::lhvm::DataType::Int)
		{
			held.insert(value.uintVal);
		}
	};
	auto add = [&addValue](const openblack::lhvm::VMVar& var) { addValue(var.type, var.value); };
	for (const auto& [id, record] : g_scriptEntities)
	{
		if (record.type == static_cast<int32_t>(ObjectType::Villager) && !g_releasedFromScript.contains(id))
		{
			held.insert(id);
		}
	}
	const auto& vm = Locator::vm::value();
	for (const auto& var : vm.GetVariables())
	{
		add(var);
	}
	for (const auto& [id, task] : vm.GetTasks())
	{
		for (const auto& var : task.localVars)
		{
			add(var);
		}
		for (uint32_t i = 0; i < task.stack.count; i++)
		{
			addValue(task.stack.types.at(i), task.stack.values.at(i));
		}
	}
	return held;
}

bool SubtypeMatches(int wanted, int actual)
{
	return wanted == k_AnySubtype || wanted < 0 || wanted == actual;
}

template <typename Component, typename SubtypeOf>
void NearestOf(int subtype, const glm::vec3& position, float& bestDistance2, entt::entity& best, SubtypeOf subtypeOf)
{
	Locator::entitiesRegistry::value().Each<const Component, const Transform>(
	    [&](entt::entity entity, const Component& component, const Transform& transform) {
		    if (!SubtypeMatches(subtype, subtypeOf(component)) ||
		        (g_searchExcludes != nullptr && g_searchExcludes->contains(static_cast<uint32_t>(entity))))
		    {
			    return;
		    }
		    // On the ground plane: scripts pass points at height 0 (CALL_NEAR's Citadel query in the Land 1
		    // opening), which by full 3D distance missed the temple standing on the land 5 units away.
		    const auto offset = glm::vec2(transform.position.x - position.x, transform.position.z - position.z);
		    const float distance2 = glm::dot(offset, offset);
		    if (distance2 < bestDistance2)
		    {
			    bestDistance2 = distance2;
			    best = entity;
		    }
	    });
}

// entt::null (0 to the script) when nothing matches within radius, or the type isn't mapped yet.
entt::entity FindNearestScriptObject(int type, int subtype, const glm::vec3& position, float radius)
{
	float bestDistance2 = radius * radius;
	entt::entity best = entt::null;
	// Numbers are the script's own object types (ScriptEnums.h), not openblack's Enums.h ObjectType.
	switch (static_cast<ObjectType>(type))
	{
	case ObjectType::Villager:
		NearestOf<ecs::components::Villager>(subtype, position, bestDistance2, best,
		                                     [](const ecs::components::Villager& v) { return static_cast<int>(v.number); });
		break;
	case ObjectType::VillagerChild:
		// Children only: Land 1's Missionaries look for a galley boy by the Ark this way, and found one of their own
		// sailors (the nearest villager) and thanked you for him.
		Locator::entitiesRegistry::value().Each<const ecs::components::Villager, const Transform>(
		    [&](entt::entity entity, const ecs::components::Villager& villager, const Transform& transform) {
			    if (villager.lifeStage != ecs::components::Villager::LifeStage::Child ||
			        !SubtypeMatches(subtype, static_cast<int>(villager.number)) ||
			        (g_searchExcludes != nullptr && g_searchExcludes->contains(static_cast<uint32_t>(entity))))
			    {
				    return;
			    }
			    const auto offset = glm::vec2(transform.position.x - position.x, transform.position.z - position.z);
			    const float distance2 = glm::dot(offset, offset);
			    if (distance2 < bestDistance2)
			    {
				    bestDistance2 = distance2;
				    best = entity;
			    }
		    });
		break;
	case ObjectType::Abode:
		NearestOf<ecs::components::Abode>(subtype, position, bestDistance2, best,
		                                  [](const ecs::components::Abode& a) { return static_cast<int>(a.type); });
		break;
	case ObjectType::Feature:
		NearestOf<ecs::components::Feature>(subtype, position, bestDistance2, best,
		                                    [](const ecs::components::Feature& f) { return static_cast<int>(f.type); });
		// Animated scenery is a feature to the scripts too (Land 1 finds the gate stone pedestal as one).
		NearestOf<ecs::components::AnimatedStatic>(subtype, position, bestDistance2, best,
		                                           [](const ecs::components::AnimatedStatic& a) { return static_cast<int>(a.type); });
		break;
	case ObjectType::Tree:
		NearestOf<ecs::components::Tree>(subtype, position, bestDistance2, best,
		                                 [](const ecs::components::Tree& t) { return static_cast<int>(t.type); });
		break;
	case ObjectType::Town:
		NearestOf<ecs::components::Town>(subtype, position, bestDistance2, best, [](const ecs::components::Town&) { return 0; });
		break;
	case ObjectType::Creature:
		NearestOf<ecs::components::Creature>(subtype, position, bestDistance2, best,
		                                     [](const ecs::components::Creature&) { return 0; });
		break;
	case ObjectType::Store:
		// A pile of food or wood, the subtype its POT_INFO (Land 1's Missionaries look for 9, wood put down, by their Ark).
		NearestOf<ecs::components::Pot>(subtype, position, bestDistance2, best,
		                                [](const ecs::components::Pot& pot) { return static_cast<int>(pot.type); });
		break;
	case ObjectType::Animal:
		// The subtype is the ANIMAL_INFO number (Land 1's lost flock asks for 4, the sheep).
		NearestOf<ecs::components::Animal>(subtype, position, bestDistance2, best,
		                                   [](const ecs::components::Animal& animal) { return static_cast<int>(animal.type); });
		break;
	case ObjectType::Citadel:
		NearestOf<ecs::components::Temple>(subtype, position, bestDistance2, best, [](const ecs::components::Temple&) { return 0; });
		break;
	case ObjectType::MobileStatic:
	case ObjectType::Rock:
		// Land 1's ProtectGateKeys finds the gate totems and the quarry rock the land put down (the Ape in the lost
		// brother's house, the Tiger by the crowd): unfound, each guard made another and there were two.
		NearestOf<ecs::components::MobileStatic>(subtype, position, bestDistance2, best,
		                                         [](const ecs::components::MobileStatic& m) { return static_cast<int>(m.type); });
		break;
	case ObjectType::Highlight:
	{
		auto& registry = Locator::entitiesRegistry::value();
		for (const auto& [id, highlightType] : g_highlights)
		{
			const auto entity = static_cast<entt::entity>(id);
			const auto* transform = registry.Valid(entity) ? registry.TryGet<Transform>(entity) : nullptr;
			if (transform == nullptr || highlightType != subtype)
			{
				continue;
			}
			const auto offset = glm::vec2(transform->position.x - position.x, transform->position.z - position.z);
			if (glm::dot(offset, offset) < bestDistance2)
			{
				bestDistance2 = glm::dot(offset, offset);
				best = entity;
			}
		}
		break;
	}
	default:
	{
		static std::set<int> s_reported;
		if (s_reported.insert(type).second)
		{
			SPDLOG_LOGGER_WARN(spdlog::get("scripting"), "CALL: script object type {} is not mapped to openblack entities yet", type);
		}
		break;
	}
	}
	return best;
}
} // namespace

struct CameraPathPlay;
void StartCameraPath(int32_t track);
void EndCameraPath();
bool CameraPathRunning();
namespace
{
float SeaDepthAt(glm::vec2 at); // below, with the walkers
bool IsWaterAt(glm::vec2 at);   // likewise
} // namespace

// A script's camera is drawn at least a unit above the land or sea floor under it, its focus raised alike, each frame
// (Camera::Update, as the original's GCamera::Update) - not moved where the script sends it.

void SetCameraPosition() // 001 SET_CAMERA_POSITION
{
	EndCameraPath(); // the script takes the camera from its path
	g_cameraFollow.target = 0;
	const auto position = PopVec();
	SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "SET_CAMERA_POSITION ({:.1f}, {:.2f}, {:.1f}), ground there {:.2f}", position.x, position.y,
	                   position.z, Locator::terrainSystem::value().GetHeightAt(glm::vec2(position.x, position.z)));
	// TODO(Daniels118): check if cinema mode is enabled
	auto& camera = Locator::camera::value();
	camera.SetOrigin(position);
}

void SetCameraFocus() // 002 SET_CAMERA_FOCUS
{
	EndCameraPath(); // the script takes the camera from its path
	g_cameraFollow.focus = 0;
	const auto position = PopVec();
	SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "SET_CAMERA_FOCUS ({:.1f}, {:.2f}, {:.1f}), ground there {:.2f}", position.x, position.y,
	                   position.z, Locator::terrainSystem::value().GetHeightAt(glm::vec2(position.x, position.z)));
	// TODO(Daniels118): check if cinema mode is enabled
	auto& camera = Locator::camera::value();
	camera.SetFocus(position);
}

void MoveCameraPosition() // 003 MOVE_CAMERA_POSITION
{
	EndCameraPath(); // the script takes the camera from its path
	g_cameraFollow.target = 0;
	const auto time = Popf();
	const auto position = PopVec();
	SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "MOVE_CAMERA_POSITION ({:.1f}, {:.2f}, {:.1f}) over {:.1f} s, ground there {:.2f}", position.x,
	                   position.y, position.z, time, Locator::terrainSystem::value().GetHeightAt(glm::vec2(position.x, position.z)));
	auto& camera = Locator::camera::value();
	camera.ScriptMoveOrigin(position, std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::duration<float>(time)));
}

void MoveCameraFocus() // 004 MOVE_CAMERA_FOCUS
{
	EndCameraPath(); // the script takes the camera from its path
	g_cameraFollow.focus = 0;
	const auto time = Popf();
	const auto position = PopVec();
	SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "MOVE_CAMERA_FOCUS ({:.1f}, {:.2f}, {:.1f}) over {:.1f} s, ground there {:.2f}", position.x,
	                   position.y, position.z, time, Locator::terrainSystem::value().GetHeightAt(glm::vec2(position.x, position.z)));
	auto& camera = Locator::camera::value();
	camera.ScriptMoveFocus(position, std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::duration<float>(time)));
}

void GetCameraPosition() // 005 GET_CAMERA_POSITION
{
	auto& camera = Locator::camera::value();
	const auto position = camera.GetOrigin();
	PushVec(position);
}

void GetCameraFocus() // 006 GET_CAMERA_FOCUS
{
	auto& camera = Locator::camera::value();
	const auto focus = camera.GetFocus();
	PushVec(focus);
}

// The script's spirit (HelpSpiritType) as an advisor: Good the Yogi, Evil the Demon (the rest taken as the good one).
helpspirits::Spirit AdvisorOf(int spirit)
{
	return static_cast<HelpSpiritType>(spirit) == HelpSpiritType::Evil ? helpspirits::Spirit::Evil : helpspirits::Spirit::Good;
}

void SpiritEject() // 007 SPIRIT_EJECT
{
	const auto spirit = Pop().intVal;
	helpspirits::Eject(AdvisorOf(spirit));
}

void SpiritHome() // 008 SPIRIT_HOME
{
	const auto spirit = Pop().intVal;
	SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "SPIRIT_HOME {}", spirit);
	helpspirits::Home(AdvisorOf(spirit));
}

void SpiritPointPos() // 009 SPIRIT_POINT_POS
{
	const auto inWorld = static_cast<bool>(Pop().intVal);
	const auto position = PopVec();
	const auto spirit = Pop().intVal;
	SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "SPIRIT_POINT_POS {} at ({:.1f}, {:.1f}, {:.1f}), in world {}", spirit,
	                   position.x, position.y, position.z, inWorld);
	helpspirits::PointAt(AdvisorOf(spirit), position);
}

void SpiritPointGameThing() // 010 SPIRIT_POINT_GAME_THING
{
	const auto inWorld = static_cast<bool>(Pop().intVal);
	const auto target = Pop().uintVal;
	const auto spirit = Pop().intVal;
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = static_cast<entt::entity>(target);
	SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "SPIRIT_POINT_GAME_THING {} at {}, in world {}", spirit, target, inWorld);
	if (registry.Valid(entity) && registry.AllOf<Transform>(entity))
	{
		helpspirits::PointAt(AdvisorOf(spirit), registry.Get<Transform>(entity).position);
	}
}

// Whether a point is on screen: inside the camera's view once projected. (A fixed cone round the view
// direction missed things low on the screen, which is where the family is when you follow them closely,
// so the Land 1 opening never let them carry on.)
bool InFieldOfView(const glm::vec3& position)
{
	const auto clip = Locator::camera::value().GetViewProjectionMatrix() * glm::vec4(position, 1.0f);
	if (clip.w <= 0.0f)
	{
		return false; // behind the camera
	}
	const auto ndc = glm::vec3(clip) / clip.w;
	return std::abs(ndc.x) <= 1.0f && std::abs(ndc.y) <= 1.0f;
}

void GameThingFieldOfView() // 011 GAME_THING_FIELD_OF_VIEW
{
	const auto object = Pop().uintVal;
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = static_cast<entt::entity>(object);
	const auto* transform = object != 0 && registry.Valid(entity) ? registry.TryGet<Transform>(entity) : nullptr;
	Pushb(transform != nullptr && InFieldOfView(transform->position));
}

void PosFieldOfView() // 012 POS_FIELD_OF_VIEW
{
	const auto position = PopVec();
	Pushb(InFieldOfView(position));
}

void RunText() // 013 RUN_TEXT
{
	[[maybe_unused]] const auto withInteraction = Pop().intVal;
	const auto textID = Pop().intVal;
	[[maybe_unused]] const auto singleLine = static_cast<bool>(Pop().intVal);
	// Spoken, from the voice runblack.exe's text table gives it (no text display yet): TEXT_READ waits for the voice
	// to finish, or k_TextReadTime for a line with none.
	g_lastTextId = textID;
	g_lastTextShown = ScriptNow();
	auto& audio = Locator::audio::value();
	if (g_textVoice != entt::null && audio.EmitterExists(g_textVoice))
	{
		audio.StopEmitter(g_textVoice); // the next line cuts in, as a new line does
	}
	g_textVoice = PlayTextVoice(textID);
	openblack::subtitles::Show(textID);
	SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "RUN_TEXT: text {}{}", textID, g_textVoice != entt::null ? " (voiced)" : "");
}

void TempText() // 014 TEMP_TEXT
{
	[[maybe_unused]] const auto withInteraction = Pop().intVal;
	[[maybe_unused]] const auto string = PopString();
	[[maybe_unused]] const auto singleLine = static_cast<bool>(Pop().intVal);
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void TextRead() // 015 TEXT_READ
{
	bool voiced = g_textVoice != entt::null && Locator::audio::value().EmitterExists(g_textVoice);
	// A voice that never ends (never started: 2026-10-18, the opening's line 5187 kept FollowUs waiting for good) is
	// over after k_LongestVoice: said so once, with the sounds there are, to find out why.
	constexpr auto k_LongestVoice = std::chrono::seconds(20);
	if (voiced && ScriptNow() - g_lastTextShown > k_LongestVoice)
	{
		static int32_t s_said = -1;
		if (s_said != g_lastTextId)
		{
			s_said = g_lastTextId;
			size_t emitters = 0;
			Locator::entitiesRegistry::value().Each<const openblack::ecs::components::AudioEmitter>(
			    [&emitters](const openblack::ecs::components::AudioEmitter&) { ++emitters; });
			const auto& emitter = Locator::entitiesRegistry::value().Get<openblack::ecs::components::AudioEmitter>(g_textVoice);
			SPDLOG_LOGGER_WARN(spdlog::get("audio"), "Voice of text {} still \"playing\" after {} s (source {}, status {}; {} sounds in all): carrying on",
			                   g_lastTextId, k_LongestVoice.count(), emitter.sourceId,
			                   static_cast<int>(Locator::audio::value().GetStatus(g_textVoice)), emitters);
		}
		voiced = false;
	}
	const bool read = g_lastTextId < 0 || (!voiced && ScriptNow() - g_lastTextShown >= k_TextReadTime);
	Pushb(read);
}

void GameThingClicked() // 016 GAME_THING_CLICKED
{
	const auto object = Pop().uintVal;
	// Every script asking during the turn after the click sees it (EndScriptTurn clears it): each scroll is asked
	// about by its ChallengeHighlightNotify and by its challenge, and taking the click away at the first question
	// left the challenge never seeing it.
	const bool clicked = object != 0 && object == g_clickedThing;
	Pushb(clicked);
}

namespace
{
void HoldInScriptState(uint32_t object, int32_t state); // below, with HoldForScript
} // namespace

void SetScriptState() // 017 SET_SCRIPT_STATE
{
	const auto state = Pop().intVal;
	const auto object = Pop().uintVal;
	HoldInScriptState(object, state);
}

void SetScriptStatePos() // 018 SET_SCRIPT_STATE_POS
{
	[[maybe_unused]] const auto position = PopVec();
	[[maybe_unused]] const auto object = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void SetScriptFloat() // 019 SET_SCRIPT_FLOAT
{
	[[maybe_unused]] const auto value = Popf();
	[[maybe_unused]] const auto object = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void SetScriptUlong() // 020 SET_SCRIPT_ULONG
{
	const auto loop = Pop().intVal;
	const auto animation = Pop().intVal;
	const auto object = Pop().uintVal;
	// Plays `animation` (an AnimId) `loop` times, a negative count for ever (the lost brother's PDying, -1);
	// PLAYED reports when it has finished.
	g_scriptAnimations[object] = {static_cast<uint32_t>(animation),
	                              loop < 0 ? std::numeric_limits<float>::max() : static_cast<float>(std::max(loop, 1))};
	// Test hook: OPENBLACK_SHOT_ON_ANIM=<AnimId>:<ms>[,<ms>...]:<png path with %d> screenshots at those
	// times into the animation (the %d becomes the ms).
	for (const char* shot : {std::getenv("OPENBLACK_SHOT_ON_ANIM"), std::getenv("OPENBLACK_SHOT_ON_ANIM2")})
	if (shot != nullptr && std::atoi(shot) == animation)
	{
		const char* times = std::strchr(shot, ':');
		const char* path = times != nullptr ? std::strchr(times + 1, ':') : nullptr;
		if (path != nullptr)
		{
			const auto now = std::chrono::steady_clock::now();
			std::string list(times + 1, path);
			std::stringstream stream(list);
			for (std::string ms; std::getline(stream, ms, ',');)
			{
				std::string file(path + 1);
				if (const auto at = file.find("%d"); at != std::string::npos)
				{
					file.replace(at, 2, ms);
				}
				g_pendingShots.emplace_back(file, now + std::chrono::milliseconds(std::stoi(ms)));
			}
		}
	}
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = static_cast<entt::entity>(object);
	if (object != 0 && registry.Valid(entity) && registry.AllOf<openblack::ecs::components::Mesh>(entity))
	{
		registry.AssignOrReplace<openblack::ecs::components::Animated>(entity, static_cast<uint32_t>(animation), 0.0f);
	}
}

// Object properties (ObjectPropertyType, B&W1 numbering). The ones openblack models are read from and
// written to the entity; the rest are remembered per object, so scripts read back what they set.
namespace
{
enum class Prop : int32_t
{
	Health = 1,
	Angle = 2,
	Scale = 8,
	Flying = 5,
	Drowning = 6, // CreatureSavingPeople: a villager in the water until carried out of it
	InHand = 9,
	Age = 16, // one more entry than openblack's ObjectPropertyType before here: scripts set 16 to ages (3-33)
	Player = 21, // a town's owner: GuideImpressTownNextLesson waits for the Aztec town's to be 1, you
	BuiltPercentage = 22, // CheckCitadel (Land 1) waits for the Citadel's to reach 1
	XPos = 23,
	YPos = 24,
	ZPos = 25,
};

std::unordered_map<uint64_t, float> g_objectProperties;

uint64_t PropertyKey(uint32_t object, int32_t prop)
{
	return (static_cast<uint64_t>(object) << 32) | static_cast<uint32_t>(prop);
}

float YawDegrees(const glm::mat3& rotation)
{
	return glm::degrees(std::atan2(rotation[2][0], rotation[2][2]));
}
} // namespace

namespace
{
float SeaDepthAt(glm::vec2 at); // below, with the walkers
bool IsWaterAt(glm::vec2 at);   // likewise
} // namespace

void GetProperty() // 021 GET_PROPERTY
{
	const auto object = Pop().uintVal;
	const auto prop = Pop().intVal;
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = static_cast<entt::entity>(object);
	const bool valid = object != 0 && registry.Valid(entity);
	const auto* transform = valid ? registry.TryGet<Transform>(entity) : nullptr;
	const auto* villager = valid ? registry.TryGet<openblack::ecs::components::Villager>(entity) : nullptr;

	float value = 0.0f;
	if (const auto it = g_objectProperties.find(PropertyKey(object, prop)); it != g_objectProperties.end())
	{
		value = it->second;
	}
	switch (static_cast<Prop>(prop))
	{
	case Prop::InHand:
		value = object != 0 && object == g_heldObject ? 1.0f : 0.0f;
		break;
	case Prop::BuiltPercentage:
		if (const auto* construction = valid ? registry.TryGet<openblack::ecs::components::Construction>(entity) : nullptr)
		{
			value = construction->built;
		}
		break;
	case Prop::Health:
		// Villagers have theirs; anything else is whole unless a script said otherwise (Land 1's LostBrother takes a
		// house under 0.75 for broken into).
		if (villager != nullptr)
		{
			value = villager->life;
		}
		else if (g_objectProperties.find(PropertyKey(object, prop)) == g_objectProperties.end())
		{
			value = 1.0f;
		}
		break;
	case Prop::Flying:
	{
		const auto knocked = g_knockedUntil.find(object);
		value = g_flying.contains(object) || (knocked != g_knockedUntil.end() && ScriptNow() < knocked->second) ? 1.0f : 0.0f;
	}
		break;
	case Prop::Drowning:
		// A villager standing in water (the land's own water cells), not carried by the hand or a creature.
		if (villager != nullptr && transform != nullptr && value == 0.0f && object != g_heldObject &&
		    std::none_of(g_creatureHolding.begin(), g_creatureHolding.end(), [object](const auto& held) { return held.second == object; }))
		{
			// In a water cell, as the original's IsDrowning (its drowning state is entered on one). Our guess of the
			// sea's depth (deeper than 0.5) instead had four of Land 1's five drowning men and the opening's boy, all
			// in the shallows, not drowning (Mike, 2026-10-23).
			value = IsWaterAt(glm::vec2(transform->position.x, transform->position.z)) ? 1.0f : 0.0f;
		}
		break;
	case Prop::Player:
		// A town's: whose it is (0 nobody, 1 you), as belief has decided.
		if (valid && registry.AllOf<openblack::ecs::components::Town>(entity))
		{
			value = static_cast<float>(TownOwnerOf(object));
		}
		break;
	case Prop::Angle:
		value = transform != nullptr ? YawDegrees(transform->rotation) : value;
		break;
	case Prop::Scale:
		if (transform != nullptr)
		{
			const auto base = g_creatureBaseScale.find(object);
			value = base != g_creatureBaseScale.end() && base->second > 0.0f ? transform->scale.x / base->second : transform->scale.x;
		}
		break;
	case Prop::Age:
		value = villager != nullptr ? static_cast<float>(villager->age) : value;
		break;
	case Prop::XPos:
		value = transform != nullptr ? transform->position.x : value;
		break;
	case Prop::YPos:
		value = transform != nullptr ? transform->position.y : value;
		if (transform != nullptr) // height above the ground, as SET_PROPERTY
		{
			value -= Locator::terrainSystem::value().GetHeightAt(glm::vec2(transform->position.x, transform->position.z));
		}
		break;
	case Prop::ZPos:
		value = transform != nullptr ? transform->position.z : value;
		break;
	}
	Pushf(value);
}

void SetProperty() // 022 SET_PROPERTY
{
	const auto value = Popf();
	const auto object = Pop().uintVal;
	const auto prop = Pop().intVal;
	g_objectProperties[PropertyKey(object, prop)] = value;

	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = static_cast<entt::entity>(object);
	if (object == 0 || !registry.Valid(entity))
	{
		return;
	}
	auto* transform = registry.TryGet<Transform>(entity);
	auto* villager = registry.TryGet<openblack::ecs::components::Villager>(entity);
	switch (static_cast<Prop>(prop))
	{
	case Prop::BuiltPercentage:
		// Land 1's PreventCitadelCompletion holds the Temple at 0.9 until the give-wood demo has played.
		if (auto* construction = registry.TryGet<openblack::ecs::components::Construction>(entity))
		{
			construction->built = std::clamp(value, 0.0f, 1.0f);
		}
		break;
	case Prop::Health:
		if (villager != nullptr)
		{
			villager->life = std::clamp(value, 0.0f, 1.0f);
		}
		break;
	case Prop::Angle:
		if (transform != nullptr)
		{
			transform->rotation = glm::mat3(glm::eulerAngleY(glm::radians(value)));
		}
		break;
	case Prop::Scale:
		if (transform != nullptr)
		{
			const auto base = g_creatureBaseScale.find(object);
			transform->scale = glm::vec3(base != g_creatureBaseScale.end() ? base->second * value : value);
		}
		break;
	case Prop::Age:
		if (villager != nullptr)
		{
			using openblack::ecs::components::Villager;
			villager->age = static_cast<uint32_t>(std::max(value, 0.0f));
			const auto type = openblack::GVillagerInfo::Find(villager->tribe, villager->number);
			villager->lifeStage = VillagerArchetype::IsChild(type, villager->age) ? Villager::LifeStage::Child : Villager::LifeStage::Adult;
			if (auto* mesh = registry.TryGet<openblack::ecs::components::Mesh>(entity))
			{
				mesh->id = VillagerArchetype::MeshResource(type, villager->age);
			}
			if (transform != nullptr)
			{
				transform->scale = glm::vec3(VillagerArchetype::ScaleForAge(type, villager->age)); // Villager::SetAge
			}
		}
		break;
	case Prop::YPos:
		// Y is a height above the ground: DidYouKnow sets a signpost's 0 (on the ground), Land 1's gold scroll 14 (up
		// on the gate), SwapCreatures the creature's height + 1, TheSculptor his rock's 0 (set down to be carved -
		// taken as a world height, it sank underground and he walked off).
		if (transform != nullptr)
		{
			transform->position.y =
			    Locator::terrainSystem::value().GetHeightAt(glm::vec2(transform->position.x, transform->position.z)) + value;
		}
		break;
	case Prop::XPos:
	case Prop::ZPos:
		if (transform != nullptr)
		{
			transform->position[prop - static_cast<int32_t>(Prop::XPos)] = value;
		}
		break;
	}
	// A thing with a physics body is where its body is: move the body too, or the next physics step puts it back.
	if (transform != nullptr && (prop == static_cast<int32_t>(Prop::XPos) || prop == static_cast<int32_t>(Prop::YPos) ||
	                             prop == static_cast<int32_t>(Prop::ZPos)))
	{
		if (auto* body = registry.TryGet<openblack::ecs::components::RigidBody>(entity))
		{
			btTransform bodyTransform = body->handle.getWorldTransform();
			bodyTransform.setOrigin(btVector3(transform->position.x, transform->position.y, transform->position.z));
			body->handle.setWorldTransform(bodyTransform);
			body->motionState->setWorldTransform(bodyTransform);
		}
	}
}

void GetPosition() // 023 GET_POSITION
{
	const auto objId = Pop().uintVal;

	glm::vec3 position(0.0f);
	if (objId != 0)
	{
		auto& registry = Locator::entitiesRegistry::value();
		const auto entity = static_cast<entt::entity>(objId);
		auto* transform = registry.Valid(entity) ? registry.TryGet<Transform>(entity) : nullptr;
		if (transform != nullptr)
		{
			position = transform->position;
		}
		else
		{
			// Read as (0, 0, 0): Land 1's hermit was sent there - his hut's door marker gone - and never came out.
			// Once per object: what it was to the scripts, and whether anything stands on its number now.
			static std::unordered_set<uint32_t> s_reported;
			if (s_reported.insert(objId).second)
			{
				const auto record = g_scriptEntities.find(objId);
				SPDLOG_LOGGER_WARN(spdlog::get("scripting"), "GET_POSITION {}: no such object ({}; registry {}), read as (0, 0, 0)", objId,
				                   record != g_scriptEntities.end() ? fmt::format("a script's type {} subtype {}", record->second.type,
				                                                                  record->second.subtype)
				                                                    : std::string("no script record"),
				                   registry.Valid(entity) ? "valid, no position" : "not valid");
			}
		}
	}

	PushVec(position);
}

namespace
{
void EndScriptMove(uint32_t object); // below, with the walkers
} // namespace

void SetPosition() // 024 SET_POSITION
{
	auto position = PopVec();
	const auto objId = Pop().uintVal;

	// Not what the player holds: scripts that keep someone in place every turn (Land 1's family, the lost brother)
	// pulled them back out of the hand.
	if (objId != 0 && objId != g_heldObject)
	{
		const auto& island = Locator::terrainSystem::value();
		position.y = island.GetHeightAt(glm::vec2(position.x, position.z));
		auto& registry = Locator::entitiesRegistry::value();
		auto* transform = registry.TryGet<Transform>(static_cast<entt::entity>(objId));
		if (transform != nullptr)
		{
			transform->position = position;
			g_scriptPlaced[objId] = position;
			SPDLOG_LOGGER_DEBUG(spdlog::get("scripting"), "SET_POSITION {} ({:.2f}, {:.2f}, {:.2f})", objId, position.x, position.y, position.z);
			// Put there, not walked on: GScript::SetPosition moves the map object itself (a creature's by
			// ForceMoveMapObjectWithoutWalking). A MOVE_GAME_THING still going had the opening's mother, set on the
			// shore and turned to her son by SET_FOCUS, turn back to where she had been walking and call that way
			// (Mike, 2026-10-22).
			EndScriptMove(objId);
		}
	}
}

void GetDistance() // 025 GET_DISTANCE
{
	const auto p1 = PopVec();
	const auto p0 = PopVec();
	auto distance = glm::length(p1 - p0);
	// Diagnostic (2026-10-18): Land 1's ThrowingStones judges its boulder moved from the start and never shows its
	// scroll; what it measures against its spot (2031.1, 2327.4).
	for (const auto& p : {p0, p1})
	{
		if (glm::distance(glm::vec2(p.x, p.z), glm::vec2(2031.13f, 2327.41f)) < 0.01f)
		{
			static auto s_lastRockCheck = std::chrono::steady_clock::time_point {};
			if (std::chrono::steady_clock::now() - s_lastRockCheck > std::chrono::seconds(3))
			{
				s_lastRockCheck = std::chrono::steady_clock::now();
				SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "GET_DISTANCE rock check {:.3f}: ({:.2f}, {:.2f}, {:.2f}) to ({:.2f}, {:.2f}, {:.2f})",
				                   distance, p0.x, p0.y, p0.z, p1.x, p1.y, p1.z);
			}
			break;
		}
	}
	// Scripts wait for a walk to end with "distance == 0". A hair's breadth off (float rounding) is there.
	if (distance > 0.0f && distance < 0.05f)
	{
		distance = 0.0f;
	}
	else if (distance > 0.0f && distance < 1.0f)
	{
		static auto s_lastNearMiss = std::chrono::steady_clock::time_point {};
		if (std::chrono::steady_clock::now() - s_lastNearMiss > std::chrono::seconds(2))
		{
			s_lastNearMiss = std::chrono::steady_clock::now();
			SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "GET_DISTANCE near miss {:.3f}: ({:.2f}, {:.2f}, {:.2f}) to ({:.2f}, {:.2f}, {:.2f})",
			                   distance, p0.x, p0.y, p0.z, p1.x, p1.y, p1.z);
		}
	}
	Pushf(distance);
}

void Call() // 026 CALL
{
	const auto excludingScripted = static_cast<bool>(Pop().intVal);
	const auto position = PopVec();
	const auto subtype = Pop().intVal;
	const auto type = Pop().intVal;
	// "call <type> at <position>": the object at that spot - nearest within a small radius.
	const auto scripted = excludingScripted ? ScriptedObjects() : std::unordered_set<uint32_t> {};
	g_searchExcludes = excludingScripted ? &scripted : nullptr;
	const auto object = FindNearestScriptObject(type, subtype, position, 20.0f);
	g_searchExcludes = nullptr;
	Pusho(object == entt::null ? 0 : static_cast<uint32_t>(object));
}

void Create() // 027 CREATE
{
	const auto position = PopVec();
	const auto subtype = Pop().intVal;
	const auto type = static_cast<ObjectType>(Pop().intVal);

	const auto object = CreateScriptObject(type, subtype, position, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);

	Pusho(static_cast<uint32_t>(object));
}

void Random() // 028 RANDOM
{
	const auto max = Popf();
	const auto min = Popf();
	const float random = min + (max - min) * static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
	Pushf(random);
}

void DllGettime() // 029 DLL_GETTIME
{
	// The game's time in seconds (the scripts' clock, which keeps pace with the play speed): scripts time reminders by
	// it (QuestHighlightNotify: one every Timing, 30 x 60). It pushed nothing at all, and every script that asked took
	// an empty stack's nothing - the "pop an empty stack" errors throughout the log.
	static const auto s_start = ScriptNow();
	Pushf(std::chrono::duration<float>(ScriptNow() - s_start).count());
}

void StartCameraControl() // 030 START_CAMERA_CONTROL
{
	// Always granted: nothing else in openblack competes for the camera yet.
	Locator::camera::value().SetScriptControl(true);
	g_scriptHasCamera = true;
	g_cameraFollow = {};
	Pushb(true);
}

void EndCameraControl() // 031 END_CAMERA_CONTROL
{
	Locator::camera::value().SetScriptControl(false);
	g_scriptHasCamera = false;
	g_cameraFollow = {};
}

void SetWidescreen() // 032 SET_WIDESCREEN
{
	const auto enabled = static_cast<bool>(Pop().intVal);
	// State only for now: the letterbox bars themselves are not drawn yet.
	Locator::camera::value().SetWidescreen(enabled);
}

// Script walks (WALK_PATH) and flocks (FLOCK_*), moved every frame by UpdateScriptFrame.
namespace
{
struct ScriptWalk
{
	std::vector<glm::vec3> points; // the track's ground curve, sampled
	std::vector<float> lengths;    // distance along it at each point
	float at;                      // distance walked so far
	float end;                     // distance to stop at (may be below `at`: walking backwards)
	int32_t track = 0;             // the camera.edt track it follows
	// A MobileObject's walk (the opening's sharks): by the track's own clock, as the original's
	// MobileObject::MoveAlongPath - 100 ms of the track a game turn, so its keys' times - placed where
	// LH3DWay::Running::GetPosAtTime puts it. Empty for a living, which walks at its own speed.
	std::vector<float> keyTimes;  // ms from the first key
	std::vector<float> keySpeeds; // m/s
	float timeMs = 0.0f;
	float endMs = 0.0f;
};
std::unordered_map<uint32_t, ScriptWalk> g_walks;

struct Flock
{
	std::vector<uint32_t> members;
	uint32_t leader = 0;
};
std::unordered_map<uint32_t, Flock> g_flocks;

// DANCE_CREATE's dances, as the original plays them (bw1-decomp's Dance/GroupBehaviour/DanceGroup, and what it has not
// decompiled yet - LoadDance, UpdateGroupBasedOnAction, UpdateGroupBasedOnDancerAction, Villager::DanceAnimation - read
// from runblack.exe 1.20 at the addresses its symbols give). A dance is a container like a flock (the script
// FLOCK_ATTACHes the dancers to it and counts them with ID_SIZE). Its choreography is Scripts/Dance/<info.dat file name>:
// keyframes of actions on groups of dancers - a shape, its size, where in it, and what the dancers do - played a game
// turn (100 ms) at a time and looped every Length minutes. Land 1's opening (FollowUs) has the Norse welcome crowd dance
// NewDanceAroundPerson by the statue: men and women in two rings, facing in and out, dancing and praying.
namespace dance
{
// DANCE_GROUP_ACTION_TYPE
enum class Action : uint32_t
{
	SetShape = 0,
	SetPos = 1,
	SetSize = 2,
	SetAction = 3,
	Proportion = 6,
	SetAngle = 11,
	SetPath = 14,
};
// DANCE_GROUP_MOVEMENT_KIND (only those the port does something with)
enum class Kind : uint32_t
{
	StandStill = 0,
	RotateDancers = 1,
	RandomDance = 4,
	Pray = 5,
	Funky = 6,
	FaceIn = 7,
	FaceOut = 8,
	RotateShape = 9,
	PlayAnim = 10,
	ChangeSize = 11,
	MoveToPos = 12,
	Sad = 15,
	Happy = 16,
	LookingForSomething = 17,
	Sex = 18,
	Mourning = 19,
	Talk = 20,
	TalkAndPoint = 21,
	WorshipCreature = 22,
	DanceX = 23,
	DanceXPartner = 24,
	FemaleA = 25,
	FemaleC = 27,
	MaleA = 28,
	MaleC = 30,
};
constexpr float k_TurnSeconds = 0.1f;   // MillisecondsPerGameTurn
constexpr int k_TurnsPerKeyBucket = 5;  // DanceKeyFrame::IsTimeEqualTo: key frames are half a second apart at most
constexpr float k_DancerFullTurn = 256.0f;
constexpr float k_ShapeFullTurn = 2048.0f;
constexpr float k_Speed = 0.8f; // SetWorshipSpeed(DANCE_DEFAULT_WORSHIP_SPEED 0.25): floor(2.5) / 10 * MaxDanceSpeed 4

struct DancerAction
{
	Kind kind = Kind::StandStill;
	uint32_t param1 = 0; // a float or an int (PLAY_ANIM's animation), as the union in the original
	float param2 = 0.0f;
	int32_t endTime = 10000;
	float Param1Float() const
	{
		float value;
		std::memcpy(&value, &param1, sizeof(value));
		return value;
	}
};
struct KeyAction
{
	std::vector<uint32_t> groups;
	Action type;
	std::array<uint32_t, 4> value; // Value, Value2, Value3, Unused
	DancerAction action;
	float Float(size_t i) const
	{
		float f;
		std::memcpy(&f, &value.at(i), sizeof(f));
		return f;
	}
};
struct KeyFrame
{
	float time;
	std::vector<KeyAction> actions;
};
struct Choreography
{
	std::vector<KeyFrame> keyFrames; // in time order
	uint32_t numGroups = 1;
	uint32_t lengthMinutes = 5;
};

// GroupBehaviour::LoadDance: version, key frames (time, edit flags, actions: groups, type, a 0x38-byte argument), then
// the start time, number of groups, two editor values, the length in minutes, a camera angle (version 2 on) and each
// group's name.
std::optional<Choreography> Load(const std::string& fileName)
{
	std::vector<uint8_t> data;
	try
	{
		auto& fileSystem = Locator::filesystem::value();
		data = fileSystem.ReadAll(fileSystem.GetPath<openblack::filesystem::Path::Scripts>() / "Dance" / fileName);
	}
	catch (const std::exception& e)
	{
		SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "Dance {}: could not read Scripts/Dance/{}: {}", fileName, fileName, e.what());
		return std::nullopt;
	}
	size_t at = 0;
	bool ok = true;
	auto u32 = [&]() -> uint32_t {
		uint32_t value = 0;
		if (at + 4 > data.size())
		{
			ok = false;
			return 0;
		}
		std::memcpy(&value, data.data() + at, 4);
		at += 4;
		return value;
	};
	auto f32 = [&]() {
		const auto bits = u32();
		float value;
		std::memcpy(&value, &bits, sizeof(value));
		return value;
	};
	Choreography dance;
	const auto version = u32();
	const auto numKeyFrames = u32();
	for (uint32_t k = 0; ok && k < numKeyFrames && k < 10000; k++)
	{
		KeyFrame frame;
		frame.time = f32();
		u32(); // edit flags
		const auto numActions = u32();
		for (uint32_t a = 0; ok && a < numActions && a < 1000; a++)
		{
			KeyAction action;
			const auto numGroups = u32();
			for (uint32_t g = 0; ok && g < numGroups && g < 64; g++)
			{
				action.groups.push_back(u32());
			}
			action.type = static_cast<Action>(u32());
			for (auto& value : action.value)
			{
				value = u32();
			}
			action.action.kind = static_cast<Kind>(u32());
			action.action.param1 = u32();
			action.action.param2 = f32();
			action.action.endTime = static_cast<int32_t>(u32());
			at += 6 * sizeof(float); // camera points
			frame.actions.push_back(std::move(action));
		}
		dance.keyFrames.push_back(std::move(frame));
	}
	f32(); // start time
	dance.numGroups = std::max(u32(), 1u);
	u32(); // editor values
	u32();
	dance.lengthMinutes = std::max(u32(), 1u);
	if (!ok || at > data.size())
	{
		SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "Dance {}: Scripts/Dance/{} ends early (version {})", fileName, fileName, version);
		return std::nullopt;
	}
	std::stable_sort(dance.keyFrames.begin(), dance.keyFrames.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
	SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "Dance {}: {} key frames, {} groups, {} min loop", fileName, dance.keyFrames.size(),
	                   dance.numGroups, dance.lengthMinutes);
	return dance;
}

// DanceGroup, what the port needs of it.
struct Group
{
	uint32_t shape = 0; // DANCE_SHAPE_CIRCLE
	bool isPath = false;
	glm::vec2 pos {0.0f};
	float size = 20.0f; // across, in metres
	float angle = 0.0f; // of the shape, a 2048th of a turn
	DancerAction action;
	float dancerAngle = 0.0f; // where round the shape the first dancer is, a 256th of a turn
	float changeSize = 0.0f;
	glm::vec2 changeMove {0.0f};
	float changeDancerRotation = 0.0f;
	float changeShapeRotation = 0.0f;
	bool fixed = false;
	uint32_t proportion = 100;
	bool facingOut = false; // FACE_OUT's, kept while the dancers dance; FACE_IN's otherwise
	uint32_t actionSerial = 0; // a new action restarts the dancers' animations
	std::vector<uint32_t> dancers;
};
} // namespace dance

struct Dance
{
	glm::vec3 centre;
	float secondsLeft;
	std::shared_ptr<const dance::Choreography> choreography; // null: the file could not be read - the dancers stand round
	std::vector<dance::Group> groups;
	float time = 0.0f; // in game turns
	float turnClock = 0.0f;
	size_t nextKeyFrame = 0;
	std::unordered_map<uint32_t, uint32_t> dancerSerial; // the action each dancer's animation is for
};
std::unordered_map<uint32_t, Dance> g_dances;

// A script's flock is the script's: its members are scripted (a town, kept here for its count, is not). Land 1's FollowUs
// gathers the citadel builders by asking CALL_IN for a Norse villager no script has, then putting him in the builders'
// flock, until none is left - without this it got the same villager back for ever (2026-10-19).
void AddFlockMembers(std::unordered_set<uint32_t>& held)
{
	auto& registry = Locator::entitiesRegistry::value();
	for (const auto& [id, flock] : g_flocks)
	{
		const auto entity = static_cast<entt::entity>(id);
		if (registry.Valid(entity) && registry.AllOf<openblack::ecs::components::Town>(entity))
		{
			continue;
		}
		held.insert(flock.members.begin(), flock.members.end());
		if (flock.leader != 0)
		{
			held.insert(flock.leader);
		}
	}
}

constexpr int32_t k_SpeedProperty = 11; // what scripts set to make livings run (0.9), walk (0.4) or stop (0)
// A living's full speed, for a script move given no speed (ScriptSpeed): a villager's fleeing speed from its
// type's info (a Norse woman 6 m/s, a man 6.5; world units are metres). Anything else: 3 units/s.
constexpr float k_UnitsPerSecond = 3.0f;
float FullSpeed(uint32_t object)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = static_cast<entt::entity>(object);
	if (const auto* villager = registry.Valid(entity) ? registry.TryGet<openblack::ecs::components::Villager>(entity) : nullptr)
	{
		const auto type = openblack::GVillagerInfo::Find(villager->tribe, villager->number);
		const auto& info = Locator::infoConstants::value().villager.at(static_cast<size_t>(type));
		return GetSpeedStateSpeed(info.speedGroup.speedFleeing);
	}
	return k_UnitsPerSecond;
}

// Flock members following their leader: moved every frame straight to their spot by the leader.
std::unordered_map<uint32_t, glm::vec3> g_followers;
// Livings a script sent somewhere (MOVE_GAME_THING): moved every frame straight to the spot at their
// script speed and placed exactly on it (scripts wait for a distance of 0).
std::unordered_map<uint32_t, glm::vec3> g_scriptMoves;
// How fast each script-moved living went (a MOVE_GAME_THING, a WALK_PATH, a flock member), units a second, and in
// which script frame: its walk plays at that, as the original's plays at the speed it gives the villager
// (Villager::MoveToPosAnimation) - not at the ground it covered between two frames. That was measured in the
// animations, before these moves, so each frame's distance went over the next frame's time: the walk's pace jumped
// with every uneven frame (Mike, 2026-10-22: the opening's family walked jittery, others smoothly - the pathfinder's
// walkers are moved in the animations themselves).
struct MoverSpeed
{
	float speed = 0.0f;
	uint64_t frame = 0;
};
std::unordered_map<uint32_t, MoverSpeed> g_moverSpeeds;
uint64_t g_scriptFrame = 0;

// A script's move or walk of it, if it has one, given up where it is.
void EndScriptMove(uint32_t object)
{
	g_scriptMoves.erase(object);
	g_walks.erase(object);
	g_moveAnimations.erase(object);
	g_moverSpeeds.erase(object); // its speed from the frame before is not a move now
}

// How deep the sea is at a point. The land keeps no depth: its sea cells lie flat at the water's height (0), drawn
// transparent over the sea plane. So, as Mike put it (2026-10-17), the farther from the shore, the deeper: a cell's
// distance to the nearest land cell (a chamfer distance over the 512 x 512 cell grid, made once), d units, gives
// 0.04 d + 0.0008 d^2 - half a unit 10 out, 3 at 40, 8 at 80, 24 at 150.
float SeaDepthAt(glm::vec2 at)
{
	constexpr int k_Cells = 512;
	constexpr float k_Cell = 10.0f;
	static std::vector<float> s_distance; // in cells, to the nearest land cell
	if (s_distance.empty())
	{
		const auto& land = Locator::terrainSystem::value();
		constexpr float k_Far = 1e6f;
		s_distance.assign(static_cast<size_t>(k_Cells * k_Cells), k_Far);
		auto index = [](int x, int z) { return static_cast<size_t>(z * k_Cells + x); };
		int waterCells = 0;
		for (int z = 0; z < k_Cells; ++z)
		{
			for (int x = 0; x < k_Cells; ++x)
			{
				const auto& cell = land.GetCell(glm::u16vec2(x, z));
				const bool water = cell.properties.hasWater || cell.properties.fullWater;
				waterCells += water ? 1 : 0;
				if (!water)
				{
					s_distance[index(x, z)] = 0.0f;
				}
			}
		}
		constexpr float k_Diagonal = 1.41421f;
		for (int z = 0; z < k_Cells; ++z)
		{
			for (int x = 0; x < k_Cells; ++x)
			{
				auto& d = s_distance[index(x, z)];
				if (x > 0) d = std::min(d, s_distance[index(x - 1, z)] + 1.0f);
				if (z > 0) d = std::min(d, s_distance[index(x, z - 1)] + 1.0f);
				if (x > 0 && z > 0) d = std::min(d, s_distance[index(x - 1, z - 1)] + k_Diagonal);
				if (x + 1 < k_Cells && z > 0) d = std::min(d, s_distance[index(x + 1, z - 1)] + k_Diagonal);
			}
		}
		for (int z = k_Cells - 1; z >= 0; --z)
		{
			for (int x = k_Cells - 1; x >= 0; --x)
			{
				auto& d = s_distance[index(x, z)];
				if (x + 1 < k_Cells) d = std::min(d, s_distance[index(x + 1, z)] + 1.0f);
				if (z + 1 < k_Cells) d = std::min(d, s_distance[index(x, z + 1)] + 1.0f);
				if (x + 1 < k_Cells && z + 1 < k_Cells) d = std::min(d, s_distance[index(x + 1, z + 1)] + k_Diagonal);
				if (x > 0 && z + 1 < k_Cells) d = std::min(d, s_distance[index(x - 1, z + 1)] + k_Diagonal);
			}
		}
		SPDLOG_LOGGER_INFO(spdlog::get("game"), "Sea depth: {} water cells of {}, measured from the shore", waterCells, k_Cells * k_Cells);
	}
	const int x = static_cast<int>(at.x / k_Cell);
	const int z = static_cast<int>(at.y / k_Cell);
	float cells = 0.0f;
	if (x < 0 || z < 0 || x >= k_Cells || z >= k_Cells)
	{
		cells = 50.0f; // off the map: open sea
	}
	else
	{
		cells = s_distance[static_cast<size_t>(z * k_Cells + x)];
	}
	const float d = std::max(cells * k_Cell - 0.5f * k_Cell, 0.0f);
	return d <= 0.0f ? 0.0f : 0.04f * d + 0.0008f * d * d;
}

// In the water, as the original decides who is drowning (MapCoords::IsWater, LH3DIsland::IsWater): the land cell under
// the point has its water flag (LNDCell's hasWater, the cell's 0x10), or the point is off the map. Not how deep it is:
// Villager's constructor and HasSunk put anyone in such a cell into VILLAGER_STATE_DROWNING.
bool IsWaterAt(glm::vec2 at)
{
	constexpr float k_Cell = 10.0f;
	constexpr int k_Cells = 512;
	const int x = static_cast<int>(std::floor(at.x / k_Cell));
	const int z = static_cast<int>(std::floor(at.y / k_Cell));
	if (x < 0 || z < 0 || x >= k_Cells || z >= k_Cells)
	{
		return true;
	}
	return Locator::terrainSystem::value().GetCell(glm::u16vec2(x, z)).properties.hasWater;
}

// Where the bottom is: the sea's bed (under its depth) out in the sea, else the land.
float SeabedAt(glm::vec2 at)
{
	const float depth = SeaDepthAt(at);
	return depth > 0.0f ? -depth : Locator::terrainSystem::value().GetHeightAt(at);
}

// The speed a script gave a living, in units a second. SET_PROPERTY's SCRIPT_OBJECT_PROPERTY_TYPE_SPEED is the
// original's SetSpeedInMetres(value, 0) (runblack.exe 0x70e9f6): metres a game turn - 10 turns a second
// (MobileWallHug::GetSpeedInMetresPerSecond is it x 10), a metre a unit (GUtils::ConvertMetersToWholeDistance and
// ConvertDistance3DToGame scale alike) - and Villager::SetSpeed with that 0 flag takes it as it is. So FollowUs's
// 0.2 is the family's 2 m/s stroll and the son's 0.6 a 6 m/s run.
std::optional<float> ScriptSpeed(uint32_t object)
{
	constexpr float k_TurnsPerSecond = 10.0f;
	const auto it = g_objectProperties.find(PropertyKey(object, k_SpeedProperty));
	if (it == g_objectProperties.end())
	{
		return std::nullopt;
	}
	return std::max(it->second, 0.0f) * k_TurnsPerSecond;
}

// Turn an object about the vertical axis to face a point. The game's people face their model's -Z (their
// heels point to +Z in the walk, and a camera placed behind two villagers told to look past it saw their
// faces until this was -Z); eulerAngleY(a) turns -Z to (-sin a, 0, -cos a).
void FaceTowards(Transform& transform, const glm::vec3& target)
{
	const auto toTarget = target - transform.position;
	if (toTarget.x != 0.0f || toTarget.z != 0.0f)
	{
		transform.rotation = glm::mat3(glm::eulerAngleY(std::atan2(-toTarget.x, -toTarget.z)));
	}
}

// Hold a living for the script: the villager AI leaves it alone and the pathfinder doesn't move it.
void HoldForScript(entt::entity entity, const glm::vec3& at)
{
	auto& registry = Locator::entitiesRegistry::value();
	registry.AssignOrReplace<ScriptMove>(entity, at, 0u, true);
	if (auto* wallHug = registry.TryGet<WallHug>(entity))
	{
		wallHug->goal = glm::vec2(at.x, at.z);
	}
	registry.Remove<MoveStateLinearTag, MoveStateOrbitTag, MoveStateExitCircleTag, MoveStateStepThroughTag,
	                MoveStateFinalStepTag, MoveStateArrivedTag, WallHugObjectReference>(entity);
}

// A script state takes a villager out of its own life: it stays where it is, playing what the script gave it
// (Land 1's lost brother: SCRIPT_PLAY_ANIM, 200, lying sick with PDying looped) until RELEASE_FROM_SCRIPT.
void HoldInScriptState(uint32_t object, int32_t state)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = static_cast<entt::entity>(object);
	if (object == 0 || !registry.Valid(entity) || !registry.AllOf<Transform>(entity))
	{
		return;
	}
	g_walks.erase(object);
	g_moveAnimations.erase(object);
	g_moverSpeeds.erase(object);
	if (g_scriptMoves.erase(object) != 0)
	{
		SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "MOVE_GAME_THING {} cut short by SET_SCRIPT_STATE", object);
	}
	g_followers.erase(object);
	if (registry.AllOf<WallHug, LivingAction>(entity))
	{
		HoldForScript(entity, registry.Get<Transform>(entity).position);
	}
	SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "SET_SCRIPT_STATE {} {}", object, state);
}

// Walk (or place) a game thing at a position. Returns false if the move was refused.
bool MoveThingTo(uint32_t objId, const glm::vec3& position)
{
	if (objId == g_heldObject && objId != 0)
	{
		SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "MOVE_GAME_THING {}: in the hand, not moved", objId);
		return false; // in the hand: it goes where the hand takes it
	}
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = static_cast<entt::entity>(objId);
	if (objId == 0 || !registry.Valid(entity) || !registry.AllOf<Transform>(entity))
	{
		SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "MOVE_GAME_THING {}: {}, not moved", objId,
		                   objId == 0 ? "no object" : !registry.Valid(entity) ? "no longer exists" : "has no position");
		return false;
	}
	for (const auto& [holder, held] : g_creatureHolding)
	{
		if (held == objId)
		{
			SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "MOVE_GAME_THING {}: held by creature {}", objId, holder);
		}
	}
	g_walks.erase(objId); // a move replaces any track walk, following or earlier move
	g_followers.erase(objId);
	g_scriptMoves.erase(objId);

	auto* wallHug = registry.TryGet<WallHug>(entity);
	auto* action = registry.TryGet<LivingAction>(entity);
	// The pathfinder indexes its grid without bounds checks: only walk to targets on the map.
	constexpr float k_MapMin = 30.0f;
	constexpr float k_MapMax = 5090.0f;
	const auto onMap = [&](const glm::vec3& p) {
		return p.x >= k_MapMin && p.x <= k_MapMax && p.z >= k_MapMin && p.z <= k_MapMax;
	};
	const auto& from = registry.Get<Transform>(entity).position;
	if (!onMap(position) || !onMap(from))
	{
		// Usually a target measured from an object CREATE doesn't make yet (its position reads as 0, 0).
		SPDLOG_LOGGER_WARN(spdlog::get("scripting"), "MOVE_GAME_THING {} from ({:.0f}, {:.0f}) to ({:.0f}, {:.0f}) is off the map: ignored",
		                   objId, from.x, from.z, position.x, position.z);
		return false;
	}
	if (wallHug == nullptr || action == nullptr)
	{
		registry.Get<Transform>(entity).position = position; // nothing walks it: place it
		SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "MOVE_GAME_THING {}: placed at ({:.1f}, {:.1f}, {:.1f}) (nothing walks it)", objId,
		                   position.x, position.y, position.z);
		return true;
	}

	// Not the pathfinder: its steps went a jump per game turn and faster than the villagers' own run.
	HoldForScript(entity, from);
	Locator::livingActionSystem::value().VillagerSetState(*action, LivingAction::Index::Top, VillagerStates::MoveToPos, true);
	g_scriptMoves[objId] = position;
	// Its own moving state from now (the original's MOVE_GAME_THING puts a villager in one, which plays its walk or
	// run): a script animation it was given before ends. The opening's father beckons (PBeckon, twice) and then runs to
	// the beach - and ran there in the beckon, his feet still (Mike, 2026-10-22).
	g_scriptAnimations.erase(objId);
	SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "MOVE_GAME_THING {} from ({:.1f}, {:.1f}, {:.1f}) to ({:.1f}, {:.1f}, {:.1f})", objId,
	                   from.x, from.y, from.z, position.x, position.y, position.z);
	return true;
}

// A flock moves as a group: its members (not the leader, who is moved on his own) gather round the
// position. Scripts that keep a flock with its leader re-send this every turn; the members then close in
// smoothly every frame (UpdateScriptFrame) rather than being re-routed through the pathfinder each turn.
void MoveFlockTo(uint32_t flockId, Flock& flock, const glm::vec3& position)
{
	auto& registry = Locator::entitiesRegistry::value();
	if (auto* transform = registry.TryGet<Transform>(static_cast<entt::entity>(flockId)))
	{
		transform->position = position;
	}
	constexpr float k_Spacing = 3.0f;
	size_t index = 0;
	for (const auto member : flock.members)
	{
		const auto entity = static_cast<entt::entity>(member);
		if (member == flock.leader || !registry.Valid(entity) || !registry.AllOf<Transform>(entity))
		{
			continue;
		}
		const float angle = glm::two_pi<float>() * static_cast<float>(index++) / 5.0f + 2.0f;
		const auto target = position + glm::vec3(std::cos(angle), 0.0f, std::sin(angle)) * k_Spacing;
		if (g_followers.find(member) == g_followers.end())
		{
			g_walks.erase(member);
			HoldForScript(entity, registry.Get<Transform>(entity).position);
		}
		g_followers[member] = target;
	}
}
} // namespace

void MoveGameThing() // 033 MOVE_GAME_THING
{
	[[maybe_unused]] const auto radius = Popf();
	const auto position = PopVec();
	const auto objId = Pop().uintVal;
	if (auto flock = g_flocks.find(objId); flock != g_flocks.end())
	{
		MoveFlockTo(objId, flock->second, position);
		return;
	}
	MoveThingTo(objId, position);
}

void SetFocus() // 034 SET_FOCUS
{
	const auto position = PopVec();
	const auto object = Pop().uintVal;
	if (object != 0)
	{
		auto& registry = Locator::entitiesRegistry::value();
		if (auto* transform = registry.TryGet<Transform>(static_cast<entt::entity>(object)))
		{
			FaceTowards(*transform, position);
			SPDLOG_LOGGER_DEBUG(spdlog::get("scripting"), "SET_FOCUS {} at ({:.1f}, {:.1f}) towards ({:.1f}, {:.1f})", object, transform->position.x,
			                    transform->position.z, position.x, position.z);
		}
	}
}

void HasCameraArrived() // 035 HAS_CAMERA_ARRIVED
{
	// With no script driving the camera (the Temple or the creature close-up gave it to the player), nothing is moving
	// it: arrived. Otherwise the player's camera restarted the move every frame and a waiting lesson waited for ever.
	const auto& camera = Locator::camera::value();
	Pushb(!CameraPathRunning() && (!camera.IsScriptControlled() || camera.HasScriptMoveArrived()));
}

void FlockCreate() // 036 FLOCK_CREATE
{
	const auto position = PopVec();
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = registry.Create();
	registry.Assign<Transform>(entity, position, glm::mat3(1.0f), glm::vec3(1.0f));
	g_flocks[static_cast<uint32_t>(entity)] = {};
	g_scriptEntities[static_cast<uint32_t>(entity)] = {static_cast<int32_t>(ObjectType::Marker), 0, 0}; // a flock is a point
	Pusho(static_cast<uint32_t>(entity));
}

void FlockAttach() // 037 FLOCK_ATTACH
{
	const auto asLeader = static_cast<bool>(Pop().intVal);
	const auto flockId = Pop().uintVal;
	const auto obj = Pop().uintVal;
	// A town takes people in the same way (Land 1's PopulateTown adds to the Aztec town while ID_SIZE of it is 15 or
	// less): with no count for a town, it never stopped and the town filled with hundreds.
	{
		auto& registry = Locator::entitiesRegistry::value();
		const auto town = static_cast<entt::entity>(flockId);
		if (!g_flocks.contains(flockId) && flockId != 0 && registry.Valid(town) &&
		    registry.AllOf<openblack::ecs::components::Town>(town))
		{
			g_flocks[flockId] = Flock {};
		}
	}
	if (auto flock = g_flocks.find(flockId); flock != g_flocks.end() && obj != 0)
	{
		auto& members = flock->second.members;
		if (std::find(members.begin(), members.end(), obj) == members.end())
		{
			members.push_back(obj);
		}
		if (asLeader)
		{
			flock->second.leader = obj;
		}
	}
	Pusho(obj);
}

void FlockDetach() // 038 FLOCK_DETACH
{
	const auto flockId = Pop().uintVal;
	const auto obj = Pop().uintVal;
	if (auto flock = g_flocks.find(flockId); flock != g_flocks.end())
	{
		auto& members = flock->second.members;
		members.erase(std::remove(members.begin(), members.end(), obj), members.end());
		if (flock->second.leader == obj)
		{
			flock->second.leader = 0;
		}
	}
	Pusho(obj);
}

void FlockDisband() // 039 FLOCK_DISBAND
{
	g_flocks.erase(Pop().uintVal);
}

void IdSize() // 040 ID_SIZE
{
	const auto container = Pop().uintVal;
	const auto flock = g_flocks.find(container);
	Pushf(flock != g_flocks.end() ? static_cast<float>(flock->second.members.size()) : 0.0f);
}

void FlockMember() // 041 FLOCK_MEMBER
{
	const auto flockId = Pop().uintVal;
	const auto obj = Pop().uintVal;
	const auto flock = g_flocks.find(flockId);
	Pushb(flock != g_flocks.end() &&
	      std::find(flock->second.members.begin(), flock->second.members.end(), obj) != flock->second.members.end());
}

void GetHandPosition() // 042 GET_HAND_POSITION
{
	const auto handEntity = Locator::handSystem::value().GetPlayerHands()[static_cast<size_t>(HandSystemInterface::Side::Left)];
	auto& handTransform = Locator::entitiesRegistry::value().Get<Transform>(handEntity);

	PushVec(handTransform.position);
}

// --- Sound ---------------------------------------------------------------------------------------------------
// The game's sound packs on disk, by their file name in lower case: the game's own lists name them in lower case,
// the files aren't (InGame.sad, Scriptsfx.sad).
const std::unordered_map<std::string, std::filesystem::path>& AudioPacks()
{
	static const auto packs = [] {
		std::unordered_map<std::string, std::filesystem::path> found;
		auto& fileSystem = Locator::filesystem::value();
		fileSystem.Iterate(fileSystem.GetPath<openblack::filesystem::Path::Audio>(), true,
		                   [&found](const std::filesystem::path& f) {
			                   auto name = f.filename().string();
			                   std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
			                   found.emplace(name, f);
		                   });
		return found;
	}();
	return packs;
}

// PLAY_SOUND_EFFECT's banks, numbered from 1 as runblack.exe's list of them (at 0x9cb3f8, slot 0 empty): Land 1's
// scripts play bank 1 sound 169 (G_CreatureSwap_01) at the creature choice, bank 5 sound 57
// (CreatureGateStoneMove_01) at the creature gate.
constexpr std::array<const char*, 11> k_SoundBanks = {nullptr, "ingame.sad", "editor.sad", "spells.sad", "creature.sad",
                                                      "scriptsfx.sad", "helpsprites.sad", "villagers.sad",
                                                      "villagersbanter.sad", "spelldialogue.sad", "guidance.sad"};

// START_MUSIC's music types, from 1 (MUSIC_TYPE_GENERIC_EVIL) as runblack.exe's music table (at 0x9c974c, after
// MUSIC_TYPE_NONE): 44-46 the citadel, 55 the singing stone circle, 65 the creature chosen.
constexpr std::array<const char*, 84> k_MusicPacks = {
	"evil.sad",
	"neutral.sad",
	"good.sad",
	"celt_evil.sad",
	"celt_neutral.sad",
	"celt_good.sad",
	"aztc_evil.sad",
	"aztc_neutral.sad",
	"aztc_good.sad",
	"japn_evil.sad",
	"japn_neutral.sad",
	"japn_good.sad",
	"indn_evil.sad",
	"indn_neutral.sad",
	"indn_good.sad",
	"egpt_evil.sad",
	"egpt_neutral.sad",
	"egpt_good.sad",
	"grek_evil.sad",
	"grek_neutral.sad",
	"grek_good.sad",
	"celt_evil.sad",
	"celt_neutral.sad",
	"celt_good.sad",
	"tbtn_evil.sad",
	"tbtn_neutral.sad",
	"tbtn_good.sad",
	"celt_chant.sad",
	"celt_chant_vox.sad",
	"aztc_chant.sad",
	"aztc_chant_vox.sad",
	"japn_chant.sad",
	"japn_chant_vox.sad",
	"indn_chant.sad",
	"indn_chant_vox.sad",
	"egpt_chant.sad",
	"egpt_chant_vox.sad",
	"grek_chant.sad",
	"grek_chant_vox.sad",
	"nrse_chant.sad",
	"nrse_chant_vox.sad",
	"tbtn_chant.sad",
	"tbtn_chant_vox.sad",
	"citadel.sad",
	"citadel.sad",
	"citadel.sad",
	"pipertune_m.sad",
	"pipercave_m.sad",
	"hermit.sad",
	"missionariesbackground.sad",
	"missionariesverse1.sad",
	"missionariesverse2.sad",
	"missionariesverse3.sad",
	"intro.sad",
	"singingstonesa.sad",
	"followuswelcome.sad",
	"script01.sad",
	"script02.sad",
	"script03.sad",
	"script04.sad",
	"epic01.sad",
	"epic02.sad",
	"epic03.sad",
	"epic04.sad",
	"creaturechosen.sad",
	"funeral.sad",
	"creatureguide.sad",
	"khazar.sad",
	"nemesis.sad",
	"twinkle.sad",
	"whistlefuneral.sad",
	"whistletwinkle.sad",
	"sleg.sad",
	"creaturefight.sad",
	"creaturebigfight.sad",
	"guardianstone.sad",
	"outro.sad",
	"failure.sad",
	"gregorian.sad",
	"christmas.sad",
	"gregorian3d.sad",
	"circus.sad",
	"circus3d.sad",
	"creatureendsequence.sad"};
constexpr int k_LastLoopingMusic = 46; // the town, chant and citadel themes loop; the story pieces play once

#include "TextSamples.inc"

// Plays sample `sample` of sound bank `bank` (k_SoundBanks' numbering), placed in the world if `position` is given;
// the sound's emitter, or null if there is no such sound.
entt::entity PlayBankSound(int32_t bank, int32_t sample, const glm::vec3* position,
                           openblack::audio::PlayType playType = openblack::audio::PlayType::Once)
{
	static std::set<std::pair<int32_t, int32_t>> missing;
	const auto& packs = AudioPacks();
	const auto pack = bank > 0 && static_cast<size_t>(bank) < k_SoundBanks.size() ? packs.find(k_SoundBanks.at(static_cast<size_t>(bank)))
	                                                                              : packs.end();
	const auto id = pack != packs.end()
	                    ? entt::hashed_string(fmt::format("{}/{}", pack->second.filename().string(), sample).c_str()).value()
	                    : entt::id_type {0};
	if (id == 0 || !Locator::resources::value().GetSounds().Contains(id))
	{
		if (missing.emplace(bank, sample).second)
		{
			SPDLOG_LOGGER_WARN(spdlog::get("audio"), "No sound {} in bank {}", sample, bank);
		}
		return entt::null;
	}
	auto& audio = Locator::audio::value();
	// An animal's call (Land 1's SheepBaa plays its sheep's baa at the sheep) is half as loud and carries half as far
	// as other sounds in the world (Mike, 2026-10-17: twice too loud, heard from too far).
	bool animal = false;
	if (position != nullptr)
	{
		Locator::entitiesRegistry::value().Each<const openblack::ecs::components::Animal, const Transform>(
		    [&](const openblack::ecs::components::Animal&, const Transform& transform) {
			    animal = animal || glm::distance(transform.position, *position) < 2.0f;
		    });
	}
	const auto emitter = audio.CreateEmitter(id, playType, position != nullptr ? *position : glm::vec3(0.0f),
	                                         glm::vec3(0.0f), animal ? glm::vec2(10.0f, 100.0f) : glm::vec2(20.0f, 200.0f),
	                                         animal ? 0.5f : 1.0f, openblack::audio::AudioStatus::Playing, position == nullptr);
	if (emitter != entt::null)
	{
		if (position != nullptr)
		{
			Locator::entitiesRegistry::value().Get<Transform>(emitter).position = *position;
		}
		audio.PlayEmitter(emitter);
	}
	return emitter;
}

namespace
{
// The voice of a RUN_TEXT line (runblack.exe's text table, TextSamples.inc), or null if it has none.
entt::entity PlayTextVoice(int32_t text)
{
	if (text < 0 || static_cast<size_t>(text) >= k_TextSamples.size() || k_TextSamples.at(static_cast<size_t>(text)).first == 0)
	{
		return entt::null;
	}
	const auto& [bank, sample] = k_TextSamples.at(static_cast<size_t>(text));
	return PlayBankSound(bank, sample, nullptr);
}
} // namespace

void PlaySoundEffect() // 043 PLAY_SOUND_EFFECT
{
	const auto withPosition = static_cast<bool>(Pop().intVal);
	const auto position = PopVec();
	const auto soundbank = Pop().intVal;
	const auto sound = Pop().intVal;
	// Sounds are loaded as "<pack file>/<sample id>" (Game.cpp); played where the script says, or everywhere.
	PlayBankSound(soundbank, sound, withPosition ? &position : nullptr);
}

// The music playing (START_MUSIC): when it started and how long it lasts.
std::chrono::steady_clock::time_point g_musicStart;
float g_musicSeconds = 0.0f;
int g_musicNumber = 0;

void StartMusic() // 044 START_MUSIC
{
	const auto music = Pop().intVal;
	const auto& packs = AudioPacks();
	const auto pack = music >= 1 && static_cast<size_t>(music) <= k_MusicPacks.size()
	                      ? packs.find(k_MusicPacks.at(static_cast<size_t>(music - 1)))
	                      : packs.end();
	if (pack == packs.end())
	{
		SPDLOG_LOGGER_WARN(spdlog::get("audio"), "START_MUSIC {}: no such music here", music);
		return;
	}
	// How long it plays, for LAST_MUSIC_LINE: the pack is one MPEG audio stream cut into segments; its length is its
	// size over the bitrate in its first frame's header.
	g_musicStart = std::chrono::steady_clock::now();
	g_musicSeconds = 0.0f;
	g_musicNumber = music;
	try
	{
		std::ifstream file(pack->second, std::ios::binary);
		const std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		const auto block = bytes.find("LHAudioWaveData");
		if (block != std::string::npos && block + 40 <= bytes.size())
		{
			uint32_t size;
			std::memcpy(&size, bytes.data() + block + 32, sizeof(size));
			const auto* frame = reinterpret_cast<const uint8_t*>(bytes.data() + block + 36);
			const bool mpeg1 = (frame[1] & 0x08) != 0;
			const int bitrateIndex = frame[2] >> 4;
			constexpr std::array<int, 15> k_Mpeg1Layer2 = {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384};
			constexpr std::array<int, 15> k_Mpeg2 = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160};
			const int kbps = bitrateIndex < 15 ? (mpeg1 ? k_Mpeg1Layer2 : k_Mpeg2).at(static_cast<size_t>(bitrateIndex)) : 0;
			if (frame[0] == 0xff && kbps > 0)
			{
				g_musicSeconds = static_cast<float>(size) * 8.0f / (static_cast<float>(kbps) * 1000.0f);
			}
		}
	}
	catch (const std::exception&)
	{
	}
	SPDLOG_LOGGER_INFO(spdlog::get("audio"), "START_MUSIC {}: {} ({:.1f} s)", music, pack->second.filename().string(), g_musicSeconds);
	Locator::audio::value().PlayMusic(pack->second.string(), music <= k_LastLoopingMusic ? openblack::audio::PlayType::Repeat
	                                                                                      : openblack::audio::PlayType::Once);
}

void StopMusic() // 045 STOP_MUSIC
{
	Locator::audio::value().StopMusic();
}

void AttachMusic() // 046 ATTACH_MUSIC
{
	[[maybe_unused]] const auto target = Pop().uintVal;
	[[maybe_unused]] const auto music = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void DetachMusic() // 047 DETACH_MUSIC
{
	[[maybe_unused]] const auto object = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void ForgetObject(uint32_t object); // below, with the hand
namespace
{
bool SetIntoPedestal(uint32_t object); // below, with the pedestal
} // namespace

void ObjectDelete() // 048 OBJECT_DELETE
{
	[[maybe_unused]] const auto withFade = Pop().intVal;
	const auto obj = Pop().uintVal;
	// Land 1's ProtectGateKey scripts delete a gate totem and make it again at its start: with no delete, they
	// piled up thousands of them.
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = static_cast<entt::entity>(obj);
	if (obj == 0 || !registry.Valid(entity))
	{
		return;
	}
	SetIntoPedestal(obj); // a gate totem in place is kept on the pedestal
	// Wood a script takes up beside the Ark (Land 1's Missionaries: amount / 1500 of it, as their ArkIncrement) builds it.
	if (const auto* pot = registry.TryGet<openblack::ecs::components::Pot>(entity);
	    pot != nullptr && registry.AllOf<Transform>(entity) &&
	    Locator::infoConstants::value().pot.at(static_cast<size_t>(pot->type)).resourceType == ResourceType::Wood)
	{
		const auto at = registry.Get<Transform>(entity).position;
		registry.Each<const openblack::ecs::components::Feature, openblack::ecs::components::Construction, const Transform>(
		    [&](entt::entity ark, const openblack::ecs::components::Feature&, openblack::ecs::components::Construction& construction,
		        const Transform& where) {
			    if (glm::distance(glm::vec2(where.position.x, where.position.z), glm::vec2(at.x, at.z)) < 40.0f)
			    {
				    construction.woodStock = std::min(construction.woodStock + static_cast<float>(pot->amount) / 1500.0f, 1.0f);
				    SPDLOG_LOGGER_INFO(spdlog::get("game"), "Ark {}: {} wood taken, to be built to {:.0f}%", static_cast<uint32_t>(ark),
				                       pot->amount, construction.woodStock * 100.0f);
			    }
		    });
	}
	ForgetObject(obj);
	registry.Destroy(entity);
}

void FocusFollow() // 049 FOCUS_FOLLOW
{
	const auto target = Pop().uintVal;
	CameraFollowFocus(target);
}

void PositionFollow() // 050 POSITION_FOLLOW
{
	const auto target = Pop().uintVal;
	CameraFollowPosition(target, std::nullopt, false);
}

void CallNear() // 051 CALL_NEAR
{
	const auto excludingScripted = static_cast<bool>(Pop().intVal);
	const auto radius = Popf();
	const auto position = PopVec();
	const auto subtype = Pop().intVal;
	const auto type = Pop().intVal;
	const auto scripted = excludingScripted ? ScriptedObjects() : std::unordered_set<uint32_t> {};
	g_searchExcludes = excludingScripted ? &scripted : nullptr;
	const auto object = FindNearestScriptObject(type, subtype, position, radius);
	g_searchExcludes = nullptr;
	if (type == static_cast<int>(ObjectType::Rock) || type == static_cast<int>(ObjectType::MobileStatic))
	{
		auto& registry = Locator::entitiesRegistry::value();
		const auto* at = object != entt::null ? registry.TryGet<Transform>(object) : nullptr;
		SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "CALL_NEAR {} {} within {:.0f} of ({:.2f}, {:.2f}, {:.2f}): {}{}",
		                   type == static_cast<int>(ObjectType::Rock) ? "rock" : "mobile static", subtype, radius, position.x,
		                   position.y, position.z, object == entt::null ? 0u : static_cast<uint32_t>(object),
		                   at != nullptr ? fmt::format(" at ({:.2f}, {:.2f}, {:.2f})", at->position.x, at->position.y, at->position.z)
		                                 : std::string());
	}
	if ((type == static_cast<int>(ObjectType::Villager) && excludingScripted) || type == static_cast<int>(ObjectType::VillagerChild))
	{
		const auto found = object == entt::null ? 0u : static_cast<uint32_t>(object);
		const auto* villager = found != 0 ? Locator::entitiesRegistry::value().TryGet<ecs::components::Villager>(object) : nullptr;
		SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "CALL_NEAR {} {} within {:.0f} of ({:.0f}, {:.0f}){}: {}{}",
		                   type == static_cast<int>(ObjectType::VillagerChild) ? "child" : "villager", subtype, radius, position.x, position.z, excludingScripted ? ", not scripted" : "", found,
		                   villager != nullptr ? fmt::format(" (type {})", static_cast<int>(villager->number)) : "");
	}
	if (type == static_cast<int>(ObjectType::Animal))
	{
		// What the lost flock's search finds, and whether that one is in a flock already: it takes only the nearest.
		const auto found = object == entt::null ? 0u : static_cast<uint32_t>(object);
		int flock = 0;
		for (const auto& [id, members] : g_flocks)
		{
			if (std::find(members.members.begin(), members.members.end(), found) != members.members.end())
			{
				flock = static_cast<int>(id);
			}
		}
		SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "CALL_NEAR animal {} within {:.0f} of ({:.0f}, {:.0f}): {}{}", subtype, radius,
		                   position.x, position.z, found, flock != 0 ? fmt::format(" (already in flock {})", flock) : "");
	}
	Pusho(object == entt::null ? 0 : static_cast<uint32_t>(object));
}

void SpecialEffectPosition() // 052 SPECIAL_EFFECT_POSITION
{
	[[maybe_unused]] const auto duration = Popf();
	[[maybe_unused]] const auto position = PopVec();
	[[maybe_unused]] const auto effect = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pusho(0);
}

void SpecialEffectObject() // 053 SPECIAL_EFFECT_OBJECT
{
	[[maybe_unused]] const auto duration = Popf();
	const auto target = Pop().uintVal;
	const auto effect = Pop().intVal;
	// Only the glitter Land 1 puts on the gate totems (34) so far. The effect is an object of its own: scripts
	// delete it to stop it, which must not delete what it is on.
	auto& registry = Locator::entitiesRegistry::value();
	if (effect != 34 || target == 0 || !registry.Valid(static_cast<entt::entity>(target)))
	{
		Pusho(0);
		return;
	}
	const auto handle = static_cast<uint32_t>(registry.Create());
	g_sparklers[handle] = {target, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), {}, {}};
	Pusho(handle);
}

void DanceCreate() // 054 DANCE_CREATE
{
	const auto duration = Popf();
	const auto position = PopVec();
	const auto type = Pop().intVal; // DanceInfo: the welcome crowd's is 14, NewDanceAroundPerson
	const auto obj = Pop().uintVal; // who it is danced around (or for)
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = registry.Create();
	registry.Assign<Transform>(entity, position, glm::mat3(1.0f), glm::vec3(1.0f));
	const auto id = static_cast<uint32_t>(entity);
	g_flocks[id] = {};
	g_scriptEntities[id] = {static_cast<int32_t>(ObjectType::Marker), 0, 0}; // a point, as a flock
	auto& made = g_dances[id];
	made = {position, duration > 0.0f ? duration : 30.0f};
	// Its choreography, by info.dat's file name for the type; read once.
	static std::unordered_map<std::string, std::shared_ptr<const dance::Choreography>> s_loaded;
	const auto& infos = Locator::infoConstants::value().dance;
	std::string fileName;
	if (type >= 0 && static_cast<size_t>(type) < infos.size())
	{
		fileName = std::string(infos.at(static_cast<size_t>(type)).fileName.data());
	}
	if (!fileName.empty())
	{
		auto [it, fresh] = s_loaded.try_emplace(fileName);
		if (fresh)
		{
			if (auto loaded = dance::Load(fileName))
			{
				it->second = std::make_shared<const dance::Choreography>(std::move(*loaded));
			}
		}
		made.choreography = it->second;
	}
	made.groups.resize(made.choreography != nullptr ? made.choreography->numGroups : 1);
	SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "DANCE_CREATE {}: type {} ({}) round {} at ({:.1f}, {:.1f}, {:.1f}) for {:.0f} s", id, type,
	                   fileName.empty() ? "no file" : fileName, obj, position.x, position.y, position.z, made.secondsLeft);
	Pusho(id);
}

void CallIn() // 055 CALL_IN
{
	const auto excludingScripted = static_cast<bool>(Pop().intVal);
	const auto container = Pop().uintVal;
	const auto subtype = Pop().intVal;
	const auto type = Pop().intVal;
	// The first of `type` in a flock or a town: Land 1's lost flock gets its shepherd this way ("get VILLAGER in
	// ShepherdFlock"). Always 0 before, and the five-sheep reward waited for ever for "nobody" to reach the soapbox.
	auto& registry = Locator::entitiesRegistry::value();
	const auto scripted = excludingScripted ? ScriptedObjects() : std::unordered_set<uint32_t> {};
	auto matches = [&](uint32_t id) {
		const auto entity = static_cast<entt::entity>(id);
		if (id == 0 || !registry.Valid(entity) || scripted.contains(id))
		{
			return false;
		}
		switch (static_cast<ObjectType>(type))
		{
		case ObjectType::Villager:
		{
			const auto* villager = registry.TryGet<ecs::components::Villager>(entity);
			return villager != nullptr && SubtypeMatches(subtype, static_cast<int>(villager->number));
		}
		case ObjectType::Animal:
		{
			const auto* animal = registry.TryGet<ecs::components::Animal>(entity);
			return animal != nullptr && SubtypeMatches(subtype, static_cast<int>(animal->type));
		}
		default:
		{
			const auto record = g_scriptEntities.find(id);
			return record != g_scriptEntities.end() && record->second.type == type && SubtypeMatches(subtype, static_cast<int>(record->second.subtype));
		}
		}
	};
	uint32_t found = 0;
	if (const auto flock = g_flocks.find(container); flock != g_flocks.end())
	{
		if (matches(flock->second.leader))
		{
			found = flock->second.leader;
		}
		for (const auto member : flock->second.members)
		{
			if (found == 0 && matches(member))
			{
				found = member;
			}
		}
	}
	else if (container != 0 && registry.Valid(static_cast<entt::entity>(container)) &&
	         registry.AllOf<ecs::components::Town>(static_cast<entt::entity>(container)))
	{
		registry.Each<const ecs::components::Villager>([&](entt::entity entity, const ecs::components::Villager& villager) {
			if (found == 0 && villager.town == static_cast<entt::entity>(container) && matches(static_cast<uint32_t>(entity)))
			{
				found = static_cast<uint32_t>(entity);
			}
		});
	}
	SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "CALL_IN type {} subtype {} in {}{}: {}", type, subtype, container,
	                   excludingScripted ? ", not scripted" : "", found);
	Pusho(found);
}

void ChangeInnerOuterProperties() // 056 CHANGE_INNER_OUTER_PROPERTIES
{
	[[maybe_unused]] const auto calm = Popf();
	[[maybe_unused]] const auto outer = Popf();
	[[maybe_unused]] const auto inner = Popf();
	[[maybe_unused]] const auto obj = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void Snapshot() // 057 SNAPSHOT
{
	[[maybe_unused]] const auto challengeId = Pop().intVal;
	[[maybe_unused]] const auto argc = Pop().intVal;
	[[maybe_unused]] const auto argv = PopVarArg(argc);
	[[maybe_unused]] const auto reminderScript = PopString();
	[[maybe_unused]] const auto titleStrID = Pop().intVal;
	[[maybe_unused]] const auto alignment = Popf();
	[[maybe_unused]] const auto success = Popf();
	[[maybe_unused]] const auto focus = PopVec();
	[[maybe_unused]] const auto position = PopVec();
	[[maybe_unused]] const auto quest = static_cast<bool>(Pop().intVal);
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void GetAlignment() // 058 GET_ALIGNMENT
{
	[[maybe_unused]] const auto zero = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushf(0.0f);
}

void SetAlignment() // 059 SET_ALIGNMENT
{
	[[maybe_unused]] const auto unk1 = Pop().intVal;
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void InfluenceObject() // 060 INFLUENCE_OBJECT
{
	[[maybe_unused]] const auto anti = Pop().intVal;
	[[maybe_unused]] const auto zero = Pop().intVal;
	[[maybe_unused]] const auto radius = Popf();
	[[maybe_unused]] const auto target = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pusho(0);
}

void InfluencePosition() // 061 INFLUENCE_POSITION
{
	[[maybe_unused]] const auto anti = Pop().intVal;
	[[maybe_unused]] const auto zero = Pop().intVal;
	[[maybe_unused]] const auto radius = Popf();
	[[maybe_unused]] const auto position = PopVec();
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pusho(0);
}

void GetInfluence() // 062 GET_INFLUENCE
{
	[[maybe_unused]] const auto position = PopVec();
	[[maybe_unused]] const auto raw = static_cast<bool>(Pop().intVal);
	[[maybe_unused]] const auto player = Popf();
	// No influence system yet, and the hand reaches everywhere: everywhere is inside the player's influence (1).
	// Land 1's ProtectGateKey and ProtectQuarryRock scripts delete a gate totem or the quarry rock set down outside
	// it and make it again at its start: the original's hand reaches the quarry, the sculptor and the pedestal, and
	// any radius short of that sent the stones back from them.
	Pushf(1.0f);
}

void SetInterfaceInteraction() // 063 SET_INTERFACE_INTERACTION
{
	[[maybe_unused]] const auto level = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void Played() // 064 PLAYED
{
	const auto obj = Pop().uintVal;
	const bool played = g_scriptAnimations.find(obj) == g_scriptAnimations.end();
	Pushb(played);
}

void RandomUlong() // 065 RANDOM_ULONG
{
	const auto max = Pop().intVal;
	const auto min = Pop().intVal;
	// Inclusive, like RANDOM's range.
	Pushi(max <= min ? min : min + rand() % (max - min + 1));
}

void SetGamespeed() // 066 SET_GAMESPEED
{
	[[maybe_unused]] const auto speed = Popf();
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void CallInNear() // 067 CALL_IN_NEAR
{
	[[maybe_unused]] const auto excludingScripted = static_cast<bool>(Pop().intVal);
	[[maybe_unused]] const auto radius = Popf();
	[[maybe_unused]] const auto pos = PopVec();
	[[maybe_unused]] const auto container = Pop().uintVal;
	[[maybe_unused]] const auto subtype = Pop().intVal;
	[[maybe_unused]] const auto type = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pusho(0);
}

void OverrideStateAnimation() // 068 OVERRIDE_STATE_ANIMATION
{
	const auto animType = Pop().intVal;
	const auto obj = Pop().uintVal;
	// The animation its state plays, in the animations' own numbering (as SET_SCRIPT_ULONG's): FollowUs gives the son
	// 379 PSkip02 and 351 PRunMan for his moves. Played while a script moves it, by the ground it covers.
	if (obj != 0 && animType >= 0)
	{
		g_moveAnimations[obj] = static_cast<uint32_t>(animType);
	}
}

namespace
{
float CreatureHeight(entt::entity entity); // below, with the fights
} // namespace

// Stands `made` at `scale` times the height of `creature`.
void SizeCreatureAs(entt::entity made, uint32_t creature, float scale)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto reference = static_cast<entt::entity>(creature);
	if (!registry.Valid(made) || !registry.Valid(reference) || !registry.AllOf<Transform>(made) || !registry.AllOf<Transform>(reference))
	{
		return;
	}
	const float want = CreatureHeight(reference) * scale;
	const float now = CreatureHeight(made);
	if (want > 0.0f && now > 0.0f)
	{
		auto& transform = registry.Get<Transform>(made);
		transform.scale *= want / now;
		g_creatureBaseScale[static_cast<uint32_t>(made)] = transform.scale.x;
	}
}

void CreatureCreateRelativeToCreature() // 069 CREATURE_CREATE_RELATIVE_TO_CREATURE
{
	const auto type = Pop().intVal;
	const auto position = PopVec();
	const auto scale = Popf();
	const auto creature = Pop().uintVal;
	// A creature of the given kind, `scale` times the size of `creature`: Land 1's CreatureGuardian makes its ogre
	// (13) by the lair this way, the same size as yours. Unmade, the quest waited on nothing.
	const auto made = CreateScriptObject(ObjectType::Creature, static_cast<uint32_t>(type), position, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
	if (made == entt::null || made == static_cast<entt::entity>(0))
	{
		SPDLOG_LOGGER_WARN(spdlog::get("scripting"), "CREATURE_CREATE_RELATIVE_TO_CREATURE: couldn't make creature type {}", type);
		Pusho(0);
		return;
	}
	SizeCreatureAs(made, creature, scale);
	SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "CREATURE_CREATE_RELATIVE_TO_CREATURE: creature {} (type {}) at ({:.0f}, {:.0f}), {:.1f} x the height of {}",
	                   static_cast<uint32_t>(made), type, position.x, position.z, scale, creature);
	Pusho(static_cast<uint32_t>(made));
}

void CreatureLearnEverything() // 070 CREATURE_LEARN_EVERYTHING
{
	[[maybe_unused]] const auto creature = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void CreatureSetKnowsAction() // 071 CREATURE_SET_KNOWS_ACTION
{
	[[maybe_unused]] const auto knows = Pop().intVal;
	[[maybe_unused]] const auto action = Pop().intVal;
	[[maybe_unused]] const auto typeOfAction = Pop().intVal;
	[[maybe_unused]] const auto creature = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void CreatureSetAgendaPriority() // 072 CREATURE_SET_AGENDA_PRIORITY
{
	[[maybe_unused]] const auto priority = Popf();
	[[maybe_unused]] const auto creature = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void CreatureTurnOffAllDesires() // 073 CREATURE_TURN_OFF_ALL_DESIRES
{
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void CreatureLearnDistinctionAboutActivityObject() // 074 CREATURE_LEARN_DISTINCTION_ABOUT_ACTIVITY_OBJECT
{
	[[maybe_unused]] const auto unk3 = Pop().intVal;
	[[maybe_unused]] const auto unk2 = Pop().intVal;
	[[maybe_unused]] const auto unk1 = Pop().intVal;
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

// --- Creature fights -------------------------------------------------------------------------------------------
// Two creatures in a ring (CREATURE_DO_ACTION 21 at another creature): each works through a queue of moves, one every
// k_FightBeat - an attack at the high, middle or low of the other, or a block of one of those. An attack on what is
// being blocked is blocked; otherwise it hits, takes fight health (script property 33, which Land 1's lesson keeps at
// 0.5 or more while it teaches) and makes the other recoil. The player queues moves by tapping (ZR) the opponent (an
// attack where tapped) or their own creature (a block there). A creature fighting on its own picks its moves.
// Numbers the scripts read: GET_CREATURE_FIGHT_ACTION 0 stance, 1 block, 2 step (the lesson waits for 1 after teaching
// blocks, 2 after asking for a tap on the ground), 4 attack, 6/7/8 recoiling from a high, middle or low hit (it waits for
// 6-8 on the guide); SET_CREATURE_QUEUE_FIGHT_MOVE n is the nth of
// ctrspec27.txt's attacks (1 high, 2 eye, 3 belly, 4 pelvis, 5 knee, 6 special). All of it is from the lesson and
// the animation list - the original's own rules are not found yet.
namespace
{
namespace CreatureFight
{
constexpr int32_t FightWith = 21;
constexpr int Stance = 0;
constexpr int Block = 1; // the original's 1: the lesson waits for it after teaching blocks
constexpr int Step = 2;  // the original's 2: the lesson waits for it after asking for a tap on the ground
constexpr int Attack = 4; // no lesson asks: kept clear of the ones they do
constexpr int RecoilHigh = 6;
constexpr int32_t k_HealthProperty = 33;
constexpr float k_FightBeat = 1.2f;
constexpr float k_HitDamage = 0.08f;
} // namespace CreatureFight

struct FightMove
{
	bool block;
	int zone; // 0 high, 1 middle, 2 low
	bool step = false;
	glm::vec3 to {}; // a step's: where to
	entt::entity star = entt::null; // the star marking it, until it is made
	uint32_t starOn = 0;            // the creature it is on (0: on the ground)
	glm::vec3 starAt {};            // in that creature's space (or on the ground)
};
struct Fighter
{
	uint32_t opponent = 0;
	std::deque<FightMove> queue;
	bool autoFight = true;
	int action = CreatureFight::Stance;
	int zone = 0;
	float actionLeft = 0.0f;
	glm::vec3 stepTo {};
};
std::unordered_map<uint32_t, Fighter> g_fighters;
glm::vec3 g_arenaCentre {};
float g_arenaRadius = 0.0f;
float FightHealth(uint32_t creature)
{
	const auto it = g_objectProperties.find(PropertyKey(creature, CreatureFight::k_HealthProperty));
	return it != g_objectProperties.end() ? it->second : 1.0f;
}

void SetFightHealth(uint32_t creature, float health)
{
	g_objectProperties[PropertyKey(creature, CreatureFight::k_HealthProperty)] = std::clamp(health, 0.0f, 1.0f);
}

// For a fight the bigger creature shrinks to the smaller one's height, and grows back to its own when the fight is
// over (Mike, 2026-10-17): Land 1's guide creature stands five times yours, too big to fight. Over k_FightResizeSeconds.
constexpr float k_FightResizeSeconds = 1.5f;
struct ScaleTween
{
	float from;
	float to;
	float t = 0.0f;
	std::optional<std::pair<glm::vec3, glm::vec3>> move; // and walked in from, to (the fighters closing in)
};
std::unordered_map<uint32_t, float> g_fightNormalScale; // a fighter's own scale, while it is resized for the fight
std::unordered_map<uint32_t, ScaleTween> g_scaleTweens;

float CreatureHeight(entt::entity entity)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto& meshes = Locator::resources::value().GetMeshes();
	const auto* mesh = registry.TryGet<openblack::ecs::components::Mesh>(entity);
	const auto& transform = registry.Get<Transform>(entity);
	return mesh != nullptr && meshes.Contains(mesh->id) ? meshes.Handle(mesh->id)->GetBoundingBox().Size().y * transform.scale.y
	                                                    : transform.scale.y;
}

void ResizeForFight(uint32_t bigger, uint32_t smaller)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto big = static_cast<entt::entity>(bigger);
	const float hBig = CreatureHeight(big);
	const float hSmall = CreatureHeight(static_cast<entt::entity>(smaller));
	if (hBig <= hSmall * 1.05f || hBig <= 0.0f)
	{
		return;
	}
	const float own = g_fightNormalScale.contains(bigger) ? g_fightNormalScale[bigger] : registry.Get<Transform>(big).scale.x;
	g_fightNormalScale[bigger] = own;
	const float current = registry.Get<Transform>(big).scale.x;
	g_scaleTweens[bigger] = {current, current * hSmall / hBig};
	// The ring was drawn round where they stood, the big one's length apart: they close in to striking distance, a
	// little under the smaller one's height, as it shrinks.
	const auto small = static_cast<entt::entity>(smaller);
	const auto pBig = registry.Get<Transform>(big).position;
	const auto pSmall = registry.Get<Transform>(small).position;
	const float apart = std::max(hSmall * 0.8f, 2.5f);
	auto flat = glm::vec3(pBig.x - pSmall.x, 0.0f, pBig.z - pSmall.z);
	flat = glm::length(flat) > 1e-3f ? glm::normalize(flat) : glm::vec3(1.0f, 0.0f, 0.0f);
	g_arenaCentre = (pBig + pSmall) * 0.5f;
	g_arenaRadius = std::max(apart * 1.5f, 4.0f);
	auto ground = [](glm::vec3 at) {
		at.y = Locator::terrainSystem::value().GetHeightAt(glm::vec2(at.x, at.z));
		return at;
	};
	g_scaleTweens[bigger].move = std::pair {pBig, ground(g_arenaCentre + flat * apart * 0.5f)};
	const float smallScale = registry.Get<Transform>(small).scale.x;
	g_scaleTweens[smaller] = {smallScale, smallScale};
	g_scaleTweens[smaller].move = std::pair {pSmall, ground(g_arenaCentre - flat * apart * 0.5f)};
	SPDLOG_LOGGER_INFO(spdlog::get("game"), "Fight: creature {} shrinks from {:.1f} tall to {:.1f}, the height of {}; they close in to {:.1f} apart, ring radius {:.1f}",
	                   bigger, hBig, hSmall, smaller, apart, g_arenaRadius);
}

// Each frame: resizes under way, and fighters whose fight is over growing back.
void UpdateFightResizing(float seconds)
{
	auto& registry = Locator::entitiesRegistry::value();
	// A fight not resized yet (one a save point brought back): now.
	for (const auto& [id, fighter] : g_fighters)
	{
		const auto self = static_cast<entt::entity>(id);
		const auto other = static_cast<entt::entity>(fighter.opponent);
		if (!g_fightNormalScale.contains(id) && !g_fightNormalScale.contains(fighter.opponent) && registry.Valid(self) &&
		    registry.Valid(other) && registry.AllOf<Transform>(self) && registry.AllOf<Transform>(other) &&
		    CreatureHeight(self) > CreatureHeight(other) * 1.05f)
		{
			ResizeForFight(id, fighter.opponent);
		}
	}
	for (auto it = g_fightNormalScale.begin(); it != g_fightNormalScale.end();)
	{
		if (g_fighters.contains(it->first))
		{
			++it;
			continue;
		}
		const auto entity = static_cast<entt::entity>(it->first);
		if (registry.Valid(entity) && registry.AllOf<Transform>(entity))
		{
			g_scaleTweens[it->first] = {registry.Get<Transform>(entity).scale.x, it->second};
			SPDLOG_LOGGER_INFO(spdlog::get("game"), "Fight over: creature {} grows back", it->first);
		}
		it = g_fightNormalScale.erase(it);
	}
	for (auto it = g_scaleTweens.begin(); it != g_scaleTweens.end();)
	{
		const auto entity = static_cast<entt::entity>(it->first);
		if (!registry.Valid(entity) || !registry.AllOf<Transform>(entity))
		{
			it = g_scaleTweens.erase(it);
			continue;
		}
		auto& tween = it->second;
		tween.t = std::min(tween.t + seconds / k_FightResizeSeconds, 1.0f);
		const float eased = tween.t * tween.t * (3.0f - 2.0f * tween.t);
		auto& transform = registry.Get<Transform>(entity);
		transform.scale = glm::vec3(glm::mix(tween.from, tween.to, eased));
		if (tween.move)
		{
			transform.position = glm::mix(tween.move->first, tween.move->second, eased);
		}
		it = tween.t >= 1.0f ? g_scaleTweens.erase(it) : std::next(it);
	}
}

void StartFight(uint32_t a, uint32_t b)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto ea = static_cast<entt::entity>(a);
	const auto eb = static_cast<entt::entity>(b);
	if (a == b || g_fighters.contains(a) || g_fighters.contains(b) || !registry.Valid(ea) || !registry.Valid(eb) ||
	    !registry.AllOf<openblack::ecs::components::Creature, Transform>(ea) ||
	    !registry.AllOf<openblack::ecs::components::Creature, Transform>(eb))
	{
		return;
	}
	const auto pa = registry.Get<Transform>(ea).position;
	const auto pb = registry.Get<Transform>(eb).position;
	g_arenaCentre = (pa + pb) * 0.5f;
	g_arenaRadius = std::max(glm::distance(glm::vec2(pa.x, pa.z), glm::vec2(pb.x, pb.z)), 6.0f);
	for (const auto [self, other] : {std::pair {a, b}, std::pair {b, a}})
	{
		Fighter fighter;
		fighter.opponent = other;
		g_fighters[self] = fighter;
		g_creatureOrders.erase(self);
		if (!g_objectProperties.contains(PropertyKey(self, CreatureFight::k_HealthProperty)))
		{
			SetFightHealth(self, 1.0f);
		}
	}
	if (CreatureHeight(ea) > CreatureHeight(eb))
	{
		ResizeForFight(a, b);
	}
	else
	{
		ResizeForFight(b, a);
	}
	SPDLOG_LOGGER_INFO(spdlog::get("game"), "Fight: creature {} against {}, ring at ({:.0f}, {:.0f}) radius {:.0f}", a, b,
	                   g_arenaCentre.x, g_arenaCentre.z, g_arenaRadius);
}

void DropFightStar(FightMove& move); // below

void EndFight(uint32_t loser)
{
	const auto it = g_fighters.find(loser);
	if (it == g_fighters.end())
	{
		return;
	}
	const auto winner = it->second.opponent;
	SPDLOG_LOGGER_INFO(spdlog::get("game"), "Fight over: creature {} beat {} ({:.2f} to {:.2f})", winner, loser, FightHealth(winner),
	                   FightHealth(loser));
	for (const auto who : {loser, winner})
	{
		for (auto& move : g_fighters[who].queue)
		{
			DropFightStar(move);
		}
	}
	g_fighters.erase(loser);
	g_fighters.erase(winner);
}

// One glittering star for each move you queue, on the part of the body tapped (or the ground stepped to), until made.
entt::entity MakeFightStar(const glm::vec4& tint)
{
	auto& resources = Locator::resources::value();
	if (!resources.GetTextures().Contains(entt::hashed_string("raw/S_SpriteSheet3a").value()))
	{
		return entt::null;
	}
	auto& registry = Locator::entitiesRegistry::value();
	const auto star = registry.Create();
	registry.Assign<openblack::ecs::components::Sprite>(star, resources.GetTextures().Handle(entt::hashed_string("raw/S_SpriteSheet3a"))->GetNativeHandle(),
	                                                   glm::vec2(0.0f), glm::vec2(1.0f / 8.0f), tint);
	registry.Assign<Transform>(star, glm::vec3(0.0f), glm::mat3(1.0f), glm::vec3(1.4f));
	return star;
}

void DropFightStar(FightMove& move)
{
	auto& registry = Locator::entitiesRegistry::value();
	if (move.star != entt::null && registry.Valid(move.star))
	{
		registry.Destroy(move.star);
	}
	move.star = entt::null;
}

void UpdateFights(float seconds)
{
	using namespace CreatureFight;
	auto& registry = Locator::entitiesRegistry::value();
	// Stars sit on what they mark, twinkling.
	static float s_starClock = 0.0f;
	s_starClock += seconds;
	UpdateFightResizing(seconds);
	if (!g_fighters.empty())
	{
		ReleaseHeldCreatures();
	}
	for (auto& [id, fighter] : g_fighters)
	{
		for (auto& move : fighter.queue)
		{
			if (move.star == entt::null || !registry.Valid(move.star))
			{
				continue;
			}
			glm::vec3 at = move.starAt;
			if (move.starOn != 0 && registry.Valid(static_cast<entt::entity>(move.starOn)))
			{
				const auto& on = registry.Get<Transform>(static_cast<entt::entity>(move.starOn));
				at = on.position + on.rotation * move.starAt;
			}
			registry.Get<Transform>(move.star).position = at;
			const int frame = 48 + static_cast<int>(s_starClock * 16.0f) % 16;
			registry.Get<openblack::ecs::components::Sprite>(move.star).uvMin =
			    glm::vec2(static_cast<float>(frame % 8) / 8.0f, static_cast<float>(frame / 8) / 8.0f);
		}
	}
	std::vector<uint32_t> ids;
	for (const auto& [id, fighter] : g_fighters)
	{
		ids.push_back(id);
	}
	for (const auto id : ids)
	{
		auto found = g_fighters.find(id);
		if (found == g_fighters.end())
		{
			continue;
		}
		auto& fighter = found->second;
		const auto self = static_cast<entt::entity>(id);
		const auto other = static_cast<entt::entity>(fighter.opponent);
		if (!registry.Valid(self) || !registry.Valid(other) || !g_fighters.contains(fighter.opponent))
		{
			g_fighters.erase(found);
			continue;
		}
		// Face the other.
		auto& transform = registry.Get<Transform>(self);
		FaceTowards(transform, registry.Get<Transform>(other).position);
		fighter.actionLeft -= seconds;
		// The next move: the queue's, else its own if it fights on its own, else stand ready.
		// A step under way: on toward its point, inside the ring.
		if (fighter.action == Step && fighter.actionLeft > 0.0f)
		{
			const auto flat = glm::vec2(fighter.stepTo.x - transform.position.x, fighter.stepTo.z - transform.position.z);
			const float distance = glm::length(flat);
			if (distance > 0.1f)
			{
				const auto step = flat / distance * std::min(6.0f * seconds, distance);
				auto next = glm::vec2(transform.position.x, transform.position.z) + step;
				const auto fromCentre = next - glm::vec2(g_arenaCentre.x, g_arenaCentre.z);
				if (glm::length(fromCentre) > g_arenaRadius)
				{
					next = glm::vec2(g_arenaCentre.x, g_arenaCentre.z) + glm::normalize(fromCentre) * g_arenaRadius;
				}
				transform.position = glm::vec3(next.x, Locator::terrainSystem::value().GetHeightAt(next), next.y);
			}
		}
		if (fighter.actionLeft > 0.0f)
		{
			continue;
		}
		std::optional<FightMove> move;
		if (!fighter.queue.empty())
		{
			move = fighter.queue.front();
			DropFightStar(fighter.queue.front());
			fighter.queue.pop_front();
		}
		else if (fighter.autoFight)
		{
			move = FightMove {std::rand() % 10 < 3, std::rand() % 3};
		}
		if (!move)
		{
			fighter.action = Stance;
			fighter.actionLeft = 0.3f;
			continue;
		}
		fighter.zone = move->zone;
		fighter.actionLeft = k_FightBeat;
		if (move->step)
		{
			fighter.action = Step;
			fighter.stepTo = move->to;
			SPDLOG_LOGGER_INFO(spdlog::get("game"), "Fight: {} steps toward ({:.0f}, {:.0f})", id, move->to.x, move->to.z);
			continue;
		}
		if (move->block)
		{
			fighter.action = Block;
			continue;
		}
		fighter.action = Attack;
		auto& target = g_fighters[fighter.opponent];
		if (target.action == Block && target.zone == move->zone && target.actionLeft > 0.0f)
		{
			SPDLOG_LOGGER_INFO(spdlog::get("game"), "Fight: {} attacks {} ({}) - blocked", id, fighter.opponent, move->zone);
			continue;
		}
		const float health = FightHealth(fighter.opponent) - k_HitDamage;
		SetFightHealth(fighter.opponent, health);
		target.action = RecoilHigh + move->zone;
		target.zone = move->zone;
		target.actionLeft = 0.8f;
		SPDLOG_LOGGER_INFO(spdlog::get("game"), "Fight: {} hits {} ({}), its fight health {:.2f}", id, fighter.opponent, move->zone,
		                   FightHealth(fighter.opponent));
		if (FightHealth(fighter.opponent) <= 0.0f)
		{
			EndFight(fighter.opponent);
		}
	}
}

// The animation a fighter shows now, by ctrspec27.txt's names.
std::optional<std::string> FightAnimation(uint32_t creature)
{
	using namespace CreatureFight;
	const auto it = g_fighters.find(creature);
	if (it == g_fighters.end())
	{
		return std::nullopt;
	}
	static constexpr std::array<const char*, 3> k_Attacks = {"attack_high", "attack_belly", "attack_knee"};
	static constexpr std::array<const char*, 3> k_Recoils = {"hi", "mi", "lo"};
	switch (it->second.action)
	{
	case Attack: return std::string(k_Attacks.at(static_cast<size_t>(it->second.zone)));
	case Block: return std::string("block");
	case Step: return std::string("step_forward");
	case RecoilHigh:
	case RecoilHigh + 1:
	case RecoilHigh + 2: return std::string(k_Recoils.at(static_cast<size_t>(it->second.action - RecoilHigh)));
	default: return std::string("stance");
	}
}
} // namespace

// A tap (ZR) during your creature's fight: on the opponent an attack there, on your creature a block there.
bool FightClick(const glm::vec3& origin, const glm::vec3& direction, const glm::vec3& ground)
{
	const auto mine = PlayersCreature();
	const auto fighter = g_fighters.find(mine);
	if (mine == 0 || fighter == g_fighters.end())
	{
		return false;
	}
	auto& registry = Locator::entitiesRegistry::value();
	const auto& meshes = Locator::resources::value().GetMeshes();
	const auto along = glm::normalize(direction);
	// On a creature, or close beside one (they are hard to tap): the height on it where the ray passes nearest.
	for (const auto [who, block] : {std::pair {fighter->second.opponent, false}, std::pair {mine, true}})
	{
		const auto entity = static_cast<entt::entity>(who);
		const auto& at = registry.Get<Transform>(entity);
		const auto* mesh = registry.TryGet<openblack::ecs::components::Mesh>(entity);
		const float height =
		    mesh != nullptr && meshes.Contains(mesh->id) ? meshes.Handle(mesh->id)->GetBoundingBox().Size().y * at.scale.y : 4.0f;
		const auto middle = at.position + glm::vec3(0.0f, height * 0.5f, 0.0f);
		const float t = std::max(glm::dot(middle - origin, along), 0.0f);
		const auto nearest = origin + along * t;
		const auto exact = CreatureRayHit(entity, origin, direction);
		if (!exact && glm::distance(nearest, middle) > height * 0.75f)
		{
			continue;
		}
		const auto point = exact ? origin + along * exact->first : nearest;
		const float up = std::clamp((point.y - at.position.y) / std::max(height, 0.1f), 0.0f, 1.0f);
		const int zone = up > 0.66f ? 0 : up > 0.33f ? 1 : 2;
		FightMove move {block, zone};
		move.star = MakeFightStar(block ? glm::vec4(0.6f, 0.8f, 1.0f, 1.0f) : glm::vec4(1.0f, 1.0f, 0.31f, 1.0f));
		move.starOn = who;
		move.starAt = glm::transpose(at.rotation) * (point - at.position);
		fighter->second.queue.push_back(move);
		SPDLOG_LOGGER_INFO(spdlog::get("game"), "Fight: you queue {} {} ({} queued)", block ? "a block" : "an attack",
		                   zone == 0 ? "high" : zone == 1 ? "middle" : "low", fighter->second.queue.size());
		return true;
	}
	// On the ground: a step that way, dodging.
	if (glm::distance(glm::vec2(ground.x, ground.z), glm::vec2(g_arenaCentre.x, g_arenaCentre.z)) < g_arenaRadius * 2.0f)
	{
		FightMove move {false, 1};
		move.step = true;
		const auto& at = registry.Get<Transform>(static_cast<entt::entity>(mine));
		const auto way = glm::vec2(ground.x - at.position.x, ground.z - at.position.z);
		const auto stepTo = glm::vec2(at.position.x, at.position.z) +
		                    (glm::length(way) > 5.0f ? glm::normalize(way) * 5.0f : way);
		move.to = glm::vec3(stepTo.x, ground.y, stepTo.y);
		move.star = MakeFightStar(glm::vec4(0.5f, 1.0f, 0.5f, 1.0f));
		move.starAt = ground + glm::vec3(0.0f, 0.5f, 0.0f);
		fighter->second.queue.push_back(move);
		SPDLOG_LOGGER_INFO(spdlog::get("game"), "Fight: you queue a step ({} queued)", fighter->second.queue.size());
		return true;
	}
	return false;
}

namespace
{
bool g_fightViewExit = false;
}

bool TakeFightViewExit()
{
	const bool exit = g_fightViewExit;
	g_fightViewExit = false;
	return exit;
}

std::optional<std::pair<glm::vec3, float>> FightRing()
{
	if (g_fighters.empty())
	{
		return std::nullopt;
	}
	return std::pair {g_arenaCentre, g_arenaRadius};
}

// The two in your creature's fight, yours first, if it is fighting.
std::optional<std::pair<uint32_t, uint32_t>> PlayersFight()
{
	const auto mine = PlayersCreature();
	const auto it = g_fighters.find(mine);
	if (mine == 0 || it == g_fighters.end())
	{
		return std::nullopt;
	}
	return std::pair {mine, it->second.opponent};
}

void CreatureDoAction() // 075 CREATURE_DO_ACTION
{
	const auto withObject = Pop().uintVal;
	const auto target = Pop().uintVal;
	const auto action = Pop().intVal;
	const auto creature = Pop().uintVal;
	// Kept as the creature's order, acted out each frame (ActOutCreatureOrder) until the next one.
	const auto previous = g_creatureOrders.find(creature);
	if (previous == g_creatureOrders.end() || previous->second.action != action || previous->second.target != target)
	{
		SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "CREATURE_DO_ACTION creature {}: {} ({}), target {}, with {}", creature,
		                   CreatureActionName(action), action, target, withObject);
		g_holdDone.erase(creature);
	}
	// What a creature was told to hold (HoldObject, 64) it puts down when told to do something else: Land 1's guide
	// lifts your creature to look at it, then is told to look at the camera, and never told to put it down.
	if (previous != g_creatureOrders.end() && previous->second.action == 64 && action != 64)
	{
		CreatureDropsHeld(creature);
	}
	if (action == CreatureFight::FightWith)
	{
		CreatureDropsHeld(creature);
		CreatureDropsHeld(target);
		StartFight(creature, target);
		return;
	}
	if (action == 261) // DeadForever: it faints and lies where it fell, for good
	{
		if (g_deadForever.insert(creature).second)
		{
			SPDLOG_LOGGER_INFO(spdlog::get("game"), "Creature {} is dead for good: it faints and stays down", creature);
		}
		g_fighters.erase(creature);
		CreatureDropsHeld(creature);
		return;
	}
	g_creatureOrders[creature] = {action, target, withObject};
}

void InCreatureHand() // 076 IN_CREATURE_HAND
{
	const auto creature = Pop().uintVal;
	const auto obj = Pop().uintVal;
	const auto held = g_creatureHolding.find(creature);
	Pushb(obj != 0 && held != g_creatureHolding.end() && held->second == obj);
}

void CreatureSetDesireValue() // 077 CREATURE_SET_DESIRE_VALUE
{
	[[maybe_unused]] const auto value = Popf();
	[[maybe_unused]] const auto desire = Pop().intVal;
	[[maybe_unused]] const auto creature = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void CreatureSetDesireActivated78() // 078 CREATURE_SET_DESIRE_ACTIVATED
{
	[[maybe_unused]] const auto active = Pop().intVal;
	[[maybe_unused]] const auto desire = Pop().intVal;
	[[maybe_unused]] const auto creature = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void CreatureSetDesireActivated79() // 079 CREATURE_SET_DESIRE_ACTIVATED
{
	[[maybe_unused]] const auto active = Pop().intVal;
	[[maybe_unused]] const auto creature = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void CreatureSetDesireMaximum() // 080 CREATURE_SET_DESIRE_MAXIMUM
{
	[[maybe_unused]] const auto value = Popf();
	[[maybe_unused]] const auto desire = Pop().intVal;
	[[maybe_unused]] const auto creature = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

// The camera enums (CONVERT_CAMERA_POSITION/FOCUS) are records in Data/camera.edt, the camera editor's
// store: each is a 32-byte key "Cam<enum>", a uint32 length (32), then position xyz, focus xyz and two
// floats this doesn't use. Found by reading runblack.exe: its lookup does sprintf("Cam%d") and a get on
// that file.
struct CameraEnumEntry
{
	glm::vec3 position;
	glm::vec3 focus;
};

const std::unordered_map<int32_t, CameraEnumEntry>& CameraEnumTable()
{
	static std::unordered_map<int32_t, CameraEnumEntry> table;
	static bool loaded = false;
	if (loaded)
	{
		return table;
	}
	loaded = true;
	std::vector<uint8_t> data;
	try
	{
		auto& fileSystem = Locator::filesystem::value();
		data = fileSystem.ReadAll(fileSystem.GetPath<openblack::filesystem::Path::Data>() / "camera.edt");
	}
	catch (const std::exception& e)
	{
		SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "Could not read camera.edt: {}", e.what());
		return table;
	}
	constexpr size_t k_KeySize = 32;
	constexpr size_t k_RecordSize = k_KeySize + sizeof(uint32_t) + 6 * sizeof(float);
	for (size_t i = 0; i + k_RecordSize <= data.size(); ++i)
	{
		if (data[i] != 'C' || data[i + 1] != 'a' || data[i + 2] != 'm')
		{
			continue;
		}
		size_t end = i + 3;
		int32_t id = 0;
		while (end < i + k_KeySize && data[end] >= '0' && data[end] <= '9')
		{
			id = id * 10 + (data[end++] - '0');
		}
		uint32_t length;
		std::memcpy(&length, &data[i + k_KeySize], sizeof(length));
		if (end == i + 3 || data[end] != 0 || length != 32)
		{
			continue;
		}
		std::array<float, 6> v {};
		std::memcpy(v.data(), &data[i + k_KeySize + sizeof(uint32_t)], sizeof(v));
		table[id] = {{v[0], v[1], v[2]}, {v[3], v[4], v[5]}};
		i += k_RecordSize - 1;
	}
	SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "Loaded {} camera enums from camera.edt", table.size());
	return table;
}

void PushCameraEnum(const char* function, int32_t cameraEnum, bool focus)
{
	glm::vec3 result(0.0f);
	const auto& table = CameraEnumTable();
	if (const auto it = table.find(cameraEnum); it != table.end())
	{
		result = focus ? it->second.focus : it->second.position;
	}
	else
	{
		SPDLOG_LOGGER_WARN(spdlog::get("scripting"), "{}({}): no such camera enum", function, cameraEnum);
	}
	Pushv(result.x);
	Pushv(result.y);
	Pushv(result.z);
}

void ConvertCameraPosition() // 081 CONVERT_CAMERA_POSITION
{
	PushCameraEnum("CONVERT_CAMERA_POSITION", Pop().intVal, false);
}

void ConvertCameraFocus() // 082 CONVERT_CAMERA_FOCUS
{
	PushCameraEnum("CONVERT_CAMERA_FOCUS", Pop().intVal, true);
}

void CreatureSetPlayer() // 083 CREATURE_SET_PLAYER
{
	// The creature becomes the player's: Land 1's glade, once one of the three is chosen.
	const auto creature = Pop().uintVal;
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = static_cast<entt::entity>(creature);
	if (auto* component = creature != 0 && registry.Valid(entity) ? registry.TryGet<openblack::ecs::components::Creature>(entity)
	                                                              : nullptr)
	{
		component->owner = PlayerNames::PLAYER_ONE;
		SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "CREATURE_SET_PLAYER: creature {} ({}) is the player's", creature,
		                   static_cast<int>(component->species));
	}
}

void StartCountdownTimer() // 084 START_COUNTDOWN_TIMER
{
	[[maybe_unused]] const auto timeout = Popf();
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void CreatureInitialiseNumTimesPerformedAction() // 085 CREATURE_INITIALISE_NUM_TIMES_PERFORMED_ACTION
{
	[[maybe_unused]] const auto unk1 = Pop().intVal;
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void CreatureGetNumTimesActionPerformed() // 086 CREATURE_GET_NUM_TIMES_ACTION_PERFORMED
{
	[[maybe_unused]] const auto unk1 = Pop().intVal;
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushf(0.0f);
}

void RemoveCountdownTimer() // 087 REMOVE_COUNTDOWN_TIMER
{
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void GetObjectDropped() // 088 GET_OBJECT_DROPPED
{
	[[maybe_unused]] const auto creature = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pusho(0);
}

void ClearDroppedByObject() // 089 CLEAR_DROPPED_BY_OBJECT
{
	[[maybe_unused]] const auto creature = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void CreateReaction() // 090 CREATE_REACTION
{
	[[maybe_unused]] const auto reaction = Pop().intVal;
	[[maybe_unused]] const auto object = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void RemoveReaction() // 091 REMOVE_REACTION
{
	[[maybe_unused]] const auto object = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void GetCountdownTimer() // 092 GET_COUNTDOWN_TIMER
{
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushf(0.0f);
}

void StartDualCamera() // 093 START_DUAL_CAMERA
{
	[[maybe_unused]] const auto obj2 = Pop().uintVal;
	[[maybe_unused]] const auto obj1 = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void UpdateDualCamera() // 094 UPDATE_DUAL_CAMERA
{
	[[maybe_unused]] const auto obj2 = Pop().uintVal;
	[[maybe_unused]] const auto obj1 = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void ReleaseDualCamera() // 095 RELEASE_DUAL_CAMERA
{
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void SetCreatureHelp() // 096 SET_CREATURE_HELP
{
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void GetTargetObject() // 097 GET_TARGET_OBJECT
{
	[[maybe_unused]] const auto obj = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pusho(0);
}

void CreatureDesireIs() // 098 CREATURE_DESIRE_IS
{
	[[maybe_unused]] const auto unk1 = Pop().intVal;
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushi(0);
}

void CountdownTimerExists() // 099 COUNTDOWN_TIMER_EXISTS
{
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushb(false);
}

void LookGameThing() // 100 LOOK_GAME_THING
{
	[[maybe_unused]] const auto target = Pop().uintVal;
	[[maybe_unused]] const auto spirit = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void GetObjectDestination() // 101 GET_OBJECT_DESTINATION
{
	[[maybe_unused]] const auto obj = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushv(0.0f); // x
	Pushv(0.0f); // y
	Pushv(0.0f); // z
}

void CreatureForceFinish() // 102 CREATURE_FORCE_FINISH
{
	[[maybe_unused]] const auto creature = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void HideCountdownTimer() // 103 HIDE_COUNTDOWN_TIMER
{
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void GetActionTextForObject() // 104 GET_ACTION_TEXT_FOR_OBJECT
{
	[[maybe_unused]] const auto obj = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushi(0);
}

void CreateDualCameraWithPoint() // 105 CREATE_DUAL_CAMERA_WITH_POINT
{
	[[maybe_unused]] const auto position = PopVec();
	[[maybe_unused]] const auto obj = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

// The original's camera facing a thing (runblack.exe 0x6ed710): looking at its middle from `distance` in front of it,
// 0.1 radians above level.
std::optional<std::pair<glm::vec3, glm::vec3>> CameraFacing(uint32_t thing, float distance)
{
	const auto focus = CameraFollowPoint(thing);
	if (!focus)
	{
		return std::nullopt;
	}
	const auto front = Locator::entitiesRegistry::value().Get<Transform>(static_cast<entt::entity>(thing)).rotation *
	                   glm::vec3(0.0f, 0.0f, -1.0f);
	const float heading = std::atan2(front.x, front.z);
	constexpr float k_Pitch = 0.1f;
	const auto origin = *focus + distance * glm::vec3(std::sin(heading) * std::cos(k_Pitch), std::sin(k_Pitch),
	                                                  std::cos(heading) * std::cos(k_Pitch));
	return std::make_pair(origin, *focus);
}

void SetCameraToFaceObject() // 106 SET_CAMERA_TO_FACE_OBJECT
{
	const auto distance = Popf();
	const auto target = Pop().uintVal;
	if (const auto facing = CameraFacing(target, distance))
	{
		EndCameraPath();
		g_cameraFollow.target = g_cameraFollow.focus = 0;
		Locator::camera::value().SetOrigin(facing->first).SetFocus(facing->second);
	}
}

void MoveCameraToFaceObject() // 107 MOVE_CAMERA_TO_FACE_OBJECT
{
	const auto time = Popf();
	const auto distance = Popf();
	const auto target = Pop().uintVal;
	if (const auto facing = CameraFacing(target, distance))
	{
		EndCameraPath();
		g_cameraFollow.target = g_cameraFollow.focus = 0;
		const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::duration<float>(time));
		Locator::camera::value().ScriptMoveOrigin(facing->first, duration).ScriptMoveFocus(facing->second, duration);
	}
}

void GetMoonPercentage() // 108 GET_MOON_PERCENTAGE
{
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushf(0.0f);
}

void PopulateContainer() // 109 POPULATE_CONTAINER
{
	[[maybe_unused]] const auto subtype = Pop().intVal;
	[[maybe_unused]] const auto type = Pop().intVal;
	[[maybe_unused]] const auto quantity = Popf();
	[[maybe_unused]] const auto obj = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void AddReference() // 110 ADD_REFERENCE
{
	const auto objId = Pop().uintVal;
	// TODO(Daniels118): implement this - HIGH PRIORITY
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pusho(objId);
}

void RemoveReference() // 111 REMOVE_REFERENCE
{
	const auto objId = Pop().uintVal;
	// TODO(Daniels118): implement this - HIGH PRIORITY
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pusho(objId);
}

// The game clock, in hours (0-24), which the sky's light follows. MOVE_GAME_TIME eases it to an hour.
namespace
{
float g_gameHour = 12.0f;
struct GameTimeMove
{
	float from;
	float to;
	float seconds;
	float elapsed;
};
std::optional<GameTimeMove> g_gameTimeMove;
// Whether a script (or a save) has set the hour; until then it is the Switch's own clock, as the original went by the
// PC's.
bool g_gameTimeSet = false;

// The sky's hour for a game hour, as the original's LH3DSky (fn_00869FD0 from fn_0086A110): the morning's game hours
// 3.5, 7.5, 8 and 8.5 are the sky's 4.5 (night ends), 7 and 7.5 (dawn) and 8.25 (full day), straight between them,
// the evening's the same about midday.
float SkyHour(float hour)
{
	constexpr std::array<float, 6> k_Game {0.0f, 3.5f, 7.5f, 8.0f, 8.5f, 12.0f};
	constexpr std::array<float, 6> k_Sky {0.0f, 4.5f, 7.0f, 7.5f, 8.25f, 12.0f};
	const bool evening = hour > 12.0f;
	const float x = evening ? 24.0f - hour : hour;
	float sky = x;
	for (size_t i = 1; i < k_Game.size(); ++i)
	{
		if (x <= k_Game.at(i))
		{
			sky = k_Sky.at(i - 1) + (x - k_Game.at(i - 1)) / (k_Game.at(i) - k_Game.at(i - 1)) * (k_Sky.at(i) - k_Sky.at(i - 1));
			break;
		}
	}
	return evening ? 24.0f - sky : sky;
}

// The game hour of a sky hour (SkyHour backwards).
float GameHourOfSky(float sky)
{
	constexpr std::array<float, 6> k_Game {0.0f, 3.5f, 7.5f, 8.0f, 8.5f, 12.0f};
	constexpr std::array<float, 6> k_Sky {0.0f, 4.5f, 7.0f, 7.5f, 8.25f, 12.0f};
	const bool evening = sky > 12.0f;
	const float x = evening ? 24.0f - sky : sky;
	float hour = x;
	for (size_t i = 1; i < k_Sky.size(); ++i)
	{
		if (x <= k_Sky.at(i))
		{
			hour = k_Game.at(i - 1) + (x - k_Sky.at(i - 1)) / (k_Sky.at(i) - k_Sky.at(i - 1)) * (k_Game.at(i) - k_Game.at(i - 1));
			break;
		}
	}
	return evening ? 24.0f - hour : hour;
}

void ApplyGameHour(float hour)
{
	g_gameHour = std::fmod(std::fmod(hour, 24.0f) + 24.0f, 24.0f);
	Locator::skySystem::value().SetTime(SkyHour(g_gameHour));
}
} // namespace

void SetGameTime() // 112 SET_GAME_TIME
{
	g_gameTimeMove.reset();
	g_gameTimeSet = true;
	ApplyGameHour(Popf());
}

void GetGameTime() // 113 GET_GAME_TIME
{
	Pushf(g_gameHour);
}

void GetRealTime() // 114 GET_REAL_TIME
{
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushf(0.0f);
}

void GetRealDay115() // 115 GET_REAL_DAY
{
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushf(0.0f);
}

void GetRealDay116() // 116 GET_REAL_DAY
{
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushf(0.0f);
}

void GetRealMonth() // 117 GET_REAL_MONTH
{
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushf(0.0f);
}

void GetRealYear() // 118 GET_REAL_YEAR
{
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushf(0.0f);
}

void RunCameraPath() // 119 RUN_CAMERA_PATH
{
	const auto track = Pop().intVal;
	StartCameraPath(track);
}

// A conversation whose script is gone is over: Land 1's CheckForCommentOnFoodItems was stopped mid-line (a comment on a
// pig by the hand) and the feeding lesson waited for ever to start its own. Holder 0 is a save made before holders.
void ReleaseDialogueOfEndedTask()
{
	if (g_dialogueActive && g_dialogueTask != 0 && !Locator::vm::value().GetTasks().contains(g_dialogueTask))
	{
		SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "Dialogue held by task {}, which has ended: released", g_dialogueTask);
		g_dialogueActive = false;
		g_dialogueTask = 0;
	}
}

// Both advisors home, as the original's HelpSystem::SpiritHome (fn_005C5200): a help script's vanish in a puff
// (fn_005C3540, HelpDude::TriggerSmoke), anyone else's fly back off their edge over a second (fn_005C3590).
void SendAdvisorsHome()
{
	const auto& vm = Locator::vm::value();
	const auto task = vm.GetTasks().find(vm.GetCurrentTaskId());
	const bool help = task != vm.GetTasks().end() && task->second.type == lhvm::ScriptType::Help;
	for (const auto spirit : {helpspirits::Spirit::Good, helpspirits::Spirit::Evil})
	{
		if (help)
		{
			helpspirits::Disappear(spirit);
		}
		else
		{
			helpspirits::Home(spirit);
		}
	}
}

void StartDialogue() // 120 START_DIALOGUE
{
	// runblack.exe 1.20's GScript::StartSpiritControl: taking a free dialogue first sends both advisors home, so
	// whatever the last one left out goes before this one's lines.
	ReleaseDialogueOfEndedTask();
	const bool granted = !g_dialogueActive;
	if (granted)
	{
		SendAdvisorsHome();
		g_dialogueActive = true;
		g_dialogueTask = Locator::vm::value().GetCurrentTaskId();
	}
	Pushb(granted);
}

void EndDialogue() // 121 END_DIALOGUE
{
	// GScript::EndSpiritControl: the task holding the dialogue sends both advisors home as it lets go. Nothing else
	// sends them back after the opening or a lesson (FollowUs never calls SPIRIT_HOME), so without this they stayed
	// on screen for good.
	if (g_dialogueActive && g_dialogueTask == Locator::vm::value().GetCurrentTaskId())
	{
		SendAdvisorsHome();
	}
	g_dialogueActive = false;
	g_dialogueTask = 0;
}

void IsDialogueReady() // 122 IS_DIALOGUE_READY
{
	ReleaseDialogueOfEndedTask();
	Pushb(!g_dialogueActive); // scripts check this before START_DIALOGUE
}

void ChangeWeatherProperties() // 123 CHANGE_WEATHER_PROPERTIES
{
	[[maybe_unused]] const auto fallspeed = Popf();
	[[maybe_unused]] const auto overcast = Popf();
	[[maybe_unused]] const auto snowfall = Popf();
	[[maybe_unused]] const auto rainfall = Popf();
	[[maybe_unused]] const auto temperature = Popf();
	[[maybe_unused]] const auto storm = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void ChangeLightningProperties() // 124 CHANGE_LIGHTNING_PROPERTIES
{
	[[maybe_unused]] const auto forkmax = Popf();
	[[maybe_unused]] const auto forkmin = Popf();
	[[maybe_unused]] const auto sheetmax = Popf();
	[[maybe_unused]] const auto sheetmin = Popf();
	[[maybe_unused]] const auto storm = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void ChangeTimeFadeProperties() // 125 CHANGE_TIME_FADE_PROPERTIES
{
	[[maybe_unused]] const auto fadeTime = Popf();
	[[maybe_unused]] const auto duration = Popf();
	[[maybe_unused]] const auto storm = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void ChangeCloudProperties() // 126 CHANGE_CLOUD_PROPERTIES
{
	[[maybe_unused]] const auto elevation = Popf();
	[[maybe_unused]] const auto blackness = Popf();
	[[maybe_unused]] const auto numClouds = Popf();
	[[maybe_unused]] const auto storm = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void SetHeadingAndSpeed() // 127 SET_HEADING_AND_SPEED
{
	[[maybe_unused]] const auto speed = Popf();
	[[maybe_unused]] const auto position = PopVec();
	[[maybe_unused]] const auto unk0 = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void StartGameSpeed() // 128 START_GAME_SPEED
{
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void EndGameSpeed() // 129 END_GAME_SPEED
{
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void BuildBuilding() // 130 BUILD_BUILDING
{
	[[maybe_unused]] const auto desire = Popf();
	[[maybe_unused]] const auto position = PopVec();
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void SetAffectedByWind() // 131 SET_AFFECTED_BY_WIND
{
	[[maybe_unused]] const auto object = Pop().uintVal;
	[[maybe_unused]] const auto enabled = static_cast<bool>(Pop().intVal);
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void WidescreenTransistionFinished() // 132 WIDESCREEN_TRANSISTION_FINISHED
{
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushb(false);
}

namespace
{
void PutInHand(uint32_t object)
{
	if (g_heldObject == 0)
	{
		g_heldObject = object;
	}
}

// A storage pit's (or a pile's) food or wood: the town's store, as the scripts count it.
openblack::ecs::components::Pot* ResourcePot(entt::entity entity, int32_t resource, size_t woodPile = 0) // entity: may become the village store
{
	auto& registry = Locator::entitiesRegistry::value();
	if (!registry.Valid(entity))
	{
		return nullptr;
	}
	if (auto* pot = registry.TryGet<openblack::ecs::components::Pot>(entity))
	{
		return pot;
	}
	auto pileOf = [&](entt::entity building) -> openblack::ecs::components::Pot* {
		if (const auto* pit = registry.TryGet<openblack::ecs::components::StoragePit>(building))
		{
			const auto pile = resource == static_cast<int32_t>(ResourceType::Food) ? pit->foodPile : pit->woodPiles.at(woodPile);
			return pile != entt::null && registry.Valid(pile) ? registry.TryGet<openblack::ecs::components::Pot>(pile) : nullptr;
		}
		return nullptr;
	};
	if (auto* own = pileOf(entity))
	{
		return own;
	}
	// A building that can't give it (no store, or a store without that pile) stands for its village's: the nearest
	// storage pit within 150 that can. Land 1's GuideImpressTownLesson takes "the nearest building within 10 of" where
	// the Aztec store stands for the store, and here that is another building: the lesson emptied nothing, saw the
	// miracle's food go elsewhere, and said "try again" for ever (Mike, 2026-10-18).
	if (!registry.AllOf<Transform>(entity))
	{
		return nullptr;
	}
	const auto at = registry.Get<Transform>(entity).position;
	float best = 150.0f;
	entt::entity store = entt::null;
	registry.Each<const openblack::ecs::components::StoragePit, const Transform>(
	    [&](entt::entity pit, const openblack::ecs::components::StoragePit&, const Transform& where) {
		    const float d = glm::distance(glm::vec2(where.position.x, where.position.z), glm::vec2(at.x, at.z));
		    if (pit != entity && d < best && pileOf(pit) != nullptr)
		    {
			    best = d;
			    store = pit;
		    }
	    });
	static std::set<std::pair<uint32_t, uint32_t>> s_said;
	if (s_said.emplace(static_cast<uint32_t>(entity), static_cast<uint32_t>(store)).second)
	{
		if (store != entt::null)
		{
			SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "Resources of building {} ({}{}) are its village store's, {} ({:.0f} away)",
			                   static_cast<uint32_t>(entity), registry.AllOf<openblack::ecs::components::StoragePit>(entity) ? "a store without that pile" : "no store",
			                   registry.AllOf<openblack::ecs::components::Abode>(entity) ? "" : ", not a building", static_cast<uint32_t>(store), best);
		}
		else
		{
			SPDLOG_LOGGER_WARN(spdlog::get("scripting"), "Building {} at ({:.0f}, {:.0f}): no village store within 150",
			                   static_cast<uint32_t>(entity), at.x, at.z);
		}
	}
	return store != entt::null ? pileOf(store) : nullptr;
}

float ContainerResource(uint32_t container, int32_t resource)
{
	const auto entity = static_cast<entt::entity>(container);
	float total = 0.0f;
	const size_t piles = resource == static_cast<int32_t>(ResourceType::Wood) ? 5 : 1;
	for (size_t i = 0; i < piles; ++i)
	{
		if (const auto* pot = ResourcePot(entity, resource, i))
		{
			total += static_cast<float>(pot->amount);
			if (Locator::entitiesRegistry::value().AllOf<openblack::ecs::components::Pot>(entity))
			{
				break; // a pile, not a pit
			}
		}
	}
	return total;
}

// Adds (or with a negative amount removes) food or wood; what was actually added.
float ChangeContainerResource(uint32_t container, int32_t resource, float amount)
{
	auto* pot = ResourcePot(static_cast<entt::entity>(container), resource);
	if (pot == nullptr)
	{
		SPDLOG_LOGGER_WARN(spdlog::get("scripting"), "Resource {} of {}: no store there", resource, container);
		return 0.0f;
	}
	const float before = pot->amount;
	pot->amount = static_cast<uint16_t>(std::clamp(before + amount, 0.0f, 65535.0f));
	SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "Resource {} of {}: {:.0f} -> {}", resource, container, before, pot->amount);
	return static_cast<float>(pot->amount) - before;
}

// Towns' belief in each player (0-1) and whose they are (0 nobody). A town that belongs to nobody believes in nobody
// at info.dat's beliefInNeutralPlayer (0.5): passing that, it is won over - Land 1's guide hands out six Food
// miracles, and six watched by the Aztecs take about that.
std::map<uint32_t, std::map<int, float>> g_townBelief;
// "+N followers" shown over a town a miracle impressed, for a few seconds (drawn by the debug overlay).
struct FollowerPopup
{
	glm::vec3 at;
	int count;
	std::chrono::steady_clock::time_point until;
};
std::vector<FollowerPopup> g_followerPopups;
std::map<uint32_t, int> g_townOwner;

int TownOwnerOf(uint32_t town)
{
	const auto it = g_townOwner.find(town);
	return it != g_townOwner.end() ? it->second : 0;
}

float TownBelief(uint32_t town, int player)
{
	const auto& info = Locator::infoConstants::value().town;
	const auto it = g_townBelief.find(town);
	if (it != g_townBelief.end())
	{
		if (const auto belief = it->second.find(player); belief != it->second.end())
		{
			return belief->second;
		}
	}
	return player == 0 ? info.beliefInNeutralPlayer : 0.0f;
}

// The player with the most belief, past the minimum, has the town.
void UpdateTownOwner(uint32_t town)
{
	const auto& belief = Locator::infoConstants::value().belief;
	const int owner = g_townOwner.contains(town) ? g_townOwner[town] : 0;
	int best = owner;
	float bestBelief = TownBelief(town, owner);
	for (const auto& [player, value] : g_townBelief[town])
	{
		if (player != owner && value > bestBelief && value >= belief.minimumThreshold)
		{
			best = player;
			bestBelief = value;
		}
	}
	if (best != owner)
	{
		g_townOwner[town] = best;
		SPDLOG_LOGGER_INFO(spdlog::get("game"), "Town {} now belongs to player {} (belief {:.3f}, the last owner's {:.3f})", town, best,
		                   bestBelief, TownBelief(town, owner));
	}
}

void SetTownBelief(uint32_t town, int player, float value)
{
	g_townBelief[town][player] = std::clamp(value, 0.0f, 1.0f);
	UpdateTownOwner(town);
}

// A miracle impresses the towns whose people see it: its info.dat impressiveValue for each villager within sight,

// Reward::InterfaceTap: the chest opened - its sting and the chest's own sound, belief in the player for it
// (beliefValue) in its town, and what is in it: food or wood (foodValue / woodValue) left in a pile where it stood,
// or a miracle (its spell seed) to take; then it is gone.
void OpenReward(uint32_t chest)
{
	auto& registry = Locator::entitiesRegistry::value();
	auto found = g_rewards.find(chest);
	const auto entity = static_cast<entt::entity>(chest);
	if (found == g_rewards.end() || found->second.opened || !registry.Valid(entity) || !registry.AllOf<Transform>(entity))
	{
		return;
	}
	auto& reward = found->second;
	reward.opened = true;
	const auto at = registry.Get<Transform>(entity).position;
	// As the original's (Reward::InterfaceTap): heard as the interface's sounds, not from out on the land, where they were
	// too far off to hear (Mike, 2026-10-19).
	PlayBankSound(1, 173, nullptr); // G_ClickSignpost: the tap
	PlayBankSound(1, 40, nullptr);  // G_RewardSting
	PlayBankSound(1, 41, nullptr);  // G_OpenChest
	const auto& infos = Locator::infoConstants::value().reward;
	if (static_cast<size_t>(reward.type) >= infos.size())
	{
		registry.Destroy(entity);
		return;
	}
	const auto& info = infos.at(static_cast<size_t>(reward.type));
	// Its town: the one it was made for, else the nearest.
	uint32_t town = reward.town;
	if (town == 0)
	{
		float best = std::numeric_limits<float>::max();
		registry.Each<const openblack::ecs::components::Town, const Transform>(
		    [&](entt::entity t, const openblack::ecs::components::Town&, const Transform& where) {
			    const float d = glm::distance(glm::vec2(where.position.x, where.position.z), glm::vec2(at.x, at.z));
			    if (d < best)
			    {
				    best = d;
				    town = static_cast<uint32_t>(t);
			    }
		    });
	}
	if (town != 0 && info.beliefValue != 0.0f)
	{
		SetTownBelief(town, 1, TownBelief(town, 1) + info.beliefValue);
	}
	std::string what = "nothing";
	const auto ground = glm::vec3(at.x, Locator::terrainSystem::value().GetHeightAt(glm::vec2(at.x, at.z)), at.z);
	if (info.foodValue > 0.0f)
	{
		openblack::ecs::archetypes::PotArchetype::Create(ground, 0.0f, PotInfo::MagicFood, static_cast<int32_t>(info.foodValue));
		what = fmt::format("{:.0f} food", info.foodValue);
	}
	else if (info.woodValue > 0)
	{
		openblack::ecs::archetypes::PotArchetype::Create(ground, 0.0f, PotInfo::MagicWood, static_cast<int32_t>(info.woodValue));
		what = fmt::format("{} wood", info.woodValue);
	}
	else if (info.seed != SpellSeedType::None)
	{
		CreateScriptObject(ObjectType::OneShotSpell, static_cast<uint32_t>(info.seed), ground + glm::vec3(0.0f, 3.0f, 0.0f), 0.0f, 0.0f,
		                   0.0f, 0.0f, 1.0f);
		what = fmt::format("a miracle (spell seed {})", static_cast<int>(info.seed));
	}
	else
	{
		what = fmt::format("something not in yet (power-up {}, scaffold {})", static_cast<int>(info.powerUp), info.scaffold);
	}
	SPDLOG_LOGGER_INFO(spdlog::get("game"), "Reward chest {} (reward {}) opened: {}; belief +{:.3f} in town {}", chest, reward.type, what,
	                   info.beliefValue, town);
	// Its lid opens (coffre.anm), it stays open a while, then it goes (UpdateScriptFrame).
	reward.openFor = 0.0f;
}
// scaled down for a town bigger than populationForUnmodifiedBelief, by the land's beliefSpeedScaleStory.
void ImpressTowns(MagicType magic, const glm::vec3& at, int player)
{
	using namespace openblack::ecs::components;
	auto& registry = Locator::entitiesRegistry::value();
	const auto& info = Locator::infoConstants::value();
	const auto index = static_cast<size_t>(magic);
	if (index >= info.magicEffect.size())
	{
		return;
	}
	const float impressive = info.magicEffect.at(index).impressiveValue;
	constexpr float k_Sight = 100.0f;
	const float landScale = info.town.beliefSpeedScaleStory.at(1); // Land 1
	registry.Each<const Town, const Transform>([&](entt::entity townEntity, const Town&, const Transform& townAt) {
		const float townDistance = glm::distance(glm::vec2(townAt.position.x, townAt.position.z), glm::vec2(at.x, at.z));
		if (townDistance > 2.0f * k_Sight)
		{
			return;
		}
		int witnesses = 0;
		int population = 0;
		registry.Each<const Villager, const Transform>([&](entt::entity, const Villager&, const Transform& where) {
			const auto xz = glm::vec2(where.position.x, where.position.z);
			if (glm::distance(xz, glm::vec2(townAt.position.x, townAt.position.z)) < k_Sight)
			{
				++population;
				if (glm::distance(xz, glm::vec2(at.x, at.z)) < k_Sight)
				{
					++witnesses;
				}
			}
		});
		if (witnesses == 0)
		{
			return;
		}
		const float sizeScale = std::min(1.0f, info.town.populationForUnmodifiedBelief / static_cast<float>(std::max(population, 1)));
		const float gain = impressive * static_cast<float>(witnesses) * sizeScale * landScale;
		const auto town = static_cast<uint32_t>(townEntity);
		const float before = TownBelief(town, player);
		SetTownBelief(town, player, before + gain);
		// Followers: belief is the share of the town that believes in you.
		const int followersBefore = static_cast<int>(std::round(before * static_cast<float>(population)));
		const int followersNow = static_cast<int>(std::round(TownBelief(town, player) * static_cast<float>(population)));
		if (followersNow > followersBefore)
		{
			g_followerPopups.push_back({townAt.position + glm::vec3(0.0f, 12.0f, 0.0f), followersNow - followersBefore,
			                            std::chrono::steady_clock::now() + std::chrono::seconds(4)});
		}
		SPDLOG_LOGGER_INFO(spdlog::get("game"), "Town {}: +{} followers, {} of {} (the town is won past {:.0f})", town,
		                   followersNow - followersBefore, followersNow, population,
		                   TownBelief(town, 0) * static_cast<float>(population));
		SPDLOG_LOGGER_INFO(spdlog::get("game"),
		                   "Town {} impressed by miracle {} ({:.4f} x {} of {} people x {:.2f} x {:.1f}): belief in player {} {:.3f} -> {:.3f}, owner {}",
		                   town, static_cast<int>(magic), impressive, witnesses, population, sizeScale, landScale, player, before,
		                   TownBelief(town, player), g_townOwner.contains(town) ? g_townOwner[town] : 0);
	});
}

MagicType MagicOfSpellSeed(SpellSeedType seed)
{
	switch (seed)
	{
	case SpellSeedType::Storm: return MagicType::StormWindRain;
	case SpellSeedType::Nature: return MagicType::Forest;
	case SpellSeedType::Fire: return MagicType::Fireball;
	case SpellSeedType::Food: return MagicType::Food;
	case SpellSeedType::Shield: return MagicType::Shield;
	case SpellSeedType::PhysicalShield: return MagicType::PhysicalShield;
	case SpellSeedType::LightningBolt: return MagicType::LightningBolt;
	case SpellSeedType::Heal: return MagicType::Heal;
	case SpellSeedType::Wood: return MagicType::Wood;
	case SpellSeedType::Water: return MagicType::Water;
	case SpellSeedType::FlockFlying: return MagicType::FlockFlying;
	case SpellSeedType::FlockGround: return MagicType::FlockGround;
	default: return MagicType::None;
	}
}

// The pours being cast (food, wood): closed down when their hand pour is over.
std::vector<openblack::psys::Handle> g_pours;

// Pot::AddResourceToPos: food or wood landed at `at` - into the store it fell on, a pile of it already there, else a
// new pile.
void AddMiracleResource(bool food, const glm::vec3& at, float amount)
{
	using namespace openblack::ecs::components;
	auto& registry = Locator::entitiesRegistry::value();
	constexpr float k_StoreReach = 20.0f; // a storage pit's footprint
	constexpr float k_PileReach = 5.0f;   // MapCoords::IsCloseToEqual: the same spot
	const auto resourceType = static_cast<int32_t>(food ? ResourceType::Food : ResourceType::Wood);
	entt::entity pit = entt::null;
	float nearest = k_StoreReach;
	registry.Each<const StoragePit, const Transform>([&](entt::entity entity, const StoragePit&, const Transform& transform) {
		const float d = glm::distance(glm::vec2(transform.position.x, transform.position.z), glm::vec2(at.x, at.z));
		if (d < nearest)
		{
			nearest = d;
			pit = entity;
		}
	});
	if (pit != entt::null && ResourcePot(pit, resourceType) != nullptr)
	{
		ChangeContainerResource(static_cast<uint32_t>(pit), resourceType, amount);
		return;
	}
	const auto kind = food ? PotInfo::MagicFood : PotInfo::MagicWood;
	Pot* pile = nullptr;
	float nearestPile = k_PileReach;
	registry.Each<Pot, const Transform>([&](entt::entity, Pot& pot, const Transform& transform) {
		const float d = glm::distance(glm::vec2(transform.position.x, transform.position.z), glm::vec2(at.x, at.z));
		if (pot.type == kind && d < nearestPile && pot.amount < pot.maxAmount)
		{
			nearestPile = d;
			pile = &pot;
		}
	});
	if (pile != nullptr)
	{
		pile->amount = static_cast<uint16_t>(std::min<float>(pile->amount + amount, pile->maxAmount));
		return;
	}
	const glm::vec3 ground(at.x, Locator::terrainSystem::value().GetHeightAt(glm::vec2(at.x, at.z)), at.z);
	PotArchetype::Create(ground, 0.0f, kind, static_cast<int32_t>(amount));
}

// A miracle let go of from the hand happens where it lands. So far food and wood (into the storage pit they fall
// by, or a pile on the ground) and heal (the people round about); the rest only log.
void CastMiracle(int spellSeed, const glm::vec3& at)
{
	using namespace openblack::ecs::components;
	auto& registry = Locator::entitiesRegistry::value();
	const auto& info = Locator::infoConstants::value();
	const auto seed = static_cast<SpellSeedType>(spellSeed);
	const glm::vec3 ground(at.x, Locator::terrainSystem::value().GetHeightAt(glm::vec2(at.x, at.z)), at.z);
	ImpressTowns(MagicOfSpellSeed(seed), ground, 1); // the hand's player: you
	if (seed == SpellSeedType::Food || seed == SpellSeedType::Wood)
	{
		// As the original's (SpellResource): poured from the hand - its effect, SF_Food or SF_Wood (the magic info's
		// particle type), the hand raised and tipped as it pours - and each piece that lands is the info's amount,
		// the first more (resourceAmountFirstEvent, then resourceAmountPerEvent), into whatever is under it
		// (SpellResource::SpellEvent, Pot::AddResourceToPos): the store, a pile already there, else a new pile. Not in
		// the sea.
		const bool isFood = seed == SpellSeedType::Food;
		const auto& resourceInfo = isFood ? static_cast<const GMagicResourceInfo&>(info.magicFood.at(0))
		                                  : static_cast<const GMagicResourceInfo&>(info.magicWood.at(0));
		openblack::psys::Start start;
		start.type = static_cast<openblack::psys::Type>(resourceInfo.particleType);
		start.origin = ground;
		start.humanCasting = true;
		const auto first = std::make_shared<bool>(true);
		const auto firstAmount = static_cast<float>(resourceInfo.resourceAmountFirstEvent);
		const auto perAmount = static_cast<float>(resourceInfo.resourceAmountPerEvent);
		start.onEvent = [isFood, first, firstAmount, perAmount](const openblack::psys::Event& event) {
			if (event.type == openblack::psys::EventType::Spell)
			{
				return true;
			}
			const float amount = *first ? firstAmount : perAmount;
			*first = false;
			if (amount > 0.0f && !IsWaterAt(glm::vec2(event.position.x, event.position.z)))
			{
				AddMiracleResource(isFood, event.position, amount);
			}
			return true;
		};
		if (const auto system = openblack::psys::Create(start); system != 0)
		{
			g_pours.push_back(system);
			SPDLOG_LOGGER_INFO(spdlog::get("game"), "Miracle {}: poured (SF_{}, {:.0f} for the first to land, {:.0f} each after)",
			                   isFood ? "food" : "wood", isFood ? "Food" : "Wood", firstAmount, perAmount);
			return;
		}
		const bool food = seed == SpellSeedType::Food;
		const auto& resource = food ? static_cast<const GMagicResourceInfo&>(info.magicFood.at(0))
		                            : static_cast<const GMagicResourceInfo&>(info.magicWood.at(0));
		const float amount = resource.resourceAmountFirstEvent > 0 ? static_cast<float>(resource.resourceAmountFirstEvent) : 1000.0f;
		constexpr float k_StoreReach = 50.0f;
		entt::entity pit = entt::null;
		float nearest = k_StoreReach;
		registry.Each<const StoragePit, const Transform>([&](entt::entity entity, const StoragePit&, const Transform& transform) {
			const float d = glm::distance(glm::vec2(transform.position.x, transform.position.z), glm::vec2(ground.x, ground.z));
			if (d < nearest)
			{
				nearest = d;
				pit = entity;
			}
		});
		const auto resourceType = static_cast<int32_t>(food ? ResourceType::Food : ResourceType::Wood);
		if (pit != entt::null && ResourcePot(pit, resourceType) != nullptr)
		{
			ChangeContainerResource(static_cast<uint32_t>(pit), resourceType, amount);
			SPDLOG_LOGGER_INFO(spdlog::get("game"), "Miracle {}: {:.0f} into storage pit {} ({:.0f} away)", food ? "food" : "wood",
			                   amount, static_cast<uint32_t>(pit), nearest);
		}
		else
		{
			PotArchetype::Create(ground, 0.0f, food ? PotInfo::MagicFood : PotInfo::MagicWood, static_cast<int32_t>(amount));
			SPDLOG_LOGGER_INFO(spdlog::get("game"), "Miracle {}: a pile of {:.0f} at ({:.1f}, {:.1f}, {:.1f})", food ? "food" : "wood",
			                   amount, ground.x, ground.y, ground.z);
		}
		return;
	}
	if (seed == SpellSeedType::Heal)
	{
		constexpr float k_HealRadius = 30.0f;
		int healed = 0;
		registry.Each<Villager, const Transform>([&](entt::entity, Villager& villager, const Transform& transform) {
			if (glm::distance(glm::vec2(transform.position.x, transform.position.z), glm::vec2(ground.x, ground.z)) < k_HealRadius)
			{
				villager.life = 1.0f;
				++healed;
			}
		});
		SPDLOG_LOGGER_INFO(spdlog::get("game"), "Miracle heal: {} people healed", healed);
		return;
	}
	SPDLOG_LOGGER_WARN(spdlog::get("game"), "Miracle (spell seed {}) at ({:.1f}, {:.1f}): not supported yet", spellSeed, ground.x, ground.z);
}

// The sphere a MagicType (the scripts' and info.dat's numbering) comes in.
int SpellSeedOfMagic(int magic)
{
	switch (static_cast<MagicType>(magic))
	{
	case MagicType::Fireball:
	case MagicType::FireballPowerUpOne:
	case MagicType::FireballPowerUpTwo: return static_cast<int>(SpellSeedType::Fire);
	case MagicType::LightningBolt:
	case MagicType::LightningBoltPowerUpOne:
	case MagicType::LightningBoltPowerUpTwo: return static_cast<int>(SpellSeedType::LightningBolt);
	case MagicType::Heal:
	case MagicType::HealPowerUpOne: return static_cast<int>(SpellSeedType::Heal);
	case MagicType::Forest: return static_cast<int>(SpellSeedType::Nature);
	case MagicType::Food:
	case MagicType::FoodPowerUpOne: return static_cast<int>(SpellSeedType::Food);
	case MagicType::StormWindRain:
	case MagicType::StormWindRainLightning:
	case MagicType::Tornado: return static_cast<int>(SpellSeedType::Storm);
	case MagicType::Shield: return static_cast<int>(SpellSeedType::Shield);
	case MagicType::PhysicalShield: return static_cast<int>(SpellSeedType::PhysicalShield);
	case MagicType::Wood: return static_cast<int>(SpellSeedType::Wood);
	case MagicType::Water:
	case MagicType::WaterPowerUpOne: return static_cast<int>(SpellSeedType::Water);
	case MagicType::FlockFlying: return static_cast<int>(SpellSeedType::FlockFlying);
	case MagicType::FlockGround: return static_cast<int>(SpellSeedType::FlockGround);
	default:
		// The creature spells, 26 freeze on, are their spheres' 12 on.
		if (magic >= static_cast<int>(MagicType::CreatureSpellFreeze) && magic <= static_cast<int>(MagicType::CreatureSpellItchy))
		{
			return static_cast<int>(SpellSeedType::CreatureSpellFreeze) + magic - static_cast<int>(MagicType::CreatureSpellFreeze);
		}
		return -1;
	}
}

// Miracle dispensers (SET_MAGIC_PROPERTIES on one): each keeps a sphere of its miracle floating over it, and makes
// another a while after it is used.
struct Dispenser
{
	int seed = -1;
	uint32_t sphere = 0;
	std::chrono::steady_clock::time_point readyAt {};
};
std::map<uint32_t, Dispenser> g_dispensers;
constexpr auto k_DispenserRecharge = std::chrono::seconds(30); // the original's not found yet

void UpdateDispensers()
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto now = ScriptNow();
	for (auto& [id, dispenser] : g_dispensers)
	{
		const auto entity = static_cast<entt::entity>(id);
		if (dispenser.seed < 0 || !registry.Valid(entity) || !registry.AllOf<Transform>(entity))
		{
			continue;
		}
		// Its sphere resting on its top (Mike, 2026-10-19: at 10 over it, it hung about a sphere's height too high): the
		// top of its model, and the bubble's radius above that.
		const auto& at = registry.Get<Transform>(entity);
		float top = 10.0f - 2.0f * k_BubbleRadius;
		if (const auto* mesh = registry.TryGet<openblack::ecs::components::Mesh>(entity);
		    mesh != nullptr && Locator::resources::value().GetMeshes().Contains(mesh->id))
		{
			const auto box = Locator::resources::value().GetMeshes().Handle(mesh->id)->GetBoundingBox();
			top = (box.Center().y + 0.5f * box.Size().y) * at.scale.y;
		}
		const auto over = at.position + glm::vec3(0.0f, top + k_BubbleRadius, 0.0f);
		static std::set<uint32_t> s_reported;
		if (s_reported.insert(id).second)
		{
			SPDLOG_LOGGER_INFO(spdlog::get("game"), "Dispenser {}: its top {:.2f} above it, so its sphere's centre {:.2f}", id, top,
			                   top + k_BubbleRadius);
		}
		if (dispenser.sphere != 0)
		{
			const auto sphere = static_cast<entt::entity>(dispenser.sphere);
			const bool onPedestal = g_oneShotSpells.contains(dispenser.sphere) && dispenser.sphere != g_heldObject &&
			                        registry.Valid(sphere) && registry.AllOf<Transform>(sphere) &&
			                        glm::distance(registry.Get<Transform>(sphere).position, over) < 3.0f;
			if (onPedestal)
			{
				continue;
			}
			SPDLOG_LOGGER_INFO(spdlog::get("game"), "Dispenser {}: sphere {} taken off, another in {} s", id, dispenser.sphere,
			                   k_DispenserRecharge.count());
			dispenser.sphere = 0;
			dispenser.readyAt = now + k_DispenserRecharge;
		}
		if (now < dispenser.readyAt)
		{
			continue;
		}
		const auto sphere = CreateScriptObject(ObjectType::OneShotSpell, static_cast<uint32_t>(dispenser.seed), over, 0.0f, 0.0f,
		                                       0.0f, 0.0f, 1.0f);
		dispenser.sphere = static_cast<uint32_t>(sphere);
		SPDLOG_LOGGER_INFO(spdlog::get("game"), "Dispenser {} made a sphere ({}) of spell seed {}", id, dispenser.sphere, dispenser.seed);
	}
}

// A spell seed's miracle's info (its magic type's), for its effects.
const GMagicInfo* MagicInfoOfSeed(int spellSeed)
{
	const auto& info = Locator::infoConstants::value();
	switch (static_cast<SpellSeedType>(spellSeed))
	{
	case SpellSeedType::Food: return &info.magicFood.at(0);
	case SpellSeedType::Wood: return &info.magicWood.at(0);
	case SpellSeedType::Water: return &info.magicWater.at(0);
	case SpellSeedType::Heal: return &info.magicHeal.at(0);
	case SpellSeedType::Shield: return &info.magicShield.at(0);
	case SpellSeedType::PhysicalShield: return &info.magicShield.at(1);
	case SpellSeedType::Nature: return &info.magicForest.at(0);
	case SpellSeedType::Storm: return &info.magicStormAndTornado.at(0);
	case SpellSeedType::FlockFlying: return &info.magicFlockFlying.at(0);
	case SpellSeedType::FlockGround: return &info.magicFlockGround.at(0);
	default: return nullptr;
	}
}

// The miracle in the hand's own effect (SpellSeed's in-hand drawing: its magic info's particleTypeInHand - water's
// SF_WaterInHand, heal's chakra), at the hand.
openblack::psys::Handle g_inHandEffect = 0;

void EndInHandEffect()
{
	if (g_inHandEffect != 0)
	{
		openblack::psys::CloseDown(g_inHandEffect);
		g_inHandEffect = 0;
	}
}

// OneOffSpellSeed::InterfaceTap: the bubble taken - it pops (G_SpellBubblePop_04, the interface's sound 0x6d) and the
// miracle is in the hand, showing its own effect there.
void TakeSeedIntoHand(int spellSeed)
{
	PlayBankSound(1, 109, nullptr);
	EndInHandEffect();
	if (const auto* magic = MagicInfoOfSeed(spellSeed); magic != nullptr && static_cast<int>(magic->particleTypeInHand) != 0)
	{
		openblack::psys::Start start;
		start.type = static_cast<openblack::psys::Type>(magic->particleTypeInHand);
		start.humanCasting = true;
		start.magnitude = 1.0f; // the hand's render scale
		g_inHandEffect = openblack::psys::Create(start);
		SPDLOG_LOGGER_INFO(spdlog::get("game"), "Spell seed {} in the hand: effect {} ({})", spellSeed,
		                   static_cast<int>(magic->particleTypeInHand), g_inHandEffect != 0 ? "running" : "no file");
	}
}

// The held sphere's miracle happens: a creature spell on your creature, anything else at the point.
void UseHeldSphere(const glm::vec3& at)
{
	EndInHandEffect();
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = static_cast<entt::entity>(g_heldObject);
	const auto spell = g_oneShotSpells.find(g_heldObject);
	if (spell == g_oneShotSpells.end())
	{
		return;
	}
	const int seed = spell->second;
	g_oneShotSpells.erase(spell);
	g_sparklers.erase(g_heldObject);
	ForgetObject(g_heldObject);
	g_heldObject = 0;
	if (registry.Valid(entity))
	{
		registry.Destroy(entity);
	}
	if (seed >= static_cast<int>(SpellSeedType::CreatureSpellFreeze))
	{
		const auto creature = PlayersCreature();
		SPDLOG_LOGGER_INFO(spdlog::get("game"), "Creature spell (spell seed {}) cast on your creature {} (no effect yet)", seed, creature);
		return;
	}
	CastMiracle(seed, at);
}
} // namespace

// A click (a tap, ZR) with a sphere under the hand takes it; with a sphere in the hand it is cast where clicked.
bool MiracleClick(const glm::vec3& ground)
{
	if (g_heldObject != 0 && g_oneShotSpells.contains(g_heldObject))
	{
		UseHeldSphere(ground);
		return true;
	}
	if (g_heldObject == 0 && g_objectUnderHand != 0 && g_oneShotSpells.contains(g_objectUnderHand))
	{
		PutInHand(g_objectUnderHand);
		TakeSeedIntoHand(g_oneShotSpells[g_heldObject]);
		SPDLOG_LOGGER_INFO(spdlog::get("game"), "Sphere {} (spell seed {}) taken: click where it should happen", g_heldObject,
		                   g_oneShotSpells[g_heldObject]);
		return true;
	}
	return false;
}

void ObjectClick(uint32_t object)
{
	// Not when the press already clicked something (a scroll, a creature, a person): a rock along the same ray took
	// the click, and Land 1's creature choice never saw the creature clicked.
	if (g_clickedThing != 0)
	{
		return;
	}
	auto& registry = Locator::entitiesRegistry::value();
	// A tall stone pointed at: the ray passes it and meets the land behind, too far for the hand's search - so the
	// stone whose box the pointer's ray enters first.
	{
		float nearest = std::numeric_limits<float>::max();
		registry.Each<const openblack::ecs::components::MobileStatic, const Transform>(
		    [&](entt::entity stone, const openblack::ecs::components::MobileStatic&, const Transform&) {
			    if (const auto hit = CreatureRayHit(stone, g_handRayOrigin, g_handRayDirection); hit && hit->first < nearest)
			    {
				    nearest = hit->first;
				    object = static_cast<uint32_t>(stone);
			    }
		    });
	}
	const auto entity = static_cast<entt::entity>(object);
	if (object == 0 || !registry.Valid(entity) || !registry.AllOf<openblack::ecs::components::MobileStatic, Transform>(entity))
	{
		return;
	}
	if (const auto placed = g_scriptPlaced.find(object);
	    placed != g_scriptPlaced.end() && glm::distance(placed->second, registry.Get<Transform>(entity).position) < 0.1f)
	{
		return; // set in place by a script: it plays on its turn only
	}
	NotifyGameThingClicked(object);
}

std::vector<std::pair<glm::vec3, int>> FollowerPopups()
{
	const auto now = std::chrono::steady_clock::now();
	std::erase_if(g_followerPopups, [now](const auto& popup) { return popup.until < now; });
	std::vector<std::pair<glm::vec3, int>> shown;
	for (const auto& popup : g_followerPopups)
	{
		shown.emplace_back(popup.at, popup.count);
	}
	return shown;
}

void GetResource() // 133 GET_RESOURCE
{
	const auto container = Pop().uintVal;
	const auto resource = Pop().intVal;
	Pushf(ContainerResource(container, resource));
}

void AddResource() // 134 ADD_RESOURCE
{
	const auto container = Pop().uintVal;
	const auto quantity = Popf();
	const auto resource = Pop().intVal;
	Pushf(ChangeContainerResource(container, resource, quantity));
}

void RemoveResource() // 135 REMOVE_RESOURCE
{
	const auto container = Pop().uintVal;
	const auto quantity = Popf();
	const auto resource = Pop().intVal;
	Pushf(-ChangeContainerResource(container, resource, -quantity));
}

void GetTargetRelativePos() // 136 GET_TARGET_RELATIVE_POS
{
	[[maybe_unused]] const auto angle = Popf();
	[[maybe_unused]] const auto distance = Popf();
	[[maybe_unused]] const auto to = PopVec();
	[[maybe_unused]] const auto from = PopVec();
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushv(0.0f); // x
	Pushv(0.0f); // y
	Pushv(0.0f); // z
}

void StopPointing() // 137 STOP_POINTING
{
	[[maybe_unused]] const auto spirit = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void StopLooking() // 138 STOP_LOOKING
{
	[[maybe_unused]] const auto spirit = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void LookAtPosition() // 139 LOOK_AT_POSITION
{
	[[maybe_unused]] const auto position = PopVec();
	[[maybe_unused]] const auto spirit = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void PlaySpiritAnim() // 140 PLAY_SPIRIT_ANIM
{
	[[maybe_unused]] const auto unk4 = Pop().intVal;
	[[maybe_unused]] const auto unk3 = Pop().intVal;
	[[maybe_unused]] const auto unk2 = Pop().intVal;
	[[maybe_unused]] const auto unk1 = Pop().intVal;
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "PLAY_SPIRIT_ANIM {} {} {} {} {}", unk0, unk1, unk2, unk3, unk4);
}

void CallInNotNear() // 141 CALL_IN_NOT_NEAR
{
	[[maybe_unused]] const auto excludingScripted = static_cast<bool>(Pop().intVal);
	[[maybe_unused]] const auto radius = Popf();
	[[maybe_unused]] const auto pos = PopVec();
	[[maybe_unused]] const auto container = Pop().uintVal;
	[[maybe_unused]] const auto subtype = Pop().intVal;
	[[maybe_unused]] const auto type = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pusho(0);
}

void SetCameraZone() // 142 SET_CAMERA_ZONE
{
	[[maybe_unused]] const auto filename = PopString();
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void GetObjectState() // 143 GET_OBJECT_STATE
{
	[[maybe_unused]] const auto obj = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushi(0);
}

void RevealCountdownTimer() // 144 REVEAL_COUNTDOWN_TIMER
{
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void SetTimerTime() // 145 SET_TIMER_TIME
{
	const auto time = Popf();
	const auto timer = Pop().uintVal;
	g_timers[timer] = ScriptNow() +
	                  std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<float>(std::max(time, 0.0f)));
}

void CreateTimer() // 146 CREATE_TIMER
{
	const auto timeout = Popf();
	const auto timer = g_nextTimer++;
	g_timers[timer] = ScriptNow() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
	                                                        std::chrono::duration<float>(std::max(timeout, 0.0f)));
	Pusho(timer);
}

void GetTimerTimeRemaining() // 147 GET_TIMER_TIME_REMAINING
{
	const auto timer = Pop().uintVal;
	const auto it = g_timers.find(timer);
	const float remaining =
	    it == g_timers.end() ? 0.0f : std::chrono::duration<float>(it->second - ScriptNow()).count();
	Pushf(std::max(remaining, 0.0f));
}

void GetTimerTimeSinceSet() // 148 GET_TIMER_TIME_SINCE_SET
{
	[[maybe_unused]] const auto timer = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushf(0.0f);
}

void MoveMusic() // 149 MOVE_MUSIC
{
	[[maybe_unused]] const auto unk1 = Pop().intVal;
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void GetInclusionDistance() // 150 GET_INCLUSION_DISTANCE
{
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushf(0.0f);
}

void GetLandHeight() // 151 GET_LAND_HEIGHT
{
	const auto position = PopVec();

	const auto& island = Locator::terrainSystem::value();
	const auto elevation = island.GetHeightAt(glm::vec2(position.x, position.z));

	Pushf(elevation);
}

void LoadMap() // 152 LOAD_MAP
{
	// const auto path = PopString();

	// auto& fileSystem = Locator::filesystem::value();
	// auto mapPath = fileSystem.GetGamePath() / path;
	// TODO(Daniels118): LoadMap(mapPath);
}

void StopAllScriptsExcluding() // 153 STOP_ALL_SCRIPTS_EXCLUDING
{
	const auto scriptNames = PopString();

	const auto names = GetUniqueWords(scriptNames);
	auto& lhvm = Locator::vm::value();
	lhvm.StopScripts([&names](const std::string& name, [[maybe_unused]] const std::string& filename) -> bool {
		return !names.contains(name);
	});
}

void StopAllScriptsInFilesExcluding() // 154 STOP_ALL_SCRIPTS_IN_FILES_EXCLUDING
{
	const auto sourceFilenames = PopString();

	const auto filenames = GetUniqueWords(sourceFilenames);
	auto& lhvm = Locator::vm::value();
	lhvm.StopScripts([&filenames]([[maybe_unused]] const std::string& name, const std::string& filename) -> bool {
		return !filenames.contains(filename);
	});
}

void StopScript() // 155 STOP_SCRIPT
{
	const auto scriptName = PopString();
	auto& lhvm = Locator::vm::value();
	lhvm.StopScripts([&scriptName](const std::string& name, [[maybe_unused]] const std::string& filename) -> bool {
		return name == scriptName;
	});
}

void ClearClickedObject() // 156 CLEAR_CLICKED_OBJECT
{
	g_clickedThing = 0;
}

void ClearClickedPosition() // 157 CLEAR_CLICKED_POSITION
{
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void PositionClicked() // 158 POSITION_CLICKED
{
	[[maybe_unused]] const auto unk3 = Pop().intVal;
	[[maybe_unused]] const auto unk2 = Pop().intVal;
	[[maybe_unused]] const auto unk1 = Pop().intVal;
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushb(false);
}

void ReleaseFromScript() // 159 RELEASE_FROM_SCRIPT
{
	const auto obj = Pop().uintVal;
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = static_cast<entt::entity>(obj);
	if (obj != 0 && registry.Valid(entity))
	{
		registry.Remove<ScriptMove>(entity); // back to the villager AI
		g_releasedFromScript.insert(obj);
		// And back to its own animations: one the script left looping (a negative count, for ever) went on after
		// the scene, Sable doing her cutscene's animation as a villager (Mike, 2026-10-18).
		if (const auto playing = g_scriptAnimations.find(obj); playing != g_scriptAnimations.end())
		{
			SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "RELEASE_FROM_SCRIPT {}: its script animation {} ends", obj,
			                   playing->second.animation);
			g_scriptAnimations.erase(playing);
		}
		// A creature: the script's standing order ends too (it keeps whatever it holds).
		if (g_creatureOrders.erase(obj) != 0)
		{
			SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "RELEASE_FROM_SCRIPT creature {}: its order ends", obj);
		}
		g_holdDone.erase(obj);
	}
}

void GetObjectHandIsOver() // 160 GET_OBJECT_HAND_IS_OVER
{
	Pusho(g_objectUnderHand);
}

void IdPoisonedSize() // 161 ID_POISONED_SIZE
{
	[[maybe_unused]] const auto container = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushf(0.0f);
}

void IsPoisoned() // 162 IS_POISONED
{
	[[maybe_unused]] const auto obj = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushb(false);
}

void CallPoisonedIn() // 163 CALL_POISONED_IN
{
	[[maybe_unused]] const auto excludingScripted = static_cast<bool>(Pop().intVal);
	[[maybe_unused]] const auto container = Pop().uintVal;
	[[maybe_unused]] const auto subtype = Pop().intVal;
	[[maybe_unused]] const auto type = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pusho(0);
}

void CallNotPoisonedIn() // 164 CALL_NOT_POISONED_IN
{
	[[maybe_unused]] const auto excludingScripted = static_cast<bool>(Pop().intVal);
	[[maybe_unused]] const auto container = Pop().uintVal;
	[[maybe_unused]] const auto subtype = Pop().intVal;
	[[maybe_unused]] const auto type = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pusho(0);
}

void SpiritPlayed() // 165 SPIRIT_PLAYED
{
	[[maybe_unused]] const auto spirit = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushb(false);
}

void ClingSpirit() // 166 CLING_SPIRIT
{
	[[maybe_unused]] const auto yPercent = Popf();
	[[maybe_unused]] const auto xPercent = Popf();
	[[maybe_unused]] const auto spirit = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void FlySpirit() // 167 FLY_SPIRIT
{
	const auto yPercent = Popf();
	const auto xPercent = Popf();
	const auto spirit = Pop().intVal;
	helpspirits::FlyTo(AdvisorOf(spirit), xPercent, yPercent);
}

void SetIdMoveable() // 168 SET_ID_MOVEABLE
{
	const auto obj = Pop().uintVal;
	const auto moveable = static_cast<bool>(Pop().intVal);
	// The hand can't pick up what a script has made unpickupable or unmoveable.
	if (moveable)
	{
		g_notPickupable.erase(obj);
	}
	else
	{
		g_notPickupable.insert(obj);
	}
}

void SetIdPickupable() // 169 SET_ID_PICKUPABLE
{
	const auto obj = Pop().uintVal;
	const auto pickupable = static_cast<bool>(Pop().intVal);
	// The hand can't pick up what a script has made unpickupable or unmoveable.
	if (pickupable)
	{
		g_notPickupable.erase(obj);
		g_scriptPickupable.insert(obj);
	}
	else
	{
		g_notPickupable.insert(obj);
	}
}

void IsOnFire() // 170 IS_ON_FIRE
{
	[[maybe_unused]] const auto obj = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushb(false);
}

void IsFireNear() // 171 IS_FIRE_NEAR
{
	[[maybe_unused]] const auto radius = Popf();
	[[maybe_unused]] const auto position = PopVec();
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushb(false);
}

void StopScriptsInFiles() // 172 STOP_SCRIPTS_IN_FILES
{
	const auto sourceFilenames = PopString();

	const auto filenames = GetUniqueWords(sourceFilenames);
	auto& lhvm = Locator::vm::value();
	lhvm.StopScripts([&filenames]([[maybe_unused]] const std::string& name, const std::string& filename) -> bool {
		return filenames.contains(filename);
	});
}

void SetPoisoned() // 173 SET_POISONED
{
	[[maybe_unused]] const auto obj = Pop().uintVal;
	[[maybe_unused]] const auto poisoned = static_cast<bool>(Pop().intVal);
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void SetTemperature() // 174 SET_TEMPERATURE
{
	[[maybe_unused]] const auto temperature = Popf();
	[[maybe_unused]] const auto obj = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void SetOnFire() // 175 SET_ON_FIRE
{
	[[maybe_unused]] const auto burnSpeed = Popf();
	[[maybe_unused]] const auto object = Pop().uintVal;
	[[maybe_unused]] const auto enable = static_cast<bool>(Pop().intVal);
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void SetTarget() // 176 SET_TARGET
{
	[[maybe_unused]] const auto time = Popf();
	[[maybe_unused]] const auto position = PopVec();
	[[maybe_unused]] const auto obj = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

// Tracks ("Track<N>" in camera.edt, where runblack.exe's WALK_PATH looks them up) are camera paths: a
// camera spline and a focus spline, each n keyframes with two Bezier control points between keys. A
// track of n keys is 76 + 88n bytes: camera keys at +40, focus keys at +76+44n, each followed by its
// 2(n-1) control points. Livings walk the focus curve, which lies on the ground.
// A camera path being flown (RUN_CAMERA_PATH): both curves sampled the same way, so sample i of one goes with
// sample i of the other, and the camera curve's speed (the track's per-key speeds, world units a second) at each.
struct CameraPathPlay
{
	int32_t track = 0;
	std::vector<glm::vec3> origins;
	std::vector<glm::vec3> focuses;
	std::vector<float> lengths; // along the camera curve
	std::vector<float> speeds;
	float at = 0.0f;
};
std::optional<CameraPathPlay> g_cameraPath;
bool CameraPathRunning()
{
	return g_cameraPath.has_value();
}

bool LoadTrack(int32_t track, CameraPathPlay& play)
{
	auto& fileSystem = Locator::filesystem::value();
	std::vector<uint8_t> data;
	try
	{
		data = fileSystem.ReadAll(fileSystem.GetPath<openblack::filesystem::Path::Data>() / "camera.edt");
	}
	catch (const std::exception& e)
	{
		SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "Could not read camera.edt: {}", e.what());
		return false;
	}
	const std::string key = "Track" + std::to_string(track);
	for (size_t i = 0; i + 36 < data.size(); ++i)
	{
		if (std::memcmp(&data[i], key.c_str(), key.size() + 1) != 0)
		{
			continue;
		}
		uint32_t length;
		std::memcpy(&length, &data[i + 32], sizeof(length));
		const uint8_t* body = &data[i + 36];
		uint32_t n;
		std::memcpy(&n, body + 8, sizeof(n));
		if (n < 2 || i + 36 + length > data.size() || length != 76 + 88 * n)
		{
			continue;
		}
		auto vec = [&](size_t at) {
			glm::vec3 v;
			std::memcpy(&v, body + at, sizeof(v));
			return v;
		};
		auto real = [&](size_t at) {
			float v;
			std::memcpy(&v, body + at, sizeof(v));
			return v;
		};
		// Each curve: a 36-byte head, n keys, 2(n-1) control points, 28 bytes, n-1 lengths, n speeds.
		const size_t cameraKeys = 40;
		const size_t focusKeys = 76 + 44 * n;
		const size_t speeds = cameraKeys + 12 * n + 24 * (n - 1) + 28 + 4 * (n - 1);
		auto point = [&](size_t keys, uint32_t k, float t) {
			const auto p0 = vec(keys + 12 * k);
			const auto c0 = vec(keys + 12 * n + 24 * k);
			const auto c1 = vec(keys + 12 * n + 24 * k + 12);
			const auto p1 = vec(keys + 12 * (k + 1));
			const float u = 1.0f - t;
			return u * u * u * p0 + 3.0f * u * u * t * c0 + 3.0f * u * t * t * c1 + t * t * t * p1;
		};
		constexpr int k_Samples = 24;
		for (uint32_t k = 0; k + 1 < n; ++k)
		{
			for (int j = (k == 0 ? 0 : 1); j <= k_Samples; ++j)
			{
				const float t = static_cast<float>(j) / k_Samples;
				play.origins.push_back(point(cameraKeys, k, t));
				play.focuses.push_back(point(focusKeys, k, t));
				play.speeds.push_back(glm::mix(real(speeds + 4 * k), real(speeds + 4 * (k + 1)), t));
			}
		}
		play.lengths.push_back(0.0f);
		for (size_t s = 1; s < play.origins.size(); ++s)
		{
			play.lengths.push_back(play.lengths.back() + glm::distance(play.origins[s - 1], play.origins[s]));
		}
		play.track = track;
		return true;
	}
	return false;
}

void StartCameraPath(int32_t track)
{
	CameraPathPlay play;
	if (!LoadTrack(track, play) || play.origins.size() < 2)
	{
		SPDLOG_LOGGER_WARN(spdlog::get("scripting"), "RUN_CAMERA_PATH {}: no such track in camera.edt", track);
		return;
	}
	auto& camera = Locator::camera::value();
	camera.SetScriptControl(true);
	camera.ScriptMoveOrigin(play.origins.front(), std::chrono::microseconds(0));
	camera.ScriptMoveFocus(play.focuses.front(), std::chrono::microseconds(0));
	camera.SetOrigin(play.origins.front());
	camera.SetFocus(play.focuses.front());
	SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "RUN_CAMERA_PATH {}: {:.0f} long, from ({:.0f}, {:.0f}, {:.0f}) to ({:.0f}, {:.0f}, {:.0f})", track,
	                   play.lengths.back(), play.origins.front().x, play.origins.front().y, play.origins.front().z,
	                   play.origins.back().x, play.origins.back().y, play.origins.back().z);
	g_cameraPath = std::move(play);
}

void EndCameraPath()
{
	if (g_cameraPath)
	{
		SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "RUN_CAMERA_PATH {}: ended by the script at {:.0f} of {:.0f}", g_cameraPath->track,
		                   g_cameraPath->at, g_cameraPath->lengths.back());
		g_cameraPath.reset();
	}
}

// The camera along its path, at the track's speed (never under 2 a second, as some tracks end on a speed of 0).
void UpdateCameraPath(float seconds)
{
	if (!g_cameraPath)
	{
		return;
	}
	auto& play = *g_cameraPath;
	const auto next = std::upper_bound(play.lengths.begin(), play.lengths.end(), play.at);
	const size_t i = std::clamp<size_t>(static_cast<size_t>(next - play.lengths.begin()), 1, play.lengths.size() - 1);
	const float span = play.lengths[i] - play.lengths[i - 1];
	const float t = span > 0.0f ? std::clamp((play.at - play.lengths[i - 1]) / span, 0.0f, 1.0f) : 1.0f;
	const float speed = std::max(glm::mix(play.speeds[i - 1], play.speeds[i], t), 2.0f);
	play.at = std::min(play.at + speed * seconds, play.lengths.back());
	auto& camera = Locator::camera::value();
	camera.SetOrigin(glm::mix(play.origins[i - 1], play.origins[i], t));
	camera.SetFocus(glm::mix(play.focuses[i - 1], play.focuses[i], t));
	if (play.at >= play.lengths.back())
	{
		camera.SetOrigin(play.origins.back());
		camera.SetFocus(play.focuses.back());
		SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "RUN_CAMERA_PATH {}: done", play.track);
		g_cameraPath.reset();
	}
}

// A camera.edt track's second curve, the one walkers follow (FollowUs's sharks are made on its first keys), sampled
// k_TrackSamples to a segment, with its keys' times and speeds: after its keys and control points, 28 bytes, then the
// times of keys 1 to n-1 in ms (track 21: 1810 ... 12885 over its 128.85) and n speeds in m/s.
constexpr int k_TrackSamples = 12;
struct TrackWay
{
	std::vector<glm::vec3> points;
	std::vector<float> keyTimes;
	std::vector<float> keySpeeds;
};

const TrackWay& LoadTrackWay(int32_t track)
{
	static std::unordered_map<int32_t, TrackWay> cache;
	if (const auto it = cache.find(track); it != cache.end())
	{
		return it->second;
	}
	auto& fileSystem = Locator::filesystem::value();
	TrackWay way;
	auto& curve = way.points;
	std::vector<uint8_t> data;
	try
	{
		data = fileSystem.ReadAll(fileSystem.GetPath<openblack::filesystem::Path::Data>() / "camera.edt");
	}
	catch (const std::exception& e)
	{
		SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "Could not read camera.edt: {}", e.what());
	}
	const std::string key = "Track" + std::to_string(track);
	for (size_t i = 0; i + 36 < data.size(); ++i)
	{
		if (std::memcmp(&data[i], key.c_str(), key.size() + 1) != 0)
		{
			continue;
		}
		uint32_t length;
		std::memcpy(&length, &data[i + 32], sizeof(length));
		const uint8_t* body = &data[i + 36];
		uint32_t n;
		std::memcpy(&n, body + 8, sizeof(n));
		if (n < 2 || i + 36 + length > data.size() || length != 76 + 88 * n)
		{
			continue;
		}
		auto vec = [&](size_t at) {
			glm::vec3 v;
			std::memcpy(&v, body + at, sizeof(v));
			return v;
		};
		const size_t keys = 76 + 44 * n;
		const size_t controls = keys + 12 * n;
		const size_t times = controls + 24 * (n - 1) + 28;
		const size_t speeds = times + 4 * (n - 1);
		auto real = [&](size_t at) {
			float v;
			std::memcpy(&v, body + at, sizeof(v));
			return v;
		};
		way.keyTimes.push_back(0.0f);
		for (uint32_t k = 0; k < n; ++k)
		{
			if (k > 0)
			{
				way.keyTimes.push_back(real(times + 4 * (k - 1)));
			}
			way.keySpeeds.push_back(real(speeds + 4 * k));
		}
		constexpr int k_Samples = k_TrackSamples;
		for (uint32_t k = 0; k + 1 < n; ++k)
		{
			const auto p0 = vec(keys + 12 * k);
			const auto c0 = vec(controls + 24 * k);
			const auto c1 = vec(controls + 24 * k + 12);
			const auto p1 = vec(keys + 12 * (k + 1));
			for (int j = (k == 0 ? 0 : 1); j <= k_Samples; ++j)
			{
				const float t = static_cast<float>(j) / k_Samples;
				const float u = 1.0f - t;
				curve.push_back(u * u * u * p0 + 3.0f * u * u * t * c0 + 3.0f * u * t * t * c1 + t * t * t * p1);
			}
		}
		break;
	}
	if (curve.empty())
	{
		SPDLOG_LOGGER_WARN(spdlog::get("scripting"), "Track {} not found in camera.edt", track);
	}
	return cache[track] = std::move(way);
}

std::vector<glm::vec3> LoadTrackGround(int32_t track)
{
	return LoadTrackWay(track).points;
}

// How far along a timed walk is `ms` into its track (LH3DWay::Running::GetPosAtTime): in the segment that time falls
// in, of length L and T seconds, from its first key's speed v0 at the steady change of speed a that ends it on time -
// v0 t + a t^2 / 2, a = 2 (L - v0 T) / T^2.
float TimedWalkDistance(const ScriptWalk& walk, float ms)
{
	const auto& times = walk.keyTimes;
	if (times.size() < 2 || walk.lengths.size() < (times.size() - 1) * k_TrackSamples + 1)
	{
		return 0.0f;
	}
	size_t k = 0;
	while (k + 2 < times.size() && ms >= times[k + 1])
	{
		++k;
	}
	const float from = walk.lengths[k * k_TrackSamples];
	const float length = walk.lengths[(k + 1) * k_TrackSamples] - from;
	const float duration = std::max((times[k + 1] - times[k]) * 0.001f, 1e-3f);
	const float t = std::clamp((ms - times[k]) * 0.001f, 0.0f, duration);
	const float v0 = walk.keySpeeds[k];
	const float a = 2.0f * (length - v0 * duration) / (duration * duration);
	return from + std::clamp(v0 * t + 0.5f * a * t * t, 0.0f, length);
}

void WalkPath() // 177 WALK_PATH
{
	const auto valTo = Popf();
	const auto valFrom = Popf();
	const auto track = Pop().intVal;
	const auto forward = static_cast<bool>(Pop().intVal);
	const auto object = Pop().uintVal;
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = static_cast<entt::entity>(object);
	auto curve = LoadTrackGround(track);
	if (object == 0 || !registry.Valid(entity) || !registry.AllOf<Transform>(entity) || curve.size() < 2)
	{
		return;
	}
	ScriptWalk walk;
	walk.lengths.push_back(0.0f);
	for (size_t i = 1; i < curve.size(); ++i)
	{
		walk.lengths.push_back(walk.lengths.back() + glm::distance(curve[i - 1], curve[i]));
	}
	walk.points = std::move(curve);
	const float total = walk.lengths.back();
	walk.at = std::clamp(forward ? valFrom : 1.0f - valFrom, 0.0f, 1.0f) * total;
	walk.end = std::clamp(forward ? valTo : 1.0f - valTo, 0.0f, 1.0f) * total;
	HoldForScript(entity, walk.points.back());
	walk.track = track;
	// A shark (the scripts' Whale, a MobileObject): GScript::SetWalkPath gives it MobileObject::SetupMoveAlongPath,
	// which starts the track's clock at valFrom of its length in time and stops it at valTo, forward only.
	if (const auto* mesh = registry.TryGet<openblack::ecs::components::Mesh>(entity);
	    mesh != nullptr && mesh->id == resources::HashIdentifier(MeshId::SharkBoned))
	{
		const auto& way = LoadTrackWay(track);
		if (way.keyTimes.size() >= 2 && walk.lengths.size() == (way.keyTimes.size() - 1) * k_TrackSamples + 1)
		{
			walk.keyTimes = way.keyTimes;
			walk.keySpeeds = way.keySpeeds;
			walk.timeMs = std::clamp(valFrom, 0.0f, 1.0f) * way.keyTimes.back();
			walk.endMs = std::clamp(valTo, 0.0f, 1.0f) * way.keyTimes.back();
			walk.at = TimedWalkDistance(walk, walk.timeMs);
			walk.end = TimedWalkDistance(walk, walk.endMs);
			SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "WALK_PATH {} along track {}: {:.1f} long, {:.1f} s", object, track, total,
			                   (walk.endMs - walk.timeMs) * 0.001f);
		}
	}
	g_walks[object] = std::move(walk);
}

namespace
{
// Put a script-moved living at a position on the ground, facing where it is heading, and keep the
// pathfinder's goal with it: the pathfinder puts any living standing at its goal back onto it (a final
// step), which would pull this one back to wherever it last stopped.
void PlaceMover(entt::entity entity, Transform& transform, glm::vec3 position, const glm::vec3& heading)
{
	auto& registry = Locator::entitiesRegistry::value();
	position.y = Locator::terrainSystem::value().GetHeightAt(glm::vec2(position.x, position.z));
	FaceTowards(transform, heading);
	transform.position = position;
	if (auto* wallHug = registry.TryGet<WallHug>(entity))
	{
		wallHug->goal = glm::vec2(position.x, position.z);
	}
	registry.Remove<MoveStateLinearTag, MoveStateOrbitTag, MoveStateExitCircleTag, MoveStateStepThroughTag,
	                MoveStateFinalStepTag, MoveStateArrivedTag, WallHugObjectReference>(entity);
	if (auto* move = registry.TryGet<ScriptMove>(entity))
	{
		move->target = position; // stands here when it stops
	}
}
} // namespace

namespace
{
// Animation clock: 10 keyframes a second - the animations' own timing (their header's duration at 0x20, in ms: 1000
// for the 10-frame walk, 4000 for a 40-frame dance; bw1-decomp's LH3DAnim::Duration). Not walks and runs, which play
// by the ground covered (below).
constexpr float k_AnimationFramesPerSecond = 10.0f;
std::unordered_map<uint32_t, glm::vec3> g_lastPositions;
struct VillagerGlide
{
	std::optional<glm::vec3> shown;
	glm::vec3 from {};
	glm::vec3 to {};
	float t = 1.0f;
};
std::unordered_map<uint32_t, VillagerGlide> g_villagerGlides;

// Each species' body animations, from its Data/CTR file, loaded the first time one is needed.
const openblack::morphposes::Poses& CreaturePoses(CreatureType species)
{
	static const std::map<CreatureType, const char*> k_Files = {
	    {CreatureType::GiantApe, "Bape.CBN"},      {CreatureType::BrownBear, "bbear.CBN"},
	    {CreatureType::Chimp, "bchimp.CBN"},       {CreatureType::Cow, "Bcow.CBN"},
	    {CreatureType::Gorilla, "bgorilla.CBN"},   {CreatureType::Ogre, "Bgreek.CBN"},
	    {CreatureType::Horse, "Bhorse.CBN"},       {CreatureType::Leopard, "bleopard.CBN"},
	    {CreatureType::Lion, "Blion.CBN"},         {CreatureType::Mandrill, "bmandrill.CBN"},
	    {CreatureType::PolarBear, "BPolarBear.CBN"}, {CreatureType::Rhino, "brhino.CBN"},
	    {CreatureType::Sheep, "bsheep.CBN"},       {CreatureType::Tiger, "btiger.CBN"},
	    {CreatureType::Tortoise, "btort.CBN"},     {CreatureType::Wolf, "bwolf.CBN"},
	    {CreatureType::Zebra, "bzebra.CBN"}};
	static std::map<CreatureType, openblack::morphposes::Poses> loaded;
	auto [it, added] = loaded.try_emplace(species);
	if (added)
	{
		if (const auto file = k_Files.find(species); file != k_Files.end())
		{
			const auto path = Locator::filesystem::value().GetPath<openblack::filesystem::Path::Data>() / "CTR" / file->second;
			openblack::morphposes::Load(path, "Creature", it->second);
		}
	}
	return it->second;
}

// The creature animations in Data/ctrspec27.txt's order, without the spec's one-letter prefix: a script's creature
// animation number (SET_SCRIPT_ULONG on a creature) is its place in this list - Land 1's glade asks for 69
// look_at_me, 74 pick_me, 72 friendly_wave, 55 happy.
const std::vector<std::string>& CreatureAnimationNames()
{
	static const auto names = [] {
		std::vector<std::string> list;
		// Through openblack's file system, which finds Data under the game path: a plain relative path found
		// nothing on the Switch, and every creature animation was skipped as unknown.
		auto& fileSystem = Locator::filesystem::value();
		std::vector<uint8_t> bytes;
		try
		{
			bytes = fileSystem.ReadAll(fileSystem.GetPath<openblack::filesystem::Path::Data>() / "ctrspec27.txt");
		}
		catch (const std::exception& e)
		{
			SPDLOG_LOGGER_ERROR(spdlog::get("game"), "Creature animation names: {}", e.what());
		}
		std::istringstream spec(std::string(bytes.begin(), bytes.end()));
		std::string line;
		std::getline(spec, line); // the spec version
		while (std::getline(spec, line))
		{
			while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
			{
				line.pop_back();
			}
			if (line.size() > 1 && line.front() != '=')
			{
				list.push_back(line.substr(1));
			}
		}
		SPDLOG_LOGGER_INFO(spdlog::get("game"), "Creature animation names: {} from ctrspec27.txt", list.size());
		return list;
	}();
	return names;
}

// What each creature is playing: the pose's name in its CBN (ctrspec27.txt's names) and how far into it.
struct CreaturePlaying
{
	std::string pose;
	float frame;
};
std::unordered_map<uint32_t, CreaturePlaying> g_creaturePlaying;
std::unordered_map<uint32_t, glm::vec3> g_creatureLastPosition; // where its walk/run cycle last measured it
// How it is going between standing and walking (LH3DCreature's CurrentSpeed and cycle times): its speed, eased
// toward how fast it is really moving, and where it is in each of the two cycles.
struct CreatureGait
{
	float speed = 0.0f;
	float walkFrame = 0.0f;
	float standFrame = 0.0f;
};
std::unordered_map<uint32_t, CreatureGait> g_creatureGait;
std::unordered_map<uint32_t, glm::vec3> g_creatureHomes; // SET_CREATURE_HOME

// The creature actions acted out so far, by their place in the table.
namespace CreatureAction
{
constexpr int32_t EatAlive = 11;
constexpr int32_t EatAfterExamining = 12;
constexpr int32_t Hurl = 15;
constexpr int32_t SleepAtHome = 17;
constexpr int32_t ExamineByLooking = 20;
constexpr int32_t HoldObject = 64;
constexpr int32_t PutDown = 67;
constexpr int32_t SleepByObject = 78;
constexpr int32_t SitDown = 81;
constexpr int32_t LookAtCamera = 82;
constexpr int32_t WaveAtPlayer = 90;
constexpr int32_t WaveAtObject = 91;
constexpr int32_t EatFromFoodPile = 158;
constexpr int32_t BeIdle = 161;
constexpr int32_t PointAtObject = 163;
constexpr int32_t PointAtCamera = 168;
constexpr int32_t BeFrightenedOnTheSpot = 174;
constexpr int32_t LookButDontApproach = 188;
constexpr int32_t LookForever = 189;
} // namespace CreatureAction

// Turns about the vertical axis toward a point, at most `maxTurn` radians; true once facing it. The model's -Z
// is taken as its front, as the villagers' (FaceTowards) - not yet checked for creatures.
bool TurnTowards(Transform& transform, const glm::vec3& target, float maxTurn)
{
	const auto to = target - transform.position;
	if (to.x == 0.0f && to.z == 0.0f)
	{
		return true;
	}
	const float wanted = std::atan2(-to.x, -to.z);
	const float now = std::atan2(transform.rotation[2][0], transform.rotation[2][2]);
	const float diff = std::remainder(wanted - now, glm::two_pi<float>());
	const float step = std::clamp(diff, -maxTurn, maxTurn);
	transform.rotation = glm::mat3(glm::eulerAngleY(now + step));
	return std::abs(diff) <= maxTurn;
}

// --- The leash ---------------------------------------------------------------------------------------------------
// Mike's controls (2026-10-16), after the original: A cycles your creature's leash - none, regular, reward,
// punishment - and the action button sends it about while it is on: to a point on the ground (it walks there), to
// something it can lift (it fetches it), or onto something bigger, a tree or a house (the leash is tied there).
// Type 2 is the one Land 1's LeashIntro waits for, taken to be the plain rope; 3 and 4 are guesses.
struct Leash
{
	uint32_t creature = 0;
	int32_t type = 0;
	uint32_t tiedTo = 0;
};
Leash g_leash;
std::unordered_map<uint32_t, glm::vec3> g_leashWalk;  // creature -> where it was sent
std::unordered_map<uint32_t, uint32_t> g_leashFetch;  // creature -> what it was sent to pick up
constexpr std::array<int32_t, 4> k_LeashCycle = {0, 2, 3, 4};

void CycleLeashNow()
{
	const auto creature = PlayersCreature();
	if (creature == 0)
	{
		return;
	}
	const auto at = std::find(k_LeashCycle.begin(), k_LeashCycle.end(), g_leash.creature == creature ? g_leash.type : 0);
	const auto next = at == k_LeashCycle.end() || at + 1 == k_LeashCycle.end() ? k_LeashCycle.front() : *(at + 1);
	g_leash = {creature, next, 0};
	if (next == 0)
	{
		g_leashWalk.erase(creature);
		g_leashFetch.erase(creature);
	}
	SPDLOG_LOGGER_INFO(spdlog::get("game"), "Leash on creature {}: {}", creature,
	                   next == 0 ? "off" : next == 2 ? "regular" : next == 3 ? "reward" : "punishment");
}

float ObjectHeight(entt::entity entity)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto& meshes = Locator::resources::value().GetMeshes();
	const auto* mesh = registry.Valid(entity) ? registry.TryGet<openblack::ecs::components::Mesh>(entity) : nullptr;
	const auto* transform = registry.Valid(entity) ? registry.TryGet<Transform>(entity) : nullptr;
	return mesh != nullptr && transform != nullptr && meshes.Contains(mesh->id)
	           ? meshes.Handle(mesh->id)->GetBoundingBox().Size().y * transform->scale.y
	           : 0.0f;
}

// Where the camera looks at a thing it follows: its middle (the original: its position, plus half its height).
std::optional<glm::vec3> CameraFollowPoint(uint32_t thing)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = static_cast<entt::entity>(thing);
	const auto* transform = thing != 0 && registry.Valid(entity) ? registry.TryGet<Transform>(entity) : nullptr;
	if (transform == nullptr)
	{
		return std::nullopt;
	}
	return transform->position + glm::vec3(0.0f, ObjectHeight(entity) * 0.5f, 0.0f);
}

// The original's GCamera::GetHeadingAndPitchFromPoints: heading from +z towards +x, pitch up from level, of origin as
// seen from focus.
std::pair<float, float> CameraHeadingAndPitch(const glm::vec3& origin, const glm::vec3& focus)
{
	const auto d = origin - focus;
	if (std::abs(d.x) < 0.01f && std::abs(d.z) < 0.01f)
	{
		return {0.0f, 1.5397f};
	}
	return {std::atan2(d.x, d.z), std::atan2(d.y, std::sqrt(d.x * d.x + d.z * d.z))};
}

// The original's CameraModeFollow, each frame while a script holds the camera: the focus eased (over its zoom time,
// 0.2 s, or at once after CAMERA_PROPERTIES' speed 0) to the middle of what it follows, and the position to its
// distance from the target's middle at its heading and pitch (at least pi/13 above level) - the heading measured from
// a walker's front when relative.
void UpdateCameraFollow()
{
	auto& follow = g_cameraFollow;
	if (!g_scriptHasCamera || (follow.focus == 0 && follow.target == 0))
	{
		return;
	}
	auto& camera = Locator::camera::value();
	const auto time = std::chrono::duration_cast<std::chrono::microseconds>(
	    std::chrono::duration<float>(1.0f /* CameraFollowEndZoomTime */ * follow.zoomTimeScale));
	const uint32_t focusThing = follow.focus != 0 ? follow.focus : follow.target;
	if (const auto point = CameraFollowPoint(focusThing))
	{
		camera.FollowFocus(*point, time);
	}
	else
	{
		follow.focus = 0;
	}
	if (follow.target == 0)
	{
		return;
	}
	const auto point = CameraFollowPoint(follow.target);
	if (!point)
	{
		follow.target = 0;
		return;
	}
	follow.pitch = std::max(follow.pitch, glm::pi<float>() / 13.0f);
	float heading = follow.heading;
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = static_cast<entt::entity>(follow.target);
	if (follow.relativeHeading &&
	    registry.AnyOf<openblack::ecs::components::Villager, openblack::ecs::components::Creature,
	                   openblack::ecs::components::Animal>(entity))
	{
		// Measured from the way it faces (its front the model's -Z, as everywhere here): FollowUs' son runs at the camera
		// with heading 22.5, as in the capture of the original.
		const auto front = registry.Get<Transform>(entity).rotation * glm::vec3(0.0f, 0.0f, -1.0f);
		heading += std::atan2(front.x, front.z);
	}
	const auto origin = *point + follow.distance * glm::vec3(std::sin(heading) * std::cos(follow.pitch), std::sin(follow.pitch),
	                                                         std::cos(heading) * std::cos(follow.pitch));
	camera.FollowOrigin(origin, time);
}

// SET_FOCUS_FOLLOW, FOCUS_FOLLOW: the focus follows `thing` (CameraModeScript::SetCameraFocus).
void CameraFollowFocus(uint32_t thing)
{
	g_cameraFollow.focus = CameraFollowPoint(thing) ? thing : 0;
	SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "Camera focus follows {}", g_cameraFollow.focus);
}

// SET_POSITION_FOLLOW and the others: the position follows `thing` (CameraModeFollow::Set), at `distance` or 8 times its
// height, keeping the camera's heading (from its front when relative: then straight ahead of it) and pitch; with `snap`,
// there at once, focus and all (SetToDestinationPosition).
void CameraFollowPosition(uint32_t thing, std::optional<float> distance, bool snap)
{
	auto& follow = g_cameraFollow;
	auto& camera = Locator::camera::value();
	if (!CameraFollowPoint(thing))
	{
		follow.target = 0;
		return;
	}
	follow.target = thing;
	const auto [heading, pitch] = CameraHeadingAndPitch(camera.GetOrigin(Camera::Interpolation::Target),
	                                                    camera.GetFocus(Camera::Interpolation::Target));
	follow.heading = heading;
	follow.pitch = pitch;
	if (distance)
	{
		follow.distance = *distance;
	}
	else
	{
		follow.distance = ObjectHeight(static_cast<entt::entity>(thing)) * 8.0f; // GCamera::GetThingViewingDistance
		if (follow.relativeHeading)
		{
			follow.heading = 0.0f;
		}
	}
	follow.distance = std::clamp(follow.distance, 2.0f, 1500.0f);
	SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "Camera position follows {} at {:.1f}, heading {:.2f}, pitch {:.2f}{}", thing,
	                   follow.distance, follow.heading, follow.pitch, snap ? ", there at once" : "");
	if (snap)
	{
		const auto zoom = follow.zoomTimeScale;
		follow.zoomTimeScale = 0.0f;
		UpdateCameraFollow();
		follow.zoomTimeScale = zoom;
	}
}

bool LeashCommandNow(const glm::vec3& ground, uint32_t objectUnderHand)
{
	if (g_leash.type == 0 || g_leash.creature == 0 || g_leash.creature != PlayersCreature())
	{
		// Diagnosis (Mike, 2026-10-16: the creature stopped going for anything): why an action press wasn't a leash command.
		static auto s_lastRefusal = std::chrono::steady_clock::time_point {};
		if (std::chrono::steady_clock::now() - s_lastRefusal > std::chrono::seconds(1))
		{
			s_lastRefusal = std::chrono::steady_clock::now();
			SPDLOG_LOGGER_INFO(spdlog::get("game"), "Leash command ignored: leash {} on creature {}, your creature is {}",
			                   g_leash.type == 0 ? "off" : "on", g_leash.creature, PlayersCreature());
		}
		return false;
	}
	const auto creature = g_leash.creature;
	if (objectUnderHand != 0 && objectUnderHand != creature)
	{
		const auto object = static_cast<entt::entity>(objectUnderHand);
		const float creatureHeight = ObjectHeight(static_cast<entt::entity>(creature));
		if (ObjectHeight(object) < creatureHeight * 0.6f)
		{
			g_leashFetch[creature] = objectUnderHand;
			g_leashWalk.erase(creature);
			SPDLOG_LOGGER_INFO(spdlog::get("game"), "Leash: creature {} sent to pick up {}", creature, objectUnderHand);
		}
		else
		{
			g_leash.tiedTo = objectUnderHand;
			g_leashWalk.erase(creature);
			g_leashFetch.erase(creature);
			SPDLOG_LOGGER_INFO(spdlog::get("game"), "Leash: creature {} tied to {}", creature, objectUnderHand);
		}
		return true;
	}
	g_leashWalk[creature] = ground;
	g_leashFetch.erase(creature);
	// A person on the leash stays on it and is led along (Land 1's Pied Piper, taken back towards his cave); a post is
	// left behind.
	{
		auto& registry = Locator::entitiesRegistry::value();
		const auto tied = static_cast<entt::entity>(g_leash.tiedTo);
		if (!(g_leash.tiedTo != 0 && registry.Valid(tied) && registry.AllOf<openblack::ecs::components::Villager>(tied)))
		{
			g_leash.tiedTo = 0;
		}
	}
	SPDLOG_LOGGER_INFO(spdlog::get("game"), "Leash: creature {} sent to ({:.1f}, {:.1f}, {:.1f})", creature, ground.x, ground.y, ground.z);
	return true;
}

bool TurnTowards(Transform& transform, const glm::vec3& target, float maxTurn); // below

// Walks a creature toward a point this frame; true once there.
bool WalkCreatureTo(Transform& transform, const glm::vec3& to, float seconds, float arrived)
{
	constexpr float k_WalkSpeed = 3.0f;
	const auto flat = glm::vec2(to.x - transform.position.x, to.z - transform.position.z);
	const float distance = glm::length(flat);
	if (distance <= arrived)
	{
		return true;
	}
	TurnTowards(transform, to, glm::radians(120.0f) * seconds);
	const auto step = flat / distance * std::min(k_WalkSpeed * seconds, distance);
	// In the sea it wades along the bed, out to where the water reaches its neck and no further (Mike, 2026-10-17).
	// Its height: the creature meshes stand about 50 tall at scale 1.
	constexpr float k_CreatureMeshHeight = 50.0f;
	const float neck = 0.8f * k_CreatureMeshHeight * transform.scale.y;
	const auto next = glm::vec2(transform.position.x + step.x, transform.position.z + step.y);
	const float bed = SeabedAt(next);
	if (-bed > neck && bed < transform.position.y)
	{
		static std::chrono::steady_clock::time_point s_said {};
		if (std::chrono::steady_clock::now() - s_said > std::chrono::seconds(5))
		{
			s_said = std::chrono::steady_clock::now();
			SPDLOG_LOGGER_INFO(spdlog::get("game"), "A creature stops at neck depth ({:.1f}; it is {:.1f} tall)", -bed, neck / 0.8f);
		}
		return true; // as far as it can go
	}
	transform.position.x = next.x;
	transform.position.z = next.y;
	transform.position.y = bed;
	return false;
}

// What the leash makes the creature do this frame, ahead of its script order: the animation, or none if nothing.
void MoveBody(entt::entity entity, const glm::vec3& position); // below, with the hand
std::optional<std::string> ActOutLeashInner(entt::entity entity, float seconds);

// Diagnosis (Mike, 2026-10-16: the creature stopped going for anything, though the log said it picked things up):
// what the leash makes the creature do, and anything else that undoes it.
struct LeashTrace
{
	glm::vec3 lastPosition {};
	bool havePosition = false;
	bool actedThisFrame = false;
	std::chrono::steady_clock::time_point lastStatus {};
	std::chrono::steady_clock::time_point lastMovedAway {};
	std::chrono::steady_clock::time_point lastNotActed {};
};
LeashTrace g_leashTrace;

bool Every(std::chrono::steady_clock::time_point& last, std::chrono::milliseconds period)
{
	const auto now = std::chrono::steady_clock::now();
	if (now - last < period)
	{
		return false;
	}
	last = now;
	return true;
}

std::optional<std::string> ActOutLeash(entt::entity entity, float seconds)
{
	const auto id = static_cast<uint32_t>(entity);
	if (g_leash.creature != id || g_leash.type == 0)
	{
		return std::nullopt;
	}
	g_leashTrace.actedThisFrame = true;
	auto& registry = Locator::entitiesRegistry::value();
	auto& transform = registry.Get<Transform>(entity);
	const bool busy = g_leashWalk.contains(id) || g_leashFetch.contains(id);
	if (busy && g_leashTrace.havePosition && glm::distance(transform.position, g_leashTrace.lastPosition) > 0.05f &&
	    Every(g_leashTrace.lastMovedAway, std::chrono::milliseconds(1000)))
	{
		SPDLOG_LOGGER_INFO(spdlog::get("game"),
		                   "Leash: creature {} was moved by something else since last frame, from ({:.1f}, {:.1f}, {:.1f}) to ({:.1f}, {:.1f}, {:.1f})",
		                   id, g_leashTrace.lastPosition.x, g_leashTrace.lastPosition.y, g_leashTrace.lastPosition.z,
		                   transform.position.x, transform.position.y, transform.position.z);
	}
	const auto result = ActOutLeashInner(entity, seconds);
	g_leashTrace.lastPosition = transform.position;
	g_leashTrace.havePosition = true;
	if (busy && Every(g_leashTrace.lastStatus, std::chrono::milliseconds(1000)))
	{
		glm::vec3 target(0.0f);
		std::string what;
		if (const auto walk = g_leashWalk.find(id); walk != g_leashWalk.end())
		{
			target = walk->second;
			what = "walking to a spot";
		}
		else if (const auto fetch = g_leashFetch.find(id); fetch != g_leashFetch.end())
		{
			const auto object = static_cast<entt::entity>(fetch->second);
			target = registry.Valid(object) ? registry.Get<Transform>(object).position : glm::vec3(0.0f);
			what = fmt::format("fetching {}", fetch->second);
		}
		else
		{
			what = "just arrived";
		}
		const auto held = g_creatureHolding.find(id);
		SPDLOG_LOGGER_INFO(spdlog::get("game"),
		                   "Leash: creature {} {} at ({:.1f}, {:.1f}, {:.1f}), target ({:.1f}, {:.1f}, {:.1f}) {:.1f} away, animation {}, holding {}",
		                   id, what, transform.position.x, transform.position.y, transform.position.z, target.x, target.y, target.z,
		                   glm::distance(glm::vec2(transform.position.x, transform.position.z), glm::vec2(target.x, target.z)),
		                   result ? *result : std::string("none"), held != g_creatureHolding.end() ? held->second : 0u);
	}
	return result;
}

std::optional<std::string> ActOutLeashInner(entt::entity entity, float seconds)
{
	const auto id = static_cast<uint32_t>(entity);
	// A person on the leash is pulled along, kept within 4 of the creature.
	if (g_leash.creature == id && g_leash.type != 0 && g_leash.tiedTo != 0)
	{
		auto& registry = Locator::entitiesRegistry::value();
		const auto person = static_cast<entt::entity>(g_leash.tiedTo);
		if (registry.Valid(person) && registry.AllOf<openblack::ecs::components::Villager, Transform>(person))
		{
			const auto& at = registry.Get<Transform>(entity).position;
			auto& where = registry.Get<Transform>(person).position;
			const auto flat = glm::vec2(at.x - where.x, at.z - where.z);
			const float distance = glm::length(flat);
			if (distance > 4.0f)
			{
				const auto step = flat / distance * std::min(distance - 4.0f, 6.0f * seconds);
				where.x += step.x;
				where.z += step.y;
				where.y = Locator::terrainSystem::value().GetHeightAt(glm::vec2(where.x, where.z));
				MoveBody(person, where);
				if (auto* wallHug = registry.TryGet<WallHug>(person))
				{
					wallHug->goal = glm::vec2(where.x, where.z);
					registry.Remove<MoveStateLinearTag, MoveStateOrbitTag, MoveStateExitCircleTag, MoveStateStepThroughTag,
					                MoveStateFinalStepTag, MoveStateArrivedTag, WallHugObjectReference>(person);
				}
				if (auto* move = registry.TryGet<ScriptMove>(person))
				{
					move->target = where;
				}
			}
		}
	}
	auto& registry = Locator::entitiesRegistry::value();
	auto& transform = registry.Get<Transform>(entity);
	if (const auto walk = g_leashWalk.find(id); walk != g_leashWalk.end())
	{
		if (WalkCreatureTo(transform, walk->second, seconds, 1.5f))
		{
			g_leashWalk.erase(walk);
			// Sent somewhere carrying something (a drowning villager to the shore): it puts it down there.
			CreatureDropsHeld(id);
			return std::string("stand");
		}
		return std::string("walk");
	}
	if (const auto fetch = g_leashFetch.find(id); fetch != g_leashFetch.end())
	{
		const auto object = static_cast<entt::entity>(fetch->second);
		if (!registry.Valid(object))
		{
			g_leashFetch.erase(fetch);
			return std::nullopt;
		}
		if (WalkCreatureTo(transform, registry.Get<Transform>(object).position, seconds, 3.0f))
		{
			CreatureDropsHeld(id);
			g_creatureHolding[id] = fetch->second;
			SPDLOG_LOGGER_INFO(spdlog::get("game"), "Creature {} picked up {}", id, fetch->second);
			g_leashFetch.erase(fetch);
			return std::string("hold");
		}
		return std::string("walk");
	}
	// Tied to something (not a person, who is led instead): it doesn't stray far from it.
	if (g_leash.tiedTo != 0 && !Locator::entitiesRegistry::value().AllOf<openblack::ecs::components::Villager>(
	                               static_cast<entt::entity>(g_leash.tiedTo)))
	{
		const auto post = static_cast<entt::entity>(g_leash.tiedTo);
		if (registry.Valid(post) && !WalkCreatureTo(transform, registry.Get<Transform>(post).position, seconds, 10.0f))
		{
			return std::string("walk");
		}
	}
	return std::nullopt;
}

// A creature's Hurl: the object it throws, at whom, and how far it has got. Land 1's punishment lesson has the creature
// throw a small stone at Sable, then waits for her to be flying (knocked over) before you punish it.
struct CreatureHurl
{
	CreatureOrder order;
	bool launched = false;
	bool done = false;
	float sinceLaunch = 0.0f;
};
std::unordered_map<uint32_t, CreatureHurl> g_creatureHurls;

// Acts out a creature's order for this frame - turning, walking - and says which of its animations fits.
std::string ActOutCreatureOrder(entt::entity entity, const CreatureOrder& order, float seconds)
{
	using namespace CreatureAction;
	constexpr float k_TurnSpeed = glm::radians(120.0f); // radians a second
	constexpr float k_WalkSpeed = 3.0f;                 // units a second (a guess: about a brisk villager's two)
	constexpr float k_Arrived = 2.0f;
	auto& registry = Locator::entitiesRegistry::value();
	auto& transform = registry.Get<Transform>(entity);
	const auto targetEntity = static_cast<entt::entity>(order.target);
	const auto* targetTransform =
	    order.target != 0 && registry.Valid(targetEntity) ? registry.TryGet<Transform>(targetEntity) : nullptr;
	const auto camera = Locator::camera::value().GetOrigin();
	const float turn = k_TurnSpeed * seconds;
	// Walks toward a point; true once there.
	auto walkTo = [&](const glm::vec3& to) {
		const auto flat = glm::vec2(to.x - transform.position.x, to.z - transform.position.z);
		const float distance = glm::length(flat);
		if (distance <= k_Arrived)
		{
			return true;
		}
		TurnTowards(transform, to, turn);
		const auto step = flat / distance * std::min(k_WalkSpeed * seconds, distance);
		transform.position.x += step.x;
		transform.position.z += step.y;
		transform.position.y = Locator::terrainSystem::value().GetHeightAt(glm::vec2(transform.position.x, transform.position.z));
		return false;
	};
	switch (order.action)
	{
	case LookAtCamera:
	case LookForever:
		TurnTowards(transform, camera, turn);
		return "stand";
	case PointAtCamera:
		TurnTowards(transform, camera, turn);
		return "look_at_me";
	case WaveAtPlayer:
		TurnTowards(transform, camera, turn);
		return "friendly_wave";
	case LookButDontApproach:
	case ExamineByLooking:
	case PointAtObject:
	case WaveAtObject:
		if (targetTransform != nullptr)
		{
			TurnTowards(transform, targetTransform->position, turn);
		}
		return order.action == WaveAtObject ? "friendly_wave" : order.action == PointAtObject ? "look_at_me" : "stand";
	case Hurl:
	{
		auto& hurl = g_creatureHurls[static_cast<uint32_t>(entity)];
		if (hurl.order.action != order.action || hurl.order.target != order.target || hurl.order.with != order.with)
		{
			hurl = {order};
		}
		// The script passes what is thrown in the target slot and whom it is thrown at in the with slot (Land 1:
		// MyCreature, HURL, TinyStone, Trainer); read the other way round, the Ape threw Sable at the stone.
		const auto thrown = order.target;
		const auto victim = order.with;
		const auto stone = static_cast<entt::entity>(thrown);
		const auto victimEntity = static_cast<entt::entity>(victim);
		const auto* victimTransform = victim != 0 && registry.Valid(victimEntity) ? registry.TryGet<Transform>(victimEntity) : nullptr;
		if (hurl.done || victimTransform == nullptr || thrown == 0 || !registry.Valid(stone))
		{
			return "stand";
		}
		if (!hurl.launched)
		{
			if (!TurnTowards(transform, victimTransform->position, turn))
			{
				return "stand";
			}
			// From its hand, on an arc that comes down on the target's chest a second later.
			constexpr float k_Gravity9 = 9.81f;
			constexpr float k_FlightSeconds = 1.0f;
			const auto& meshes = Locator::resources::value().GetMeshes();
			const auto* mesh = registry.TryGet<openblack::ecs::components::Mesh>(entity);
			const float height = mesh != nullptr && meshes.Contains(mesh->id) ? meshes.Handle(mesh->id)->GetBoundingBox().Size().y * transform.scale.y : 5.0f;
			auto& stoneTransform = registry.Get<Transform>(stone);
			stoneTransform.position = transform.position + transform.rotation * glm::vec3(0.0f, height * 0.7f, -height * 0.3f);
			const auto aim = victimTransform->position + glm::vec3(0.0f, 1.0f, 0.0f);
			g_flying[thrown] = (aim - stoneTransform.position) / k_FlightSeconds + glm::vec3(0.0f, 0.5f * k_Gravity9 * k_FlightSeconds, 0.0f);
			g_creatureHolding.erase(static_cast<uint32_t>(entity));
			hurl.launched = true;
			SPDLOG_LOGGER_INFO(spdlog::get("game"), "Creature {} hurls {} at {}", static_cast<uint32_t>(entity), thrown, victim);
		}
		hurl.sinceLaunch += seconds;
		// It lands on the target, or anywhere near: either way the target is knocked off its feet.
		const auto& stoneAt = registry.Get<Transform>(stone).position;
		const bool hit = glm::distance(stoneAt, victimTransform->position + glm::vec3(0.0f, 1.0f, 0.0f)) < 3.0f;
		const bool landed = !g_flying.contains(thrown) && hurl.sinceLaunch > 0.2f;
		if (hit || landed)
		{
			const auto away = glm::normalize(glm::vec3(victimTransform->position.x - transform.position.x, 0.0f,
			                                           victimTransform->position.z - transform.position.z) + glm::vec3(1e-3f, 0.0f, 0.0f));
			g_flying[victim] = away * 5.0f + glm::vec3(0.0f, 12.0f, 0.0f); // about 2.5 s in the air
			g_knockedUntil[victim] = ScriptNow() + std::chrono::seconds(6);
			hurl.done = true;
			SPDLOG_LOGGER_INFO(spdlog::get("game"), "{} knocked flying by creature {}'s throw", victim, static_cast<uint32_t>(entity));
		}
		return hurl.sinceLaunch < 1.0f ? "hurl_flat" : "stand";
	}
	case SitDown:
		return "sit";
	case BeFrightenedOnTheSpot:
		return "frightened";
	case HoldObject:
	{
		// Takes the target into its hand (Land 1's PiperCaught has your creature pick the Piper up, then waits for him
		// IN_CREATURE_HAND).
		const auto id = static_cast<uint32_t>(entity);
		if (targetTransform != nullptr && !g_holdDone.contains(id) &&
		    !(g_creatureHolding.contains(id) && g_creatureHolding[id] == order.target))
		{
			if (!walkTo(targetTransform->position))
			{
				return "walk";
			}
			CreatureDropsHeld(id);
			g_creatureHolding[id] = order.target;
			g_holdDone.insert(id);
			SPDLOG_LOGGER_INFO(spdlog::get("game"), "Creature {} picked up {} (HOLD_OBJECT)", id, order.target);
		}
		return "hold";
	}
	case PutDown:
		CreatureDropsHeld(static_cast<uint32_t>(entity));
		return "put_down";
	case SleepAtHome:
	{
		const auto home = g_creatureHomes.find(static_cast<uint32_t>(entity));
		if (home != g_creatureHomes.end() && !walkTo(home->second))
		{
			return "walk";
		}
		return "sleep";
	}
	case SleepByObject:
	case EatFromFoodPile:
		if (targetTransform != nullptr && !walkTo(targetTransform->position))
		{
			return "walk";
		}
		return order.action == EatFromFoodPile ? "eat" : "sleep";
	default: // BeIdle, and everything not acted out yet
		return "stand";
	}
}
// For a pose whose CBN header gives no length: the old guess.
constexpr float k_CreatureFramesPerSecond = 15.0f;
// A creature's walking pace here (ActOutCreatureOrder's and WalkCreatureTo's), units a second, and how quickly
// its speed changes: the original's Acceleration of 12 against its WalkSpeed of 9 at size 1 (LH3DCreature::SetSize)
// takes it from standing to walking pace in three quarters of a second.
constexpr float k_CreatureWalkSpeed = 3.0f;
constexpr float k_CreatureAcceleration = k_CreatureWalkSpeed * 12.0f / 9.0f;

// The creature's size as the original's Size1 (LH3DCreature::SetSize keeps it in 0.05..4): its SCALE property.
float CreatureSize(uint32_t creature)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = static_cast<entt::entity>(creature);
	const auto base = g_creatureBaseScale.find(creature);
	if (!registry.Valid(entity) || !registry.AllOf<Transform>(entity) || base == g_creatureBaseScale.end() || base->second <= 0.0f)
	{
		return 1.0f;
	}
	return std::clamp(registry.Get<Transform>(entity).scale.x / base->second, 0.05f, 4.0f);
}

// Keyframes per second of a creature pose. The original plays each animation over its header's Duration (CAnim,
// ms), on a clock LH3DCreature::UpdateTime runs at (1.6 - Size1 * 0.5 * 0.85) of game time: small creatures
// move quicker. Logged once per pose.
float CreatureFramesPerSecond(const std::string& name, const openblack::morph::Animation& pose, float size)
{
	const auto durationMs = static_cast<float>(pose.header.unknown0x0);
	static std::set<std::string> s_reported;
	if (s_reported.emplace(name).second)
	{
		SPDLOG_LOGGER_INFO(spdlog::get("game"), "Creature pose {}: {} ms over {} keyframes (header frames {})", name,
		                   pose.header.unknown0x0, pose.keyframes.size(), pose.header.frameCount);
	}
	if (durationMs <= 0.0f || pose.keyframes.empty())
	{
		return k_CreatureFramesPerSecond;
	}
	const float pace = std::max(1.6f - size * 0.5f * 0.85f, 0.05f);
	// A cyclic pose's frames take its duration in all; one played once reaches its last frame at the end.
	const auto steps = pose.header.unknown0x4 != 0 ? pose.keyframes.size() : std::max<size_t>(pose.keyframes.size() - 1, 1);
	return static_cast<float>(steps) * 1000.0f / durationMs * pace;
}

// Animation units (keyframe time) per second of play, and one play's length in those units.
std::pair<float, float> AnimationRate(uint32_t animation)
{
	const auto& animations = Locator::resources::value().GetAnimations();
	const auto id = openblack::resources::HashIdentifier(animation);
	if (!animations.Contains(id))
	{
		return {0.0f, 0.0f};
	}
	const auto handle = animations.Handle(id);
	const auto frames = handle->GetFrames().size();
	const auto duration = static_cast<float>(handle->GetDuration());
	return {frames > 0 ? duration / static_cast<float>(frames) * k_AnimationFramesPerSecond : 0.0f, duration};
}

void MoveBody(entt::entity entity, const glm::vec3& position); // below, with the hand

// A boned mesh's bones posed by `animation` at `time`, composed parent to child as the renderer does: model space.
std::vector<glm::mat4> PosedBones(entt::id_type meshId, uint32_t animation, float time)
{
	const auto& meshes = Locator::resources::value().GetMeshes();
	const auto& animations = Locator::resources::value().GetAnimations();
	const auto animationId = openblack::resources::HashIdentifier(animation);
	if (!meshes.Contains(meshId) || !animations.Contains(animationId))
	{
		return {};
	}
	const auto mesh = meshes.Handle(meshId);
	auto bones = mesh->GetBoneMatrices();
	const auto posed = animations.Handle(animationId)->GetBoneMatrices(static_cast<uint32_t>(time));
	const auto& parents = mesh->GetBoneParents();
	if (posed.size() != bones.size())
	{
		return {};
	}
	for (size_t i = 0; i < posed.size(); ++i)
	{
		bones[i] = parents[i] != std::numeric_limits<uint32_t>::max() ? bones[parents[i]] * posed[i] : posed[i];
	}
	return bones;
}

// What rests on the gate plinth - the three gate stones - rides its posts down as it opens, and up as it shuts.
struct PlinthRider
{
	entt::entity entity;
	size_t bone;       // the post it sits on: the bone nearest it
	glm::vec3 base;    // where it was when the plinth began to open
	float stretch = 1.0f; // how many times the post's sinking it sinks: the whole stack goes in, not just the post's drop
};
std::unordered_map<uint32_t, std::vector<PlinthRider>> g_plinthRiders;

void CarryPlinthRiders(entt::entity plinth, bool open, float time)
{
	using openblack::ecs::components::AnimatedStatic;
	using openblack::ecs::components::Creature;
	using openblack::ecs::components::Mesh;
	using openblack::ecs::components::Villager;
	auto& registry = Locator::entitiesRegistry::value();
	const auto id = static_cast<uint32_t>(plinth);
	const auto& transform = registry.Get<Transform>(plinth);
	const auto& mesh = registry.Get<Mesh>(plinth);
	const auto& info = registry.Get<AnimatedStatic>(plinth);
	const auto animation =
	    static_cast<uint32_t>(Locator::infoConstants::value().animatedStatic.at(static_cast<size_t>(info.type)).defaultAnim);
	const auto rest = PosedBones(mesh.id, animation, 0.0f);
	if (rest.empty())
	{
		return;
	}
	const glm::mat4 model = glm::translate(glm::mat4(1.0f), transform.position) * glm::mat4(transform.rotation) *
	                        glm::scale(glm::mat4(1.0f), transform.scale);
	auto riders = g_plinthRiders.find(id);
	if (riders == g_plinthRiders.end())
	{
		if (!open)
		{
			return;
		}
		// Starting to open: everything loose standing on it is a rider. The totems stack on its middle and sink into
		// it, so they ride whichever part of it sinks furthest by the end of its opening.
		const auto& animations = Locator::resources::value().GetAnimations();
		const auto animationId = openblack::resources::HashIdentifier(animation);
		const float last = animations.Contains(animationId)
		                       ? std::max(static_cast<float>(animations.Handle(animationId)->GetDuration()) - 1.0f, 0.0f)
		                       : 0.0f;
		const auto opened = PosedBones(mesh.id, animation, last);
		size_t sinking = 0;
		float deepest = 0.0f;
		std::string drops;
		for (size_t i = 0; i < rest.size() && i < opened.size(); ++i)
		{
			const float drop = (glm::vec3(model * opened[i][3]) - glm::vec3(model * rest[i][3])).y;
			drops += fmt::format(" {}:{:.2f}", i, drop);
			if (drop < deepest)
			{
				deepest = drop;
				sinking = i;
			}
		}
		SPDLOG_LOGGER_INFO(spdlog::get("game"), "Plinth {} bones' drop by the end of its opening:{}", id, drops);
		// Opening already under way (a load): where things stand now is part way down; their base is above that.
		const auto partway = PosedBones(mesh.id, animation, time);
		const auto box = Locator::resources::value().GetMeshes().Handle(mesh.id)->GetBoundingBox();
		const float radius = 0.5f * std::max(box.Size().x * transform.scale.x, box.Size().z * transform.scale.z) + 1.0f;
		std::vector<PlinthRider> found;
		registry.Each<const Transform>([&](entt::entity other, const Transform& where) {
			if (other == plinth || registry.AnyOf<AnimatedStatic, Villager, Creature, openblack::ecs::components::Animal,
			                                      openblack::ecs::components::Hand>(other) ||
			    !registry.AllOf<Mesh>(other) || where.position.y < transform.position.y - 1.0f ||
			    glm::distance(glm::vec2(where.position.x, where.position.z), glm::vec2(transform.position.x, transform.position.z)) > radius)
			{
				return;
			}
			const auto already = sinking < partway.size()
			                         ? glm::vec3(model * partway[sinking][3]) - glm::vec3(model * rest[sinking][3])
			                         : glm::vec3(0.0f);
			found.push_back({other, sinking, where.position - already});
		});
		// The stones on it go all the way in: the stack sinks its own height, the post's drop stretched to fit.
		float bottom = std::numeric_limits<float>::max();
		float top = std::numeric_limits<float>::lowest();
		const auto& meshes = Locator::resources::value().GetMeshes();
		for (const auto& rider : found)
		{
			const auto* riderMesh = registry.TryGet<Mesh>(rider.entity);
			const auto& riderTransform = registry.Get<Transform>(rider.entity);
			if (riderMesh != nullptr && meshes.Contains(riderMesh->id))
			{
				const auto riderBox = meshes.Handle(riderMesh->id)->GetBoundingBox();
				bottom = std::min(bottom, riderTransform.position.y + riderBox.minima.y * riderTransform.scale.y);
				top = std::max(top, riderTransform.position.y + riderBox.maxima.y * riderTransform.scale.y);
			}
		}
		const float stack = top > bottom ? top - bottom : 0.0f;
		const float stretch = deepest < 0.0f ? std::max(1.0f, stack / -deepest) : 1.0f;
		for (auto& rider : found)
		{
			rider.stretch = stretch;
		}
		SPDLOG_LOGGER_INFO(spdlog::get("game"),
		                   "Plinth {} opens: {} thing(s) on it ride bone {} of {}, which sinks {:.2f}; the stack is {:.2f} tall, so x{:.2f}",
		                   id, found.size(), sinking, rest.size(), -deepest, stack, stretch);
		riders = g_plinthRiders.emplace(id, std::move(found)).first;
	}
	const auto now = PosedBones(mesh.id, animation, time);
	for (const auto& rider : riders->second)
	{
		if (!registry.Valid(rider.entity) || rider.bone >= now.size())
		{
			continue;
		}
		const glm::vec3 drop =
		    (glm::vec3(model * now[rider.bone][3]) - glm::vec3(model * rest[rider.bone][3])) * rider.stretch;
		const auto position = rider.base + drop;
		registry.Get<Transform>(rider.entity).position = position;
		MoveBody(rider.entity, position);
	}
	// Fully down, they are gone from sight (Mike, 2026-10-18: the three stones before the creature choice vanish
	// the moment they have sunk); kept as objects, for the scripts that name them, and shown again if it shuts.
	{
		static std::unordered_map<uint32_t, entt::id_type> s_hidden; // rider -> its mesh
		const auto& animations = Locator::resources::value().GetAnimations();
		const auto animationId = openblack::resources::HashIdentifier(animation);
		const float last = animations.Contains(animationId)
		                       ? std::max(static_cast<float>(animations.Handle(animationId)->GetDuration()) - 1.0f, 0.0f)
		                       : 0.0f;
		const bool down = open && last > 0.0f && time >= last * 0.98f; // within the last moment of the opening
		for (const auto& rider : riders->second)
		{
			const auto riderId = static_cast<uint32_t>(rider.entity);
			if (!registry.Valid(rider.entity))
			{
				continue;
			}
			if (down && !s_hidden.contains(riderId))
			{
				if (const auto* mesh = registry.TryGet<openblack::ecs::components::Mesh>(rider.entity))
				{
					s_hidden[riderId] = mesh->id;
					registry.Remove<openblack::ecs::components::Mesh>(rider.entity);
					SPDLOG_LOGGER_INFO(spdlog::get("game"), "Plinth {}: {} has sunk out of sight", id, riderId);
				}
			}
			else if (!down && s_hidden.contains(riderId))
			{
				registry.AssignOrReplace<openblack::ecs::components::Mesh>(rider.entity, s_hidden[riderId], static_cast<int8_t>(0),
				                                                           static_cast<int8_t>(0));
				s_hidden.erase(riderId);
			}
		}
	}
	if (!open && time <= 0.0f)
	{
		g_plinthRiders.erase(riders); // shut again: whatever stands on it next time is picked up afresh
	}
}

} // namespace
bool InCutscene(); // below
namespace
{

// The creature's own life, when nothing else has it (no script order but BeIdle, no leash, no fight, no cutscene):
// the original's desires, as info.dat names them (CreatureDesires) - Hunger, Tiredness (more at night), ToPoo,
// Curiosity, Wanderlust - each growing with time, the strongest past its threshold choosing what it does next:
// eat (a food pile or a tree), sleep (at home, or where it is), poo, go and look at something, or wander. Within
// k_LifeRange of its home (or of where its life began), so it doesn't walk off from what a lesson needs it near.
// Rates and thresholds are not the original's (its numbers in info.dat aren't decoded); first pass, 2026-10-17.
struct CreatureLife
{
	float hunger = 0.3f;
	float tiredness = 0.2f;
	float poo = 0.1f;
	float curiosity = 0.4f;
	float wanderlust = 0.5f;
	enum class Activity
	{
		Idle,
		Wander,
		Investigate,
		Eat,
		Sleep,
		Poo,
	} activity = Activity::Idle;
	glm::vec3 target {};
	uint32_t object = 0;
	float timer = 0.0f; // in this activity's current part
	int part = 0;       // 0 getting there, then the animation's start, loop, end
	glm::vec3 anchor {};
	bool anchored = false;
	uint64_t lastFrame = 0;
};
std::unordered_map<uint32_t, CreatureLife> g_creatureLives;
uint64_t g_lifeFrame = 0;

std::optional<std::string> ActOutCreatureLife(entt::entity entity, const openblack::morphposes::Poses& poses, float seconds)
{
	using Activity = CreatureLife::Activity;
	constexpr float k_LifeRange = 60.0f;
	auto& registry = Locator::entitiesRegistry::value();
	const auto id = static_cast<uint32_t>(entity);
	auto& life = g_creatureLives[id];
	auto& transform = registry.Get<Transform>(entity);
	// Picked up again after something else had it: start afresh from where it is now.
	if (life.lastFrame + 1 != g_lifeFrame)
	{
		life.activity = Activity::Idle;
		life.timer = 0.0f;
		life.part = 0;
	}
	life.lastFrame = g_lifeFrame;
	if (const auto home = g_creatureHomes.find(id); home != g_creatureHomes.end())
	{
		life.anchor = home->second;
		life.anchored = true;
	}
	else if (!life.anchored)
	{
		life.anchor = transform.position;
		life.anchored = true;
	}
	const bool night = g_gameHour >= 21.0f || g_gameHour < 6.0f;
	life.hunger = std::min(life.hunger + seconds / 600.0f, 1.0f);
	life.tiredness = std::min(life.tiredness + seconds / (night ? 240.0f : 1200.0f), 1.0f);
	life.poo = std::min(life.poo + seconds / 900.0f, 1.0f);
	life.curiosity = std::min(life.curiosity + seconds / 90.0f, 1.0f);
	life.wanderlust = std::min(life.wanderlust + seconds / 60.0f, 1.0f);
	auto has = [&poses](const char* name) { return poses.contains(name); };
	auto lasts = [&poses, entity](const char* name) {
		const auto pose = poses.find(name);
		return pose == poses.end() ? 0.0f
		                           : static_cast<float>(pose->second.keyframes.size()) /
		                                 CreatureFramesPerSecond(pose->first, pose->second, CreatureSize(static_cast<uint32_t>(entity)));
	};
	auto random = [](float lo, float hi) { return Locator::rng::value().NextValue(lo, hi); };
	auto near = [&](const glm::vec3& at) {
		return glm::distance(glm::vec2(at.x, at.z), glm::vec2(life.anchor.x, life.anchor.z)) < k_LifeRange;
	};
	auto say = [&](const std::string& what) {
		SPDLOG_LOGGER_INFO(spdlog::get("game"), "Creature {} (own life; hunger {:.2f}, tired {:.2f}, poo {:.2f}, curious {:.2f}, wanderlust {:.2f}): {}",
		                   id, life.hunger, life.tiredness, life.poo, life.curiosity, life.wanderlust, what);
	};
	// A start/loop/end animation (sleep, poo): start, loop for `loop` seconds, end; true once done.
	auto sequence = [&](const std::string& name, float loop, std::string& animation) {
		const std::string start = "start_" + name;
		const std::string end = "end_" + name;
		life.timer += seconds;
		const std::array<std::pair<std::string, float>, 3> parts = {
		    std::pair {start, has(start.c_str()) ? lasts(start.c_str()) : 0.0f}, std::pair {name, has(name.c_str()) ? loop : 0.0f},
		    std::pair {end, has(end.c_str()) ? lasts(end.c_str()) : 0.0f}};
		while (life.part >= 1 && life.part <= 3 && life.timer >= parts.at(static_cast<size_t>(life.part - 1)).second)
		{
			life.timer -= parts.at(static_cast<size_t>(life.part - 1)).second;
			++life.part;
		}
		if (life.part > 3)
		{
			return true;
		}
		animation = parts.at(static_cast<size_t>(life.part - 1)).first;
		return false;
	};

	if (life.activity == Activity::Idle)
	{
		life.timer += seconds;
		if (life.timer < 4.0f)
		{
			return std::string("stand");
		}
		life.timer = 0.0f;
		life.part = 0;
		const float tiredAt = night ? 0.6f : 0.85f;
		if (life.tiredness >= tiredAt)
		{
			life.activity = Activity::Sleep;
			life.target = g_creatureHomes.contains(id) ? g_creatureHomes[id] : transform.position;
			say(fmt::format("tired: goes to sleep at ({:.0f}, {:.0f})", life.target.x, life.target.z));
		}
		else if (life.hunger >= 0.7f)
		{
			// Food: the nearest pile of food, or a tree, within reach of home.
			uint32_t best = 0;
			float bestDistance = std::numeric_limits<float>::max();
			registry.Each<const openblack::ecs::components::Pot, const Transform>(
			    [&](entt::entity pot, const openblack::ecs::components::Pot& contents, const Transform& where) {
				    const auto& info = Locator::infoConstants::value().pot.at(static_cast<size_t>(contents.type));
				    const float d = glm::distance(where.position, transform.position);
				    if (info.resourceType == ResourceType::Food && contents.amount > 0 && near(where.position) && d < bestDistance)
				    {
					    bestDistance = d;
					    best = static_cast<uint32_t>(pot);
				    }
			    });
			if (best == 0)
			{
				registry.Each<const openblack::ecs::components::Tree, const Transform>(
				    [&](entt::entity tree, const openblack::ecs::components::Tree&, const Transform& where) {
					    const float d = glm::distance(where.position, transform.position);
					    if (near(where.position) && d < bestDistance)
					    {
						    bestDistance = d;
						    best = static_cast<uint32_t>(tree);
					    }
				    });
			}
			if (best != 0)
			{
				life.activity = Activity::Eat;
				life.object = best;
				life.target = registry.Get<Transform>(static_cast<entt::entity>(best)).position;
				say(fmt::format("hungry: goes to eat {} at ({:.0f}, {:.0f})", best, life.target.x, life.target.z));
			}
			else
			{
				life.hunger = 0.5f; // nothing to eat near home: it puts up with it for a while
				say("hungry, but there is nothing to eat near home");
			}
		}
		else if (life.poo >= 0.85f)
		{
			life.activity = Activity::Poo;
			const float a = random(0.0f, glm::two_pi<float>());
			life.target = transform.position + glm::vec3(std::cos(a), 0.0f, std::sin(a)) * 8.0f;
			say("needs a poo");
		}
		else if (life.curiosity >= 0.8f)
		{
			// Something to look at: a villager, an animal, a tree or a house, near home, picked at random.
			std::vector<uint32_t> things;
			auto gather = [&](entt::entity thing, const Transform& where) {
				if (near(where.position) && glm::distance(where.position, transform.position) > 6.0f && things.size() < 64)
				{
					things.push_back(static_cast<uint32_t>(thing));
				}
			};
			registry.Each<const openblack::ecs::components::Villager, const Transform>(
			    [&](entt::entity e, const openblack::ecs::components::Villager&, const Transform& t) { gather(e, t); });
			registry.Each<const openblack::ecs::components::Animal, const Transform>(
			    [&](entt::entity e, const openblack::ecs::components::Animal&, const Transform& t) { gather(e, t); });
			registry.Each<const openblack::ecs::components::Abode, const Transform>(
			    [&](entt::entity e, const openblack::ecs::components::Abode&, const Transform& t) { gather(e, t); });
			if (!things.empty())
			{
				life.object = things.at(static_cast<size_t>(random(0.0f, static_cast<float>(things.size()) - 0.01f)));
				life.activity = Activity::Investigate;
				say(fmt::format("curious: goes to look at {}", life.object));
			}
			life.curiosity = 0.3f;
		}
		else if (life.wanderlust >= 0.7f)
		{
			const float a = random(0.0f, glm::two_pi<float>());
			const float r = random(10.0f, k_LifeRange * 0.8f);
			life.target = life.anchor + glm::vec3(std::cos(a), 0.0f, std::sin(a)) * r;
			life.activity = Activity::Wander;
			say(fmt::format("wanders to ({:.0f}, {:.0f})", life.target.x, life.target.z));
		}
		return std::string("stand");
	}

	std::string animation = "stand";
	switch (life.activity)
	{
	case Activity::Wander:
		if (WalkCreatureTo(transform, life.target, seconds, 2.0f))
		{
			life.wanderlust = 0.0f;
			life.activity = Activity::Idle;
			return std::string("stand");
		}
		return std::string("walk");
	case Activity::Investigate:
	{
		const auto thing = static_cast<entt::entity>(life.object);
		if (!registry.Valid(thing) || !registry.AllOf<Transform>(thing))
		{
			life.activity = Activity::Idle;
			return std::string("stand");
		}
		const auto at = registry.Get<Transform>(thing).position;
		if (life.part == 0)
		{
			if (!WalkCreatureTo(transform, at, seconds, 5.0f))
			{
				return std::string("walk");
			}
			life.part = 1;
			life.timer = 0.0f;
		}
		TurnTowards(transform, at, glm::radians(120.0f) * seconds);
		life.timer += seconds;
		if (life.timer > 5.0f)
		{
			life.activity = Activity::Idle;
			life.timer = 0.0f;
		}
		return std::string(has("puzzled") && life.timer < 2.0f ? "puzzled" : "stand");
	}
	case Activity::Eat:
	{
		const auto food = static_cast<entt::entity>(life.object);
		if (!registry.Valid(food))
		{
			life.activity = Activity::Idle;
			return std::string("stand");
		}
		if (life.part == 0)
		{
			if (!WalkCreatureTo(transform, life.target, seconds, 4.0f))
			{
				return std::string("walk");
			}
			life.part = 1;
			life.timer = 0.0f;
		}
		TurnTowards(transform, life.target, glm::radians(120.0f) * seconds);
		life.timer += seconds;
		if (life.timer >= std::max(lasts("eat"), 2.0f))
		{
			if (auto* pot = registry.TryGet<openblack::ecs::components::Pot>(food))
			{
				pot->amount = static_cast<uint16_t>(pot->amount > 100 ? pot->amount - 100 : 0);
			}
			life.hunger = 0.0f;
			life.poo = std::min(life.poo + 0.4f, 1.0f);
			say(fmt::format("ate from {}", life.object));
			life.activity = Activity::Idle;
			life.timer = 0.0f;
		}
		return std::string(has("eat") ? "eat" : "stand");
	}
	case Activity::Sleep:
		if (life.part == 0)
		{
			if (!WalkCreatureTo(transform, life.target, seconds, 3.0f))
			{
				return std::string("walk");
			}
			life.part = 1;
			life.timer = 0.0f;
		}
		// Sleeps until morning at night, a while by day.
		if (life.part == 2 && night)
		{
			life.timer = 0.0f;
		}
		if (sequence("sleep", night ? 30.0f : 20.0f, animation))
		{
			life.tiredness = 0.0f;
			say("wakes up");
			life.activity = Activity::Idle;
			life.timer = 0.0f;
			return std::string("stand");
		}
		return animation;
	case Activity::Poo:
		if (life.part == 0)
		{
			if (!WalkCreatureTo(transform, life.target, seconds, 2.0f))
			{
				return std::string("walk");
			}
			life.part = 1;
			life.timer = 0.0f;
		}
		if (sequence("poo", 3.0f, animation))
		{
			life.poo = 0.0f;
			say("done");
			life.activity = Activity::Idle;
			life.timer = 0.0f;
			return std::string("stand");
		}
		return animation;
	case Activity::Idle:
		break;
	}
	return animation;
}

// Which body animations an animal species has for being thrown: by its mesh's name (the land's animals are cows,
// horses, pigs and sheep; others have none and fly as they stand).
struct ThrownAnimations
{
	AnimId thrown;
	AnimId getUp;
	AnimId fallDead;
};
std::optional<ThrownAnimations> AnimalThrownAnimations(entt::entity entity)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto* mesh = registry.TryGet<openblack::ecs::components::Mesh>(entity);
	if (mesh == nullptr || !Locator::resources::value().GetMeshes().Contains(mesh->id))
	{
		return std::nullopt;
	}
	const auto name = Locator::resources::value().GetMeshes().Handle(mesh->id)->GetDebugName();
	if (name.find("Cow") != std::string::npos)
	{
		return ThrownAnimations {AnimId::ACowThrown, AnimId::ACowLandedLeftSide, AnimId::ACowDeadOnLhs};
	}
	if (name.find("Horse") != std::string::npos)
	{
		return ThrownAnimations {AnimId::AHorseThrown, AnimId::AHorseLandGetUplhs, AnimId::AHorseFallDeadlhs};
	}
	if (name.find("Pig") != std::string::npos)
	{
		return ThrownAnimations {AnimId::APigThrown, AnimId::APigLandGetUplhs, AnimId::APigFallDeadlhs};
	}
	if (name.find("Sheep") != std::string::npos)
	{
		return ThrownAnimations {AnimId::ASheepThrown, AnimId::ASheepLandedGetuplhs, AnimId::ASheepFallDeadlhs};
	}
	return std::nullopt;
}

std::unordered_map<uint32_t, float> g_animalHealth; // 1 whole; thrown animals lose it landing
std::unordered_set<uint32_t> g_fallenDead;         // killed by a landing: they lie in their dead pose
std::unordered_map<uint32_t, float> g_thrownAnimalLanding; // animals getting up (or falling dead) after a landing
std::unordered_map<uint32_t, uint32_t> g_animalDefaultAnimation; // what a thrown animal goes back to once up
// In the water out of their depth (Mike, 2026-10-17): people tread water a while, then drown; animals sooner.
// Seconds in so far.
constexpr float k_PersonDrownSeconds = 30.0f;
constexpr float k_AnimalDrownSeconds = 15.0f;
std::unordered_map<uint32_t, float> g_inDeepWater;
// Things thrown in the sea go down to its bed (rocks and the like): the script's stay there, the rest are gone.
std::unordered_map<uint32_t, float> g_sinking;
// Bounces so far of what is in the air, and what it last struck (not struck again for a moment).
std::unordered_map<uint32_t, int> g_bounces;
std::unordered_map<uint32_t, std::pair<uint32_t, float>> g_lastStruck;
// A thrown thing's tumble, if it got one (about half do): an axis times radians a second, slow enough to follow.
std::unordered_map<uint32_t, glm::vec3> g_spin;

constexpr float k_SafeLanding = 12.0f;
constexpr float k_DeadlyLanding = 40.0f;

// The hand's and the land's own sounds for throwing (InGame.sad, bank 1, by sample id): one of a range at random.
enum class ThrowSound
{
	Woosh,     // 46-49 let go of hard
	YellMan,   // 187-193 (G_PickUpMan) a person thrown
	YellWoman, // 194-200
	YellChild, // 180-186
	Splash,    // 99-108 (G_HandInWater) into the sea
	Squash,    // 143-146 a person or animal landing hard
	RockTap,   // 130-133 a rock or such on the land or against another
	Roof,      // 110-112 (G_KnockRoofMulti) against a building
	Wood,      // 86-91 (G_PileWood) wood landing
	WoodKnock, // 151-154 (G_ScaffoldTap) a tree or fence striking the land
};
void PlayThrowSound(ThrowSound sound, const glm::vec3& at)
{
	static const std::map<ThrowSound, std::pair<int32_t, int32_t>> k_Samples = {
	    {ThrowSound::Woosh, {46, 49}},    {ThrowSound::YellMan, {187, 193}}, {ThrowSound::YellWoman, {194, 200}},
	    {ThrowSound::YellChild, {180, 186}}, {ThrowSound::Splash, {99, 108}}, {ThrowSound::Squash, {143, 146}},
	    {ThrowSound::RockTap, {130, 133}}, {ThrowSound::Roof, {110, 112}},    {ThrowSound::Wood, {86, 91}},
	    {ThrowSound::WoodKnock, {151, 154}}};
	const auto& [first, last] = k_Samples.at(sound);
	const auto sample = static_cast<int32_t>(Locator::rng::value().NextValue(static_cast<float>(first), static_cast<float>(last) + 0.99f));
	PlayBankSound(1, sample, &at);
}

// A tree striking the land, bouncing or coming to rest: G_PlantTree_02, G_TreeGrow_02 or G_TreeGrow_04 (the in-game
// bank's 84, 121, 123), Mike's choice by ear, 2026-10-18.
void PlayTreeThud(const glm::vec3& at)
{
	constexpr std::array<int32_t, 3> k_Thuds = {84, 121, 123};
	PlayBankSound(1, k_Thuds.at(static_cast<size_t>(Locator::rng::value().NextValue(0.0f, 2.999f))), &at);
}

// What a thrown thing sounds like hitting the land or something on it.
void PlayImpactSound(entt::entity entity, const glm::vec3& at, float speed)
{
	auto& registry = Locator::entitiesRegistry::value();
	if (registry.AnyOf<openblack::ecs::components::Villager, openblack::ecs::components::Animal>(entity))
	{
		if (speed > k_SafeLanding)
		{
			PlayThrowSound(ThrowSound::Squash, at);
		}
		return;
	}
	if (const auto* pot = registry.TryGet<openblack::ecs::components::Pot>(entity);
	    pot != nullptr && Locator::infoConstants::value().pot.at(static_cast<size_t>(pot->type)).resourceType == ResourceType::Wood)
	{
		PlayThrowSound(ThrowSound::Wood, at);
		return;
	}
	// A tree bounces with a soft, leafy thud (Mike, 2026-10-18, by ear: not G_TreeFall_01, which grinds, nor a knock).
	if (registry.AllOf<openblack::ecs::components::Tree>(entity))
	{
		if (speed > 3.0f)
		{
			PlayTreeThud(at);
		}
		return;
	}
	// Wooden things knock like wood, never like stone (Mike, 2026-10-18: a thrown fence sounded of rock).
	if (const auto* stone = registry.TryGet<openblack::ecs::components::MobileStatic>(entity);
	    stone != nullptr && (stone->type == MobileStaticInfo::CeltFenceShort || stone->type == MobileStaticInfo::CeltFenceTall))
	{
		if (speed > 3.0f)
		{
			PlayThrowSound(ThrowSound::WoodKnock, at);
		}
		return;
	}
	if (speed > 3.0f)
	{
		PlayThrowSound(ThrowSound::RockTap, at);
	}
}

// A blow to a living thing (a bounce, something thrown into it): its health down by how hard; true if it killed it.
bool HurtByImpact(entt::entity entity, float speed)
{
	auto& registry = Locator::entitiesRegistry::value();
	const float hurt = std::clamp((speed - k_SafeLanding) / (k_DeadlyLanding - k_SafeLanding), 0.0f, 1.0f);
	if (hurt <= 0.0f)
	{
		return false;
	}
	if (auto* villager = registry.TryGet<openblack::ecs::components::Villager>(entity))
	{
		villager->life = std::max(villager->life - hurt, 0.0f);
		return villager->life <= 0.0f;
	}
	if (registry.AllOf<openblack::ecs::components::Animal>(entity))
	{
		auto& health = g_animalHealth.try_emplace(static_cast<uint32_t>(entity), 1.0f).first->second;
		health = std::max(health - hurt, 0.0f);
		return health <= 0.0f;
	}
	return false;
}

void ShowThrown(entt::entity entity, bool flying, float impactSpeed)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto id = static_cast<uint32_t>(entity);
	const bool person = registry.AllOf<openblack::ecs::components::Villager>(entity);
	const auto animal = registry.AllOf<openblack::ecs::components::Animal>(entity) ? AnimalThrownAnimations(entity) : std::nullopt;
	if (!person && !animal)
	{
		return;
	}
	if (flying)
	{
		if (person)
		{
			g_scriptAnimations[id] = {static_cast<uint32_t>(AnimId::PThrown), std::numeric_limits<float>::max()};
		}
		else if (auto* animated = registry.TryGet<openblack::ecs::components::Animated>(entity);
		         animated == nullptr || animated->animation != static_cast<uint32_t>(animal->thrown))
		{
			if (animated != nullptr && !g_thrownAnimalLanding.contains(id))
			{
				g_animalDefaultAnimation.try_emplace(id, animated->animation);
			}
			registry.AssignOrReplace<openblack::ecs::components::Animated>(entity, static_cast<uint32_t>(animal->thrown), 0.0f);
		}
		return;
	}
	const float hurt = std::clamp((impactSpeed - k_SafeLanding) / (k_DeadlyLanding - k_SafeLanding), 0.0f, 1.0f);
	bool dead = false;
	if (auto* villager = registry.TryGet<openblack::ecs::components::Villager>(entity))
	{
		villager->life = std::max(villager->life - hurt, 0.0f);
		dead = villager->life <= 0.0f;
		// Killed: the death throes on the ground, PDying, held on its last frame (lying still). Not PThrownDead, which is
		// upright from start to end - the killed stood (Mike, 2026-10-18; measured: its top bone stays at standing
		// height, 1.24, where PDying ends at 0.23).
		g_scriptAnimations[id] = {static_cast<uint32_t>(dead ? AnimId::PDying : AnimId::PLanded),
		                          dead ? std::numeric_limits<float>::max() : 1.0f};
	}
	else
	{
		auto& health = g_animalHealth.try_emplace(id, 1.0f).first->second;
		health = std::max(health - hurt, 0.0f);
		dead = health <= 0.0f;
		registry.AssignOrReplace<openblack::ecs::components::Animated>(entity, static_cast<uint32_t>(dead ? animal->fallDead : animal->getUp),
		                                                               0.0f);
		g_thrownAnimalLanding[id] = 0.0f;
	}
	if (dead)
	{
		g_fallenDead.insert(id);
		HoldForScript(entity, registry.Get<Transform>(entity).position); // the dead stay where they fell
	}
	SPDLOG_LOGGER_INFO(spdlog::get("game"), "Thrown {} landed at {:.1f} units/s: {}", id, impactSpeed,
	                   dead ? "killed" : hurt > 0.0f ? fmt::format("hurt ({:.0f}%)", hurt * 100.0f) : std::string("unhurt"));
}

// The building a point is inside (within its footprint, under its roof), if any - not `self`.
std::optional<entt::entity> BuildingAt(const glm::vec3& point, entt::entity self)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto& meshes = Locator::resources::value().GetMeshes();
	std::optional<entt::entity> found;
	registry.Each<const openblack::ecs::components::Abode, const openblack::ecs::components::Mesh, const Transform>(
	    [&](entt::entity building, const openblack::ecs::components::Abode&, const openblack::ecs::components::Mesh& mesh,
	        const Transform& where) {
		    if (found || building == self || !meshes.Contains(mesh.id))
		    {
			    return;
		    }
		    const auto box = meshes.Handle(mesh.id)->GetBoundingBox();
		    const float reach = 0.8f * std::max(box.maxima.x - box.minima.x, box.maxima.z - box.minima.z) * 0.5f * where.scale.x;
		    const float top = where.position.y + box.maxima.y * where.scale.y;
		    if (glm::distance(glm::vec2(where.position.x, where.position.z), glm::vec2(point.x, point.z)) < reach && point.y < top &&
		        point.y > where.position.y - 1.0f)
		    {
			    found = building;
		    }
	    });
	return found;
}

// A building struck by something thrown at `speed`: its health (the scripts' property 1) goes down by how hard,
// from nothing under 10 units/s to all of it at 50; an ordinary house (A-F) with none left falls (is removed).
// Chosen numbers, not the original's.
void HitBuilding(entt::entity building, float speed, uint32_t by)
{
	constexpr float k_HarmlessHit = 10.0f;
	constexpr float k_DestroyingHit = 50.0f;
	auto& registry = Locator::entitiesRegistry::value();
	const auto id = static_cast<uint32_t>(building);
	auto& health = g_objectProperties.try_emplace(PropertyKey(id, static_cast<int32_t>(Prop::Health)), 1.0f).first->second;
	health = std::max(health - std::clamp((speed - k_HarmlessHit) / (k_DestroyingHit - k_HarmlessHit), 0.0f, 1.0f), 0.0f);
	const auto type = registry.Get<openblack::ecs::components::Abode>(building).type;
	const bool house = static_cast<int32_t>(type) >= static_cast<int32_t>(AbodeNumber::A) &&
	                   static_cast<int32_t>(type) <= static_cast<int32_t>(AbodeNumber::F);
	SPDLOG_LOGGER_INFO(spdlog::get("game"), "Building {} hit by {} at {:.1f} units/s: health {:.2f}{}", id, by, speed, health,
	                   health <= 0.0f && house ? ", destroyed" : "");
	if (health <= 0.0f && house)
	{
		ForgetObject(id);
		registry.Destroy(building);
	}
}

// The land's animals at large (Mike, 2026-10-17: bring them to life, nothing heavy). Birds fly - each at its own
// height band, wandering and banking over where they began, flapping when they climb or now and then, else gliding.
// Cows, horses, pigs and sheep stand and graze a while, then amble somewhere near on dry land. Anything held,
// thrown, in the water, dead, in a script's flock or moved by a script is left alone.
struct AnimalLife
{
	glm::vec3 home {};
	float timer = 0.0f;
	bool walking = false;
	glm::vec3 target {};
	float heading = 0.0f;  // birds: the way it flies (radians about the vertical)
	float turn = 0.0f;     // birds: how fast it is turning now
	float altitude = 0.0f; // birds: its height over the land (or sea)
	float speed = 0.0f;
	bool flapping = true;
};
std::unordered_map<uint32_t, AnimalLife> g_animalLives;

struct AnimalMoves
{
	AnimId stand;
	AnimId walk;
};
std::optional<AnimalMoves> LandAnimalMoves(AnimalInfo type)
{
	switch (type)
	{
	case AnimalInfo::Cow:
	case AnimalInfo::PuzzleCow: return AnimalMoves {AnimId::ACowStand, AnimId::ACowWalk};
	case AnimalInfo::Horse:
	case AnimalInfo::PuzzleHorse: return AnimalMoves {AnimId::AHorseStand, AnimId::AHorseWalk};
	case AnimalInfo::Pig:
	case AnimalInfo::PuzzlePig: return AnimalMoves {AnimId::APigStand, AnimId::APigWalk};
	case AnimalInfo::Sheep:
	case AnimalInfo::PuzzleSheep: return AnimalMoves {AnimId::ASheepStand, AnimId::ASheepWalk};
	default: return std::nullopt;
	}
}
std::optional<std::pair<AnimId, AnimId>> BirdWings(AnimalInfo type) // flap, glide
{
	switch (type)
	{
	case AnimalInfo::Crow:
	case AnimalInfo::Swallow:
	case AnimalInfo::Vulture: return std::pair {AnimId::CrowFlap, AnimId::CrowGlide};
	case AnimalInfo::Dove: return std::pair {AnimId::DoveFlap, AnimId::DoveGlide};
	case AnimalInfo::Pigeon: return std::pair {AnimId::PigeonFlap, AnimId::PigeonGlide};
	case AnimalInfo::Seagull: return std::pair {AnimId::SeagullFlap, AnimId::SeagullGlide};
	default: return std::nullopt;
	}
}

void UpdateAnimalLife(float seconds)
{
	auto& registry = Locator::entitiesRegistry::value();
	auto& rng = Locator::rng::value();
	const auto& terrain = Locator::terrainSystem::value();
	std::unordered_set<uint32_t> inFlocks;
	for (const auto& [flock, members] : g_flocks)
	{
		inFlocks.insert(members.members.begin(), members.members.end());
	}
	// The animals a script holds (a quest's sheep, the Pied Piper's flock) stay where the script put them; looked up
	// every couple of seconds, not every frame.
	static std::unordered_set<uint32_t> s_scripted;
	static float s_scriptedAge = 1e9f;
	s_scriptedAge += seconds;
	if (s_scriptedAge > 2.0f)
	{
		s_scripted = ScriptedObjects();
		s_scriptedAge = 0.0f;
	}
	registry.Each<const openblack::ecs::components::Animal, Transform>(
	    [&](entt::entity entity, const openblack::ecs::components::Animal& animal, Transform& transform) {
		    const auto id = static_cast<uint32_t>(entity);
		    if (id == g_heldObject || g_flying.contains(id) || g_inDeepWater.contains(id) || g_fallenDead.contains(id) ||
		        g_thrownAnimalLanding.contains(id) || g_sinking.contains(id) || g_vortexSpirals.contains(id) || inFlocks.contains(id) ||
		        g_scriptMoves.contains(id) || g_walks.contains(id) || g_followers.contains(id) || s_scripted.contains(id))
		    {
			    return;
		    }
		    auto [life, fresh] = g_animalLives.try_emplace(id);
		    auto& me = life->second;
		    if (fresh)
		    {
			    me.home = transform.position;
			    me.heading = rng.NextValue(0.0f, glm::two_pi<float>());
			    me.altitude = rng.NextValue(15.0f, 90.0f); // treetops to high over the land, a band each
			    me.speed = rng.NextValue(7.0f, 13.0f);
			    me.timer = rng.NextValue(0.0f, 8.0f);
		    }
		    auto* animated = registry.TryGet<openblack::ecs::components::Animated>(entity);
		    auto play = [&](AnimId animation) {
			    if (animated != nullptr && animated->animation != static_cast<uint32_t>(animation))
			    {
				    animated->animation = static_cast<uint32_t>(animation);
				    animated->time = 0.0f;
			    }
		    };
		    if (const auto wings = BirdWings(animal.type))
		    {
			    // Steering: the turn wanders, and pulls back towards home once far from it.
			    me.timer -= seconds;
			    if (me.timer <= 0.0f)
			    {
				    me.timer = rng.NextValue(2.0f, 6.0f);
				    me.turn = rng.NextValue(-0.6f, 0.6f);
				    me.flapping = rng.NextValue(0.0f, 1.0f) < 0.35f;
			    }
			    const auto away = glm::vec2(transform.position.x - me.home.x, transform.position.z - me.home.z);
			    float turn = me.turn;
			    if (glm::length(away) > 250.0f)
			    {
				    const float homeward = std::atan2(-away.x, -away.y);
				    float delta = std::remainder(homeward - me.heading, glm::two_pi<float>());
				    turn = glm::clamp(delta, -0.8f, 0.8f);
			    }
			    me.heading += turn * seconds;
			    const auto way = glm::vec3(std::sin(me.heading), 0.0f, std::cos(me.heading));
			    auto position = transform.position + way * me.speed * seconds;
			    const float below = std::max(terrain.GetHeightAt(glm::vec2(position.x, position.z)), 0.0f);
			    const float wanted = below + me.altitude;
			    const float climb = glm::clamp((wanted - position.y) * 0.5f, -4.0f, 6.0f);
			    position.y += climb * seconds;
			    position.y = std::max(position.y, below + 2.0f);
			    FaceTowards(transform, transform.position + way);
			    // Banked into the turn.
			    transform.rotation = transform.rotation * glm::mat3(glm::eulerAngleZ(-turn * 0.5f));
			    transform.position = position;
			    MoveBody(entity, position);
			    play(climb > 1.5f || me.flapping ? wings->first : wings->second);
			    return;
		    }
		    const auto moves = LandAnimalMoves(animal.type);
		    if (!moves)
		    {
			    return;
		    }
		    me.timer -= seconds;
		    if (!me.walking)
		    {
			    play(moves->stand);
			    if (me.timer <= 0.0f)
			    {
				    // Somewhere near home on dry land.
				    for (int tries = 0; tries < 4; ++tries)
				    {
					    const float a = rng.NextValue(0.0f, glm::two_pi<float>());
					    const float r = rng.NextValue(3.0f, 14.0f);
					    const auto spot = me.home + glm::vec3(std::cos(a), 0.0f, std::sin(a)) * r;
					    if (SeaDepthAt(glm::vec2(spot.x, spot.z)) <= 0.0f)
					    {
						    me.target = spot;
						    me.walking = true;
						    break;
					    }
				    }
				    me.timer = rng.NextValue(5.0f, 15.0f);
			    }
			    return;
		    }
		    constexpr float k_AmbleSpeed = 1.0f;
		    const auto to = glm::vec2(me.target.x - transform.position.x, me.target.z - transform.position.z);
		    const float distance = glm::length(to);
		    if (distance < 0.3f)
		    {
			    me.walking = false;
			    me.timer = rng.NextValue(6.0f, 18.0f);
			    return;
		    }
		    const auto step = to / distance * std::min(k_AmbleSpeed * seconds, distance);
		    const auto next = glm::vec3(transform.position.x + step.x, 0.0f, transform.position.z + step.y);
		    FaceTowards(transform, glm::vec3(me.target.x, transform.position.y, me.target.z));
		    transform.position = glm::vec3(next.x, terrain.GetHeightAt(glm::vec2(next.x, next.z)), next.z);
		    MoveBody(entity, transform.position);
		    play(moves->walk);
	    });
}

void UpdateAnimations(float seconds)
{
	// The dead lie where they fell, and after a while are a skeleton, lying the same way: the original's skeleton
	// people have a villager's 22 bones, so the pose they died in fits them (Mike, 2026-10-18: there were no
	// skeletons). Not the drowned, who float at the sea's surface.
	{
		static std::unordered_map<uint32_t, float> s_deadFor;
		constexpr float k_BodyLies = 120.0f; // seconds before a body is a skeleton
		auto& reg = Locator::entitiesRegistry::value();
		for (auto it = s_deadFor.begin(); it != s_deadFor.end();)
		{
			it = g_fallenDead.contains(it->first) && reg.Valid(static_cast<entt::entity>(it->first)) ? std::next(it) : s_deadFor.erase(it);
		}
		for (const auto id : g_fallenDead)
		{
			const auto entity = static_cast<entt::entity>(id);
			auto* villager = reg.Valid(entity) ? reg.TryGet<openblack::ecs::components::Villager>(entity) : nullptr;
			auto* mesh = villager != nullptr ? reg.TryGet<openblack::ecs::components::Mesh>(entity) : nullptr;
			if (mesh == nullptr || g_inDeepWater.contains(id) || id == g_heldObject || g_flying.contains(id))
			{
				continue;
			}
			auto& deadFor = s_deadFor.try_emplace(id, 0.0f).first->second;
			const float before = deadFor;
			deadFor += seconds;
			if (before < k_BodyLies && deadFor >= k_BodyLies)
			{
				const auto skeleton = villager->sex == openblack::ecs::components::Villager::Sex::FEMALE ? MeshId::PersonSkeletonFemale
				                                                                                         : MeshId::PersonSkeletonMale;
				mesh->id = resources::HashIdentifier(skeleton);
				SPDLOG_LOGGER_INFO(spdlog::get("game"), "Villager {} is a skeleton now", id);
			}
		}
	}
	using openblack::ecs::components::Animated;
	using openblack::ecs::components::AnimatedStatic;
	using openblack::ecs::components::Mesh;
	using openblack::ecs::components::Villager;
	auto& registry = Locator::entitiesRegistry::value();

	// Villagers: the script's animation while it plays, else walk, run or stand by speed.
	registry.Each<const Villager, const Mesh, const Transform>(
	    [&registry, seconds](entt::entity entity, const Villager& villager, const Mesh&, const Transform& transform) {
		    const auto id = static_cast<uint32_t>(entity);
		    // Going about on its own, a villager is stepped by the pathfinder once a game turn: shown gliding from
		    // each step to the next over a turn, rather than jumping ten times a second (Mike, 2026-10-17: "jumpy").
		    // The pathfinder then steps on from where it is shown, a turn behind at most. Jumps over 5 are put
		    // straight there (a drop, a teleport).
		    if (!registry.AllOf<ScriptMove>(entity) && registry.AllOf<WallHug>(entity) && id != g_heldObject)
		    {
			    auto& glide = g_villagerGlides[id];
			    auto& placed = registry.Get<Transform>(entity);
			    if (glide.shown && glm::distance(*glide.shown, placed.position) > 1e-4f)
			    {
				    if (glm::distance(*glide.shown, placed.position) < 5.0f)
				    {
					    glide.from = *glide.shown;
					    glide.to = placed.position;
					    glide.t = 0.0f;
				    }
				    else
				    {
					    glide.from = glide.to = placed.position;
					    glide.t = 1.0f;
				    }
			    }
			    else if (!glide.shown)
			    {
				    glide.from = glide.to = placed.position;
				    glide.t = 1.0f;
			    }
			    glide.t = std::min(glide.t + seconds / 0.1f, 1.0f);
			    placed.position = glm::mix(glide.from, glide.to, glide.t);
			    glide.shown = placed.position;
		    }
		    else
		    {
			    g_villagerGlides.erase(id);
		    }
		    const auto last = g_lastPositions.find(id);
		    const auto moved = g_moverSpeeds.find(id);
		    const float speed = moved != g_moverSpeeds.end() && moved->second.frame + 1 >= g_scriptFrame ? moved->second.speed
		                        : last != g_lastPositions.end() && seconds > 0.0f
		                            ? glm::distance(glm::vec2(last->second.x, last->second.z),
		                                            glm::vec2(transform.position.x, transform.position.z)) /
		                                  seconds
		                            : 0.0f;
		    g_lastPositions[id] = transform.position;
		    if (static const char* s_trace = std::getenv("OPENBLACK_TRACE_WALK"); s_trace != nullptr && registry.AllOf<ScriptMove>(entity))
		    {
			    const auto* a = registry.TryGet<openblack::ecs::components::Animated>(entity);
			    SPDLOG_LOGGER_INFO(spdlog::get("game"), "TRACEWALK {} dt {:.4f} pos ({:.3f}, {:.3f}, {:.3f}) speed {:.3f} anim {} t {:.1f} script {} glide {}", id, seconds,
			                       transform.position.x, transform.position.y, transform.position.z, speed, a ? a->animation : 0u, a ? a->time : 0.0f,
			                       registry.AllOf<ScriptMove>(entity), g_villagerGlides.contains(id));
		    }
		    // Measured pace of villagers going about on their own (not builders, not a script's), logged now and then.
		    {
			    static std::vector<float> s_paces;
			    static float s_paceClock = 0.0f;
			    if (speed > 0.2f && !registry.AllOf<ScriptMove>(entity)) // builders and script walkers are held by a ScriptMove
			    {
				    s_paces.push_back(speed);
			    }
			    s_paceClock += seconds / 256.0f; // per villager per frame: roughly every few seconds of play
			    if (s_paceClock > 15.0f && !s_paces.empty())
			    {
				    s_paceClock = 0.0f;
				    std::nth_element(s_paces.begin(), s_paces.begin() + static_cast<long>(s_paces.size() / 2), s_paces.end());
				    SPDLOG_LOGGER_INFO(spdlog::get("game"), "Villagers on their own walk at {:.2f} units/s (median of {} samples); builders' walk {:.2f}",
				                       s_paces[s_paces.size() / 2], s_paces.size(), 0.0f); // builders: their state's speed now (Villager::SetStateSpeed)
				    s_paces.clear();
			    }
		    }

		    const bool woman = villager.sex == Villager::Sex::FEMALE;
		    uint32_t wanted;
		    // Drowning only in the water: a drowning man put on the land stops (Mike, 2026-10-17: carried out, he
		    // went on drowning there). Land is a cell without the water flag, as the original's ExitDrowning.
		    if (const auto drowning = g_scriptAnimations.find(id);
		        drowning != g_scriptAnimations.end() && drowning->second.animation == static_cast<uint32_t>(AnimId::PDrowning) &&
		        !IsWaterAt(glm::vec2(transform.position.x, transform.position.z)))
		    {
			    g_scriptAnimations.erase(drowning);
		    }
		    // Moved by a script, it plays a moving animation - the one the script gave it, else its walk or run - never a
		    // standing one with its feet sliding over the ground.
		    // (A move still going: a speed from the frame before a SET_POSITION stopped it took the opening's son's swim away
		    // and stood him up in the sea, 2026-10-23.)
		    const bool scriptMoving = moved != g_moverSpeeds.end() && moved->second.frame + 1 >= g_scriptFrame && speed > 0.2f &&
		                              (g_scriptMoves.contains(id) || g_walks.contains(id) || g_followers.contains(id));
		    if (scriptMoving)
		    {
			    g_scriptAnimations.erase(id);
		    }
		    const auto override = scriptMoving ? g_moveAnimations.find(id) : g_moveAnimations.end();
		    const auto scripted = g_scriptAnimations.find(id);
		    if (override != g_moveAnimations.end())
		    {
			    wanted = override->second;
		    }
		    else if (scripted != g_scriptAnimations.end())
		    {
			    wanted = scripted->second.animation;
		    }
		    else if (const auto* walking = registry.TryGet<LivingAction>(entity);
		             speed > 0.2f && walking != nullptr &&
		             static_cast<VillagerStates>(walking->states.at(static_cast<size_t>(LivingAction::Index::Final))) ==
		                 VillagerStates::HousewifeReturnHomeWithFood)
		    {
			    wanted = static_cast<uint32_t>(AnimId::PCarryingPot); // a housewife home with the dinner on her head
		    }
		    else if (speed > 0.2f)
		    {
			    // Villager::MoveToPosAnimation: walk, run or sprint by info.dat's speed thresholds for a man
			    // (SPEED_THRESHOLD_VILLAGER_MAN: walk to 3 m/s, run to 4) and anyone else (_NORMAL: 2, 3).
			    const auto& threshold = Locator::infoConstants::value().speedThreshold.at(woman ? 0 : 1);
			    const float walkMax = static_cast<float>(threshold.speedMaxWalk) * 100.0f / 65536.0f;
			    const float runMax = static_cast<float>(threshold.speedMaxRun) * 100.0f / 65536.0f;
			    wanted = static_cast<uint32_t>(speed <= walkMax ? (woman ? AnimId::PWalkWoman : AnimId::PWalkMan)
			                                   : speed <= runMax ? (woman ? AnimId::PRunWoman : AnimId::PRunMan)
			                                                     : (woman ? AnimId::PSprintRunWoman : AnimId::PSprintRunMan));
		    }
		    else
		    {
			    // Standing: what its state shows (Villager::SitDownAnimation picks one of two ways of sitting; a
			    // homeless villager sleeps out on the ground).
			    wanted = static_cast<uint32_t>(AnimId::PStand);
			    if (const auto* action = registry.TryGet<LivingAction>(entity))
			    {
				    const auto state = static_cast<VillagerStates>(action->states.at(static_cast<size_t>(LivingAction::Index::Top)));
				    if (state == VillagerStates::SitAndChillout)
				    {
					    wanted = static_cast<uint32_t>(id % 2 == 0 ? AnimId::PSittingDown1Sitting : AnimId::PSittingDown2Sitting);
				    }
				    else if (state == VillagerStates::HousewifePickupFromStoragePit)
				    {
					    wanted = static_cast<uint32_t>(AnimId::PPickUpPotOnHead);
				    }
				    else if (state == VillagerStates::EatFood)
				    {
					    wanted = static_cast<uint32_t>(AnimId::PEat); // Villager::EatFood (the state table's animation)
				    }
				    else if (state == VillagerStates::SleepInTent)
				    {
					    wanted = static_cast<uint32_t>(Locator::infoConstants::value().villagerStateTable.at(static_cast<size_t>(state)).field0x0);
				    }
			    }
		    }

		    auto* animated = registry.TryGet<Animated>(entity);
		    if (animated == nullptr || animated->animation != wanted)
		    {
			    animated = &registry.AssignOrReplace<Animated>(entity, wanted, 0.0f);
		    }
		    const auto [rate, duration] = AnimationRate(wanted);
		    // Walking and running play by the ground covered, so feet keep pace with the ground.
		    const auto& animations = Locator::resources::value().GetAnimations();
		    const auto animationId = openblack::resources::HashIdentifier(wanted);
		    // A script's animation plays at its own rate: the carrying ones are 92-frame loops said to cover about a
		    // metre, and played by the ground covered they shook (Mike, 2026-10-17: "wobbling").
		    const float stride = scripted == g_scriptAnimations.end() && animations.Contains(animationId)
		                             ? animations.Handle(animationId)->GetDistancePerPlay()
		                             : 0.0f;
		    animated->time += stride > 0.0f ? duration * speed * seconds / stride : rate * seconds;
		    if (g_fallenDead.contains(id) && duration > 0.0f)
		    {
			    animated->time = std::min(animated->time, duration - 1.0f); // killed by a landing: lies where it fell
		    }
		    else if (scripted != g_scriptAnimations.end() && duration > 0.0f && animated->time >= duration)
		    {
			    animated->time -= duration;
			    if (--scripted->second.playsLeft <= 0.0f)
			    {
				    g_scriptAnimations.erase(scripted); // PLAYED now reports it finished
			    }
		    }
		    else if (duration > 0.0f)
		    {
			    animated->time = std::fmod(animated->time, duration);
		    }
	    });

	// Animals: their type's default animation, over and over (they don't wander yet).
	// A thrown one plays its getting up (or falling dead) once, then goes back to its own (or lies dead).
	registry.Each<const openblack::ecs::components::Animal, Animated>(
	    [seconds](entt::entity entity, const openblack::ecs::components::Animal&, Animated& animated) {
		    const auto id = static_cast<uint32_t>(entity);
		    const auto [rate, duration] = AnimationRate(animated.animation);
		    if (duration <= 0.0f)
		    {
			    return;
		    }
		    if (g_thrownAnimalLanding.contains(id))
		    {
			    animated.time += rate * seconds;
			    if (animated.time >= duration)
			    {
				    if (g_fallenDead.contains(id))
				    {
					    animated.time = duration - 1.0f; // lies where it fell
					    return;
				    }
				    g_thrownAnimalLanding.erase(id);
				    if (const auto own = g_animalDefaultAnimation.find(id); own != g_animalDefaultAnimation.end())
				    {
					    animated.animation = own->second;
					    g_animalDefaultAnimation.erase(own);
				    }
				    animated.time = 0.0f;
			    }
			    return;
		    }
		    animated.time = std::fmod(animated.time + rate * seconds, duration);
	    });

	// Creatures: posed from their species' own body animations, standing until something says otherwise.
	g_leashTrace.actedThisFrame = false;
	++g_lifeFrame;
	registry.Each<const openblack::ecs::components::Creature, const Mesh>(
	    [&registry, seconds](entt::entity entity, const openblack::ecs::components::Creature& creature, const Mesh& mesh) {
		    const auto& poses = CreaturePoses(creature.species);
		    const auto id = static_cast<uint32_t>(entity);
		    auto& playing = g_creaturePlaying.try_emplace(id, CreaturePlaying {"stand", 0.0f}).first->second;
		    // A script's animation while it plays (PLAYED waits on it), then back to standing. One this species
		    // has no pose for counts as played at once, so a script waiting on it carries on.
		    auto scripted = g_scriptAnimations.find(id);
		    std::string wanted = "stand";
		    const bool dead = g_deadForever.contains(id);
		    // Its order (CREATURE_DO_ACTION) moves it and picks its animation, unless a script animation plays.
		    if (dead)
		    {
			    wanted = poses.contains("faint") ? std::string("faint") : std::string("stand");
			    scripted = g_scriptAnimations.end();
		    }
		    else if (const auto fighting = FightAnimation(id))
		    {
			    wanted = poses.contains(*fighting) ? *fighting : std::string("stand");
		    }
		    else if (const auto leashed = ActOutLeash(entity, seconds))
		    {
			    wanted = *leashed;
		    }
		    else if (const auto order = g_creatureOrders.find(id);
		             order != g_creatureOrders.end() && order->second.action != CreatureAction::BeIdle)
		    {
			    wanted = ActOutCreatureOrder(entity, order->second, seconds);
			    if (!poses.contains(wanted))
			    {
				    wanted = "stand"; // this species has no such animation
			    }
		    }
		    else if (id == PlayersCreature() && scripted == g_scriptAnimations.end() && id != g_heldObject && !InCutscene())
		    {
			    if (const auto life = ActOutCreatureLife(entity, poses, seconds))
			    {
				    wanted = poses.contains(*life) ? *life : std::string("stand");
			    }
		    }
		    if (scripted != g_scriptAnimations.end())
		    {
			    const auto& names = CreatureAnimationNames();
			    const auto number = scripted->second.animation;
			    if (number < names.size() && poses.contains(names[number]))
			    {
				    wanted = names[number];
			    }
			    else
			    {
				    SPDLOG_LOGGER_WARN(spdlog::get("scripting"), "Creature {} has no animation {} ({})", id, number,
				                       number < names.size() ? names[number] : "past the spec's list");
				    g_scriptAnimations.erase(scripted);
				    scripted = g_scriptAnimations.end();
			    }
		    }
		    if (playing.pose != wanted)
		    {
			    playing = {wanted, 0.0f};
		    }
		    const auto pose = poses.find(playing.pose);
		    const auto& meshes = Locator::resources::value().GetMeshes();
		    if (pose == poses.end() || pose->second.keyframes.empty() || !meshes.Contains(mesh.id))
		    {
			    return;
		    }
		    // One play's frames: a cyclic pose comes round to its first again, one played once ends on its last.
		    const auto frames = pose->second.header.unknown0x4 != 0
		                            ? static_cast<float>(pose->second.keyframes.size())
		                            : std::max(static_cast<float>(pose->second.keyframes.size()) - 1.0f, 1.0f);
		    // Walking and running go by ground covered, not time (LH3DCreature::AdvanceMovementAnimations): one play
		    // per the animation's root movement times the creature's scale, so its feet keep to the land. Standing
		    // still in either, or jumped somewhere, it goes by time.
		    const auto& position = registry.Get<Transform>(entity).position;
		    const auto last = g_creatureLastPosition.try_emplace(id, position).first;
		    const float moved = glm::distance(glm::vec2(position.x, position.z), glm::vec2(last->second.x, last->second.z));
		    last->second = position;
		    const auto& header = pose->second.header;
		    const float stride = glm::length(glm::vec3(header.unknown0x10, header.unknown0x14, header.unknown0x18)) *
		                         registry.Get<Transform>(entity).scale.x;
		    if ((playing.pose == "walk" || playing.pose == "run") && stride > 0.0f && moved > 0.0f && moved < stride)
		    {
			    playing.frame += moved / stride * frames;
		    }
		    else
		    {
			    playing.frame += seconds * CreatureFramesPerSecond(playing.pose, pose->second, CreatureSize(id));
		    }
		    if (dead)
		    {
			    playing.frame = std::min(playing.frame, frames - 1.0f); // fallen, it stays down
		    }
		    else if (playing.frame >= frames)
		    {
			    playing.frame = std::fmod(playing.frame, frames);
			    if (scripted != g_scriptAnimations.end() && --scripted->second.playsLeft <= 0.0f)
			    {
				    g_scriptAnimations.erase(scripted); // PLAYED now reports it finished
				    playing = {"stand", 0.0f};
			    }
		    }
		    const auto stand = poses.find("stand");
		    const auto* standing = stand != poses.end() ? &stand->second : nullptr;
		    auto bones = openblack::morphposes::CreatureBones(*meshes.Handle(mesh.id), pose->second, playing.frame, standing);
		    // Standing and walking mix by speed, as the original's (LH3DCreature::AdvanceMovementAnimations and
		    // UpdateBuffers): walking's weight is its speed over walking pace, the rest is standing's. Its speed eases
		    // up and down, so it sets off and comes to a stop through the mix instead of snapping between the two.
		    const auto walk = poses.find("walk");
		    if (!dead && scripted == g_scriptAnimations.end() && !g_fighters.contains(id) && standing != nullptr &&
		        !standing->keyframes.empty() && walk != poses.end() && !walk->second.keyframes.empty() &&
		        (playing.pose == "stand" || playing.pose == "walk"))
		    {
			    auto& gait = g_creatureGait[id];
			    // Faster than four times walking pace is a jump (put down, teleported), not a walk.
			    const float measured = seconds > 0.0f && moved < 4.0f * k_CreatureWalkSpeed * seconds ? moved / seconds : 0.0f;
			    const float change = k_CreatureAcceleration * seconds;
			    gait.speed = std::clamp(measured, gait.speed - change, gait.speed + change);
			    const float weight = std::clamp(gait.speed / k_CreatureWalkSpeed, 0.0f, 1.0f);
			    // The walk goes round once for each stride covered, its stride shrunk by its weight: its feet keep
			    // their pace through the mix.
			    const auto& walkHeader = walk->second.header;
			    const float walkStride = glm::length(glm::vec3(walkHeader.unknown0x10, walkHeader.unknown0x14, walkHeader.unknown0x18)) *
			                             registry.Get<Transform>(entity).scale.x;
			    const auto walkFrames = static_cast<float>(walk->second.keyframes.size());
			    if (weight > 0.0f && walkStride > 0.0f && measured > 0.0f)
			    {
				    gait.walkFrame += std::min(moved / (walkStride * weight), 1.0f) * walkFrames;
				    gait.walkFrame = std::fmod(gait.walkFrame, walkFrames);
			    }
			    const auto standFrames = static_cast<float>(standing->keyframes.size());
			    gait.standFrame = std::fmod(gait.standFrame + seconds * CreatureFramesPerSecond("stand", *standing, CreatureSize(id)),
			                                standFrames);
			    if (weight > 0.0f)
			    {
				    bones = openblack::morphposes::BlendedCreatureBones(
				        *meshes.Handle(mesh.id), {{standing, gait.standFrame, 1.0f - weight}, {&walk->second, gait.walkFrame, weight}},
				        standing);
			    }
			    else
			    {
				    bones = openblack::morphposes::CreatureBones(*meshes.Handle(mesh.id), *standing, gait.standFrame, standing);
			    }
		    }
		    // A fight pose kept on its feet: if its root sits lower than standing's, the creature is lifted by the
		    // difference (Mike, 2026-10-17: in a fight both looked sunk in the ground). Logged once per pose.
		    if (g_fighters.contains(id) && !bones.empty())
		    {
			    if (standing != nullptr && !standing->keyframes.empty())
			    {
				    const auto standBones = openblack::morphposes::CreatureBones(*meshes.Handle(mesh.id), *standing, 0.0f, standing);
				    auto& transform = registry.Get<Transform>(entity);
				    const float lift = standBones.empty() ? 0.0f : (standBones[0][3].y - bones[0][3].y) * transform.scale.y;
				    static std::set<std::pair<int, std::string>> s_reported;
				    if (s_reported.emplace(static_cast<int>(creature.species), playing.pose).second)
				    {
					    SPDLOG_LOGGER_INFO(spdlog::get("game"), "Fight pose {} of species {}: root {:.2f}, standing {:.2f}, lift {:.2f}",
					                       playing.pose, static_cast<int>(creature.species), bones[0][3].y,
					                       standBones.empty() ? 0.0f : standBones[0][3].y, lift);
				    }
				    transform.position.y =
				        Locator::terrainSystem::value().GetHeightAt(glm::vec2(transform.position.x, transform.position.z)) + std::max(lift, 0.0f);
			    }
		    }
		    if (!bones.empty())
		    {
			    registry.AssignOrReplace<Animated>(entity, 0u, 0.0f, std::move(bones));
		    }
	    });

	if (g_leash.type != 0 && (g_leashWalk.contains(g_leash.creature) || g_leashFetch.contains(g_leash.creature)) &&
	    !g_leashTrace.actedThisFrame && Every(g_leashTrace.lastNotActed, std::chrono::milliseconds(1000)))
	{
		const auto entity = static_cast<entt::entity>(g_leash.creature);
		SPDLOG_LOGGER_INFO(spdlog::get("game"), "Leash: creature {} has a walk or fetch but was not acted out this frame (valid {}, creature {}, mesh {})",
		                   g_leash.creature, registry.Valid(entity),
		                   registry.Valid(entity) && registry.AllOf<openblack::ecs::components::Creature>(entity),
		                   registry.Valid(entity) && registry.AllOf<Mesh>(entity));
	}
	UpdateCreatureHolding();

	// Anything else a script animated (not a villager): play its animation through, then drop it.
	for (auto it = g_scriptAnimations.begin(); it != g_scriptAnimations.end();)
	{
		const auto entity = static_cast<entt::entity>(it->first);
		if (!registry.Valid(entity))
		{
			it = g_scriptAnimations.erase(it);
			continue;
		}
		if (registry.AnyOf<Villager, openblack::ecs::components::Creature>(entity))
		{
			++it;
			continue;
		}
		auto* animated = registry.TryGet<Animated>(entity);
		const auto [rate, duration] = AnimationRate(it->second.animation);
		if (animated == nullptr || duration <= 0.0f)
		{
			it = g_scriptAnimations.erase(it);
			continue;
		}
		animated->time += rate * seconds;
		if (animated->time >= duration)
		{
			animated->time -= duration;
			if (--it->second.playsLeft <= 0.0f)
			{
				registry.Remove<Animated>(entity);
				it = g_scriptAnimations.erase(it);
				continue;
			}
		}
		++it;
	}

	// Animated scenery loops its default animation (chess pieces), except what opens - the Norse gate, the gate
	// plinth, the Piper cave, the phone box - whose animation is its opening: shut until SET_OPEN_CLOSE opens it,
	// then played through once and held open (and back again when closed).
	registry.Each<const AnimatedStatic, const Mesh>([&registry, seconds](entt::entity entity, const AnimatedStatic& info,
	                                                                     const Mesh&) {
		const auto animation =
		    static_cast<uint32_t>(Locator::infoConstants::value().animatedStatic.at(static_cast<size_t>(info.type)).defaultAnim);
		auto* animated = registry.TryGet<Animated>(entity);
		if (animated == nullptr || animated->animation != animation)
		{
			animated = &registry.AssignOrReplace<Animated>(entity, animation, 0.0f);
		}
		const auto [rate, duration] = AnimationRate(animation);
		const bool opens = info.type == AnimatedStaticInfo::NorseGate || info.type == AnimatedStaticInfo::GateStonePlinth ||
		                   info.type == AnimatedStaticInfo::PiperCaveEntrance || info.type == AnimatedStaticInfo::PhoneBox;
		if (!opens)
		{
			animated->time = duration > 0.0f ? std::fmod(animated->time + rate * seconds, duration) : 0.0f;
			return;
		}
		const bool open = g_openedStatics.contains(static_cast<uint32_t>(entity));
		const float end = std::max(duration - 1.0f, 0.0f); // the last keyframe, not wrapped round to the first
		animated->time = std::clamp(animated->time + (open ? rate : -rate) * seconds, 0.0f, end);
		if (info.type == AnimatedStaticInfo::GateStonePlinth)
		{
			CarryPlinthRiders(entity, open, animated->time);
		}
	});
}
} // namespace

namespace
{
constexpr float k_CarryHeight = 1.5f; // held things sit in the hand's grip (the hand is drawn 1.5 above the ground)
// Where the held thing was over the last moment, for the speed it's released with.
constexpr auto k_ThrowWindow = std::chrono::milliseconds(120);
// The hand's velocity as the original keeps it: smoothed, lagging the hand by about a tenth of a second (its
// throwrock.hnd hand demo records it, fields +100..+108, beside the hand's place: at the throw it read 90 units/s
// where the hand itself, frame to frame, was doing 120-240). What is let go of flies off at it.
glm::vec3 g_handVelocity {0.0f};
std::chrono::steady_clock::time_point g_pickedUpAt {}; // when the hand last picked something up
// The smoothed velocity over the last k_ThrowMemory: a throw goes at the fastest of it, so a swipe that has slowed
// or stopped by the time the trigger is let go still throws (Mike, 2026-10-17: the rock stopped where ZL was let go).
std::deque<std::pair<std::chrono::steady_clock::time_point, glm::vec3>> g_handVelocities;
constexpr auto k_ThrowMemory = std::chrono::milliseconds(400);
std::optional<std::pair<std::chrono::steady_clock::time_point, glm::vec3>> g_handLast;
constexpr float k_HandVelocityLag = 0.1f; // seconds
constexpr float k_Gravity = 9.81f; // world units are metres

// The air's hold on a thrown thing, as the original's (its PhysOb, read from its memory in flight on 2026-10-19):
// it slows by this times its speed times its velocity each second - its type's WindResist (PhysicsConstants.txt)
// x 0.3 x its radius squared, over its mass. Measured: a rock 0.5731 / 2000, a tree 5.4066 / 721.7, a villager
// 0.9163 / 72.5. Animals as villagers, and miracles as trees, until they are measured.
// An effect's sprite sheet, Data/Textures/<name>.raw (256x256 RGB) with its alpha <name>a.raw if any, made once. Never
// freed (see the miracle bubble's texture). Null if it cannot be read.
openblack::graphics::Texture2D* SpriteSheet(const std::string& name)
{
	static std::unordered_map<std::string, openblack::graphics::Texture2D*> s_sheets;
	if (const auto it = s_sheets.find(name); it != s_sheets.end())
	{
		return it->second;
	}
	auto& slot = s_sheets[name];
	try
	{
		auto& fs = Locator::filesystem::value();
		const auto textures = fs.GetPath<openblack::filesystem::Path::Data>() / "Textures";
		const auto rgb = fs.ReadAll(textures / (name + ".raw"));
		std::vector<uint8_t> alpha; // none for one drawn adding its light (the sun's), its alpha all 255
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
			slot = new openblack::graphics::Texture2D(name);
			slot->Create(256, 256, 1, openblack::graphics::TextureFormat::RGBA8, openblack::graphics::Wrapping::ClampEdge,
			             openblack::graphics::Filter::Linear, bgfx::copy(pixels.data(), static_cast<uint32_t>(pixels.size())));
			SPDLOG_LOGGER_INFO(spdlog::get("game"), "Sprite sheet {} loaded", name);
		}
	}
	catch (const std::exception& e)
	{
		SPDLOG_LOGGER_WARN(spdlog::get("game"), "Sprite sheet {}: {}", name, e.what());
	}
	return slot;
}

float AirDrag(entt::entity entity)
{
	auto& registry = Locator::entitiesRegistry::value();
	if (registry.AnyOf<openblack::ecs::components::Villager, openblack::ecs::components::Animal>(entity))
	{
		return 0.9163f / 72.54f;
	}
	if (registry.AllOf<openblack::ecs::components::Tree>(entity) || g_oneShotSpells.contains(static_cast<uint32_t>(entity)))
	{
		return 5.4066f / 721.73f;
	}
	return 0.5731f / 2000.0f;
}
std::deque<std::pair<std::chrono::steady_clock::time_point, glm::vec3>> g_handTrail;

void MoveBody(entt::entity entity, const glm::vec3& position)
{
	auto& registry = Locator::entitiesRegistry::value();
	if (auto* body = registry.TryGet<openblack::ecs::components::RigidBody>(entity))
	{
		btTransform transform = body->handle.getWorldTransform();
		transform.setOrigin(btVector3(position.x, position.y, position.z));
		body->handle.setWorldTransform(transform);
		body->motionState->setWorldTransform(transform); // the physics sync copies this back onto the Transform
	}
}
} // namespace

bool CanPickUp(uint32_t object)
{
	using namespace openblack::ecs::components;
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = static_cast<entt::entity>(object);
	if (object == 0 || !registry.Valid(entity) || !registry.AllOf<Transform>(entity))
	{
		return false;
	}
	// Not the things that make up the land and the towns, nor what a script has made unpickupable. Nor a fixed feature
	// (a rock pillar - Land 1's Fat Pilar Lime was being carried off), nor a singing stone's base: the holes of the
	// circle, which Land 1's script never makes unpickupable, so the original holds them by kind.
	auto refuse = [object](const char* why) {
		static uint32_t s_last = 0;
		if (s_last != object) // once per thing, not every frame the hand is over it
		{
			s_last = object;
			SPDLOG_LOGGER_INFO(spdlog::get("game"), "Hand can't pick up {}: {}", object, why);
		}
		return false;
	};
	if (const auto* mobile = registry.TryGet<MobileStatic>(entity); mobile != nullptr && mobile->type == MobileStaticInfo::SingingStoneBase)
	{
		return refuse("a singing stone's base");
	}
	// An animal its info.dat says the player can't pick up.
	if (const auto* animal = registry.TryGet<openblack::ecs::components::Animal>(entity))
	{
		const auto index = static_cast<size_t>(animal->type);
		const auto& infos = Locator::infoConstants::value().animal;
		if (index < infos.size() && infos.at(index).playerCanPickUp == 0)
		{
			return refuse("an animal info.dat keeps from the hand");
		}
	}
	// A man the script has drowning (CreatureSavingPeople): the creature rescues him, not the hand (Mike, 2026-10-19).
	// Not someone the player threw out of their depth (g_inDeepWater), who the hand may take out again.
	if (const auto drowning = g_scriptAnimations.find(object); drowning != g_scriptAnimations.end() &&
	    drowning->second.animation == static_cast<uint32_t>(AnimId::PDrowning) && !g_inDeepWater.contains(object))
	{
		return refuse("drowning, for the creature to rescue");
	}
	// The story's people (villager types 56 on: the Sculptor, the creature trainer, the Missionaries' sailors...) only
	// when a script has said so - the scripts make them pickupable and not, scene by scene, 145 times on Land 1.
	if (const auto* villager = registry.TryGet<Villager>(entity); villager != nullptr && !g_scriptPickupable.contains(object))
	{
		if (const auto record = g_scriptEntities.find(object); record != g_scriptEntities.end() && record->second.subtype >= 56)
		{
			return refuse("a story character no script has made pickupable");
		}
	}
	if (g_notPickupable.contains(object))
	{
		return refuse("made unpickupable by a script");
	}
	if (registry.AllOf<openblack::ecs::components::Asleep>(entity))
	{
		return refuse("asleep in its house");
	}
	return !registry.AnyOf<Abode, Temple, Town, Field, Footpath, Stream, Forest, Hand, Feature>(entity) &&
	       !g_notPickupable.contains(object);
}

uint32_t HandObjectUnder()
{
	if (g_heldObject != 0)
	{
		return 0;
	}
	return CanPickUp(g_objectUnderHand) ? g_objectUnderHand : g_woodUnderHand;
}

namespace
{
// Collecting from a wood pile while the Action Button is held (see HandCollect).
uint32_t g_collectingFrom = 0;
float g_collectSeconds = 0.0f;
float g_collectTurnTime = 0.0f;

const GPotInfo* PotInfoOf(entt::entity entity)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto* pot = registry.Valid(entity) ? registry.TryGet<openblack::ecs::components::Pot>(entity) : nullptr;
	if (pot == nullptr || pot->type == PotInfo::_COUNT)
	{
		return nullptr;
	}
	return &Locator::infoConstants::value().pot.at(static_cast<size_t>(pot->type));
}

bool IsHandWood(uint32_t object)
{
	const auto* info = PotInfoOf(static_cast<entt::entity>(object));
	return info != nullptr && &*info == &Locator::infoConstants::value().pot.at(static_cast<size_t>(PotInfo::HandWood));
}

// Grain (food) gathered into the hand from a store or a pile, as wood is (HandFood).
bool IsHandFood(uint32_t object)
{
	const auto* info = PotInfoOf(static_cast<entt::entity>(object));
	return info != nullptr && &*info == &Locator::infoConstants::value().pot.at(static_cast<size_t>(PotInfo::HandFood));
}

bool IsHandResource(uint32_t object)
{
	return IsHandWood(object) || IsHandFood(object);
}

ResourceType PileResource(uint32_t pile)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = static_cast<entt::entity>(pile);
	if (pile != 0 && registry.Valid(entity) && registry.AllOf<openblack::ecs::components::Field>(entity))
	{
		return ResourceType::Food; // a grain field
	}
	const auto* info = PotInfoOf(entity);
	return info != nullptr ? info->resourceType : ResourceType::None;
}

// What the hand collects from: a pile (a store's, or loose) or a grain field (Mike, 2026-10-18: collected just as from
// the store). How much is left in it, and taking some.
bool IsPile(entt::entity entity)
{
	auto& registry = Locator::entitiesRegistry::value();
	return PotInfoOf(entity) != nullptr || (registry.Valid(entity) && registry.AllOf<openblack::ecs::components::Field>(entity));
}

uint32_t PileAmount(entt::entity entity)
{
	auto& registry = Locator::entitiesRegistry::value();
	if (!registry.Valid(entity))
	{
		return 0;
	}
	if (const auto* field = registry.TryGet<openblack::ecs::components::Field>(entity))
	{
		return static_cast<uint32_t>(std::max(field->grain, 0.0f));
	}
	const auto* pot = registry.TryGet<openblack::ecs::components::Pot>(entity);
	return pot != nullptr ? pot->amount : 0;
}

void AddToPile(entt::entity entity, int32_t amount)
{
	auto& registry = Locator::entitiesRegistry::value();
	if (auto* field = registry.TryGet<openblack::ecs::components::Field>(entity))
	{
		field->grain = std::clamp(field->grain + static_cast<float>(amount), 0.0f, field->capacity);
		// Field::RemoveFood: a fully grown field emptied is bare - sown again from nothing.
		const auto& type = Locator::infoConstants::value().fieldType.at(field->fieldType);
		if (amount < 0 && field->grain <= 0.0f && field->growth >= type.ageRecolt)
		{
			field->crops = 0;
			field->growth = 0.0f;
		}
	}
	else if (auto* pot = registry.TryGet<openblack::ecs::components::Pot>(entity))
	{
		pot->amount = static_cast<uint16_t>(std::clamp<int32_t>(pot->amount + amount, 0, std::numeric_limits<uint16_t>::max()));
	}
}
} // namespace

namespace
{
glm::vec3 g_handGround(0.0f);
bool g_citadelInterface = true; // SET_INTERFACE_CITADEL
// The Temple's door, in the model's own space: Land 1's CitadelGuideCinema has it at (1902.97, 2516.50) for a
// Temple at (1915.05, 2508.89) turned by the feature script's rotation 36000.
constexpr glm::vec3 k_TempleDoorLocal(-6.0f, 0.0f, -12.95f);
constexpr float k_TempleDoorReach = 10.0f;
} // namespace

std::optional<glm::vec3> TempleDoor()
{
	using namespace openblack::ecs::components;
	auto& registry = Locator::entitiesRegistry::value();
	std::optional<glm::vec3> door;
	registry.Each<const Temple, const Transform>([&](entt::entity entity, const Temple&, const Transform& transform) {
		const auto* construction = registry.TryGet<Construction>(entity);
		if (!door && (construction == nullptr || construction->built >= 1.0f))
		{
			auto at = transform.position + transform.rotation * (k_TempleDoorLocal * transform.scale);
			at.y = Locator::terrainSystem::value().GetHeightAt(glm::vec2(at.x, at.z));
			door = at;
		}
	});
	return door;
}

void SetHandGround(const glm::vec3& position)
{
	g_handGround = position;
}


void SetHandRay(const glm::vec3& origin, const glm::vec3& direction)
{
	g_handRayOrigin = origin;
	g_handRayDirection = direction;
}

bool HandAtTempleDoor()
{
	const auto door = TempleDoor();
	if (!g_citadelInterface || !door || InTemple() ||
	    glm::distance(glm::vec2(door->x, door->z), glm::vec2(g_handGround.x, g_handGround.z)) >= k_TempleDoorReach)
	{
		return false;
	}
	// Only the doorway itself: a press on your creature, a person or an animal standing in front of it is on them.
	const auto creature = PlayersCreature();
	if ((creature != 0 && CreatureRayHit(static_cast<entt::entity>(creature), g_handRayOrigin, g_handRayDirection)) ||
	    VillagerOnRay(g_handRayOrigin, g_handRayDirection) != 0)
	{
		return false;
	}
	bool animal = false;
	Locator::entitiesRegistry::value().Each<const openblack::ecs::components::Animal>(
	    [&](entt::entity entity, const openblack::ecs::components::Animal&) {
		    animal = animal || CreatureRayHit(entity, g_handRayOrigin, g_handRayDirection).has_value();
	    });
	return !animal;
}

bool InTemple()
{
	return Locator::temple::has_value() && Locator::temple::value().Active();
}

namespace
{
// Inside the Temple (the rooms are built round the world origin, in the Temple's own coordinates). Each room has a
// camera path in Data/Citadel/engine/<room>.cam: u32 file size, u32 duration (ms), u32 frame count, then per frame
// the camera position and focus. main.cam walks in from the entrance; each other room's walks from its doorway
// in the main room to where the player stands in it.
struct TempleRoomPath
{
	std::string name;
	std::string file;
	float seconds = 0.0f;
	std::vector<std::pair<glm::vec3, glm::vec3>> frames; // position, focus
};
std::vector<TempleRoomPath> g_templeRooms; // [0] is the main room
size_t g_templeRoom = 0;
// A path being walked: which room's, forwards (into it) or back (out to the main room), and how far.
std::optional<size_t> g_templeWalk;
bool g_templeWalkBack = false;
float g_templeWalkTime = 0.0f;
float g_templeYaw = 0.0f;   // radians about the vertical, 0 looking along -Z
float g_templePitch = 0.0f; // radians, up positive
constexpr float k_TempleTurnSpeed = 1.5f; // radians a second, rotating or tilting
constexpr float k_TempleDoorwayAngle = glm::radians(12.0f); // how near a long press must be to a doorway
constexpr float k_TempleMapRadius = 15.0f; // the map in the middle of the main room's floor (its pool)

void LoadTempleRooms()
{
	if (!g_templeRooms.empty())
	{
		return;
	}
	const std::array<std::pair<const char*, const char*>, 7> rooms = {{{"Main Room", "main.cam"},
	                                                                   {"Challenge Room", "challenge.cam"},
	                                                                   {"Creature Lair", "creature.cam"},
	                                                                   {"Library", "credits.cam"},
	                                                                   {"Future Room", "multi.cam"},
	                                                                   {"Options Room", "options.cam"},
	                                                                   {"Save/Load Game Room", "savegame.cam"}}};
	auto& fileSystem = Locator::filesystem::value();
	for (const auto& [name, file] : rooms)
	{
		TempleRoomPath room {name, file};
		try
		{
			const auto data = fileSystem.ReadAll(fileSystem.GetPath<openblack::filesystem::Path::Citadel>() / "engine" / file);
			uint32_t header[3] = {};
			if (data.size() >= sizeof(header))
			{
				std::memcpy(header, data.data(), sizeof(header));
				room.seconds = static_cast<float>(header[1]) / 1000.0f;
				for (uint32_t i = 0; i < header[2] && 12 + (i + 1) * 24 <= data.size(); ++i)
				{
					float f[6];
					std::memcpy(f, data.data() + 12 + i * 24, sizeof(f));
					room.frames.emplace_back(glm::vec3(f[0], f[1], f[2]), glm::vec3(f[3], f[4], f[5]));
				}
			}
		}
		catch (const std::exception& e)
		{
			SPDLOG_LOGGER_ERROR(spdlog::get("game"), "Temple room {}: {}", file, e.what());
		}
		g_templeRooms.push_back(std::move(room));
	}
}

// Where the player stands in a room: the middle of the main room (at the height its path ends), elsewhere the end of
// the room's path.
glm::vec3 TempleRoomSpot(size_t room)
{
	if (room >= g_templeRooms.size() || g_templeRooms[room].frames.empty())
	{
		return glm::vec3(0.0f, 7.0f, 0.0f);
	}
	const auto end = g_templeRooms[room].frames.back().first;
	return room == 0 ? glm::vec3(0.0f, end.y, 0.0f) : end;
}

// Where the view stands once a walk in has ended: the walk's own last point, so the hand-over is seamless (Mike,
// 2026-10-17: entering the Temple ended its track, then the view jumped to the room's standing spot). None after
// walking back out: the main room's spot, as before.
std::optional<glm::vec3> g_templeStand;

void LookAlong(const glm::vec3& from, const glm::vec3& to)
{
	const auto d = to - from;
	g_templeYaw = std::atan2(-d.x, -d.z);
	g_templePitch = std::atan2(d.y, glm::length(glm::vec2(d.x, d.z)));
}

void StartTempleWalk(size_t room, bool back)
{
	if (room >= g_templeRooms.size() || g_templeRooms[room].frames.empty())
	{
		return;
	}
	g_templeWalk = room;
	g_templeWalkBack = back;
	g_templeWalkTime = 0.0f;
	SPDLOG_LOGGER_INFO(spdlog::get("game"), "Temple: {} the {}", back ? "back out of" : "into", g_templeRooms[room].name);
}
} // namespace

void EnterTemple()
{
	if (Locator::temple::has_value() && !InTemple())
	{
		SPDLOG_LOGGER_INFO(spdlog::get("game"), "Entering the Temple");
		Locator::temple::value().Activate();
		LoadTempleRooms();
		g_templeRoom = 0;
		StartTempleWalk(0, false); // in through the entrance to the middle of the main room
	}
}

void ExitTemple()
{
	g_fightViewExit = true; // B also leaves the side-on fight view
	if (InTemple())
	{
		SPDLOG_LOGGER_INFO(spdlog::get("game"), "Leaving the Temple");
		Locator::temple::value().Deactivate();
		g_templeWalk.reset();
		g_templeStand.reset();
	}
}

// Inside the Temple the camera stays where the player stands: rotating and tilting turn the view in place, and
// moving and zooming do nothing. Called every frame after the camera's own update, whose moves it overrides.
void UpdateTemple(std::chrono::microseconds dt)
{
	if (!InTemple() || g_templeRooms.empty())
	{
		return;
	}
	const float seconds = std::min(std::chrono::duration<float>(dt).count(), 0.1f);
	auto& camera = Locator::camera::value();
	if (g_templeWalk)
	{
		const auto& room = g_templeRooms[*g_templeWalk];
		g_templeWalkTime += seconds;
		const float t = room.seconds > 0.0f ? std::min(g_templeWalkTime / room.seconds, 1.0f) : 1.0f;
		const float along = (g_templeWalkBack ? 1.0f - t : t) * static_cast<float>(room.frames.size() - 1);
		const auto a = static_cast<size_t>(along);
		const auto b = std::min(a + 1, room.frames.size() - 1);
		const float f = along - static_cast<float>(a);
		const auto origin = glm::mix(room.frames[a].first, room.frames[b].first, f);
		const auto focus = glm::mix(room.frames[a].second, room.frames[b].second, f);
		camera.SetOrigin(origin);
		camera.SetFocus(focus);
		if (t >= 1.0f)
		{
			// Arrived: stand in the room (back in the main room's middle after walking out of one), looking on.
			g_templeRoom = g_templeWalkBack ? 0 : *g_templeWalk;
			LookAlong(origin, focus);
			g_templeStand = g_templeWalkBack ? std::nullopt : std::optional(origin);
			if (g_templeWalkBack)
			{
				g_templeYaw += glm::pi<float>(); // facing back into the main room
				g_templePitch = 0.0f;
			}
			g_templeWalk.reset();
			SPDLOG_LOGGER_INFO(spdlog::get("game"), "Temple: in the {}", g_templeRooms[g_templeRoom].name);
		}
		return;
	}
	const auto& actions = Locator::gameActionSystem::value();
	using openblack::input::BindableActionMap;
	if (actions.Get(BindableActionMap::ROTATE_LEFT))
	{
		g_templeYaw += k_TempleTurnSpeed * seconds;
	}
	if (actions.Get(BindableActionMap::ROTATE_RIGHT))
	{
		g_templeYaw -= k_TempleTurnSpeed * seconds;
	}
	if (actions.Get(BindableActionMap::TILT_UP))
	{
		g_templePitch += k_TempleTurnSpeed * seconds;
	}
	if (actions.Get(BindableActionMap::TILT_DOWN))
	{
		g_templePitch -= k_TempleTurnSpeed * seconds;
	}
	g_templePitch = glm::clamp(g_templePitch, glm::radians(-89.0f), glm::radians(89.0f));
	const auto spot = g_templeStand ? *g_templeStand : TempleRoomSpot(g_templeRoom);
	const auto look = glm::vec3(-std::sin(g_templeYaw) * std::cos(g_templePitch), std::sin(g_templePitch),
	                            -std::cos(g_templeYaw) * std::cos(g_templePitch));
	camera.SetOrigin(spot);
	camera.SetFocus(spot + look * 10.0f);
}

// A long press inside the Temple, at a point on the screen (0..1 each way): in the main room, on a doorway walks into
// that room and on the map in the floor leaves the Temple for that place on the land; in another room, on its
// doorway walks back out to the main room.
void TempleLongPress(glm::vec2 screen)
{
	if (!InTemple() || g_templeWalk || g_templeRooms.empty())
	{
		return;
	}
	auto& camera = Locator::camera::value();
	glm::vec3 origin;
	glm::vec3 direction;
	camera.DeprojectScreenToWorld(screen, origin, direction);
	direction = glm::normalize(direction);
	auto nearDoorway = [&](size_t room) {
		const auto& frames = g_templeRooms[room].frames;
		if (frames.empty())
		{
			return false;
		}
		const auto toDoor = frames.front().first - origin;
		return glm::length(toDoor) > 0.1f && glm::acos(glm::clamp(glm::dot(glm::normalize(toDoor), direction), -1.0f, 1.0f)) <
		                                          k_TempleDoorwayAngle;
	};
	if (g_templeRoom != 0)
	{
		if (nearDoorway(g_templeRoom))
		{
			StartTempleWalk(g_templeRoom, true);
		}
		return;
	}
	for (size_t room = 1; room < g_templeRooms.size(); ++room)
	{
		if (nearDoorway(room))
		{
			StartTempleWalk(room, false);
			return;
		}
	}
	// The map: where the press meets the floor, within the pool in the middle, is that place on the land.
	if (direction.y < -0.01f)
	{
		const auto floor = origin + direction * (-origin.y / direction.y);
		if (glm::length(glm::vec2(floor.x, floor.z)) < k_TempleMapRadius)
		{
			const auto extent = Locator::terrainSystem::value().GetExtent();
			const auto uv = (glm::vec2(floor.x, floor.z) + k_TempleMapRadius) / (2.0f * k_TempleMapRadius);
			const auto land = glm::mix(extent.minimum, extent.maximum, uv);
			SPDLOG_LOGGER_INFO(spdlog::get("game"), "Temple map: to ({:.0f}, {:.0f})", land.x, land.y);
			ExitTemple();
			const float ground = Locator::terrainSystem::value().GetHeightAt(land);
			const auto target = glm::vec3(land.x, ground, land.y);
			camera.SetOrigin(target + glm::vec3(0.0f, 60.0f, 60.0f));
			camera.SetFocus(target);
		}
	}
}

namespace
{
// The gate stone pedestal (Land 1's "Gate Stone Plinth"): the gate totems go on its top, one on another, and
// OBJECT_INFO_BITS on it says which are there (ChooseYourCreature waits for 7: all three).
std::unordered_map<uint32_t, std::vector<uint32_t>> g_pedestalStones;
// Gate totems a script deleted once they were on the pedestal (ProtectGateKey deletes its key when it is in place):
// set into it for good - a copy that cannot be taken stays there, still counted - by pedestal: bits and copies.
std::unordered_map<uint32_t, int> g_pedestalSetBits;
std::unordered_map<uint32_t, std::vector<entt::entity>> g_pedestalSetStones;
constexpr float k_PedestalReach = 3.0f; // how far out from the pedestal's edge a stone let go still goes on it

struct Pedestal
{
	entt::entity entity;
	glm::vec3 center;
	float top;    // the top of the pedestal, or of the stones on it
	float radius; // its footprint
};

float MeshHeight(entt::entity entity)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto* mesh = registry.TryGet<openblack::ecs::components::Mesh>(entity);
	const auto* transform = registry.TryGet<Transform>(entity);
	if (mesh == nullptr || transform == nullptr)
	{
		return 0.0f;
	}
	return Locator::resources::value().GetMeshes().Handle(mesh->id)->GetBoundingBox().Size().y * transform->scale.y;
}

// The pedestal whose top a point is over (within reach of its footprint), if any.
std::optional<Pedestal> PedestalUnder(const glm::vec3& point)
{
	using namespace openblack::ecs::components;
	auto& registry = Locator::entitiesRegistry::value();
	std::optional<Pedestal> found;
	registry.Each<const AnimatedStatic, const Transform, const Mesh>(
	    [&](entt::entity entity, const AnimatedStatic& info, const Transform& transform, const Mesh& mesh) {
		    if (found || info.type != AnimatedStaticInfo::GateStonePlinth)
		    {
			    return;
		    }
		    const auto box = Locator::resources::value().GetMeshes().Handle(mesh.id)->GetBoundingBox();
		    const float radius = std::max(box.Size().x, box.Size().z) * 0.5f * std::max(transform.scale.x, transform.scale.z);
		    if (glm::distance(glm::vec2(point.x, point.z), glm::vec2(transform.position.x, transform.position.z)) >
		        radius + k_PedestalReach)
		    {
			    return;
		    }
		    float top = transform.position.y + box.maxima.y * transform.scale.y;
		    for (const auto stone : g_pedestalStones[static_cast<uint32_t>(entity)])
		    {
			    top += MeshHeight(static_cast<entt::entity>(stone));
		    }
		    for (const auto stone : g_pedestalSetStones[static_cast<uint32_t>(entity)])
		    {
			    top += registry.Valid(stone) ? MeshHeight(stone) : 0.0f;
		    }
		    // The middle of its top, by its shape: a model's origin need not be its middle.
		    const auto middle = transform.position + transform.rotation * (box.Center() * transform.scale);
		    found = Pedestal {entity, glm::vec3(middle.x, transform.position.y, middle.z), top, radius};
	    });
	return found;
}

bool IsGateTotem(entt::entity entity)
{
	const auto* mobile = Locator::entitiesRegistry::value().TryGet<openblack::ecs::components::MobileStatic>(entity);
	return mobile != nullptr && (mobile->type == MobileStaticInfo::GateTotemApe || mobile->type == MobileStaticInfo::GateTotemCow ||
	                             mobile->type == MobileStaticInfo::GateTotemTiger || mobile->type == MobileStaticInfo::GateTotemBlank);
}

// A gate totem let go (or landing) over the pedestal goes on the centre of its top, on any stones already there.
bool SetOnPedestal(entt::entity entity, const glm::vec3& at)
{
	if (!IsGateTotem(entity))
	{
		return false;
	}
	const auto pedestal = PedestalUnder(at);
	if (!pedestal)
	{
		return false;
	}
	auto& registry = Locator::entitiesRegistry::value();
	auto& transform = registry.Get<Transform>(entity);
	transform.rotation = registry.Get<Transform>(pedestal->entity).rotation;
	// The stone's own middle over the pedestal's, its bottom on the top (the Ape's origin is off to one side).
	glm::vec3 base(0.0f);
	if (const auto* mesh = registry.TryGet<openblack::ecs::components::Mesh>(entity))
	{
		const auto box = Locator::resources::value().GetMeshes().Handle(mesh->id)->GetBoundingBox();
		base = glm::vec3(box.Center().x, box.minima.y, box.Center().z);
	}
	transform.position = glm::vec3(pedestal->center.x, pedestal->top, pedestal->center.z) -
	                     transform.rotation * (base * transform.scale);
	MoveBody(entity, transform.position);
	g_flying.erase(static_cast<uint32_t>(entity));
	g_pedestalStones[static_cast<uint32_t>(pedestal->entity)].push_back(static_cast<uint32_t>(entity));
	SPDLOG_LOGGER_INFO(spdlog::get("game"), "Gate totem {} set on the pedestal ({} there)", static_cast<uint32_t>(entity),
	                   g_pedestalStones[static_cast<uint32_t>(pedestal->entity)].size());
	return true;
}

int GateTotemBit(entt::entity entity)
{
	const auto* mobile = Locator::entitiesRegistry::value().TryGet<openblack::ecs::components::MobileStatic>(entity);
	if (mobile == nullptr)
	{
		return 0;
	}
	return mobile->type == MobileStaticInfo::GateTotemApe     ? 1
	       : mobile->type == MobileStaticInfo::GateTotemTiger ? 2
	       : mobile->type == MobileStaticInfo::GateTotemCow   ? 4
	                                                          : 0;
}

// A gate totem on the pedestal a script deletes stays set into it: a copy of it (mesh and place only, so the hand
// cannot take it) and its bit. Returns whether it was on a pedestal.
bool SetIntoPedestal(uint32_t object)
{
	auto& registry = Locator::entitiesRegistry::value();
	for (auto& [pedestal, stones] : g_pedestalStones)
	{
		if (std::find(stones.begin(), stones.end(), object) == stones.end())
		{
			continue;
		}
		const auto entity = static_cast<entt::entity>(object);
		const auto copy = registry.Create();
		registry.Assign<Transform>(copy, registry.Get<Transform>(entity));
		registry.Assign<openblack::ecs::components::Mesh>(copy, registry.Get<openblack::ecs::components::Mesh>(entity));
		g_pedestalSetBits[pedestal] |= GateTotemBit(entity);
		g_pedestalSetStones[pedestal].push_back(copy);
		SPDLOG_LOGGER_INFO(spdlog::get("game"), "Gate totem {} set into the pedestal", object);
		return true;
	}
	return false;
}

void TakeOffPedestal(uint32_t object)
{
	for (auto& [pedestal, stones] : g_pedestalStones)
	{
		stones.erase(std::remove(stones.begin(), stones.end(), object), stones.end());
	}
}
} // namespace

// What the hand gathers from where it is (wood or grain). Over a village store: its circle split in two - the half
// toward its food pile gives grain, the other half wood (Mike, 2026-10-18: no more than which half you are on).
// Elsewhere: the nearest loose pile of either within `radius`.
uint32_t FindWoodNear(const glm::vec3& point, float radius)
{
	using namespace openblack::ecs::components;
	auto& registry = Locator::entitiesRegistry::value();
	uint32_t fromStore = 0;
	registry.Each<const StoragePit, const Transform>([&](entt::entity, const StoragePit& pit, const Transform& transform) {
		if (fromStore != 0)
		{
			return;
		}
		const auto centre = glm::vec2(transform.position.x, transform.position.z);
		const auto hand = glm::vec2(point.x, point.z);
		constexpr float k_StoreReach = 14.0f; // the store's circle
		if (glm::distance(centre, hand) > k_StoreReach)
		{
			return;
		}
		auto at = [&](entt::entity e) {
			const auto& where = registry.Get<Transform>(e).position;
			return glm::vec2(where.x, where.z);
		};
		glm::vec2 woods(0.0f);
		int woodCount = 0;
		for (const auto pile : pit.woodPiles)
		{
			if (pile != entt::null && registry.Valid(pile) && registry.AllOf<Transform>(pile))
			{
				woods += at(pile);
				++woodCount;
			}
		}
		const bool haveFood = pit.foodPile != entt::null && registry.Valid(pit.foodPile) && registry.AllOf<Transform>(pit.foodPile);
		// The food half faces from the wood piles toward the food pile (from the centre, if one side is missing).
		const auto foodSide = (haveFood ? at(pit.foodPile) : centre) - (woodCount > 0 ? woods / static_cast<float>(woodCount) : centre);
		const bool onFood = glm::length(foodSide) > 1e-3f ? glm::dot(hand - centre, foodSide) >= 0.0f : true;
		if (onFood)
		{
			if (haveFood && registry.Get<Pot>(pit.foodPile).amount > 0)
			{
				fromStore = static_cast<uint32_t>(pit.foodPile);
			}
			return;
		}
		// Collecting already: the same pile till it is empty (choosing the fullest each frame switched piles as they
		// emptied, and collecting stopped at the first handful - Mike, 2026-10-18).
		for (const auto pile : pit.woodPiles)
		{
			if (static_cast<uint32_t>(pile) == g_collectingFrom && g_collectingFrom != 0 && registry.Valid(pile) &&
			    registry.AllOf<Pot>(pile) && registry.Get<Pot>(pile).amount > 0)
			{
				fromStore = g_collectingFrom;
				return;
			}
		}
		uint32_t fullest = 0;
		uint16_t most = 0;
		for (const auto pile : pit.woodPiles)
		{
			if (pile != entt::null && registry.Valid(pile) && registry.AllOf<Pot>(pile) && registry.Get<Pot>(pile).amount > most)
			{
				most = registry.Get<Pot>(pile).amount;
				fullest = static_cast<uint32_t>(pile);
			}
		}
		fromStore = fullest;
	});
	if (fromStore != 0)
	{
		return fromStore;
	}
	// A grain field the hand is over, with grain left: inside its wheat's square (the mesh's box, turned with it).
	uint32_t fromField = 0;
	registry.Each<const openblack::ecs::components::Field, const Transform, const Mesh>(
	    [&](entt::entity entity, const openblack::ecs::components::Field& field, const Transform& transform, const Mesh& mesh) {
		    if (fromField != 0 || field.grain < 1.0f || !Locator::resources::value().GetMeshes().Contains(mesh.id))
		    {
			    return;
		    }
		    const auto box = Locator::resources::value().GetMeshes().Handle(mesh.id)->GetBoundingBox();
		    const auto local = glm::transpose(transform.rotation) * (point - transform.position);
		    const auto centre = box.Center() * transform.scale;
		    const auto half = box.Size() * 0.5f * transform.scale;
		    if (std::abs(local.x - centre.x) <= half.x + 1.0f && std::abs(local.z - centre.z) <= half.z + 1.0f)
		    {
			    fromField = static_cast<uint32_t>(entity);
		    }
	    });
	if (fromField != 0)
	{
		return fromField;
	}
	std::unordered_set<uint32_t> storePiles;
	registry.Each<const StoragePit>([&](entt::entity, const StoragePit& pit) {
		storePiles.insert(static_cast<uint32_t>(pit.foodPile));
		for (const auto pile : pit.woodPiles)
		{
			storePiles.insert(static_cast<uint32_t>(pile));
		}
	});
	uint32_t nearest = 0;
	float nearestDistance = radius * radius;
	registry.Each<const Pot, const Transform>([&](entt::entity entity, const Pot& pot, const Transform& transform) {
		const auto resource = pot.type == PotInfo::_COUNT ? ResourceType::None
		                                                  : Locator::infoConstants::value().pot.at(static_cast<size_t>(pot.type)).resourceType;
		if (pot.amount == 0 || pot.type == PotInfo::_COUNT || pot.type == PotInfo::HandWood || pot.type == PotInfo::HandFood ||
		    static_cast<uint32_t>(entity) == g_heldObject || storePiles.contains(static_cast<uint32_t>(entity)) ||
		    (resource != ResourceType::Wood && resource != ResourceType::Food))
		{
			return;
		}
		const auto offset = glm::vec2(transform.position.x - point.x, transform.position.z - point.z);
		const float distance = glm::dot(offset, offset);
		if (distance < nearestDistance)
		{
			nearestDistance = distance;
			nearest = static_cast<uint32_t>(entity);
		}
	});
	return nearest;
}

void SetWoodUnderHand(uint32_t pile)
{
	g_woodUnderHand = pile;
}

uint32_t HandWoodUnder()
{
	// Empty-handed, or already holding wood (which collecting adds to).
	// Empty-handed, or holding the same (wood onto wood, grain onto grain).
	if (g_heldObject == 0)
	{
		return g_woodUnderHand;
	}
	const auto under = PileResource(g_woodUnderHand);
	return (IsHandWood(g_heldObject) && under == ResourceType::Wood) || (IsHandFood(g_heldObject) && under == ResourceType::Food)
	           ? g_woodUnderHand
	           : 0;
}

// Grain or wood in the hand pressed over a village store goes straight into its reserve (HandDrop's store branch),
// with nothing put down in between. Collecting more onto it there is for loose piles only (Mike, 2026-10-18: a
// second press over the store had collected more).
bool HandDepositIntoStore(const glm::vec3& point)
{
	if (g_heldObject == 0 || !IsHandResource(g_heldObject))
	{
		return false;
	}
	auto& registry = Locator::entitiesRegistry::value();
	bool overStore = false;
	registry.Each<const openblack::ecs::components::StoragePit, const Transform>(
	    [&](entt::entity, const openblack::ecs::components::StoragePit&, const Transform& where) {
		    overStore = overStore ||
		                glm::distance(glm::vec2(where.position.x, where.position.z), glm::vec2(point.x, point.z)) <= 14.0f;
	    });
	if (!overStore)
	{
		return false;
	}
	HandDrop(point);
	return true;
}

namespace
{
// G_PickUpWood (the bank's 98) looping while wood is collected, G_PickUpFood (44) while grain is (Mike, 2026-10-18).
entt::entity g_collectWoodSound = entt::null;

void StopCollectWoodSound();

// The collecting loop for what `pile` gives (wood or grain), if it isn't already playing.
void StartCollectSound(uint32_t pile)
{
	const auto resource = PileResource(pile);
	if (g_collectWoodSound != entt::null || (resource != ResourceType::Wood && resource != ResourceType::Food))
	{
		return;
	}
	const auto at = Locator::entitiesRegistry::value().Get<Transform>(static_cast<entt::entity>(pile)).position;
	g_collectWoodSound = PlayBankSound(1, resource == ResourceType::Wood ? 98 : 44, &at, openblack::audio::PlayType::Repeat);
}

void StopCollectWoodSound()
{
	if (g_collectWoodSound != entt::null && Locator::audio::has_value())
	{
		auto& audio = Locator::audio::value();
		audio.StopEmitter(g_collectWoodSound);
		audio.DestroyEmitter(g_collectWoodSound);
	}
	g_collectWoodSound = entt::null;
}
} // namespace

bool HandCollectBegin(uint32_t pile)
{
	if (pile == 0 || (g_heldObject != 0 && !IsHandResource(g_heldObject)))
	{
		return false;
	}
	g_collectingFrom = pile;
	g_collectSeconds = 0.0f;
	g_collectTurnTime = 0.0f;
	StopCollectWoodSound();
	StartCollectSound(pile);
	HandCollect(std::chrono::microseconds(0));
	return true;
}

void HandCollectEnd()
{
	StopCollectWoodSound();
	if (g_collectingFrom != 0 && IsHandResource(g_heldObject))
	{
		SPDLOG_LOGGER_INFO(spdlog::get("game"), "Hand holding {} wood after {:.1f} s",
		                   Locator::entitiesRegistry::value()
		                       .Get<openblack::ecs::components::Pot>(static_cast<entt::entity>(g_heldObject))
		                       .amount,
		                   g_collectSeconds);
	}
	g_collectingFrom = 0;
}

bool HandCollecting()
{
	return g_collectingFrom != 0;
}

void HandCollect(std::chrono::microseconds dt)
{
	using namespace openblack::ecs::components;
	auto& registry = Locator::entitiesRegistry::value();
	// Still held over the same store or pile kind, moved on from an emptied pile: on to the one under the hand.
	if (g_woodUnderHand != 0 && g_woodUnderHand != g_collectingFrom && g_heldObject != 0 &&
	    PileResource(g_woodUnderHand) == (IsHandFood(g_heldObject) ? ResourceType::Food : ResourceType::Wood) &&
	    (g_collectingFrom == 0 || PileAmount(static_cast<entt::entity>(g_collectingFrom)) == 0))
	{
		g_collectingFrom = g_woodUnderHand;
	}
	const auto pileEntity = static_cast<entt::entity>(g_collectingFrom);
	if (!IsPile(pileEntity) || g_woodUnderHand != g_collectingFrom)
	{
		StopCollectWoodSound(); // nothing coming in: quiet (Mike, 2026-10-18)
		return; // the hand has moved off the pile: holding on keeps what it has
	}
	// The original's numbers (info.dat, on the hand's wood - HandWood): an amount at once, then an amount each
	// game turn that ramps from amountPickedUpPerTurn to amountPickedUpPerTurnEnd over multiPickUpRampTime
	// seconds, up to maxAmountCanBePickedUp (25, 8 -> 70 over 6 s, 20000).
	const bool grain = PileResource(g_collectingFrom) == ResourceType::Food;
	const auto handPot = grain ? PotInfo::HandFood : PotInfo::HandWood;
	const auto& info = Locator::infoConstants::value().pot.at(static_cast<size_t>(handPot));
	const bool first = g_heldObject == 0;
	uint32_t take = 0;
	if (first)
	{
		take = info.amountPickedUpInitially;
	}
	else
	{
		const float seconds = std::chrono::duration<float>(dt).count();
		g_collectSeconds += seconds;
		g_collectTurnTime += seconds;
		constexpr float k_Turn = 0.1f;
		while (g_collectTurnTime >= k_Turn)
		{
			g_collectTurnTime -= k_Turn;
			const float ramp =
			    info.multiPickUpRampTime > 0.0f ? glm::clamp(g_collectSeconds / info.multiPickUpRampTime, 0.0f, 1.0f) : 1.0f;
			take += static_cast<uint32_t>(glm::mix(static_cast<float>(info.amountPickedUpPerTurn),
			                                       static_cast<float>(info.amountPickedUpPerTurnEnd), ramp));
		}
	}
	uint32_t held = first ? 0 : registry.Get<Pot>(static_cast<entt::entity>(g_heldObject)).amount;
	const uint32_t cap = std::min<uint32_t>(info.maxAmountCanBePickedUp, std::numeric_limits<uint16_t>::max());
	// The collecting sound plays while more can come: the source has some left and the hand has room. Emptied, or the
	// hand full, it stops though the button is still held (Mike, 2026-10-18); back on a fuller pile, it plays again.
	if (PileAmount(pileEntity) == 0 || held >= cap)
	{
		StopCollectWoodSound();
	}
	else
	{
		StartCollectSound(g_collectingFrom);
	}
	take = std::min({take, PileAmount(pileEntity), cap > held ? cap - held : 0u});
	if (take == 0)
	{
		return;
	}
	AddToPile(pileEntity, -static_cast<int32_t>(take));
	if (first)
	{
		const auto position = registry.Get<Transform>(pileEntity).position;
		const auto wood = openblack::ecs::archetypes::PotArchetype::Create(position, 0.0f, handPot, static_cast<int32_t>(take));
		if (wood == entt::null)
		{
			AddToPile(pileEntity, static_cast<int32_t>(take));
			return;
		}
		g_heldObject = static_cast<uint32_t>(wood);
		g_handTrail.clear();
		SPDLOG_LOGGER_INFO(spdlog::get("game"), "Hand collecting {} from {}: {} at once ({} left in the pile)", grain ? "grain" : "wood",
		                   g_collectingFrom, take, PileAmount(pileEntity));
	}
	else
	{
		registry.Get<Pot>(static_cast<entt::entity>(g_heldObject)).amount = static_cast<uint16_t>(held + take);
	}
	if (PileAmount(pileEntity) == 0 && !registry.AllOf<openblack::ecs::components::Field>(pileEntity))
	{
		registry.Remove<Mesh>(pileEntity); // taken to the last stick (a field stays, flattened: UpdateFields)
		// Still collecting while held: the next pile under the hand is taken up (HandCollect); let go, it ends.
	}
}

bool HandPickUp(uint32_t object)
{
	using namespace openblack::ecs::components;
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = static_cast<entt::entity>(object);
	if (g_heldObject != 0 || !CanPickUp(object))
	{
		return false;
	}
	g_flying.erase(object);
	TakeOffPedestal(object);
	g_walks.erase(object);
	g_followers.erase(object);
	g_scriptMoves.erase(object);
	if (registry.AllOf<WallHug, LivingAction>(entity))
	{
		HoldForScript(entity, registry.Get<Transform>(entity).position); // no walking off while held
	}
	if (auto* body = registry.TryGet<RigidBody>(entity))
	{
		Locator::dynamicsSystem::value().RemoveRigidBody(&body->handle); // or the hand's ray would hit it
	}
	// A tree taken up stops being in the way where it stood: villagers walked round the empty spot.
	if (registry.AllOf<Tree>(entity))
	{
		if (const auto* fixed = registry.TryGet<Fixed>(entity))
		{
			g_treeFootprints.insert_or_assign(object, *fixed); // for when it is replanted
		}
		registry.Remove<Fixed>(entity);
		// Torn up by the roots: G_TreeBreak_01-03 (the in-game bank's 32-34), Mike's choice by ear, 2026-10-18.
		PlayBankSound(1, 32 + static_cast<int32_t>(Locator::rng::value().NextValue(0.0f, 2.99f)), &registry.Get<Transform>(entity).position);
	}
	g_heldObject = object;
	g_pickedUpAt = std::chrono::steady_clock::now();
	g_handTrail.clear();
	g_handVelocity = glm::vec3(0.0f);
	g_handLast.reset();
	g_handVelocities.clear();
	const auto* mesh = registry.TryGet<Mesh>(entity);
	SPDLOG_LOGGER_INFO(spdlog::get("game"), "Hand picked up {} (mesh {}, {}{}{}{})", object,
	                   mesh != nullptr ? Locator::resources::value().GetMeshes().Handle(mesh->id)->GetDebugName() : "none",
	                   registry.AllOf<Mobile>(entity) ? "mobile " : "", registry.AllOf<Fixed>(entity) ? "fixed " : "",
	                   registry.AllOf<Feature>(entity) ? "feature " : "", registry.AllOf<Villager>(entity) ? "villager" : "");
	return true;
}

bool TakeSphereUnderHand()
{
	if (g_heldObject != 0)
	{
		return false;
	}
	// The nearest miracle sphere to the press (along the pointer's ray), close by; else the nearest thing the hand can
	// take right where it is pressed. Nothing further off: a press clearly on bare land still grabs the land.
	constexpr float k_SphereReach = 6.0f;
	auto& registry = Locator::entitiesRegistry::value();
	const auto direction = glm::normalize(g_handRayDirection);
	uint32_t sphere = 0;
	float bestOff = k_SphereReach;
	for (const auto& [id, seed] : g_oneShotSpells)
	{
		const auto* transform = registry.Valid(static_cast<entt::entity>(id)) ? registry.TryGet<Transform>(static_cast<entt::entity>(id)) : nullptr;
		if (transform == nullptr)
		{
			continue;
		}
		const auto toSphere = transform->position - g_handRayOrigin;
		const float along = glm::dot(toSphere, direction);
		const float off = glm::length(toSphere - direction * along);
		if (along > 0.0f && off < bestOff)
		{
			bestOff = off;
			sphere = id;
		}
	}
	if (sphere != 0)
	{
		PutInHand(sphere);
		TakeSeedIntoHand(g_oneShotSpells[g_heldObject]);
		SPDLOG_LOGGER_INFO(spdlog::get("game"), "Sphere {} picked up: let go or tap where it should happen", sphere);
		return true;
	}
	constexpr float k_TakeReach = 4.0f;
	const auto object = FindObjectNear(g_handGround, k_TakeReach);
	return object != 0 && object != PlayersCreature() && HandPickUp(object);
}

void TeleportCreatureToHand()
{
	const auto creature = PlayersCreature();
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = static_cast<entt::entity>(creature);
	if (creature == 0 || !registry.Valid(entity))
	{
		return;
	}
	auto& transform = registry.Get<Transform>(entity);
	transform.position = glm::vec3(g_handGround.x, Locator::terrainSystem::value().GetHeightAt(glm::vec2(g_handGround.x, g_handGround.z)),
	                               g_handGround.z);
	MoveBody(entity, transform.position);
	g_leashWalk.erase(creature);
	g_leashFetch.erase(creature);
	SPDLOG_LOGGER_INFO(spdlog::get("game"), "Creature {} brought to the hand at ({:.1f}, {:.1f}, {:.1f})", creature,
	                   transform.position.x, transform.position.y, transform.position.z);
}


// A deleted object: nothing of ours keeps it (scroll, pedestal stone, flight, walk, hand...).
void ForgetObject(uint32_t object)
{
	g_highlights.erase(object);
	TakeOffPedestal(object);
	g_flying.erase(object);
	g_walks.erase(object);
	g_followers.erase(object);
	g_scriptMoves.erase(object);
	g_scriptAnimations.erase(object);
	g_scriptEntities.erase(object);
	if (g_heldObject == object)
	{
		g_heldObject = 0;
	}
	if (g_clickedThing == object)
	{
		g_clickedThing = 0;
	}
}

bool HandHolding()
{
	return g_heldObject != 0;
}

float HandHoldOpenness()
{
	// The original's (ObtainRequiredHandPosition, BW1W120 005b3c70): what is held gives a hold radius, and the hand
	// opens to it over 3.2 (the hand's size, fn_0046c040) - over 1.2 times that for an ordinary object - up to fully
	// open; the hold pose then stands at that much of half its length. A tree's hold radius is a fifth of its size
	// (DeadTree::GetHoldRadius); an object's is three quarters of its radius (Object::GetHoldRadius). Measured on
	// the original's hand (2026-10-19): a tree 0.4 open, a miracle 0.27, a giant miracle 0.8.
	if (g_heldObject == 0)
	{
		return 0.0f;
	}
	constexpr float k_HandSize = 3.2f;
	auto& registry = Locator::entitiesRegistry::value();
	const auto held = static_cast<entt::entity>(g_heldObject);
	if (!registry.Valid(held) || !registry.AllOf<Transform>(held))
	{
		return 0.0f;
	}
	const auto& transform = registry.Get<Transform>(held);
	if (g_oneShotSpells.contains(g_heldObject))
	{
		return 0.27f; // measured on the original's hand round a miracle (2026-10-19): 0.4 of hold_side's frames
	}
	const auto* mesh = registry.TryGet<openblack::ecs::components::Mesh>(held);
	const auto& meshes = Locator::resources::value().GetMeshes();
	if (mesh == nullptr || !meshes.Contains(mesh->id))
	{
		return 0.0f;
	}
	const auto size = meshes.Handle(mesh->id)->GetBoundingBox().Size() * transform.scale;
	const bool tree = registry.AllOf<openblack::ecs::components::Tree>(held);
	const float openness = tree ? std::min(size.y * 0.2f / k_HandSize, 1.0f)
	                            : std::min(0.5f * std::max(size.x, size.z) * 0.75f / (k_HandSize * 1.2f), 1.0f);
	static uint32_t s_reported = 0;
	if (s_reported != g_heldObject)
	{
		s_reported = g_heldObject;
		SPDLOG_LOGGER_INFO(spdlog::get("game"), "Hand holds {} ({}), {:.1f} x {:.1f} x {:.1f}: open {:.2f}", g_heldObject,
		                   tree ? "tree" : "object", size.x, size.y, size.z, openness);
	}
	return openness;
}

void HandCarry(const glm::vec3& handPosition)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = static_cast<entt::entity>(g_heldObject);
	if (g_heldObject == 0 || !registry.Valid(entity))
	{
		g_heldObject = 0;
		return;
	}
	auto position = handPosition + glm::vec3(0.0f, k_CarryHeight, 0.0f);
	// Over the gate stone pedestal a totem is held at its top, as the original keeps a held thing at the height
	// of what the hand is over.
	if (const auto pedestal = IsGateTotem(entity) ? PedestalUnder(handPosition) : std::nullopt)
	{
		position.y = std::max(position.y, pedestal->top + k_CarryHeight);
	}
	registry.Get<openblack::ecs::components::Transform>(entity).position = position;
	MoveBody(entity, position);
	// A carried villager's walking goal goes with the hand, and it has no step in hand for the pathfinder to put it
	// back on: otherwise it is pulled back each turn to where it was picked up, and let go of there (Land 1's lost
	// brother vanished back to the woods).
	if (auto* wallHug = registry.TryGet<WallHug>(entity))
	{
		wallHug->goal = glm::vec2(position.x, position.z);
		registry.Remove<MoveStateLinearTag, MoveStateOrbitTag, MoveStateExitCircleTag, MoveStateStepThroughTag,
		                MoveStateFinalStepTag, MoveStateArrivedTag, WallHugObjectReference>(entity);
	}
	if (auto* move = registry.TryGet<ScriptMove>(entity))
	{
		move->target = position;
	}
	g_handTrail.emplace_back(std::chrono::steady_clock::now(), position);
	while (g_handTrail.size() > 2 && g_handTrail.front().first < std::chrono::steady_clock::now() - k_ThrowWindow)
	{
		g_handTrail.pop_front();
	}
	{
		const auto now = std::chrono::steady_clock::now();
		if (g_handLast)
		{
			const float dt = std::chrono::duration<float>(now - g_handLast->first).count();
			if (dt > 1e-4f)
			{
				const auto raw = (position - g_handLast->second) / dt;
				g_handVelocity = glm::mix(g_handVelocity, raw, 1.0f - std::exp(-dt / k_HandVelocityLag));
				g_handVelocities.emplace_back(now, g_handVelocity);
				while (!g_handVelocities.empty() && g_handVelocities.front().first < now - k_ThrowMemory)
				{
					g_handVelocities.pop_front();
				}
			}
		}
		g_handLast = std::pair {now, position};
	}
}

std::chrono::steady_clock::time_point HandPickedUpAt()
{
	return g_pickedUpAt;
}

void CycleLeash()
{
	CycleLeashNow();
}

void ToggleDaylight()
{
	auto& sky = Locator::skySystem::value();
	sky.SetForceDaylight(!sky.GetForceDaylight());
	SPDLOG_LOGGER_INFO(spdlog::get("game"), "Daylight: {}", sky.GetForceDaylight() ? "forced on" : "the time of day's own");
}

bool HandDemoPlaying(); // below, with the hand demo player

std::optional<std::pair<glm::vec3, uint32_t>> HandResourceLabel()
{
	// What the hand holds, if a pile of wood or food: its amount, refreshed a few times a second.
	static uint32_t s_shown = 0;
	static auto s_next = std::chrono::steady_clock::time_point {};
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = static_cast<entt::entity>(g_heldObject);
	const auto* pot = g_heldObject != 0 && registry.Valid(entity) ? registry.TryGet<openblack::ecs::components::Pot>(entity) : nullptr;
	if (pot == nullptr)
	{
		s_shown = 0;
		return std::nullopt;
	}
	if (const auto now = std::chrono::steady_clock::now(); now >= s_next)
	{
		s_shown = pot->amount;
		s_next = now + std::chrono::milliseconds(250);
	}
	return std::pair {g_handGround + glm::vec3(0.0f, 4.0f, 0.0f), s_shown};
}

bool InCutscene()
{
	return (g_scriptHasCamera || Locator::camera::value().IsWidescreen()) && !HandDemoPlaying();
}

std::string LeashStatus()
{
	if (g_leash.type == 0 || g_leash.creature == 0)
	{
		return "Leash: off";
	}
	const char* kind = g_leash.type == 2 ? "regular" : g_leash.type == 3 ? "reward" : g_leash.type == 4 ? "punishment" : "on";
	return g_leash.tiedTo != 0 ? fmt::format("Leash: {}, tied to {}", kind, g_leash.tiedTo) : fmt::format("Leash: {}", kind);
}

bool LeashTieAtPointer()
{
	if (g_leash.type == 0 || g_leash.creature == 0 || g_leash.creature != PlayersCreature())
	{
		return false;
	}
	// The fixed thing (a tree, a house) the pointer is on: the nearest whose box the ray enters.
	auto& registry = Locator::entitiesRegistry::value();
	entt::entity post = entt::null;
	float nearest = std::numeric_limits<float>::max();
	registry.Each<const openblack::ecs::components::Fixed, const openblack::ecs::components::Mesh, const Transform>(
	    [&](entt::entity entity, const openblack::ecs::components::Fixed&, const openblack::ecs::components::Mesh&,
	        const Transform&) {
		    if (const auto hit = CreatureRayHit(entity, g_handRayOrigin, g_handRayDirection); hit && hit->first < nearest)
		    {
			    nearest = hit->first;
			    post = entity;
		    }
	    });
	// Or a person: Land 1's Pied Piper is led away on your creature's leash.
	if (const auto person = VillagerOnRay(g_handRayOrigin, g_handRayDirection); person != 0)
	{
		const auto personEntity = static_cast<entt::entity>(person);
		if (const auto hit = CreatureRayHit(personEntity, g_handRayOrigin, g_handRayDirection); hit && hit->first < nearest)
		{
			nearest = hit->first;
			post = personEntity;
		}
	}
	if (post == entt::null)
	{
		return false;
	}
	const auto id = static_cast<uint32_t>(post);
	// Tying is for what you've led the creature up to: farther off, the long press is the hand's own (it collects
	// wood from a tree, with the leash on or not).
	constexpr float k_LeashTieReach = 30.0f;
	const auto* creatureTransform = registry.TryGet<Transform>(static_cast<entt::entity>(g_leash.creature));
	if (g_leash.tiedTo != id && creatureTransform != nullptr)
	{
		const auto& at = registry.Get<Transform>(post).position;
		if (glm::distance(glm::vec2(at.x, at.z), glm::vec2(creatureTransform->position.x, creatureTransform->position.z)) >
		    k_LeashTieReach)
		{
			return false;
		}
	}
	g_leashWalk.erase(g_leash.creature);
	g_leashFetch.erase(g_leash.creature);
	g_leash.tiedTo = g_leash.tiedTo == id ? 0 : id;
	SPDLOG_LOGGER_INFO(spdlog::get("game"), "Leash: creature {} {} {}", g_leash.creature,
	                   g_leash.tiedTo != 0 ? "tied to" : "untied from", id);
	return true;
}

bool LeashCommand(const glm::vec3& ground, uint32_t objectUnderHand)
{
	return LeashCommandNow(ground, objectUnderHand);
}

// The hand gives what it holds to your creature when the action button is pressed with the hand on it.
bool HandGiveToCreature(const glm::vec3& origin, const glm::vec3& direction)
{
	const auto creature = PlayersCreature();
	if (g_heldObject == 0 || creature == 0 || !CreatureRayHit(static_cast<entt::entity>(creature), origin, direction))
	{
		return false;
	}
	CreatureDropsHeld(creature);
	g_creatureHolding[creature] = g_heldObject;
	SPDLOG_LOGGER_INFO(spdlog::get("game"), "Hand gave object {} to creature {}", g_heldObject, creature);
	g_heldObject = 0;
	g_scriptAnimations[creature] = {85u, 1.0f}; // hold (ctrspec27.txt)
	return true;
}

void CreatureEatsHeld(uint32_t creature)
{
	const auto held = g_creatureHolding.find(creature);
	if (held == g_creatureHolding.end())
	{
		return;
	}
	const auto object = held->second;
	g_creatureHolding.erase(held);
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = static_cast<entt::entity>(object);
	const bool alive = registry.Valid(entity) && registry.AllOf<openblack::ecs::components::Villager>(entity);
	const int32_t action = alive ? CreatureAction::EatAlive : CreatureAction::EatAfterExamining;
	g_creatureActionCounts[{creature, action}] += 1.0f;
	g_scriptAnimations[creature] = {96u, 1.0f}; // eat
	if (registry.Valid(entity))
	{
		ForgetObject(object);
		registry.Destroy(entity);
	}
	SPDLOG_LOGGER_INFO(spdlog::get("game"), "Creature {} ate object {} ({})", creature, object, CreatureActionName(action));
}

void CreatureDropsHeld(uint32_t creature)
{
	const auto held = g_creatureHolding.find(creature);
	if (held == g_creatureHolding.end())
	{
		return;
	}
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = static_cast<entt::entity>(held->second);
	const auto creatureEntity = static_cast<entt::entity>(creature);
	if (registry.Valid(entity) && registry.Valid(creatureEntity))
	{
		auto& transform = registry.Get<Transform>(entity);
		const auto& at = registry.Get<Transform>(creatureEntity);
		transform.position = at.position + at.rotation * glm::vec3(0.0f, 0.0f, -2.0f);
		transform.position.y = Locator::terrainSystem::value().GetHeightAt(glm::vec2(transform.position.x, transform.position.z));
		// Where it was put down is now its place, as when the hand lets go: a villager a script holds (the drowning
		// men) was pulled straight back to where it was picked up.
		MoveBody(entity, transform.position);
		if (auto* move = registry.TryGet<ScriptMove>(entity))
		{
			move->target = transform.position;
		}
		if (auto* wallHug = registry.TryGet<WallHug>(entity))
		{
			wallHug->goal = glm::vec2(transform.position.x, transform.position.z);
		}
		SPDLOG_LOGGER_INFO(spdlog::get("game"), "Creature {} put {} down at ({:.1f}, {:.1f}, {:.1f})", creature, held->second,
		                   transform.position.x, transform.position.y, transform.position.z);
	}
	SPDLOG_LOGGER_INFO(spdlog::get("game"), "Creature {} dropped object {}", creature, held->second);
	g_creatureHolding.erase(held);
}

// What creatures hold goes with them, in front of their chest.
void UpdateCreatureHolding()
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto& meshes = Locator::resources::value().GetMeshes();
	for (auto it = g_creatureHolding.begin(); it != g_creatureHolding.end();)
	{
		const auto creature = static_cast<entt::entity>(it->first);
		const auto object = static_cast<entt::entity>(it->second);
		if (!registry.Valid(creature) || !registry.Valid(object))
		{
			it = g_creatureHolding.erase(it);
			continue;
		}
		const auto& at = registry.Get<Transform>(creature);
		const auto* mesh = registry.TryGet<openblack::ecs::components::Mesh>(creature);
		const float height = mesh != nullptr && meshes.Contains(mesh->id) ? meshes.Handle(mesh->id)->GetBoundingBox().Size().y * at.scale.y : 5.0f;
		auto& held = registry.Get<Transform>(object).position;
		// Diagnosis: anything that moved what it holds since last frame (a script's hold pulling a villager back).
		static std::unordered_map<uint32_t, glm::vec3> s_lastHeld;
		static auto s_lastPulled = std::chrono::steady_clock::time_point {};
		if (const auto last = s_lastHeld.find(it->second);
		    last != s_lastHeld.end() && glm::distance(last->second, held) > 0.05f && Every(s_lastPulled, std::chrono::milliseconds(1000)))
		{
			SPDLOG_LOGGER_INFO(spdlog::get("game"),
			                   "Creature {}'s held {} was moved by something else, from ({:.1f}, {:.1f}, {:.1f}) to ({:.1f}, {:.1f}, {:.1f})",
			                   it->first, it->second, last->second.x, last->second.y, last->second.z, held.x, held.y, held.z);
		}
		held = at.position + at.rotation * glm::vec3(0.0f, height * 0.55f, -height * 0.3f);
		held.y = std::max(held.y, 0.5f); // carried out of the water, not under it
		s_lastHeld[it->second] = held;
		// As the hand carries (HandCarry): its walking goal and the script's hold go with it, with no step left for the
		// pathfinder - otherwise a drowning man was put back in the water every turn while the creature "held" him.
		MoveBody(object, held);
		if (auto* wallHug = registry.TryGet<WallHug>(object))
		{
			wallHug->goal = glm::vec2(held.x, held.z);
			registry.Remove<MoveStateLinearTag, MoveStateOrbitTag, MoveStateExitCircleTag, MoveStateStepThroughTag,
			                MoveStateFinalStepTag, MoveStateArrivedTag, WallHugObjectReference>(object);
		}
		if (auto* move = registry.TryGet<ScriptMove>(object))
		{
			move->target = held;
		}
		++it;
	}
}

namespace
{
// What a village store shreds into wood: trees and fences (Mike, 2026-10-18), for info.dat's woodValue of each.
std::optional<uint32_t> ShredWood(entt::entity entity)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto& info = Locator::infoConstants::value();
	if (const auto* tree = registry.TryGet<openblack::ecs::components::Tree>(entity))
	{
		return info.tree.at(static_cast<size_t>(tree->type)).woodValue;
	}
	if (const auto* stone = registry.TryGet<openblack::ecs::components::MobileStatic>(entity);
	    stone != nullptr && (stone->type == MobileStaticInfo::CeltFenceShort || stone->type == MobileStaticInfo::CeltFenceTall))
	{
		return info.mobileStatic.at(static_cast<size_t>(stone->type)).woodValue;
	}
	return std::nullopt;
}

// The emptiest wood pile of a village store at `at` (within its circle), if there is one.
entt::entity StoreWoodPileAt(const glm::vec3& at)
{
	auto& registry = Locator::entitiesRegistry::value();
	entt::entity into = entt::null;
	registry.Each<const openblack::ecs::components::StoragePit, const Transform>(
	    [&](entt::entity, const openblack::ecs::components::StoragePit& pit, const Transform& where) {
		    if (into != entt::null || glm::distance(glm::vec2(where.position.x, where.position.z), glm::vec2(at.x, at.z)) > 14.0f)
		    {
			    return;
		    }
		    uint16_t least = std::numeric_limits<uint16_t>::max();
		    for (const auto pile : pit.woodPiles)
		    {
			    if (pile != entt::null && registry.Valid(pile) && registry.AllOf<openblack::ecs::components::Pot>(pile) &&
			        !registry.AllOf<openblack::ecs::components::Construction>(pile) &&
			        registry.Get<openblack::ecs::components::Pot>(pile).amount < least)
			    {
				    least = registry.Get<openblack::ecs::components::Pot>(pile).amount;
				    into = pile;
			    }
		    }
	    });
	return into;
}

// A tree or fence put down on (or landing on) a village store is shredded: gone, with the original's mulching sound
// (G_TreeMulch, the in-game bank's 155-158), its wood into the store's emptiest wood pile.
// A building being built at `at` (within 20, as wood from the hand goes to one), not the Ark - a feature its script
// builds its own way.
entt::entity BuildingSiteAt(const glm::vec3& at)
{
	auto& registry = Locator::entitiesRegistry::value();
	constexpr float k_SiteReach = 20.0f;
	entt::entity site = entt::null;
	float nearest = k_SiteReach * k_SiteReach;
	registry.Each<const openblack::ecs::components::Construction, const Transform>(
	    [&](entt::entity building, const openblack::ecs::components::Construction& construction, const Transform& where) {
		    const auto offset = glm::vec2(where.position.x - at.x, where.position.z - at.z);
		    if (construction.built < 1.0f && glm::dot(offset, offset) < nearest &&
		        !registry.AllOf<openblack::ecs::components::Feature>(building))
		    {
			    nearest = glm::dot(offset, offset);
			    site = building;
		    }
	    });
	return site;
}

// Somewhere a tree or fence put down or landing is shredded: a village store or a building site.
bool ShreddingAt(const glm::vec3& at)
{
	return StoreWoodPileAt(at) != entt::null || BuildingSiteAt(at) != entt::null;
}

bool ShredIntoStore(entt::entity entity, const glm::vec3& at)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto wood = ShredWood(entity);
	const auto into = wood ? StoreWoodPileAt(at) : entt::null;
	// No store there: a building site takes its wood the same way (Mike, 2026-10-18), into the site's stock.
	if (const auto site = wood && into == entt::null ? BuildingSiteAt(at) : entt::null; site != entt::null)
	{
		auto& construction = registry.Get<openblack::ecs::components::Construction>(site);
		construction.woodStock += static_cast<float>(*wood);
		PlayBankSound(1, 155 + static_cast<int32_t>(Locator::rng::value().NextValue(0.0f, 3.99f)), &at);
		const auto id = static_cast<uint32_t>(entity);
		SPDLOG_LOGGER_INFO(spdlog::get("game"), "{} shredded into building {}: {} wood ({:.0f} in stock)", id,
		                   static_cast<uint32_t>(site), *wood, construction.woodStock);
		g_treeFootprints.erase(id);
		ForgetObject(id);
		registry.Destroy(entity);
		return true;
	}
	if (into == entt::null)
	{
		return false;
	}
	auto& pot = registry.Get<openblack::ecs::components::Pot>(into);
	pot.amount = static_cast<uint16_t>(std::min<uint32_t>(pot.amount + *wood, std::numeric_limits<uint16_t>::max()));
	if (!registry.AllOf<openblack::ecs::components::Mesh>(into))
	{
		const auto& potInfo = Locator::infoConstants::value().pot.at(static_cast<size_t>(pot.type));
		registry.Assign<openblack::ecs::components::Mesh>(into, resources::HashIdentifier(potInfo.meshId), static_cast<int8_t>(0),
		                                                  static_cast<int8_t>(0));
	}
	// The bank's ids, as its headers number them (one more than their place in it): 155-158 G_TreeMulch_01-04.
	PlayBankSound(1, 155 + static_cast<int32_t>(Locator::rng::value().NextValue(0.0f, 3.99f)), &at);
	const auto id = static_cast<uint32_t>(entity);
	SPDLOG_LOGGER_INFO(spdlog::get("game"), "{} shredded into the store: {} wood ({} there now)", id, *wood, pot.amount);
	g_treeFootprints.erase(id);
	ForgetObject(id);
	registry.Destroy(entity);
	return true;
}

// A thrown tree lands lying on its side, fallen the way it was going (Mike, 2026-10-18: it landed standing on its
// stump), resting on its trunk's side rather than half in the ground.
void LayTreeDown(entt::entity entity, glm::vec3 position, const glm::vec3& velocity)
{
	auto& registry = Locator::entitiesRegistry::value();
	auto& transform = registry.Get<Transform>(entity);
	auto way = glm::vec3(velocity.x, 0.0f, velocity.z);
	if (glm::length(way) < 1e-3f)
	{
		const float a = Locator::rng::value().NextValue(0.0f, glm::two_pi<float>());
		way = glm::vec3(std::cos(a), 0.0f, std::sin(a));
	}
	way = glm::normalize(way);
	const auto axis = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), way)); // turns up to `way`
	const float spinAbout = Locator::rng::value().NextValue(0.0f, glm::two_pi<float>()); // which side it rests on
	transform.rotation = glm::mat3(glm::rotate(glm::mat4(1.0f), glm::half_pi<float>(), axis)) * glm::mat3(glm::eulerAngleY(spinAbout));
	float lift = 0.5f;
	if (const auto* mesh = registry.TryGet<openblack::ecs::components::Mesh>(entity);
	    mesh != nullptr && Locator::resources::value().GetMeshes().Contains(mesh->id))
	{
		const auto size = Locator::resources::value().GetMeshes().Handle(mesh->id)->GetBoundingBox().Size();
		lift = 0.3f * std::max(size.x, size.z) * std::max(transform.scale.x, transform.scale.z);
	}
	position.y += lift;
	transform.position = position;
	PlayTreeThud(position); // the same soft thud as its bounces
}

// A tree put down gently takes root where it is: upright, in the walkers' way again (Mike, 2026-10-18: replanting).
void ReplantTree(entt::entity entity, const glm::vec3& position)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto id = static_cast<uint32_t>(entity);
	auto& transform = registry.Get<Transform>(entity);
	const auto facing = transform.rotation * glm::vec3(0.0f, 0.0f, 1.0f);
	transform.rotation = glm::mat3(glm::eulerAngleY(std::atan2(facing.x, facing.z)));
	transform.position = position;
	if (const auto footprint = g_treeFootprints.find(id); footprint != g_treeFootprints.end())
	{
		registry.AssignOrReplace<openblack::ecs::components::Fixed>(entity, glm::vec2(position.x, position.z),
		                                                            footprint->second.boundingRadius);
		g_treeFootprints.erase(footprint);
	}
	PlayBankSound(1, 85, &position); // G_PlantTree_03, Mike's choice by ear (2026-10-18)
	SPDLOG_LOGGER_INFO(spdlog::get("game"), "Tree {} replanted at ({:.1f}, {:.1f}, {:.1f})", id, position.x, position.y, position.z);
}
} // namespace

void HandDrop(const glm::vec3& handPosition)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = static_cast<entt::entity>(g_heldObject);
	const bool holding = g_heldObject != 0;
	g_lastDropAt = handPosition; // a portal there takes it (UpdateVortices)
	g_lastDropTime = std::chrono::steady_clock::now();
	g_heldObject = 0;
	if (!holding || !registry.Valid(entity))
	{
		return;
	}
	// A miracle sphere let go of is cast: it happens under the hand, and the sphere is used up.
	if (g_oneShotSpells.contains(static_cast<uint32_t>(entity)))
	{
		g_heldObject = static_cast<uint32_t>(entity);
		UseHeldSphere(handPosition);
		return;
	}
	// A tree or fence let go of over a village store is shredded into its wood.
	if (ShredIntoStore(entity, handPosition))
	{
		return;
	}
	// Grain or wood let go of over a village store goes back into it (grain to its food pile, wood to its emptiest
	// wood pile).
	if (IsHandResource(static_cast<uint32_t>(entity)))
	{
		const bool grain = IsHandFood(static_cast<uint32_t>(entity));
		const auto amount = registry.Get<openblack::ecs::components::Pot>(entity).amount;
		entt::entity into = entt::null;
		registry.Each<const openblack::ecs::components::StoragePit, const Transform>(
		    [&](entt::entity, const openblack::ecs::components::StoragePit& pit, const Transform& where) {
			    if (into != entt::null ||
			        glm::distance(glm::vec2(where.position.x, where.position.z), glm::vec2(handPosition.x, handPosition.z)) > 14.0f)
			    {
				    return;
			    }
			    if (grain)
			    {
				    into = pit.foodPile;
				    return;
			    }
			    uint16_t least = std::numeric_limits<uint16_t>::max();
			    for (const auto pile : pit.woodPiles)
			    {
				    if (pile != entt::null && registry.Valid(pile) && registry.AllOf<openblack::ecs::components::Pot>(pile) &&
				        registry.Get<openblack::ecs::components::Pot>(pile).amount < least)
				    {
					    least = registry.Get<openblack::ecs::components::Pot>(pile).amount;
					    into = pile;
				    }
			    }
		    });
		if (into != entt::null && registry.Valid(into) && registry.AllOf<openblack::ecs::components::Pot>(into) &&
		    (grain || !registry.AllOf<openblack::ecs::components::Construction>(into)))
		{
			auto& pot = registry.Get<openblack::ecs::components::Pot>(into);
			pot.amount = static_cast<uint16_t>(std::min<uint32_t>(pot.amount + amount, std::numeric_limits<uint16_t>::max()));
			if (!registry.AllOf<openblack::ecs::components::Mesh>(into))
			{
				// A pile taken to its last was hidden: it shows again (its mesh from its kind, as it was made)
				const auto& potInfo = Locator::infoConstants::value().pot.at(static_cast<size_t>(pot.type));
				registry.Assign<openblack::ecs::components::Mesh>(into, resources::HashIdentifier(potInfo.meshId), static_cast<int8_t>(0),
				                                                  static_cast<int8_t>(0));
			}
			registry.Destroy(entity);
			if (!grain)
			{
				PlayThrowSound(ThrowSound::Wood, handPosition); // G_PileWood, as wood given to a building site (Mike, 2026-10-18)
			}
			SPDLOG_LOGGER_INFO(spdlog::get("game"), "Hand put {} {} back into the store ({} there now)", amount, grain ? "grain" : "wood",
			                   pot.amount);
			return;
		}
		if (grain)
		{
			auto position = handPosition;
			position.y = Locator::terrainSystem::value().GetHeightAt(glm::vec2(position.x, position.z));
			registry.Destroy(entity);
			openblack::ecs::archetypes::PotArchetype::Create(position, 0.0f, PotInfo::MagicFood, amount);
			SPDLOG_LOGGER_INFO(spdlog::get("game"), "Hand put down {} grain at ({:.1f}, {:.1f}, {:.1f})", amount, position.x,
			                   position.y, position.z);
			return;
		}
	}
	// Wood carried in the hand builds a building under construction it is let go over; anywhere else it is put
	// down as a pile of that much wood on the ground under the hand.
	if (IsHandWood(static_cast<uint32_t>(entity)))
	{
		const auto amount = registry.Get<openblack::ecs::components::Pot>(entity).amount;
		constexpr float k_BuildingSiteReach = 20.0f;
		entt::entity site = entt::null;
		float nearest = k_BuildingSiteReach * k_BuildingSiteReach;
		registry.Each<const openblack::ecs::components::Construction, const Transform>(
		    [&](entt::entity building, const openblack::ecs::components::Construction& construction, const Transform& transform) {
			    const auto offset = glm::vec2(transform.position.x - handPosition.x, transform.position.z - handPosition.z);
			    if (construction.built < 1.0f && glm::dot(offset, offset) < nearest &&
			        !registry.AllOf<openblack::ecs::components::Feature>(building)) // a feature (the Ark) is built its own way
			    {
				    nearest = glm::dot(offset, offset);
				    site = building;
			    }
		    });
		if (site != entt::null)
		{
			auto& construction = registry.Get<openblack::ecs::components::Construction>(site);
			construction.woodStock += static_cast<float>(amount);
			PlayThrowSound(ThrowSound::Wood, handPosition); // G_PileWood, as wood put down
			registry.Destroy(entity);
			SPDLOG_LOGGER_INFO(spdlog::get("game"), "Hand gave {} wood to building {} ({:.0f} in stock, {:.0f}% built)", amount,
			                   static_cast<uint32_t>(site), construction.woodStock, construction.built * 100.0f);
			return;
		}
		auto position = handPosition;
		position.y = Locator::terrainSystem::value().GetHeightAt(glm::vec2(position.x, position.z));
		registry.Destroy(entity);
		// A pile of wood, not a barrel (WoodPot): the original's wood let go of is MagicWood, what Land 1's Missionaries
		// look for by their Ark.
		openblack::ecs::archetypes::PotArchetype::Create(position, 0.0f, PotInfo::MagicWood, amount);
		// Wood set down (Mike, 2026-10-18: it had no sound): G_PileWood_01-06, the bank's 86-91 - his choice by ear.
		PlayThrowSound(ThrowSound::Wood, position);
		SPDLOG_LOGGER_INFO(spdlog::get("game"), "Hand put down {} wood at ({:.1f}, {:.1f}, {:.1f})", amount, position.x,
		                   position.y, position.z);
		return;
	}
	// Things that move (people, animals, rocks, logs, food) fly on with the hand's speed, or fall where
	// they were let go; fixtures (features like the totem) are set down on the ground under the hand.
	if (SetOnPedestal(entity, registry.Get<Transform>(entity).position))
	{
		return;
	}
	// A tree is thrown too when the hand is swiping (as fast as a throw needs, k_MinThrowSpeed below); held steady,
	// it is set down and planted (Mike, 2026-10-18: trees were only ever planted - they aren't Mobile).
	float swipe = 0.0f;
	for (const auto& [when, v] : g_handVelocities)
	{
		swipe = std::max(swipe, glm::length(glm::vec2(v.x, v.z)));
	}
	const bool throwTree = registry.AllOf<openblack::ecs::components::Tree>(entity) && swipe >= 4.0f;
	if (registry.AllOf<openblack::ecs::components::Mobile>(entity) || throwTree)
	{
		// A home run (Mike, 2026-10-17): the swipe's fastest moment gives the speed and the way, and it always goes up at
		// k_ThrowAngle, so how hard the swipe was decides how far it flies (range v^2 sin 2a / g, v half the swipe's
		// speed: a 30 units/s swipe carries it about 22, a 60 one about 90).
		glm::vec3 velocity(0.0f);
		for (const auto& [when, v] : g_handVelocities)
		{
			if (glm::length(glm::vec2(v.x, v.z)) > glm::length(glm::vec2(velocity.x, velocity.z)))
			{
				velocity = v;
			}
		}
		g_handVelocities.clear();
		// Up at the original's: its throws read from its memory (2026-10-19) left the hand 12, 21 and 24 degrees up (its
		// hand rises as it sweeps away from the camera; this one keeps to the ground, so the lift is given).
		const float k_ThrowAngle = glm::radians(20.0f);
		// Of the swipe's speed (Mike, 2026-10-17: at all of it, twice too strong; 2026-10-18: at 0.5, still twice as far
		// as it should go: 0.35 at 40 degrees). At 20 degrees, the same range for a rock: 0.35 x sqrt(sin 80 / sin 40).
		constexpr float k_ThrowPower = 0.5f * 0.7071f * 1.2379f;
		constexpr float k_MinThrowSpeed = 4.0f; // under it, it is let go of, not thrown
		const float across = glm::length(glm::vec2(velocity.x, velocity.z));
		if (across < k_MinThrowSpeed)
		{
			velocity = glm::vec3(0.0f);
		}
		else
		{
			const auto way = glm::vec2(velocity.x, velocity.z) / across;
			const float speed = across * k_ThrowPower;
			velocity = glm::vec3(way.x * speed * std::cos(k_ThrowAngle), speed * std::sin(k_ThrowAngle), way.y * speed * std::cos(k_ThrowAngle));
		}
		constexpr float k_MaxThrowSpeed = 150.0f * 0.7071f; // the demo's throw left the hand at 90; the farthest throw halved too
		if (glm::length(velocity) > k_MaxThrowSpeed)
		{
			velocity = glm::normalize(velocity) * k_MaxThrowSpeed;
		}
		g_flying[static_cast<uint32_t>(entity)] = velocity;
		SPDLOG_LOGGER_INFO(spdlog::get("game"), "Hand let go of {} at {:.1f} m/s", static_cast<uint32_t>(entity),
		                   glm::length(velocity));
		if (glm::length(velocity) > 0.0f)
		{
			auto& rng = Locator::rng::value();
			// About half tumble; a tree always does (Mike, 2026-10-18).
			if (rng.NextValue(0.0f, 1.0f) < 0.5f || registry.AllOf<openblack::ecs::components::Tree>(entity))
			{
				auto axis = glm::vec3(rng.NextValue(-1.0f, 1.0f), rng.NextValue(-1.0f, 1.0f), rng.NextValue(-1.0f, 1.0f));
				axis = glm::length(axis) > 1e-3f ? glm::normalize(axis) : glm::vec3(0.0f, 1.0f, 0.0f);
				g_spin[static_cast<uint32_t>(entity)] = axis * rng.NextValue(0.8f, 2.5f);
			}
			const auto& at = registry.Get<Transform>(entity).position;
			PlayThrowSound(ThrowSound::Woosh, at);
			if (const auto* villager = registry.TryGet<openblack::ecs::components::Villager>(entity))
			{
				PlayThrowSound(villager->lifeStage == openblack::ecs::components::Villager::LifeStage::Child ? ThrowSound::YellChild
				               : villager->sex == openblack::ecs::components::Villager::Sex::FEMALE  ? ThrowSound::YellWoman
				                                                                                     : ThrowSound::YellMan,
				               at);
			}
		}
		return; // lands in UpdateScriptFrame
	}
	auto position = handPosition;
	position.y = Locator::terrainSystem::value().GetHeightAt(glm::vec2(position.x, position.z));
	registry.Get<openblack::ecs::components::Transform>(entity).position = position;
	if (registry.AllOf<openblack::ecs::components::Tree>(entity))
	{
		ReplantTree(entity, position);
	}
	MoveBody(entity, position);
	if (auto* body = registry.TryGet<openblack::ecs::components::RigidBody>(entity))
	{
		Locator::dynamicsSystem::value().AddRigidBody(&body->handle);
	}
	if (auto* move = registry.TryGet<ScriptMove>(entity))
	{
		move->target = position;
	}
	SPDLOG_LOGGER_INFO(spdlog::get("game"), "Hand dropped {} at ({:.1f}, {:.1f}, {:.1f})", static_cast<uint32_t>(entity),
	                   position.x, position.y, position.z);
}

// A long press on the touchscreen with something in the hand lets go of it where the press is (Mike, 2026-10-18), set
// down, never thrown: on a village store grain and wood go into it and a tree or fence is shredded (HandDrop), anywhere
// else it is put down there (a tree planted).
bool LetGoAtPointer()
{
	auto& registry = Locator::entitiesRegistry::value();
	if (g_heldObject == 0 || !registry.Valid(static_cast<entt::entity>(g_heldObject)))
	{
		return false;
	}
	g_handVelocities.clear(); // a press held still: whatever swipe came before it doesn't throw it
	HandDrop(g_handGround);
	return true;
}

void SetObjectUnderHand(uint32_t object)
{
	g_objectUnderHand = object;
}

namespace
{
uint32_t& chosenObject()
{
	static uint32_t s_object = 0;
	return s_object;
}
} // namespace

std::optional<std::pair<glm::vec3, bool>> TestPickupStep()
{
	// OPENBLACK_TEST_COLLECT: the same with the nearest wood pile (held 3 s to collect, then given away).
	static const bool collect = std::getenv("OPENBLACK_TEST_COLLECT") != nullptr;
	static const bool enabled = collect || std::getenv("OPENBLACK_TEST_PICKUP") != nullptr;
	static auto start = std::chrono::steady_clock::now();
	static glm::vec3 target(0.0f);
	static bool chosen = false;
	if (!enabled)
	{
		return std::nullopt;
	}
	const float t = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
	if (t < 15.0f)
	{
		return std::nullopt; // let the level settle
	}
	if (!chosen)
	{
		const auto focus = Locator::camera::value().GetFocus();
		auto object = collect ? FindWoodNear(focus, 2000.0f) : FindObjectNear(focus, 200.0f, false);
		// OPENBLACK_TEST_PICKUP=sick: a villager no script or flock has, put in a script state lying sick as Land 1's
		// lost brother is, then set down 3 away.
		static const bool sick = std::string_view(std::getenv("OPENBLACK_TEST_PICKUP") != nullptr ? std::getenv("OPENBLACK_TEST_PICKUP") : "") == "sick";
		if (sick)
		{
			object = 0;
			float best = 1e12f;
			Locator::entitiesRegistry::value().Each<const openblack::ecs::components::Villager, const Transform>(
			    [&](entt::entity entity, const openblack::ecs::components::Villager& villager, const Transform& transform) {
				    const auto id = static_cast<uint32_t>(entity);
				    bool inFlock = false;
				    for (const auto& [flockId, flock] : g_flocks)
				    {
					    inFlock = inFlock || flock.leader == id ||
					              std::find(flock.members.begin(), flock.members.end(), id) != flock.members.end();
				    }
				    const float d = glm::distance(transform.position, focus);
				    if (!inFlock && !g_scriptEntities.contains(id) && villager.lifeStage == openblack::ecs::components::Villager::LifeStage::Adult && d < best)
				    {
					    best = d;
					    object = id;
				    }
			    });
			if (object != 0)
			{
				g_scriptAnimations[object] = {static_cast<uint32_t>(AnimId::PDying), std::numeric_limits<float>::max()};
				HoldInScriptState(object, 200);
			}
		}
		if (object == 0)
		{
			return std::nullopt;
		}
		target = Locator::entitiesRegistry::value().Get<Transform>(static_cast<entt::entity>(object)).position;
		chosen = true;
		chosenObject() = object;
		SPDLOG_LOGGER_INFO(spdlog::get("game"), "Test pickup: aiming at {} at ({:.1f}, {:.1f}, {:.1f})", object, target.x,
		                   target.y, target.z);
	}
	const float k = t - 15.0f;
	if (collect)
	{
		// ...carried to the nearest building under construction (else 10 m on), watched from beside it.
		static std::optional<glm::vec3> site;
		if (!site)
		{
			Locator::entitiesRegistry::value().Each<const openblack::ecs::components::Construction, const Transform>(
			    [](entt::entity, const openblack::ecs::components::Construction&, const Transform& transform) {
				    site = transform.position;
			    });
			site = site.value_or(target + glm::vec3(10.0f, 0.0f, 0.0f));
		}
		{
			auto& camera = Locator::camera::value();
			camera.SetOrigin(*site + glm::vec3(70.0f, 45.0f, 70.0f));
			camera.SetFocus(*site);
		}
		if (k < 0.3f || (k >= 3.3f && k < 3.6f))
		{
			return std::make_pair(target, false);
		}
		if (k < 3.3f)
		{
			return std::make_pair(target, true); // hold over the pile
		}
		if (k < 9.0f)
		{
			return std::make_pair(*site, k >= 4.0f && k < 4.2f); // press, release
		}
		return std::nullopt;
	}
	if (k < 0.3f)
	{
		return std::make_pair(target, false); // hand over it
	}
	if (k < 0.5f)
	{
		return std::make_pair(target, true); // press: pick up
	}
	if (k < 1.0f)
	{
		return std::make_pair(target, false); // release: still held
	}
	static const bool gentle = std::string_view(std::getenv("OPENBLACK_TEST_PICKUP") != nullptr ? std::getenv("OPENBLACK_TEST_PICKUP") : "") == "drop" ||
	                           std::string_view(std::getenv("OPENBLACK_TEST_PICKUP") != nullptr ? std::getenv("OPENBLACK_TEST_PICKUP") : "") == "sick";
	if (gentle && k > 1.5f && k < 1.6f)
	{
		const auto held = static_cast<entt::entity>(chosenObject());
		if (Locator::entitiesRegistry::value().Valid(held))
		{
			const auto at = Locator::entitiesRegistry::value().Get<Transform>(held).position;
			SPDLOG_LOGGER_INFO(spdlog::get("game"), "Test carry: {} at ({:.1f}, {:.1f}, {:.1f}), holding {}", chosenObject(), at.x, at.y,
			                   at.z, g_heldObject);
		}
	}
	if (k < 2.0f)
	{
		// press, swing at 10 m/s (or, for OPENBLACK_TEST_PICKUP=drop, held still to be let go of gently)
		return std::make_pair(target + glm::vec3(gentle ? 3.0f : 10.0f * (k - 1.0f), 0.0f, 0.0f), true);
	}
	if (k < 2.2f)
	{
		return std::make_pair(target + glm::vec3(gentle ? 3.0f : 10.0f, 0.0f, 0.0f), false); // release: thrown or dropped
	}
	if (gentle && k < 8.0f)
	{
		static int s_lastSecond = -1;
		const auto held = static_cast<entt::entity>(chosenObject());
		if (static_cast<int>(k) != s_lastSecond && Locator::entitiesRegistry::value().Valid(held))
		{
			s_lastSecond = static_cast<int>(k);
			const auto at = Locator::entitiesRegistry::value().Get<Transform>(held).position;
			SPDLOG_LOGGER_INFO(spdlog::get("game"), "Test drop: {} at ({:.1f}, {:.1f}, {:.1f})", static_cast<uint32_t>(held), at.x,
			                   at.y, at.z);
		}
		return std::nullopt;
	}
	return std::nullopt;
}

uint32_t FindObjectNear(const glm::vec3& point, float radius, bool byPointer)
{
	using namespace openblack::ecs::components;
	auto& registry = Locator::entitiesRegistry::value();
	// A miracle sphere floats well above the land: it is under the hand when the pointer's ray passes close by it.
	{
		constexpr float k_SphereReach = 4.0f;
		uint32_t sphere = 0;
		float best = std::numeric_limits<float>::max();
		const auto direction = glm::normalize(g_handRayDirection);
		for (const auto& [id, seed] : g_oneShotSpells)
		{
			const auto* transform = registry.Valid(static_cast<entt::entity>(id)) ? registry.TryGet<Transform>(static_cast<entt::entity>(id)) : nullptr;
			if (transform == nullptr || id == g_heldObject)
			{
				continue;
			}
			const auto toSphere = transform->position - g_handRayOrigin;
			const float along = glm::dot(toSphere, direction);
			const float off = glm::length(toSphere - direction * along);
			if (along > 0.0f && off < k_SphereReach && along < best)
			{
				best = along;
				sphere = id;
			}
		}
		if (sphere != 0)
		{
			return sphere;
		}
	}
	// An animal or person the pointer is on: the one nearest the pointer's ray, before the ground point is looked at.
	// Among rocks the ground point jumps from one rock to the next, and the sculptor's sheep, standing between his
	// rocks, was never the nearest thing to it.
	{
		constexpr float k_LivingReach = 2.0f; // from the ray to the animal's middle
		const auto direction = glm::normalize(g_handRayDirection);
		const float toGround = glm::distance(g_handRayOrigin, point) + 3.0f;
		uint32_t living = 0;
		float bestOff = k_LivingReach;
		registry.Each<const Mobile, const Transform>([&](entt::entity entity, const Mobile&, const Transform& transform) {
			const auto id = static_cast<uint32_t>(entity);
			if (id == g_heldObject || g_notPickupable.contains(id) || !registry.AnyOf<Animal, Villager>(entity))
			{
				return;
			}
			const auto middle = transform.position + glm::vec3(0.0f, 0.5f * transform.scale.y, 0.0f);
			const auto toIt = middle - g_handRayOrigin;
			const float along = glm::dot(toIt, direction);
			const float off = glm::length(toIt - direction * along);
			if (along > 0.0f && along < toGround && off < bestOff)
			{
				bestOff = off;
				living = id;
			}
		});
		if (byPointer && living != 0)
		{
			return living;
		}
	}
	uint32_t nearest = 0;
	float nearestDistance = radius * radius;
	registry.Each<const Mobile, const Transform>([&](entt::entity entity, const Mobile&, const Transform& transform) {
		const auto offset = glm::vec2(transform.position.x - point.x, transform.position.z - point.z);
		const float distance = glm::dot(offset, offset);
		if (distance < nearestDistance && !g_notPickupable.contains(static_cast<uint32_t>(entity)) &&
		    static_cast<uint32_t>(entity) != g_heldObject)
		{
			nearestDistance = distance;
			nearest = static_cast<uint32_t>(entity);
		}
	});
	// Trees too (Mike, 2026-10-18: they could not be picked up - they have no body for the ray, and are not Mobile).
	registry.Each<const Tree, const Transform>([&](entt::entity entity, const Tree&, const Transform& transform) {
		const auto offset = glm::vec2(transform.position.x - point.x, transform.position.z - point.z);
		const float distance = glm::dot(offset, offset);
		if (distance < nearestDistance && !g_notPickupable.contains(static_cast<uint32_t>(entity)) &&
		    static_cast<uint32_t>(entity) != g_heldObject)
		{
			nearestDistance = distance;
			nearest = static_cast<uint32_t>(entity);
		}
	});
	return nearest;
}

// What a press of the Action Button picks up: what the hand is on if it can be picked up, else the nearest thing that
// can, close to where it was pressed (Mike, 2026-10-18: some leeway - the hand had to be right on it).
uint32_t PickUpTarget(uint32_t underHand, const glm::vec3& point)
{
	using namespace openblack::ecs::components;
	if (CanPickUp(underHand))
	{
		return underHand;
	}
	constexpr float k_PickUpLeeway = 6.0f;
	auto& registry = Locator::entitiesRegistry::value();
	std::vector<std::pair<float, uint32_t>> near;
	auto consider = [&](entt::entity entity, const Transform& transform) {
		const auto id = static_cast<uint32_t>(entity);
		const float distance = glm::distance(glm::vec2(transform.position.x, transform.position.z), glm::vec2(point.x, point.z));
		if (distance < k_PickUpLeeway && id != g_heldObject && id != PlayersCreature() && !g_notPickupable.contains(id))
		{
			near.emplace_back(distance, id);
		}
	};
	registry.Each<const Mobile, const Transform>([&](entt::entity entity, const Mobile&, const Transform& transform) { consider(entity, transform); });
	registry.Each<const Tree, const Transform>([&](entt::entity entity, const Tree&, const Transform& transform) { consider(entity, transform); });
	std::sort(near.begin(), near.end());
	for (const auto& [distance, id] : near)
	{
		if (CanPickUp(id))
		{
			SPDLOG_LOGGER_INFO(spdlog::get("game"), "Pick up: {} taken, {:.1f} from the press", id, distance);
			return id;
		}
	}
	return 0;
}

bool HandDemoPlaying(); // below, with the hand demo player

namespace
{
// Villagers building (the original's villager states ArrivesAtStoragePitForMaterials, ArrivesAtBuildingSite,
// Building, TakeWoodFromPotForBuilding, WaitForWood): the town's villagers take up a building site on their own,
// once it has been given the town's totalWoodNeededToStartBuilding (600), fetch wood from the village store,
// carry it to the site and build with it. Only builders turn the site's wood
// into progress. The numbers are the original's, per villager type (info.dat): maxWoodCarried (250) a trip,
// woodUsedPerBuildCycle (40-50) spent each jobSpeed (4200 ms) cycle; percentLazy (a quarter) don't join in.
// A site is done after woodRequiredPerBuild x timeToBuild wood (the Citadel heart's 5 x 150).
struct Builder
{
	enum class Stage
	{
		ToStore,
		ToSite,
		Building,
		Waiting,
	} stage = Stage::ToStore;
	uint32_t site = 0;
	uint32_t pile = 0;
	float carrying = 0.0f;
	float timer = 0.0f;
	float speed = 1.5f; // metres a second, its state's (Villager::SetStateSpeed)
	uint32_t position = 0; // which of its places round the site it builds at (StandardBuildingSite::GetNextPosFromIndex)
	bool working = false; // a build animation playing (one a cycle: Villager::Building)
	// For the trace: where the builder was put last frame, the stage last logged, and when it was last reported.
	std::optional<glm::vec3> placed;
	int loggedStage = -1;
	float reportTimer = 0.0f;
};
std::unordered_map<uint32_t, Builder> g_builders;

// Villagers getting wood for their town (its wood desire: LivingActionSystem picks them, as the original's
// Villager::CheckSatisfyWoodDesire): to a tree of the town's nearest forest, chopping it (PChoppingTree, once - the
// original's PlayAnimThenSetState into ForesterChopsTree), the tree felled where it stood (FelledTree), up to
// maxWoodCarried of its woodValue taken and the rest left lying as a pile, then to the village store
// (GotoStoragePitForDropOff / ArrivesAtStoragePitForDropOff). A pile left by an earlier felling near the forest is
// taken from first.
struct Forester
{
	enum class Stage
	{
		ToTree,
		Chopping,
		Felled,
		ToPile,
		ToStore,
	} stage = Stage::ToTree;
	uint32_t tree = 0;
	uint32_t pile = 0;
	uint32_t town = 0;
	float speed = 1.5f; // metres a second, the state's (Villager::SetStateSpeed)
	float carrying = 0.0f;
	float timer = 0.0f;
};
std::unordered_map<uint32_t, Forester> g_foresters;

// Villagers getting food for their town (its food desire, the original's Villager::CheckSatisfyFoodDesire: the best
// field by Town::FindBestField, then VillagerBecomesFarmer): to a field that wants farming - sowing it, crop by crop
// at spots about it (FarmerArrivesAtFarm / FarmerPlantsCrop, PFarmerSowingSeeds), till its timesToSow are in; or, ripe,
// harvesting it (FarmerDigsUpCrop, PFarmerHarvesting: Field::RemoveFood up to what it can carry) and carrying the grain
// to the village store (GotoStoragePitForDropOff). Shepherds and fishermen, the other two ways, not yet.
struct Farmer
{
	enum class Stage
	{
		ToField,
		Sowing,
		Harvesting,
		ToStore,
	} stage = Stage::ToField;
	uint32_t field = 0;
	uint32_t town = 0;
	int activity = 1; // Field::GetFieldActivity: 1 sow, 2 harvest
	float speed = 1.5f;
	float carrying = 0.0f;
	float timer = 0.0f;
	glm::vec3 spot {0.0f};
	bool working = false;
};
std::unordered_map<uint32_t, Farmer> g_farmers;

// The plank a builder carries to the site: the original's ObjectWoodInHand (a 1.8 tall board), at 0.6, lying along
// the right shoulder, pointing the way it walks.
std::unordered_map<uint32_t, entt::entity> g_builderPlanks;
void CarryPlank(uint32_t builder, bool carrying, MeshId carried = MeshId::ObjectWoodInHand)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto it = g_builderPlanks.find(builder);
	const auto who = static_cast<entt::entity>(builder);
	if (!carrying || !registry.Valid(who))
	{
		if (it != g_builderPlanks.end())
		{
			if (registry.Valid(it->second))
			{
				registry.Destroy(it->second);
			}
			g_builderPlanks.erase(it);
		}
		return;
	}
	const auto mesh = resources::HashIdentifier(carried);
	auto plank = it != g_builderPlanks.end() ? it->second : entt::null;
	if (plank == entt::null || !registry.Valid(plank))
	{
		plank = registry.Create();
		registry.Assign<Transform>(plank, glm::vec3(0.0f), glm::mat3(1.0f), glm::vec3(1.0f));
		registry.Assign<openblack::ecs::components::Mesh>(plank, mesh, static_cast<int8_t>(0), static_cast<int8_t>(0));
		g_builderPlanks[builder] = plank;
	}
	else if (auto& shown = registry.Get<openblack::ecs::components::Mesh>(plank); shown.id != mesh)
	{
		shown.id = mesh; // the plank for wood, the sheaf for grain
	}
	constexpr float k_PlankScale = 0.6f;
	const auto& body = registry.Get<Transform>(who);
	const auto& meshes = Locator::resources::value().GetMeshes();
	const auto box = meshes.Contains(mesh) ? std::optional(meshes.Handle(mesh)->GetBoundingBox()) : std::nullopt;
	const glm::vec3 middle = box ? box->Center() : glm::vec3(0.0f);
	// Lying down, its height is its depth (z): it sits that much lower, on the shoulder rather than above it
	// (Mike, 2026-10-17).
	const float lying = box ? (box->maxima.z - box->minima.z) * k_PlankScale * body.scale.y : 0.0f;
	// The board's length is its y: laid down along the walker's forward (-z), on its right shoulder (+x).
	const auto lie = glm::mat3(glm::eulerAngleX(-glm::half_pi<float>()));
	const auto shoulder = glm::vec3(0.22f, 1.38f, 0.0f) * body.scale.y - glm::vec3(0.0f, lying, 0.0f);
	auto& transform = registry.Get<Transform>(plank);
	transform.rotation = body.rotation * lie;
	transform.scale = glm::vec3(k_PlankScale * body.scale.y);
	transform.position = body.position + body.rotation * shoulder - transform.rotation * (middle * transform.scale);
}
float g_builderRecruitTimer = 0.0f;
constexpr float k_BuilderRecruitRadius = 300.0f; // Land 1's village by its store is ~150 from the Temple
constexpr float k_BuildingSiteRadius = 16.0f; // builders work round the site at about this distance
constexpr float k_WoodSearchRadius = 400.0f;
constexpr float k_BuilderWalk = 0.3f; // of full (fleeing) speed: a walk

const GVillagerInfo& VillagerInfoOf(const openblack::ecs::components::Villager& villager)
{
	return Locator::infoConstants::value().villager.at(
	    static_cast<size_t>(openblack::GVillagerInfo::Find(villager.tribe, villager.number)));
}

// Why a villager can't take up building (nullptr: it can): only adults no script, flock, walk or hand has hold
// of, and not the lazy ones.
const char* NotFreeToBuild(entt::entity entity, const openblack::ecs::components::Villager& villager)
{
	const auto id = static_cast<uint32_t>(entity);
	if (villager.lifeStage != openblack::ecs::components::Villager::LifeStage::Adult)
	{
		return "child";
	}
	if (id == g_heldObject || g_flying.contains(id))
	{
		return "in the hand";
	}
	if (g_scriptEntities.contains(id))
	{
		return "the script's";
	}
	if (g_walks.contains(id) || g_followers.contains(id) || g_scriptMoves.contains(id))
	{
		return "moved by a script";
	}
	if (g_scriptAnimations.contains(id))
	{
		return "animated by a script";
	}
	if (g_foresters.contains(id))
	{
		return "getting wood";
	}
	if (g_farmers.contains(id))
	{
		return "farming";
	}
	// The lazy ones: a fixed share of the villagers, by who they are.
	const auto hash = (id * 2654435761u) % 100u;
	return static_cast<float>(hash) < VillagerInfoOf(villager).percentLazy * 100.0f ? "lazy" : nullptr;
}

// Step a builder towards a point at a walk; true once there.
bool WalkBuilder(entt::entity entity, Transform& transform, const glm::vec3& target, float seconds, float speed)
{
	const auto toTarget = glm::vec2(target.x - transform.position.x, target.z - transform.position.z);
	const float distance = glm::length(toTarget);
	const float step = speed * seconds;
	if (distance <= std::max(step, 0.5f))
	{
		return true;
	}
	PlaceMover(entity, transform, transform.position + glm::vec3(toTarget.x, 0.0f, toTarget.y) / distance * step, target);
	return false;
}

void SetBuilderAnimation(uint32_t id, std::optional<AnimId> animation)
{
	if (!animation)
	{
		g_scriptAnimations.erase(id);
		return;
	}
	auto& scripted = g_scriptAnimations[id];
	if (scripted.animation != static_cast<uint32_t>(*animation))
	{
		scripted = {static_cast<uint32_t>(*animation), std::numeric_limits<float>::max()};
	}
	else
	{
		scripted.playsLeft = std::numeric_limits<float>::max();
	}
}

void CarryPlank(uint32_t builder, bool carrying, MeshId carried);
void ReleaseBuilder(uint32_t id)
{
	auto& registry = Locator::entitiesRegistry::value();
	g_scriptAnimations.erase(id);
	CarryPlank(id, false);
	const auto entity = static_cast<entt::entity>(id);
	if (registry.Valid(entity))
	{
		registry.Remove<ScriptMove>(entity); // back to its own life
	}
}

void UpdateBuilders(float seconds)
{
	using openblack::ecs::components::Construction;
	using openblack::ecs::components::Pot;
	using openblack::ecs::components::Villager;
	auto& registry = Locator::entitiesRegistry::value();

	// Take up sites: now and then, free villagers near a site that is still being built join in.
	g_builderRecruitTimer -= seconds;
	static float s_reportTimer = 0.0f;
	s_reportTimer -= seconds;
	if (g_builderRecruitTimer <= 0.0f)
	{
		g_builderRecruitTimer = 1.0f;
		std::map<std::string, int> passedOver;
		int sitesWithoutBuilders = 0;
		registry.Each<Construction, const Transform>(
		    [&](entt::entity site, Construction& construction, const Transform& siteTransform) {
			    construction.started = construction.started ||
			                           construction.woodStock >= Locator::infoConstants::value().town.totalWoodNeededToStartBuilding;
			    if (construction.built >= 1.0f || !construction.started)
			    {
				    return;
			    }
			    sitesWithoutBuilders += std::none_of(g_builders.begin(), g_builders.end(), [site](const auto& builder) {
				                            return builder.second.site == static_cast<uint32_t>(site);
			                            })
			                                ? 1
			                                : 0;
			    // Its builders come of the town's desire to build (StartBuilder, from LivingActionSystem): not any free
			    // villager near it, as before the original's (2026-10-19).
			    static_cast<void>(siteTransform);
		    });
		// A started site nobody has taken up: say why, now and then.
		if (sitesWithoutBuilders > 0 && s_reportTimer <= 0.0f)
		{
			s_reportTimer = 30.0f;
			std::string why;
			for (const auto& [reason, count] : passedOver)
			{
				why += fmt::format("{}{} {}", why.empty() ? "" : ", ", count, reason);
			}
			SPDLOG_LOGGER_INFO(spdlog::get("game"), "Building site without builders (waiting on the town's building desire): {}",
			                   why.empty() ? "none turned down" : why);
		}
	}

	for (auto it = g_builders.begin(); it != g_builders.end();)
	{
		const auto id = it->first;
		auto& builder = it->second;
		const auto entity = static_cast<entt::entity>(id);
		const auto siteEntity = static_cast<entt::entity>(builder.site);
		auto* construction = registry.Valid(siteEntity) ? registry.TryGet<Construction>(siteEntity) : nullptr;
		auto* villager = registry.Valid(entity) ? registry.TryGet<Villager>(entity) : nullptr;
		// Done (or taken away: picked up, or a script has it now): the builder goes back to its own life.
		if (construction == nullptr || construction->built >= 1.0f || villager == nullptr || id == g_heldObject ||
		    g_walks.contains(id) || g_followers.contains(id) || g_scriptMoves.contains(id) || g_flying.contains(id))
		{
			ReleaseBuilder(id);
			it = g_builders.erase(it);
			continue;
		}
		auto& transform = registry.Get<Transform>(entity);
		const auto& site = registry.Get<Transform>(siteEntity).position;
		const auto& info = VillagerInfoOf(*villager);
		// Trace (Mike, 2026-10-17: builders looked "sucked backwards" instead of carrying wood to the site): each
		// stage once, and anything else moving the builder since this put it somewhere, at most once a second.
		builder.reportTimer -= seconds;
		static constexpr std::array<const char*, 4> k_StageNames = {"to the wood store", "to the site", "building", "waiting for wood"};
		if (builder.loggedStage != static_cast<int>(builder.stage))
		{
			builder.loggedStage = static_cast<int>(builder.stage);
			SPDLOG_LOGGER_INFO(spdlog::get("game"), "Builder {}: {} (at ({:.1f}, {:.1f}), site {:.1f} away, carrying {:.0f})", id,
			                   k_StageNames.at(static_cast<size_t>(builder.stage)), transform.position.x, transform.position.z,
			                   glm::distance(glm::vec2(transform.position.x, transform.position.z), glm::vec2(site.x, site.z)),
			                   builder.carrying);
		}
		if (builder.placed && glm::distance(glm::vec2(builder.placed->x, builder.placed->z),
		                                    glm::vec2(transform.position.x, transform.position.z)) > 0.05f &&
		    builder.reportTimer <= 0.0f)
		{
			builder.reportTimer = 1.0f;
			SPDLOG_LOGGER_INFO(spdlog::get("game"),
			                   "Builder {} ({}) moved by something else: from ({:.1f}, {:.1f}) to ({:.1f}, {:.1f}); state {}, script move {}, anim {}",
			                   id, k_StageNames.at(static_cast<size_t>(builder.stage)), builder.placed->x, builder.placed->z,
			                   transform.position.x, transform.position.z,
			                   registry.TryGet<openblack::ecs::components::LivingAction>(entity) != nullptr
			                       ? static_cast<int>(registry.Get<openblack::ecs::components::LivingAction>(entity)
			                                              .states[static_cast<size_t>(openblack::ecs::components::LivingAction::Index::Top)])
			                       : -1,
			                   registry.AllOf<ScriptMove>(entity), g_scriptAnimations.contains(id) ? static_cast<int>(g_scriptAnimations[id].animation) : -1);
		}
		// Each builder has a place of its own round the site, and moves on to the next after each go at it.
		const float angle = static_cast<float>((id * 2654435761u + builder.position * 47u) % 360u) * glm::pi<float>() / 180.0f;
		const auto spot = site + glm::vec3(std::cos(angle), 0.0f, std::sin(angle)) * k_BuildingSiteRadius;

		switch (builder.stage)
		{
		case Builder::Stage::ToStore:
		{
			SetBuilderAnimation(id, std::nullopt);
			auto* pile = registry.Valid(static_cast<entt::entity>(builder.pile))
			                 ? registry.TryGet<Pot>(static_cast<entt::entity>(builder.pile))
			                 : nullptr;
			if (pile == nullptr || pile->amount == 0)
			{
				builder.pile = FindWoodNear(site, k_WoodSearchRadius);
				if (builder.pile == 0)
				{
					builder.stage = Builder::Stage::Waiting; // WaitForWood
					break;
				}
				pile = &registry.Get<Pot>(static_cast<entt::entity>(builder.pile));
			}
			if (WalkBuilder(entity, transform, registry.Get<Transform>(static_cast<entt::entity>(builder.pile)).position, seconds, builder.speed))
			{
				// TakeWoodFromPotForBuilding
				const auto take = std::min<uint32_t>(info.maxWoodCarried, pile->amount);
				pile->amount = static_cast<uint16_t>(pile->amount - take);
				builder.carrying = static_cast<float>(take);
				builder.stage = Builder::Stage::ToSite;
			}
			break;
		}
		case Builder::Stage::ToSite:
			// Walking (by the ground covered) with a plank on the shoulder: M_P_Carrying_Sticks is no walk - its feet
			// stay put, and played along the way the builder was dragged, arms and head swaying (Mike, 2026-10-17).
			SetBuilderAnimation(id, std::nullopt);
			if (WalkBuilder(entity, transform, spot, seconds, builder.speed))
			{
				construction->woodStock += builder.carrying; // ArrivesAtBuildingSite
				builder.carrying = 0.0f;
				builder.stage = Builder::Stage::Building;
				builder.working = false;
			}
			break;
		case Builder::Stage::Building:
		{
			// Villager::Building, each time its build animation has played (BuildingAnimation: one of the three at
			// random each go): the wood it uses - woodUsedPerBuildCycle, or what the site has left - builds it by
			// that share of its wood value; then on to its next place, or for more wood.
			if (glm::distance(glm::vec2(transform.position.x, transform.position.z), glm::vec2(spot.x, spot.z)) > 3.0f)
			{
				builder.stage = Builder::Stage::ToSite;
				break;
			}
			FaceTowards(transform, site);
			if (!builder.working)
			{
				if (construction->woodStock <= 0.0f)
				{
					builder.stage = Builder::Stage::ToStore; // out of wood: fetch more (SetupGetBuildingSupplies)
					break;
				}
				constexpr std::array k_Work = {AnimId::PHammering, AnimId::PSawWood, AnimId::PSledgehammer};
				const auto animation = k_Work.at(static_cast<size_t>(Locator::rng::value().NextValue(0.0f, 2.99f)));
				const auto [rate, duration] = AnimationRate(static_cast<uint32_t>(animation));
				builder.timer = rate > 0.0f ? duration / rate : 2.0f;
				builder.working = true;
				g_scriptAnimations[id] = {static_cast<uint32_t>(animation), 1.0f};
				break;
			}
			builder.timer -= seconds;
			if (builder.timer > 0.0f && g_scriptAnimations.contains(id))
			{
				break;
			}
			builder.working = false;
			const float use = std::min(construction->woodStock, static_cast<float>(info.woodUsedPerBuildCycle));
			construction->woodStock -= use;
			const float before = construction->built;
			construction->built = std::min(construction->built + use / (construction->woodPerStep * construction->steps), 1.0f);
			if (std::floor(construction->built * 10.0f) > std::floor(before * 10.0f) && construction->built < 1.0f)
			{
				SPDLOG_LOGGER_INFO(spdlog::get("game"), "Building {} {:.0f}% built ({:.0f} wood in stock)", builder.site,
				                   construction->built * 100.0f, construction->woodStock);
			}
			if (construction->built >= 1.0f)
			{
				SPDLOG_LOGGER_INFO(spdlog::get("game"), "Building {} built ({:.0f} wood left over)", builder.site, construction->woodStock);
			}
			++builder.position;
			builder.stage = construction->woodStock > 0.0f ? Builder::Stage::ToSite : Builder::Stage::ToStore;
			break;
		}
		case Builder::Stage::Waiting:
			SetBuilderAnimation(id, std::nullopt);
			WalkBuilder(entity, transform, spot, seconds, builder.speed);
			builder.timer += seconds;
			if (builder.timer >= 2.0f)
			{
				builder.timer = 0.0f;
				// Wood at the site: go and build with it (walking there first - ToSite); else fetch some.
				builder.stage = construction->woodStock >= info.woodUsedPerBuildCycle ? Builder::Stage::ToSite
				                                                                      : Builder::Stage::ToStore;
			}
			break;
		}
		builder.placed = transform.position;
		CarryPlank(id, builder.stage == Builder::Stage::ToSite && builder.carrying > 0.0f);
		++it;
	}
}
} // namespace


namespace
{
// The town a building site is its: the nearest town to it.
uint32_t SiteTown(entt::entity site)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto& at = registry.Get<Transform>(site).position;
	uint32_t best = 0;
	float bestDistance = std::numeric_limits<float>::max();
	registry.Each<const openblack::ecs::components::Town, const Transform>(
	    [&](entt::entity town, const openblack::ecs::components::Town&, const Transform& where) {
		    const float d = glm::distance(glm::vec2(where.position.x, where.position.z), glm::vec2(at.x, at.z));
		    if (d < bestDistance)
		    {
			    bestDistance = d;
			    best = static_cast<uint32_t>(town);
		    }
	    });
	return best;
}

// BuildingSite::GetBuildersNeeded / GetDesireForVillagers: the builders it still wants of its building's
// maxVillagerNeededToBuild (the Citadel heart's for a planned Citadel), as a share of them.
std::pair<int, float> SiteBuilderDesire(entt::entity site, const openblack::ecs::components::Construction& construction)
{
	if (!construction.started || construction.built >= 1.0f)
	{
		return {0, 0.0f};
	}
	const auto most = static_cast<int>(std::max(Locator::infoConstants::value().citadelHeart.maxVillagerNeededToBuild, 1u));
	const auto working = static_cast<int>(std::count_if(g_builders.begin(), g_builders.end(),
	                                                     [site](const auto& b) { return b.second.site == static_cast<uint32_t>(site); }));
	const int needed = most - working;
	return {needed, std::clamp(static_cast<float>(needed) / static_cast<float>(most), 0.0f, 1.0f)};
}
} // namespace

float TownBuildDesire(uint32_t town)
{
	// The town's TO_BUILD desire (0x748640): its building sites' desire for builders, together, at most 1.
	auto& registry = Locator::entitiesRegistry::value();
	float desire = 0.0f;
	registry.Each<openblack::ecs::components::Construction, const Transform>(
	    [&](entt::entity site, openblack::ecs::components::Construction& construction, const Transform&) {
		    if (registry.AllOf<openblack::ecs::components::Feature>(site))
		    {
			    return; // the Ark: the Missionaries' quest builds it, not the town
		    }
		    // Test hook: OPENBLACK_TEST_START_SITES=1 starts every planned site with 1000 wood given.
		    static const bool s_startSites = std::getenv("OPENBLACK_TEST_START_SITES") != nullptr;
		    if (s_startSites && !construction.started)
		    {
			    construction.woodStock = std::max(construction.woodStock, 1000.0f);
		    }
		    construction.started = construction.started ||
		                           construction.woodStock >= Locator::infoConstants::value().town.totalWoodNeededToStartBuilding;
		    if (SiteTown(site) == town)
		    {
			    desire += SiteBuilderDesire(site, construction).second;
		    }
	    });
	return std::min(desire, 1.0f);
}

bool StartBuilder(uint32_t villager, float metresPerSecond)
{
	using namespace openblack::ecs::components;
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = static_cast<entt::entity>(villager);
	const auto* person = registry.Valid(entity) ? registry.TryGet<Villager>(entity) : nullptr;
	if (person == nullptr || g_builders.contains(villager) || NotFreeToBuild(entity, *person) != nullptr || person->town == entt::null)
	{
		return false;
	}
	const auto& at = registry.Get<Transform>(entity).position;
	// Town::GetBestBuildingSite: of its sites wanting builders, the nearest, weighed by how much (distance x
	// (0.95 desire + 0.1), the least).
	entt::entity best = entt::null;
	float bestScore = std::numeric_limits<float>::max();
	registry.Each<const Construction, const Transform>([&](entt::entity site, const Construction& construction, const Transform& where) {
		if (registry.AllOf<Feature>(site) || SiteTown(site) != static_cast<uint32_t>(person->town))
		{
			return;
		}
		const auto [needed, desire] = SiteBuilderDesire(site, construction);
		if (needed <= 0)
		{
			return;
		}
		const float score = glm::distance(where.position, at) * (desire * 0.95f + 0.1f);
		if (score < bestScore)
		{
			bestScore = score;
			best = site;
		}
	});
	if (best == entt::null)
	{
		return false;
	}
	const auto& construction = registry.Get<Construction>(best);
	const auto& info = VillagerInfoOf(*person);
	// BuildingSite::ShouldIGetWood: near it (within 50), only if it has none; farther, if it has less than it needs.
	const float distance = glm::distance(registry.Get<Transform>(best).position, at);
	const float needed = construction.woodPerStep * construction.steps * (1.0f - construction.built);
	const bool fetch = distance < 50.0f ? construction.woodStock <= 0.0f : construction.woodStock < std::min(needed, static_cast<float>(info.woodUsedPerBuildCycle));
	HoldForScript(entity, at);
	Builder builder;
	builder.stage = fetch ? Builder::Stage::ToStore : Builder::Stage::ToSite;
	builder.site = static_cast<uint32_t>(best);
	builder.speed = metresPerSecond;
	builder.position = villager % 8;
	g_builders[villager] = builder;
	SPDLOG_LOGGER_INFO(spdlog::get("game"), "Villager {} takes up building {} ({})", villager, static_cast<uint32_t>(best),
	                   fetch ? "for wood first" : "straight to it");
	return true;
}

namespace
{
void ReleaseForester(uint32_t id)
{
	auto& registry = Locator::entitiesRegistry::value();
	g_scriptAnimations.erase(id);
	CarryPlank(id, false);
	const auto entity = static_cast<entt::entity>(id);
	if (registry.Valid(entity))
	{
		registry.Remove<ScriptMove>(entity); // back to its own life (DECIDE_WHAT_TO_DO)
		if (auto* action = registry.TryGet<LivingAction>(entity))
		{
			Locator::livingActionSystem::value().VillagerSetState(*action, LivingAction::Index::Top, VillagerStates::DecideWhatToDo, true);
		}
	}
}

// A step towards a point at the forester's speed; true once there.
bool WalkForester(entt::entity entity, Transform& transform, const glm::vec3& target, float speed, float seconds)
{
	const auto toTarget = glm::vec2(target.x - transform.position.x, target.z - transform.position.z);
	const float distance = glm::length(toTarget);
	const float step = speed * seconds;
	if (distance <= std::max(step, 0.5f))
	{
		return true;
	}
	PlaceMover(entity, transform, transform.position + glm::vec3(toTarget.x, 0.0f, toTarget.y) / distance * step, target);
	return false;
}

// Where a forester stands to chop a tree: at its trunk, on its own side (Tree::GetWorkingPos).
glm::vec3 TreeWorkingPos(const glm::vec3& tree, const glm::vec3& from)
{
	auto away = glm::vec2(from.x - tree.x, from.z - tree.z);
	away = glm::length(away) > 1e-3f ? glm::normalize(away) : glm::vec2(1.0f, 0.0f);
	return tree + glm::vec3(away.x, 0.0f, away.y) * 1.5f;
}

bool TreeTaken(uint32_t tree)
{
	return std::any_of(g_foresters.begin(), g_foresters.end(), [tree](const auto& f) { return f.second.tree == tree; });
}

// A standing tree for the town: of its nearest forest (Town::FindNearestForestToPos), nearest the forester there
// (FindTreeNearVillager); without a forest, the nearest standing tree within 150.
uint32_t FindTreeToFell(const glm::vec3& villager, const glm::vec3& town)
{
	using namespace openblack::ecs::components;
	auto& registry = Locator::entitiesRegistry::value();
	std::optional<glm::vec3> forest;
	float nearest = 400.0f;
	registry.Each<const Forest, const Transform>([&](entt::entity, const Forest&, const Transform& where) {
		const float d = glm::distance(glm::vec2(where.position.x, where.position.z), glm::vec2(town.x, town.z));
		if (d < nearest)
		{
			nearest = d;
			forest = where.position;
		}
	});
	const auto around = forest.value_or(villager);
	const float reach = forest ? 40.0f : 150.0f;
	if (std::getenv("OPENBLACK_TRACE_FORESTERS") != nullptr)
	{
		int fixed = 0, loose = 0;
		registry.Each<const Tree, const Transform>([&](entt::entity e, const Tree&, const Transform& where) {
			if (glm::distance(glm::vec2(where.position.x, where.position.z), glm::vec2(around.x, around.z)) <= reach)
			{
				(registry.AllOf<Fixed>(e) ? fixed : loose)++;
			}
		});
		SPDLOG_LOGGER_INFO(spdlog::get("game"), "Tree search for town at ({:.0f}, {:.0f}): forest {} ({:.0f} away), {} rooted / {} not within {:.0f}",
		                   town.x, town.z, forest ? "found" : "none", nearest, fixed, loose, reach);
	}
	uint32_t best = 0;
	float bestDistance = std::numeric_limits<float>::max();
	registry.Each<const Tree, const Transform, const Fixed>([&](entt::entity entity, const Tree&, const Transform& where, const Fixed&) {
		const auto id = static_cast<uint32_t>(entity);
		if (id == g_heldObject || g_flying.contains(id) || TreeTaken(id) ||
		    glm::distance(glm::vec2(where.position.x, where.position.z), glm::vec2(around.x, around.z)) > reach)
		{
			return;
		}
		const float d = glm::distance(where.position, villager);
		if (d < bestDistance)
		{
			bestDistance = d;
			best = id;
		}
	});
	return best;
}

// Wood a felling left lying (a loose pile, not a store's) within `radius`.
uint32_t LooseWoodNear(const glm::vec3& at, float radius)
{
	using namespace openblack::ecs::components;
	auto& registry = Locator::entitiesRegistry::value();
	uint32_t best = 0;
	float bestDistance = radius;
	registry.Each<const Pot, const Transform>([&](entt::entity entity, const Pot& pot, const Transform& where) {
		const auto id = static_cast<uint32_t>(entity);
		if (pot.type != PotInfo::MagicWood || pot.amount == 0 || id == g_heldObject || g_flying.contains(id) ||
		    std::any_of(g_foresters.begin(), g_foresters.end(), [id](const auto& f) { return f.second.pile == id; }))
		{
			return;
		}
		const float d = glm::distance(where.position, at);
		if (d < bestDistance)
		{
			bestDistance = d;
			best = id;
		}
	});
	return best;
}

// The town's village store (the nearest storage pit to its centre).
std::optional<glm::vec3> TownStore(uint32_t town)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = static_cast<entt::entity>(town);
	if (!registry.Valid(entity) || !registry.AllOf<Transform>(entity))
	{
		return std::nullopt;
	}
	const auto centre = registry.Get<Transform>(entity).position;
	std::optional<glm::vec3> store;
	float best = 300.0f;
	registry.Each<const openblack::ecs::components::StoragePit, const Transform>(
	    [&](entt::entity, const openblack::ecs::components::StoragePit&, const Transform& where) {
		    const float d = glm::distance(where.position, centre);
		    if (d < best)
		    {
			    best = d;
			    store = where.position;
		    }
	    });
	return store;
}

void UpdateForesters(float seconds)
{
	using namespace openblack::ecs::components;
	auto& registry = Locator::entitiesRegistry::value();
	for (auto it = g_foresters.begin(); it != g_foresters.end();)
	{
		const auto id = it->first;
		auto& forester = it->second;
		const auto entity = static_cast<entt::entity>(id);
		auto* villager = registry.Valid(entity) ? registry.TryGet<Villager>(entity) : nullptr;
		// Taken away (picked up, thrown, a script's now): the wood it carried goes with it, the job ends.
		if (villager == nullptr || id == g_heldObject || g_flying.contains(id) || g_walks.contains(id) || g_followers.contains(id) ||
		    g_scriptMoves.contains(id) || g_builders.contains(id))
		{
			ReleaseForester(id);
			it = g_foresters.erase(it);
			continue;
		}
		auto& transform = registry.Get<Transform>(entity);
		const auto& info = VillagerInfoOf(*villager);
		bool done = false;
		switch (forester.stage)
		{
		case Forester::Stage::ToTree:
		{
			const auto treeEntity = static_cast<entt::entity>(forester.tree);
			if (!registry.Valid(treeEntity) || !registry.AllOf<Tree, Fixed>(treeEntity) || forester.tree == g_heldObject)
			{
				done = true; // gone (uprooted, thrown): it thinks again
				break;
			}
			SetBuilderAnimation(id, std::nullopt);
			const auto& tree = registry.Get<Transform>(treeEntity).position;
			if (WalkForester(entity, transform, TreeWorkingPos(tree, transform.position), forester.speed, seconds))
			{
				FaceTowards(transform, tree);
				forester.stage = Forester::Stage::Chopping;
				const auto [rate, duration] = AnimationRate(static_cast<uint32_t>(AnimId::PChoppingTree));
				forester.timer = rate > 0.0f ? duration / rate : 1.0f; // one play of it
				g_scriptAnimations[id] = {static_cast<uint32_t>(AnimId::PChoppingTree), 1.0f};
			}
			break;
		}
		case Forester::Stage::Chopping:
		{
			const auto treeEntity = static_cast<entt::entity>(forester.tree);
			forester.timer -= seconds;
			if (!registry.Valid(treeEntity) || !registry.AllOf<Tree, Fixed>(treeEntity))
			{
				done = true;
				break;
			}
			if (forester.timer > 0.0f && g_scriptAnimations.contains(id))
			{
				break;
			}
			// FelledTree::Create: down it comes, away from the forester.
			const auto& tree = registry.Get<Transform>(treeEntity).position;
			const auto away = tree - transform.position;
			registry.Remove<Fixed>(treeEntity); // no longer in the walkers' way
			LayTreeDown(treeEntity, tree, glm::vec3(away.x, 0.0f, away.z));
			forester.stage = Forester::Stage::Felled;
			forester.timer = 1.5f; // lying felled a moment
			SPDLOG_LOGGER_INFO(spdlog::get("game"), "Villager {} fells tree {} for town {}", id, forester.tree, forester.town);
			break;
		}
		case Forester::Stage::Felled:
		{
			forester.timer -= seconds;
			if (forester.timer > 0.0f)
			{
				break;
			}
			const auto treeEntity = static_cast<entt::entity>(forester.tree);
			if (!registry.Valid(treeEntity))
			{
				done = true;
				break;
			}
			const auto wood = ShredWood(treeEntity).value_or(0u);
			const auto at = registry.Get<Transform>(treeEntity).position;
			const auto take = std::min<uint32_t>(info.maxWoodCarried, wood);
			if (wood > take)
			{
				openblack::ecs::archetypes::PotArchetype::Create(glm::vec3(at.x, Locator::terrainSystem::value().GetHeightAt(glm::vec2(at.x, at.z)), at.z), 0.0f,
				                                                 PotInfo::MagicWood, static_cast<int32_t>(wood - take));
			}
			ForgetObject(forester.tree);
			g_treeFootprints.erase(forester.tree);
			registry.Destroy(treeEntity);
			forester.tree = 0;
			forester.carrying = static_cast<float>(take);
			PlayBankSound(1, 151, &at); // the wood knock: the logs taken up
			forester.stage = Forester::Stage::ToStore;
			break;
		}
		case Forester::Stage::ToPile:
		{
			auto* pot = registry.Valid(static_cast<entt::entity>(forester.pile)) ? registry.TryGet<Pot>(static_cast<entt::entity>(forester.pile)) : nullptr;
			if (pot == nullptr || pot->amount == 0 || forester.pile == g_heldObject)
			{
				done = true;
				break;
			}
			SetBuilderAnimation(id, std::nullopt);
			if (WalkForester(entity, transform, registry.Get<Transform>(static_cast<entt::entity>(forester.pile)).position, forester.speed, seconds))
			{
				const auto take = std::min<uint32_t>(info.maxWoodCarried, pot->amount);
				pot->amount = static_cast<uint16_t>(pot->amount - take);
				forester.carrying = static_cast<float>(take);
				if (pot->amount == 0)
				{
					ForgetObject(forester.pile);
					registry.Destroy(static_cast<entt::entity>(forester.pile));
				}
				forester.pile = 0;
				forester.stage = Forester::Stage::ToStore;
			}
			break;
		}
		case Forester::Stage::ToStore:
		{
			SetBuilderAnimation(id, std::nullopt);
			const auto store = TownStore(forester.town);
			if (!store)
			{
				done = true;
				break;
			}
			if (WalkForester(entity, transform, *store, forester.speed * 0.9f, seconds))
			{
				// ArrivesAtStoragePitForDropOff: into the store's wood.
				const auto pile = StoreWoodPileAt(*store);
				if (pile != entt::null)
				{
					AddToPile(pile, static_cast<int32_t>(forester.carrying));
					PlayBankSound(1, 155 + static_cast<int32_t>(Locator::rng::value().NextValue(0.0f, 3.99f)), &*store);
					SPDLOG_LOGGER_INFO(spdlog::get("game"), "Villager {} brings {:.0f} wood to town {}'s store", id, forester.carrying,
					                   forester.town);
				}
				forester.carrying = 0.0f;
				done = true;
			}
			break;
		}
		}
		CarryPlank(id, forester.stage == Forester::Stage::ToStore && forester.carrying > 0.0f);
		if (done)
		{
			ReleaseForester(id);
			it = g_foresters.erase(it);
			continue;
		}
		++it;
	}
}
} // namespace

bool StartForester(uint32_t villager, float metresPerSecond)
{
	using namespace openblack::ecs::components;
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = static_cast<entt::entity>(villager);
	const auto* person = registry.Valid(entity) ? registry.TryGet<Villager>(entity) : nullptr;
	const char* why = person == nullptr ? "not a villager" : g_foresters.contains(villager) ? "already at it" : NotFreeToBuild(entity, *person);
	if (why == nullptr && (person->town == entt::null || !registry.Valid(person->town) || !registry.AllOf<Transform>(person->town)))
	{
		why = "no town";
	}
	static std::unordered_map<uint32_t, std::string> s_toldWhy; // each reason once per villager
	if (why != nullptr)
	{
		if (auto [told, fresh] = s_toldWhy.try_emplace(villager, why); fresh || told->second != why)
		{
			told->second = why;
			SPDLOG_LOGGER_INFO(spdlog::get("game"), "Villager {} can't go for wood: {}", villager, why);
		}
		return false;
	}
	const auto& at = registry.Get<Transform>(entity).position;
	const auto& town = registry.Get<Transform>(person->town).position;
	Forester forester;
	forester.town = static_cast<uint32_t>(person->town);
	forester.speed = metresPerSecond;
	forester.tree = FindTreeToFell(at, town);
	if (forester.tree != 0)
	{
		forester.pile = LooseWoodNear(registry.Get<Transform>(static_cast<entt::entity>(forester.tree)).position, 25.0f);
	}
	if (forester.pile != 0)
	{
		forester.tree = 0;
		forester.stage = Forester::Stage::ToPile;
	}
	else if (forester.tree == 0)
	{
		if (auto [told, fresh] = s_toldWhy.try_emplace(villager, "no tree"); fresh || told->second != "no tree")
		{
			told->second = "no tree";
			SPDLOG_LOGGER_INFO(spdlog::get("game"), "Villager {} can't go for wood: no standing tree near town {}", villager, forester.town);
		}
		return false; // no forest for it (DecideHowToGetWood: nothing)
	}
	HoldForScript(entity, at);
	g_foresters[villager] = forester;
	SPDLOG_LOGGER_INFO(spdlog::get("game"), "Villager {} goes for wood for town {} ({})", villager, forester.town,
	                   forester.pile != 0 ? "a pile left lying" : "a tree to fell");
	return true;
}

uint32_t ForesterCount(uint32_t town)
{
	return static_cast<uint32_t>(std::count_if(g_foresters.begin(), g_foresters.end(), [town](const auto& f) { return f.second.town == town; }));
}

namespace
{
// Field::GetFieldActivity: 1 to sow (not all its crops in), 2 to harvest (ripe: grown to ageGrowth), 0 growing.
int FieldActivity(const openblack::ecs::components::Field& field)
{
	const auto& type = Locator::infoConstants::value().fieldType.at(field.fieldType);
	if (type.timesToSow > 0.0f && static_cast<float>(field.crops) / type.timesToSow < 1.0f)
	{
		return 1;
	}
	return field.growth >= type.ageGrowth ? 2 : 0;
}

uint32_t FarmersOn(uint32_t field)
{
	return static_cast<uint32_t>(std::count_if(g_farmers.begin(), g_farmers.end(), [field](const auto& f) { return f.second.field == field; }));
}

// Field::GetDesireToBeFarmed: sowing wanted the less it is sown and the fewer farm it, harvesting (fully grown) the
// fewer farm it - against its maxFarmerInFarm.
float DesireToBeFarmed(uint32_t id, const openblack::ecs::components::Field& field)
{
	const auto& type = Locator::infoConstants::value().fieldType.at(field.fieldType);
	const float crowd = std::min(static_cast<float>(FarmersOn(id)) / static_cast<float>(std::max(type.maxFarmerInFarm, 1u)), 1.0f);
	const float room = 1.0f - crowd;
	const float sown = type.timesToSow > 0.0f ? std::min(static_cast<float>(field.crops) / type.timesToSow, 1.0f) : 1.0f;
	switch (FieldActivity(field))
	{
	case 2:
		return field.growth >= type.ageRecolt ? room : 0.0f;
	case 1:
		return (1.0f - sown) * room * room * room;
	default:
		return 0.0f;
	}
}

// GUtils::GetDistanceModifier: 1 near, a half at half `reach`, nothing past it (its sigmoid table, from runblack.exe).
float DistanceModifier(float distance, float reach)
{
	static constexpr std::array<float, 41> k_Sigmoid {0.0f,   0.0f,   0.0f,   0.0f,   0.0f,   0.0f,   0.0f,   0.0f,   0.0f,   0.0f,   0.0f,
	                                                   0.0f,   0.0f,   0.001f, 0.002f, 0.006f, 0.016f, 0.044f, 0.114f, 0.264f, 0.5f,   0.736f,
	                                                   0.886f, 0.956f, 0.984f, 0.994f, 0.998f, 0.999f, 1.0f,   1.0f,   1.0f,   1.0f,   1.0f,
	                                                   1.0f,   1.0f,   1.0f,   1.0f,   1.0f,   1.0f,   1.0f,   1.0f};
	const float near = 1.0f - std::min(distance, reach) / reach;
	const float v = std::clamp(std::clamp(near, -1.0f, 1.0f) - 0.5f, -1.0f, 1.0f);
	return k_Sigmoid.at(std::min(static_cast<size_t>((v + 1.0f) * 20.5f), size_t {40}));
}

// A spot in the field to work (runblack.exe's 0x528970: its middle, a few metres either way).
glm::vec3 FieldSpot(const glm::vec3& middle)
{
	auto& rng = Locator::rng::value();
	auto at = middle + glm::vec3(5.0f - rng.NextValue(0.0f, 10.0f), 0.0f, 5.0f - rng.NextValue(0.0f, 10.0f));
	at.y = Locator::terrainSystem::value().GetHeightAt(glm::vec2(at.x, at.z));
	return at;
}

// The town's village store itself (for its food pile).
entt::entity TownStoreEntity(uint32_t town)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = static_cast<entt::entity>(town);
	if (!registry.Valid(entity) || !registry.AllOf<Transform>(entity))
	{
		return entt::null;
	}
	const auto centre = registry.Get<Transform>(entity).position;
	entt::entity store = entt::null;
	float best = 300.0f;
	registry.Each<const openblack::ecs::components::StoragePit, const Transform>(
	    [&](entt::entity pit, const openblack::ecs::components::StoragePit&, const Transform& where) {
		    const float d = glm::distance(where.position, centre);
		    if (d < best)
		    {
			    best = d;
			    store = pit;
		    }
	    });
	return store;
}

void ReleaseFarmer(uint32_t id)
{
	ReleaseForester(id); // the same: its animation and what it carried gone, back to its own life
}

void UpdateFarmers(float seconds)
{
	using namespace openblack::ecs::components;
	auto& registry = Locator::entitiesRegistry::value();
	for (auto it = g_farmers.begin(); it != g_farmers.end();)
	{
		const auto id = it->first;
		auto& farmer = it->second;
		const auto entity = static_cast<entt::entity>(id);
		auto* villager = registry.Valid(entity) ? registry.TryGet<Villager>(entity) : nullptr;
		const auto fieldEntity = static_cast<entt::entity>(farmer.field);
		auto* field = registry.Valid(fieldEntity) ? registry.TryGet<Field>(fieldEntity) : nullptr;
		if (villager == nullptr || id == g_heldObject || g_flying.contains(id) || g_walks.contains(id) || g_followers.contains(id) ||
		    g_scriptMoves.contains(id) || g_builders.contains(id) || (field == nullptr && farmer.stage != Farmer::Stage::ToStore))
		{
			ReleaseFarmer(id);
			it = g_farmers.erase(it);
			continue;
		}
		auto& transform = registry.Get<Transform>(entity);
		const auto& info = VillagerInfoOf(*villager);
		bool done = false;
		auto work = [&](AnimId animation) {
			// One play of the work, then on (PlayAnimThenSetState).
			const auto [rate, duration] = AnimationRate(static_cast<uint32_t>(animation));
			farmer.timer = rate > 0.0f ? duration / rate : 1.0f;
			farmer.working = true;
			g_scriptAnimations[id] = {static_cast<uint32_t>(animation), 1.0f};
		};
		switch (farmer.stage)
		{
		case Farmer::Stage::ToField:
			SetBuilderAnimation(id, std::nullopt);
			if (WalkForester(entity, transform, farmer.spot, farmer.speed, seconds))
			{
				farmer.stage = farmer.activity == 1 ? Farmer::Stage::Sowing : Farmer::Stage::Harvesting;
				FaceTowards(transform, registry.Get<Transform>(fieldEntity).position);
				work(farmer.activity == 1 ? AnimId::PFarmerSowingSeeds : AnimId::PFarmerHarvesting);
			}
			break;
		case Farmer::Stage::Sowing:
		{
			farmer.timer -= seconds;
			if (farmer.timer > 0.0f && g_scriptAnimations.contains(id))
			{
				break;
			}
			// Field::PlantCrop; then the next spot while crops are still wanted (GetPlantCropPos).
			const auto& type = Locator::infoConstants::value().fieldType.at(field->fieldType);
			if (static_cast<float>(field->crops) < type.timesToSow)
			{
				++field->crops;
				if (static_cast<float>(field->crops) >= type.timesToSow)
				{
					field->growth = 0.0f;
					SPDLOG_LOGGER_INFO(spdlog::get("game"), "Field {} sown ({} crops) by villager {}", farmer.field, field->crops, id);
				}
			}
			if (static_cast<float>(field->crops) < type.timesToSow)
			{
				farmer.spot = FieldSpot(registry.Get<Transform>(fieldEntity).position);
				farmer.stage = Farmer::Stage::ToField;
			}
			else
			{
				done = true;
			}
			break;
		}
		case Farmer::Stage::Harvesting:
		{
			farmer.timer -= seconds;
			if (farmer.timer > 0.0f && g_scriptAnimations.contains(id))
			{
				break;
			}
			// Field::RemoveFood up to what it can still carry (GetFoodCapacity).
			const float room = static_cast<float>(info.maxFoodCarried) - farmer.carrying;
			const float take = std::min(room, field->grain);
			if (take > 0.0f)
			{
				AddToPile(fieldEntity, -static_cast<int32_t>(take));
				farmer.carrying += take;
			}
			if (farmer.carrying >= static_cast<float>(info.maxFoodCarried) || field->grain <= 0.0f || take <= 0.0f)
			{
				farmer.stage = farmer.carrying > 0.0f ? Farmer::Stage::ToStore : Farmer::Stage::ToField;
				done = farmer.carrying <= 0.0f;
			}
			else
			{
				farmer.spot = FieldSpot(registry.Get<Transform>(fieldEntity).position);
				farmer.stage = Farmer::Stage::ToField;
			}
			break;
		}
		case Farmer::Stage::ToStore:
		{
			SetBuilderAnimation(id, std::nullopt);
			const auto store = TownStoreEntity(farmer.town);
			if (store == entt::null)
			{
				done = true;
				break;
			}
			const auto at = registry.Get<Transform>(store).position;
			if (WalkForester(entity, transform, at, farmer.speed, seconds))
			{
				ChangeContainerResource(static_cast<uint32_t>(store), static_cast<int32_t>(ResourceType::Food), farmer.carrying);
				SPDLOG_LOGGER_INFO(spdlog::get("game"), "Villager {} brings {:.0f} grain to town {}'s store", id, farmer.carrying, farmer.town);
				farmer.carrying = 0.0f;
				done = true;
			}
			break;
		}
		}
		CarryPlank(id, farmer.stage == Farmer::Stage::ToStore && farmer.carrying > 0.0f, MeshId::ObjectWheatInHand);
		if (done)
		{
			ReleaseFarmer(id);
			it = g_farmers.erase(it);
			continue;
		}
		++it;
	}
}
} // namespace

float TakeFromTownStore(uint32_t town, bool wood, float amount)
{
	const auto store = TownStoreEntity(town);
	if (store == entt::null || amount <= 0.0f)
	{
		return 0.0f;
	}
	return -ChangeContainerResource(static_cast<uint32_t>(store), static_cast<int32_t>(wood ? ResourceType::Wood : ResourceType::Food), -amount);
}

void VillagerDiesOf(uint32_t villager, const char* why)
{
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = static_cast<entt::entity>(villager);
	if (!registry.Valid(entity) || g_fallenDead.contains(villager))
	{
		return;
	}
	if (auto* person = registry.TryGet<openblack::ecs::components::Villager>(entity))
	{
		person->life = 0.0f;
	}
	g_scriptAnimations[villager] = {static_cast<uint32_t>(AnimId::PDying), std::numeric_limits<float>::max()};
	g_fallenDead.insert(villager);
	HoldForScript(entity, registry.Get<Transform>(entity).position); // the dead stay where they fell
	SPDLOG_LOGGER_INFO(spdlog::get("game"), "Villager {} died: {}", villager, why);
}

bool StartFarmer(uint32_t villager, float metresPerSecond)
{
	using namespace openblack::ecs::components;
	auto& registry = Locator::entitiesRegistry::value();
	const auto entity = static_cast<entt::entity>(villager);
	const auto* person = registry.Valid(entity) ? registry.TryGet<Villager>(entity) : nullptr;
	if (person == nullptr || g_farmers.contains(villager) || NotFreeToBuild(entity, *person) != nullptr || person->town == entt::null ||
	    !registry.Valid(person->town) || !registry.AllOf<Town>(person->town))
	{
		return false;
	}
	const auto& at = registry.Get<Transform>(entity).position;
	const auto townId = registry.Get<Town>(person->town).id;
	// Town::FindBestField: its fields, by how much each wants farming and how near (within 300).
	uint32_t best = 0;
	float bestScore = 0.0f;
	registry.Each<const Field, const Transform>([&](entt::entity entity, const Field& field, const Transform& where) {
		if (static_cast<uint32_t>(field.town) != townId)
		{
			return;
		}
		const float score = DesireToBeFarmed(static_cast<uint32_t>(entity), field) * DistanceModifier(glm::distance(where.position, at), 300.0f);
		if (score > bestScore)
		{
			bestScore = score;
			best = static_cast<uint32_t>(entity);
		}
	});
	if (best == 0)
	{
		return false;
	}
	Farmer farmer;
	farmer.field = best;
	farmer.town = static_cast<uint32_t>(person->town);
	farmer.speed = metresPerSecond;
	farmer.activity = FieldActivity(registry.Get<Field>(static_cast<entt::entity>(best)));
	farmer.spot = FieldSpot(registry.Get<Transform>(static_cast<entt::entity>(best)).position);
	HoldForScript(entity, at);
	g_farmers[villager] = farmer;
	SPDLOG_LOGGER_INFO(spdlog::get("game"), "Villager {} goes to {} field {} for town {}", villager, farmer.activity == 1 ? "sow" : "harvest",
	                   best, farmer.town);
	return true;
}

uint32_t FarmerCount(uint32_t town)
{
	return static_cast<uint32_t>(std::count_if(g_farmers.begin(), g_farmers.end(), [town](const auto& f) { return f.second.town == town; }));
}

namespace
{
void UpdateVortices(float seconds)
{
	using namespace openblack::ecs::components;
	auto& registry = Locator::entitiesRegistry::value();
	constexpr float k_Reach = 12.0f;
	constexpr float k_AttractReach = 10.0f; // what stands this near is drawn in
	for (auto it = g_vortices.begin(); it != g_vortices.end();)
	{
		const auto vortex = static_cast<entt::entity>(*it);
		if (!registry.Valid(vortex))
		{
			it = g_vortices.erase(it);
			continue;
		}
		auto& at = registry.Get<Transform>(vortex);
		at.rotation = glm::mat3(glm::eulerAngleY(seconds * 1.5f)) * at.rotation; // turning
		// Anything the hand could pick up that stands or grows near it is drawn in (the original's UR_VortexAttract,
		// Mike 2026-10-17), and anything let go of over it.
		const bool dropped = std::chrono::steady_clock::now() - g_lastDropTime < std::chrono::seconds(5) &&
		                     glm::distance(glm::vec2(g_lastDropAt.x, g_lastDropAt.z), glm::vec2(at.position.x, at.position.z)) < k_Reach;
		registry.Each<const Transform>([&](entt::entity thing, const Transform& where) {
			const auto id = static_cast<uint32_t>(thing);
			if (thing == vortex || g_vortexSpirals.contains(id) || id == g_heldObject || g_vortices.contains(id) ||
			    !registry.AnyOf<Mobile, Pot, Tree, Villager, Animal>(thing) || registry.AnyOf<Creature, Hand>(thing))
			{
				return;
			}
			const float distance = glm::distance(glm::vec2(where.position.x, where.position.z), glm::vec2(at.position.x, at.position.z));
			const bool letGo = dropped && glm::distance(glm::vec2(where.position.x, where.position.z), glm::vec2(g_lastDropAt.x, g_lastDropAt.z)) < 4.0f;
			if (!(letGo || (distance < k_AttractReach && CanPickUp(id))))
			{
				return;
			}
			const auto offset = glm::vec2(where.position.x - at.position.x, where.position.z - at.position.z);
			// Each tumbles about an axis of its own (the original's AppearanceRuleTumble).
			const float a = static_cast<float>(id % 360) * 0.0174533f;
			g_vortexSpirals[id] = {*it, std::atan2(offset.y, offset.x), std::max(glm::length(offset), 2.0f),
			                       where.position.y - at.position.y, 0.0f, glm::normalize(glm::vec3(std::cos(a), 0.6f, std::sin(a)))};
			g_walks.erase(id);
			g_scriptMoves.erase(id);
			SPDLOG_LOGGER_INFO(spdlog::get("game"), "Vortex {} draws in {} from {:.1f} away", *it, id, distance);
		});
		++it;
	}
	// Spiralling in: round and down and smaller, then gone - sent on.
	for (auto it = g_vortexSpirals.begin(); it != g_vortexSpirals.end();)
	{
		const auto thing = static_cast<entt::entity>(it->first);
		const auto vortex = static_cast<entt::entity>(it->second.vortex);
		if (!registry.Valid(thing) || !registry.Valid(vortex))
		{
			it = g_vortexSpirals.erase(it);
			continue;
		}
		// Always turning, faster as it closes in; up first, lifted off the land and over the vortex, then down into it.
		constexpr float k_SpiralSeconds = 3.5f;
		constexpr float k_Rise = 8.0f;
		constexpr float k_Sink = 6.0f;
		auto& spiral = it->second;
		spiral.time += seconds;
		const float u = std::min(spiral.time / k_SpiralSeconds, 1.0f);
		const float left = 1.0f - u;
		const float radius = spiral.radius * std::max(left, 0.05f);
		spiral.angle += seconds * std::min(2.0f * spiral.radius / std::max(radius, 0.5f), 18.0f); // as a skater pulling in
		const float height = spiral.height * left + k_Rise * std::sin(glm::pi<float>() * std::min(u * 1.4f, 1.0f)) - k_Sink * u * u;
		const auto centre = registry.Get<Transform>(vortex).position;
		auto& where = registry.Get<Transform>(thing);
		where.position = centre + glm::vec3(std::cos(spiral.angle) * radius, height, std::sin(spiral.angle) * radius);
		constexpr float k_TumbleSpeed = 3.0f; // radians a second: the original's MaxTumbleSpeed
		where.rotation = glm::mat3(glm::rotate(glm::mat4(1.0f), k_TumbleSpeed * seconds, spiral.tumbleAxis)) * where.rotation;
		where.scale *= 1.0f - 0.25f * seconds / k_SpiralSeconds * (u > 0.7f ? 3.0f : 0.0f);
		MoveBody(thing, where.position);
		if (left > 0.0f)
		{
			++it;
			continue;
		}
		std::string what = "object";
		if (const auto* mesh = registry.TryGet<Mesh>(thing))
		{
			what = fmt::format("mesh {}", mesh->id);
		}
		if (registry.AllOf<Villager>(thing))
		{
			what = "villager";
		}
		else if (const auto* pot = registry.TryGet<Pot>(thing))
		{
			what = fmt::format("{} of {}", pot->amount,
			                   Locator::infoConstants::value().pot.at(static_cast<size_t>(pot->type)).resourceType == ResourceType::Wood
			                       ? "wood"
			                       : "food");
		}
		g_vortexCargo.push_back(what);
		SPDLOG_LOGGER_INFO(spdlog::get("game"), "Into the portal: {} ({}, {} sent on)", it->first, what, g_vortexCargo.size());
		ForgetObject(it->first);
		g_flying.erase(it->first);
		registry.Destroy(thing);
		it = g_vortexSpirals.erase(it);
	}
	// The dead fade away.
	for (auto it = g_dying.begin(); it != g_dying.end();)
	{
		const auto creature = static_cast<entt::entity>(it->first);
		if (!registry.Valid(creature))
		{
			it = g_dying.erase(it);
			continue;
		}
		static std::unordered_map<uint32_t, glm::vec3> s_scale;
		auto& transform = registry.Get<Transform>(creature);
		const auto base = s_scale.try_emplace(it->first, transform.scale).first->second;
		it->second += seconds;
		const float left = std::max(0.0f, 1.0f - it->second / 3.0f);
		transform.scale = base * left;
		if (left > 0.0f)
		{
			++it;
			continue;
		}
		SPDLOG_LOGGER_INFO(spdlog::get("game"), "Creature {} is gone (dead for good)", it->first);
		g_fighters.erase(it->first);
		g_creatureOrders.erase(it->first);
		ForgetObject(it->first);
		registry.Destroy(creature);
		s_scale.erase(it->first);
		it = g_dying.erase(it);
	}
}
} // namespace

namespace
{
// One game turn of a dance's choreography: the key frames due (each once; the original applies one for every turn of
// its half-second), then each group's changes under way (DanceGroup::Process), then the clock, which loops.
void DanceTurn(Dance& dance)
{
	using namespace dance;
	const auto& keyFrames = dance.choreography->keyFrames;
	const auto bucket = [](float t) { return static_cast<int>(t) / k_TurnsPerKeyBucket; };
	while (dance.nextKeyFrame < keyFrames.size() && bucket(keyFrames[dance.nextKeyFrame].time) <= bucket(dance.time))
	{
		for (const auto& action : keyFrames[dance.nextKeyFrame].actions)
		{
			for (const auto index : action.groups)
			{
				if (index >= 64)
				{
					continue;
				}
				if (index >= dance.groups.size())
				{
					dance.groups.resize(index + 1);
				}
				auto& group = dance.groups[index];
				// UpdateGroupBasedOnAction
				switch (action.type)
				{
				case Action::SetShape:
					group.shape = action.value[0];
					break;
				case Action::SetPos:
					group.pos = glm::vec2(action.Float(0), action.Float(1));
					break;
				case Action::SetSize:
					group.size = action.Float(0);
					break;
				case Action::SetAngle:
					group.angle = action.Float(0);
					break;
				case Action::SetPath:
					group.isPath = action.value[0] != 0;
					break;
				case Action::Proportion:
					if (!group.isPath)
					{
						group.fixed = action.value[0] != 0;
						group.proportion = action.value[1];
					}
					break;
				case Action::SetAction:
				{
					// UpdateGroupBasedOnDancerAction: the change per turn that gets there by the action's end.
					const auto& to = action.action;
					group.action = to;
					group.actionSerial++;
					const int remaining = to.endTime - static_cast<int>(dance.time);
					const float rate = remaining > 0 ? k_Speed / static_cast<float>(remaining) : 0.0f;
					switch (to.kind)
					{
					case Kind::RotateDancers:
						group.changeDancerRotation = rate * k_DancerFullTurn;
						break;
					case Kind::RotateShape:
						group.changeShapeRotation = (to.param2 - group.angle) * rate;
						break;
					case Kind::ChangeSize:
						group.changeSize = (to.Param1Float() - group.size) * rate;
						break;
					case Kind::MoveToPos:
						group.changeMove = glm::vec2(to.Param1Float(), to.param2) * rate;
						break;
					case Kind::FaceIn:
						group.facingOut = false;
						break;
					case Kind::FaceOut:
						group.facingOut = true;
						break;
					default:
						break;
					}
				}
				break;
				default:
					break; // paths, cameras, lights, signals: not in the port
				}
			}
		}
		dance.nextKeyFrame++;
	}
	for (auto& group : dance.groups)
	{
		const bool ended = static_cast<int>(dance.time) >= group.action.endTime;
		switch (group.action.kind)
		{
		case Kind::ChangeSize:
			group.size += group.changeSize;
			break;
		case Kind::MoveToPos:
			group.pos += group.changeMove;
			break;
		case Kind::RotateDancers:
			group.dancerAngle = std::fmod(group.dancerAngle + (group.action.param1 != 0 ? -1.0f : 1.0f) * group.changeDancerRotation + k_DancerFullTurn,
			                              k_DancerFullTurn);
			break;
		case Kind::RotateShape:
			group.angle = std::fmod(group.angle + group.changeShapeRotation + k_ShapeFullTurn, k_ShapeFullTurn);
			break;
		default:
			break;
		}
		if (ended && (group.action.kind == Kind::ChangeSize || group.action.kind == Kind::MoveToPos))
		{
			group.action.kind = Kind::StandStill;
			group.changeSize = 0.0f;
			group.changeMove = glm::vec2(0.0f);
		}
	}
	dance.time += 1.0f;
	if (dance.time >= 600.0f * static_cast<float>(dance.choreography->lengthMinutes)) // Length minutes of turns
	{
		dance.time = 0.0f;
		dance.nextKeyFrame = 0;
	}
}

// Where in its shape a dancer stands (GroupBehaviour::CalculateDancePosition): spread evenly round it from the
// group's dancer angle, Size metres across, moved by the group's position.
glm::vec3 DancePlace(const Dance& dance, const dance::Group& group, size_t index)
{
	using namespace dance;
	const size_t count = group.fixed ? std::max<size_t>(group.proportion, group.dancers.size()) : group.dancers.size();
	const float along = std::fmod(static_cast<float>(index) * k_DancerFullTurn / static_cast<float>(std::max<size_t>(count, 1)) + group.dancerAngle,
	                              k_DancerFullTurn) / k_DancerFullTurn; // 0..1 round the shape
	glm::vec2 point; // the shape's own, half a unit across its middle
	switch (group.shape)
	{
	case 5: // line (V)
		point = glm::vec2(0.0f, along - 0.5f);
		break;
	case 6: // line (H)
		point = glm::vec2(along - 0.5f, 0.0f);
		break;
	case 7: // point
		point = glm::vec2(0.0f);
		break;
	default: // the circle - and, not yet in the port, the other shapes
		point = glm::vec2(std::cos(along * glm::two_pi<float>()), std::sin(along * glm::two_pi<float>())) * 0.5f;
		break;
	}
	if (group.angle != 0.0f)
	{
		const float turn = group.angle / k_ShapeFullTurn * glm::two_pi<float>();
		point = glm::vec2(point.x * std::cos(turn) - point.y * std::sin(turn), point.x * std::sin(turn) + point.y * std::cos(turn));
	}
	const auto offset = point * group.size + group.pos;
	return dance.centre + glm::vec3(offset.x, 0.0f, offset.y);
}

// What a dancer plays for its group's movement (Villager::DanceAnimation); nothing when it walks (the villager system's
// own walk shows).
std::optional<AnimId> DanceAnimation(const dance::DancerAction& action, bool woman)
{
	using dance::Kind;
	// PLAY_ANIM's animations by number, the man's and the woman's (runblack.exe 1.20's table at 0xbe6970).
	static constexpr std::array<std::pair<AnimId, AnimId>, 45> k_Played {{
	    {AnimId::PMDanceA, AnimId::PFDanceA}, {AnimId::PMDanceB, AnimId::PFDanceB}, {AnimId::PMDanceC, AnimId::PFDanceC},
	    {AnimId::PDancePissTake, AnimId::PDancePissTake}, {AnimId::PDanceTogetherArmlink, AnimId::PDanceTogetherArmlink},
	    {AnimId::PDanceTogetherCircling, AnimId::PDanceTogetherCircling},
	    {AnimId::PDanceTogetherCirclingPartner, AnimId::PDanceTogetherCirclingPartner}, {AnimId::PDanceX, AnimId::PDanceX},
	    {AnimId::PDanceXPartner, AnimId::PDanceXPartner}, {AnimId::PGossipMan, AnimId::PGossipWoman_1},
	    {AnimId::PFrightDuck, AnimId::PFrightDuck}, {AnimId::PFrightJump, AnimId::PFrightJump}, {AnimId::PScaredStiff, AnimId::PScaredStiff},
	    {AnimId::PInspectObject_1, AnimId::PInspectObject_1}, {AnimId::PInspectObject_2, AnimId::PInspectObject_2},
	    {AnimId::PInspectObject_3, AnimId::PInspectObject_3}, {AnimId::PIntoMourning, AnimId::PIntoMourning},
	    {AnimId::PMourning, AnimId::PMourning}, {AnimId::POutOfMourning, AnimId::POutOfMourning},
	    {AnimId::PIntoPointing, AnimId::PIntoPointing}, {AnimId::POutOfPointing, AnimId::POutOfPointing},
	    {AnimId::PIntoPray, AnimId::PIntoPray}, {AnimId::PPray, AnimId::PPray}, {AnimId::POutOfPray, AnimId::POutOfPray},
	    {AnimId::PPuzzled, AnimId::PPuzzled}, {AnimId::PSittingDown1Into, AnimId::PSittingDown1Into},
	    {AnimId::PSittingDown1Sitting, AnimId::PSittingDown1Sitting}, {AnimId::PSittingDown1OutOf, AnimId::PSittingDown1OutOf},
	    {AnimId::PSittingDown2Into, AnimId::PSittingDown2Into}, {AnimId::PSittingDown2Sitting, AnimId::PSittingDown2Sitting},
	    {AnimId::PSittingDown2OutOf, AnimId::PSittingDown2OutOf}, {AnimId::PSittingDown2Into, AnimId::PSittingDown2Into},
	    {AnimId::PSittingSwingingLegs, AnimId::PSittingSwingingLegs}, {AnimId::PSittingDown2OutOf, AnimId::PSittingDown2OutOf},
	    {AnimId::PStandDespair_1, AnimId::PStandDespair_1}, {AnimId::PStandDespair_2, AnimId::PStandDespair_2},
	    {AnimId::PStandDespair_3, AnimId::PStandDespair_3}, {AnimId::PYawn, AnimId::PYawn}, {AnimId::PYawn2, AnimId::PYawn2},
	    {AnimId::POverworked1, AnimId::POverworked1}, {AnimId::POverworked2, AnimId::POverworked2},
	    {AnimId::PCoupleKissMan, AnimId::PCoupleKissWoman}, {AnimId::PCrowdImpressed_1, AnimId::PCrowdImpressed_1},
	    {AnimId::PCrowdWon, AnimId::PCrowdWon}, {AnimId::PCrowdWon_2, AnimId::PCrowdWon_2},
	}};
	const auto pick = [woman](AnimId man, AnimId womans) { return woman ? womans : man; };
	switch (action.kind)
	{
	case Kind::RotateDancers:
	case Kind::ChangeSize:
	case Kind::MoveToPos:
		return std::nullopt; // walking (MoveToPosAnimation)
	case Kind::RandomDance:
	{
		const auto which = static_cast<int>(std::rand() % 3);
		return static_cast<AnimId>(static_cast<int>(pick(AnimId::PMDanceA, AnimId::PFDanceA)) + which);
	}
	case Kind::Pray:
	case Kind::Sex:
	case Kind::WorshipCreature:
		return AnimId::PPray;
	case Kind::Funky:
	case Kind::RotateShape:
		return pick(AnimId::PDanceX, AnimId::PDanceXPartner);
	case Kind::PlayAnim:
		if (action.param1 < k_Played.size())
		{
			const auto& [man, womans] = k_Played.at(action.param1);
			return pick(man, womans);
		}
		return AnimId::PStand;
	case Kind::Sad:
		return AnimId::PCrowdLost;
	case Kind::Happy:
		return AnimId::PCrowdWon;
	case Kind::LookingForSomething:
		return AnimId::PLookingForSomething;
	case Kind::Mourning:
		return AnimId::PMourning;
	case Kind::Talk:
		return AnimId::PTalkToNeighbour;
	case Kind::TalkAndPoint:
		return AnimId::PTalkingAndPointing;
	case Kind::DanceX:
		return AnimId::PDanceX;
	case Kind::DanceXPartner:
		return AnimId::PDanceXPartner;
	default:
		if (action.kind >= Kind::FemaleA && action.kind <= Kind::FemaleC)
		{
			return static_cast<AnimId>(static_cast<int>(AnimId::PFDanceA) + static_cast<int>(action.kind) - static_cast<int>(Kind::FemaleA));
		}
		if (action.kind >= Kind::MaleA && action.kind <= Kind::MaleC)
		{
			return static_cast<AnimId>(static_cast<int>(AnimId::PMDanceA) + static_cast<int>(action.kind) - static_cast<int>(Kind::MaleA));
		}
		return AnimId::PStand; // standing still, facing in or out, looking...
	}
}

void UpdateDances(float seconds)
{
	using openblack::ecs::components::Villager;
	auto& registry = Locator::entitiesRegistry::value();
	for (auto it = g_dances.begin(); it != g_dances.end();)
	{
		auto& dance = it->second;
		const auto flock = g_flocks.find(it->first);
		dance.secondsLeft -= seconds;
		if (dance.secondsLeft <= 0.0f || flock == g_flocks.end())
		{
			if (flock != g_flocks.end())
			{
				for (const auto member : flock->second.members)
				{
					if (dance.dancerSerial.contains(member))
					{
						g_scriptAnimations.erase(member);
					}
					if (const auto entity = static_cast<entt::entity>(member); registry.Valid(entity))
					{
						registry.Remove<ScriptMove>(entity); // back to the villager AI
					}
				}
				SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "Dance {} over: {} dancers go back to their lives", it->first,
				                   flock->second.members.size());
				g_flocks.erase(flock);
			}
			it = g_dances.erase(it);
			continue;
		}
		if (dance.choreography != nullptr)
		{
			dance.turnClock += seconds;
			while (dance.turnClock >= dance::k_TurnSeconds)
			{
				dance.turnClock -= dance::k_TurnSeconds;
				DanceTurn(dance);
			}
		}
		else if (!dance.groups.empty()) // no file: one ring, dancing as it likes
		{
			dance.groups[0].size = std::max(8.0f, static_cast<float>(flock->second.members.size()) * 1.4f);
			dance.groups[0].action.kind = dance::Kind::RandomDance;
		}

		// The dancers into the groups (GroupBehaviour's proportions): a fixed group takes its number, the others share
		// the rest as their proportions say, in the order the dancers joined.
		for (auto& group : dance.groups)
		{
			group.dancers.clear();
		}
		std::vector<uint32_t> dancers;
		for (const auto member : flock->second.members)
		{
			const auto entity = static_cast<entt::entity>(member);
			if (registry.Valid(entity) && registry.AllOf<Transform>(entity) && member != g_heldObject && !g_flying.contains(member))
			{
				dancers.push_back(member);
			}
		}
		size_t next = 0;
		uint32_t shares = 0;
		for (auto& group : dance.groups)
		{
			if (group.fixed)
			{
				for (uint32_t n = 0; n < group.proportion && next < dancers.size(); n++)
				{
					group.dancers.push_back(dancers[next++]);
				}
			}
			else
			{
				shares += group.proportion;
			}
		}
		const size_t rest = dancers.size() - next;
		size_t given = 0;
		for (size_t g = 0; g < dance.groups.size(); g++)
		{
			auto& group = dance.groups[g];
			if (group.fixed || shares == 0)
			{
				continue;
			}
			size_t take = rest * group.proportion / shares;
			const bool lastShared = std::none_of(dance.groups.begin() + static_cast<long>(g) + 1, dance.groups.end(),
			                                     [](const auto& other) { return !other.fixed && other.proportion > 0; });
			if (lastShared)
			{
				take = rest - given; // the remainder of the rounding
			}
			for (size_t n = 0; n < take && next < dancers.size(); n++)
			{
				group.dancers.push_back(dancers[next++]);
			}
			given += take;
		}

		for (const auto& group : dance.groups)
		{
			const auto groupCentre = dance.centre + glm::vec3(group.pos.x, 0.0f, group.pos.y);
			for (size_t i = 0; i < group.dancers.size(); i++)
			{
				const auto member = group.dancers[i];
				const auto entity = static_cast<entt::entity>(member);
				auto& transform = registry.Get<Transform>(entity);
				// The dance has them now: not a walk or a flock's gathering point from before.
				g_walks.erase(member);
				g_followers.erase(member);
				g_scriptMoves.erase(member);
				const auto place = DancePlace(dance, group, i);
				const auto toPlace = glm::vec2(place.x - transform.position.x, place.z - transform.position.z);
				const float distance = glm::length(toPlace);
				const auto* villager = registry.TryGet<Villager>(entity);
				const bool woman = villager != nullptr && villager->sex == Villager::Sex::FEMALE;
				const auto animation = DanceAnimation(group.action, woman);
				auto playing = g_scriptAnimations.find(member);
				if (distance > 0.3f || !animation)
				{
					// Walking to its place, or the group's movement walks it (a ring turning or growing): no dance on
					// the way; faster when far behind, so a dancer keeps up with its place.
					if (playing != g_scriptAnimations.end() && dance.dancerSerial.contains(member))
					{
						g_scriptAnimations.erase(playing);
					}
					dance.dancerSerial.erase(member);
					HoldForScript(entity, transform.position);
					if (distance > 0.01f)
					{
						const float speed = FullSpeed(member) * 0.4f * std::clamp(distance, 1.0f, 3.0f);
						const float step = std::min(distance, speed * seconds);
						PlaceMover(entity, transform, transform.position + glm::vec3(toPlace.x, 0.0f, toPlace.y) / distance * step, place);
					}
					continue;
				}
				HoldForScript(entity, place);
				// Facing the group's middle, or away from it after FACE_OUT.
				const auto awayFrom = group.facingOut ? transform.position * 2.0f - groupCentre : groupCentre;
				FaceTowards(transform, awayFrom);
				const auto serial = dance.dancerSerial.find(member);
				if (playing == g_scriptAnimations.end() || serial == dance.dancerSerial.end() || serial->second != group.actionSerial)
				{
					g_scriptAnimations[member] = {static_cast<uint32_t>(*animation), 1.0f}; // again each time it has played
					dance.dancerSerial[member] = group.actionSerial;
				}
			}
		}
		++it;
	}
}
} // namespace

namespace
{
void UpdateIntro(float seconds);      // below, with PLAY_JC_SPECIAL
void UpdateSharkWakes(float seconds); // and the sharks' wakes
void UpdateSun(float seconds);        // and the sun
}

void UpdateScriptFrame(std::chrono::microseconds dt)
{
	++g_scriptFrame;
	g_scriptClock += std::chrono::duration_cast<std::chrono::steady_clock::duration>(dt);
	const float seconds = std::min(std::chrono::duration<float>(dt).count(), 0.1f * PlaySpeed()); // a long frame (a load) is cut short, at any play speed
	auto& registry = Locator::entitiesRegistry::value();
	// The advisor whose line is being said comes nearer while it is.
	{
		const bool saying = g_textVoice != entt::null && Locator::audio::value().EmitterExists(g_textVoice);
		const int speaker = saying ? openblack::subtitles::SpeakerOf(g_lastTextId) : 0;
		helpspirits::SetTalking(helpspirits::Spirit::Good, speaker == 1);
		helpspirits::SetTalking(helpspirits::Spirit::Evil, speaker == 2);
	}
	UpdateCameraFollow();
	UpdateIntro(seconds);
	UpdateSharkWakes(seconds);
	UpdateSun(seconds);
	openblack::psys::Update(seconds); // the particle systems' game turns
	if (g_inHandEffect != 0 && (g_heldObject == 0 || !g_oneShotSpells.contains(g_heldObject)))
	{
		EndInHandEffect(); // the miracle is no longer in the hand
	}
	for (auto it = g_pours.begin(); it != g_pours.end();)
	{
		const float pourTime = openblack::psys::PourTime(*it);
		if (!openblack::psys::Exists(*it) || (pourTime >= 0.0f && openblack::psys::Age(*it) >= pourTime))
		{
			openblack::psys::CloseDown(*it);
			it = g_pours.erase(it);
			continue;
		}
		++it;
	}
	// Test hook: OPENBLACK_TEST_EFFECT=<particle type>[,<type>...] starts each, cast from a point 20 in front of the
	// camera, every 6 seconds (closed down after 3), saying how many atoms they have.
	if (static const char* s_test = std::getenv("OPENBLACK_TEST_EFFECT"); s_test != nullptr)
	{
		static float s_since = 5.0f;
		static std::vector<openblack::psys::Handle> s_running;
		s_since += seconds;
		const auto& camera = Locator::camera::value();
		const auto at = camera.GetOrigin() + camera.GetForward() * 20.0f + camera.GetUp() * 4.0f;
		if (s_since > 6.0f)
		{
			s_since = 0.0f;
			s_running.clear();
			std::string types = s_test;
			for (size_t from = 0; from < types.size();)
			{
				const size_t comma = types.find(',', from);
				openblack::psys::Start start;
				start.type = static_cast<openblack::psys::Type>(std::stoi(types.substr(from, comma - from)));
				start.origin = at;
				start.humanCasting = false;
				start.onEvent = [](const openblack::psys::Event& event) {
					SPDLOG_LOGGER_DEBUG(spdlog::get("game"), "Effect event {} at ({:.1f}, {:.1f}, {:.1f})", static_cast<int>(event.type),
					                    event.position.x, event.position.y, event.position.z);
					return true;
				};
				s_running.push_back(openblack::psys::Create(start));
				from = comma == std::string::npos ? types.size() : comma + 1;
			}
		}
		static float s_logged = 0.0f;
		s_logged += seconds;
		if (s_since > 3.0f)
		{
			for (const auto system : s_running)
			{
				openblack::psys::CloseDown(system);
			}
		}
		if (s_logged > 0.5f)
		{
			s_logged = 0.0f;
			int systems = 0;
			int atoms = 0;
			openblack::psys::Stats(systems, atoms);
			SPDLOG_LOGGER_INFO(spdlog::get("game"), "Effects: {} systems, {} atoms (t {:.1f})", systems, atoms, s_since);
		}
	}
	UpdateBuilders(seconds);
	UpdateForesters(seconds);
	UpdateFarmers(seconds);
	// Opened chests: the lid opening over coffre.anm's length, open a while, then gone.
	for (auto it = g_rewards.begin(); it != g_rewards.end();)
	{
		auto& chest = it->second;
		const auto entity = static_cast<entt::entity>(it->first);
		if (!chest.opened)
		{
			++it;
			continue;
		}
		chest.openFor += seconds;
		if (!registry.Valid(entity) || chest.openFor > k_ChestOpenSeconds + k_ChestStaysOpenSeconds)
		{
			ForgetObject(it->first);
			if (registry.Valid(entity))
			{
				registry.Destroy(entity);
			}
			it = g_rewards.erase(it);
			continue;
		}
		if (auto* animated = registry.TryGet<openblack::ecs::components::Animated>(entity))
		{
			animated->localPose = ChestPose(chest.openFor / k_ChestOpenSeconds);
		}
		++it;
	}
	// Reward chests from the sky (rewardfromsky): down to the ground, then there to be opened.
	for (auto& [id, chest] : g_rewards)
	{
		if (chest.falling <= 0.0f || chest.opened)
		{
			continue;
		}
		const auto entity = static_cast<entt::entity>(id);
		if (!registry.Valid(entity) || !registry.AllOf<Transform>(entity))
		{
			chest.falling = 0.0f;
			continue;
		}
		const float drop = std::min(chest.falling, 20.0f * seconds);
		chest.falling -= drop;
		registry.Get<Transform>(entity).position.y -= drop;
		if (chest.falling <= 0.0f)
		{
			PlayTreeThud(registry.Get<Transform>(entity).position); // down it comes
		}
	}
	UpdateDispensers();
	UpdateVortices(seconds);
	UpdateDances(seconds);
	// The Ark: built up to what its wood allows, a little each second, as its sailors work.
	registry.Each<const openblack::ecs::components::Feature, openblack::ecs::components::Construction>(
	    [seconds](entt::entity, const openblack::ecs::components::Feature&, openblack::ecs::components::Construction& construction) {
		    construction.built = std::min(construction.woodStock, construction.built + 0.02f * seconds);
	    });
	// A town PopulateTown overfilled before towns counted their people: back to the 15 the script fills to.
	{
		static bool s_trimmed = false;
		if (!s_trimmed)
		{
			s_trimmed = true;
			constexpr size_t k_TownFill = 15;
			registry.Each<const openblack::ecs::components::Town, const Transform>(
			    [&](entt::entity town, const openblack::ecs::components::Town&, const Transform& townAt) {
				    std::vector<uint32_t> made;
				    for (const auto& [id, record] : g_scriptEntities)
				    {
					    const auto villager = static_cast<entt::entity>(id);
					    if (record.type == static_cast<int32_t>(ObjectType::Villager) && registry.Valid(villager) &&
					        registry.AllOf<Transform>(villager) &&
					        glm::distance(registry.Get<Transform>(villager).position, townAt.position) < 120.0f)
					    {
						    made.push_back(id);
					    }
				    }
				    if (made.size() <= k_TownFill * 2)
				    {
					    return;
				    }
				    for (size_t i = k_TownFill; i < made.size(); ++i)
				    {
					    ForgetObject(made[i]);
					    registry.Destroy(static_cast<entt::entity>(made[i]));
					    g_scriptEntities.erase(made[i]);
				    }
				    SPDLOG_LOGGER_INFO(spdlog::get("game"), "Town {}: {} script-made people, back to {}", static_cast<uint32_t>(town),
				                       made.size(), k_TownFill);
			    });
		}
	}
	UpdateFights(seconds);
	// A loose animal let down near a flock of its kind joins it, as the original's flocks gather their strays: Land 1's
	// lost flock counts the shepherd's flock (ID_SIZE) and only looks for sheep itself while it has one.
	{
		static float s_sinceFlockCheck = 0.0f;
		s_sinceFlockCheck += seconds;
		if (s_sinceFlockCheck >= 1.0f)
		{
			s_sinceFlockCheck = 0.0f;
			std::unordered_map<uint32_t, uint32_t> inFlock; // animal -> its flock
			for (const auto& [id, flock] : g_flocks)
			{
				for (const auto member : flock.members)
				{
					inFlock[member] = id;
				}
			}
			const auto& infos = Locator::infoConstants::value().animal;
			registry.Each<const openblack::ecs::components::Animal, const Transform>(
			    [&](entt::entity entity, const openblack::ecs::components::Animal& animal, const Transform& transform) {
				    const auto id = static_cast<uint32_t>(entity);
				    const auto index = static_cast<size_t>(animal.type);
				    const float info = index < infos.size() ? infos.at(index).flockDistance : 0.0f;
				    const float reach = info > 0.0f ? info : 20.0f;
				    // In a flock and with it, it stays; taken far from its own (the hand carried it off), it is a stray:
				    // Land 1's lost sheep are each a flock of their own, and the one carried to the shepherd's pen
				    // (Mike, 2026-10-19) never joined his.
				    std::optional<uint32_t> ownFlock;
				    if (const auto own = inFlock.find(id); own != inFlock.end())
				    {
					    const auto* centre = registry.TryGet<Transform>(static_cast<entt::entity>(own->second));
					    if (centre == nullptr ||
					        glm::distance(glm::vec2(centre->position.x, centre->position.z), glm::vec2(transform.position.x, transform.position.z)) <= reach * 2.0f)
					    {
						    return;
					    }
					    ownFlock = own->second;
				    }
				    if (id == g_heldObject || g_flying.contains(id) ||
				        std::any_of(g_creatureHolding.begin(), g_creatureHolding.end(), [id](const auto& h) { return h.second == id; }))
				    {
					    return;
				    }
				    for (auto& [flockId, flock] : g_flocks)
				    {
					    if (ownFlock && flockId == *ownFlock)
					    {
						    continue;
					    }
					    const auto* centre = registry.TryGet<Transform>(static_cast<entt::entity>(flockId));
					    const bool ofItsKind = std::any_of(flock.members.begin(), flock.members.end(), [&](uint32_t member) {
						    const auto* other = registry.Valid(static_cast<entt::entity>(member))
						                            ? registry.TryGet<openblack::ecs::components::Animal>(static_cast<entt::entity>(member))
						                            : nullptr;
						    return other != nullptr && other->type == animal.type;
					    });
					    if (centre == nullptr || !ofItsKind ||
					        glm::distance(glm::vec2(centre->position.x, centre->position.z), glm::vec2(transform.position.x, transform.position.z)) > reach)
					    {
						    continue;
					    }
					    if (ownFlock)
					    {
						    auto& old = g_flocks[*ownFlock].members;
						    old.erase(std::remove(old.begin(), old.end(), id), old.end());
					    }
					    flock.members.push_back(id);
					    SPDLOG_LOGGER_INFO(spdlog::get("game"), "Animal {} (type {}) joined flock {} within {:.0f} (info flockDistance {:.1f}): {} now{}",
					                       id, static_cast<int>(animal.type), flockId, reach, info, flock.members.size(),
					                       ownFlock ? fmt::format(" (a stray from flock {})", *ownFlock) : std::string());
					    break;
				    }
			    });
		}
	}
	// Sparkles: each keeps a few glitter sprites falling through what it is on, looping back to the top.
	{
		constexpr int k_SparklesEach = 4; // just a few, for now
		constexpr float k_ColumnTop = 9.0f;    // above the object
		constexpr float k_ColumnBottom = -1.0f; // below it
		constexpr float k_FallSpeed = 2.5f;
		constexpr float k_ColumnRadius = 1.8f;
		constexpr float k_SparkleFramesPerSecond = 16.0f;
		static float s_clock = 0.0f;
		s_clock += seconds;
		const auto* sheet = Locator::resources::value().GetTextures().Contains(entt::hashed_string("raw/S_SpriteSheet3a").value())
		                        ? &*Locator::resources::value().GetTextures().Handle(entt::hashed_string("raw/S_SpriteSheet3a"))
		                        : nullptr;
		for (auto it = g_sparklers.begin(); it != g_sparklers.end();)
		{
			auto& sparkler = it->second;
			const auto target = static_cast<entt::entity>(sparkler.target);
			const bool alive = registry.Valid(static_cast<entt::entity>(it->first)) && registry.Valid(target) &&
			                   registry.AllOf<Transform>(target) && sheet != nullptr;
			if (!alive)
			{
				for (const auto sprite : sparkler.sprites)
				{
					if (registry.Valid(sprite) && registry.AllOf<openblack::ecs::components::Sprite>(sprite))
					{
						registry.Destroy(sprite);
					}
				}
				if (it->first != sparkler.target && registry.Valid(static_cast<entt::entity>(it->first)))
				{
					registry.Destroy(static_cast<entt::entity>(it->first)); // the effect's own object
				}
				it = g_sparklers.erase(it);
				continue;
			}
			const auto centre = registry.Get<Transform>(target).position;
			// A sprite something else destroyed (or whose id went to something else) is made again: reading it crashed
			// the Switch.
			for (size_t i = 0; i < sparkler.sprites.size();)
			{
				const auto sprite = sparkler.sprites[i];
				if (registry.Valid(sprite) && registry.AllOf<openblack::ecs::components::Sprite, Transform>(sprite))
				{
					++i;
					continue;
				}
				sparkler.sprites.erase(sparkler.sprites.begin() + static_cast<std::ptrdiff_t>(i));
				sparkler.phases.erase(sparkler.phases.begin() + static_cast<std::ptrdiff_t>(i));
			}
			while (sparkler.sprites.size() < k_SparklesEach)
			{
				const auto i = sparkler.sprites.size();
				const auto sprite = registry.Create();
				registry.Assign<openblack::ecs::components::Sprite>(sprite, sheet->GetNativeHandle(), glm::vec2(0.0f),
				                                                   glm::vec2(1.0f / 8.0f), sparkler.tint);
				registry.Assign<Transform>(sprite, centre, glm::mat3(1.0f), glm::vec3(0.45f));
				sparkler.sprites.push_back(sprite);
				// Spread through the column, each on its own frame of the glitter's animation.
				sparkler.phases.push_back(static_cast<float>((i * 7919u + it->first * 104729u) % 1000u) / 1000.0f);
			}
			const float column = k_ColumnTop - k_ColumnBottom;
			for (size_t i = 0; i < sparkler.sprites.size(); ++i)
			{
				const float phase = sparkler.phases[i];
				const float fallen = std::fmod(phase * column + s_clock * k_FallSpeed, column);
				const float angle = phase * 97.0f; // somewhere round the column, fixed per sprite
				const float radius = k_ColumnRadius * std::sqrt(std::fmod(phase * 13.0f, 1.0f));
				auto& transform = registry.Get<Transform>(sparkler.sprites[i]);
				transform.position = centre + glm::vec3(std::cos(angle) * radius, k_ColumnTop - fallen, std::sin(angle) * radius);
				const int frame = 48 + static_cast<int>(phase * 16.0f + s_clock * k_SparkleFramesPerSecond) % 16;
				auto& sprite = registry.Get<openblack::ecs::components::Sprite>(sparkler.sprites[i]);
				sprite.uvMin = glm::vec2(static_cast<float>(frame % 8) / 8.0f, static_cast<float>(frame / 8) / 8.0f);
				// Fading in at the top and out at the bottom.
				const float along = fallen / column;
				sprite.tint = sparkler.tint * glm::vec4(glm::vec3(1.0f), std::min({1.0f, along * 5.0f, (1.0f - along) * 5.0f}));
			}
			++it;
		}
		static bool s_reported = false;
		if (!s_reported && !g_sparklers.empty())
		{
			s_reported = true;
			SPDLOG_LOGGER_INFO(spdlog::get("game"), "Sparkles: {} objects, sheet {}", g_sparklers.size(), sheet != nullptr);
		}
	}
	// Test hook: OPENBLACK_TEST_SPHERES=<seconds>: that long into the run, food, water, heal, wood and fire spheres in a
	// grid in front of the camera - a row below the eye (seen from above), one level with it, one above - kept there
	// each frame, to look at from every angle (screenshots: OPENBLACK_SHOT_DIR).
	{
		static const char* s_testSpheres = std::getenv("OPENBLACK_TEST_SPHERES");
		static std::vector<std::pair<uint32_t, glm::vec2>> s_placed; // sphere, its place across and up
		static uint32_t s_testChest = 0;
		static uint32_t s_chestShown = 0;
		static uint32_t s_testDispenser = 0;
		if (s_testSpheres != nullptr)
		{
			static float s_since = 0.0f;
			s_since += seconds;
			const auto& camera = Locator::camera::value();
			const auto eye = camera.GetOrigin();
			const auto forward = glm::normalize(camera.GetForward());
			const auto right = glm::normalize(camera.GetRight());
			const auto up = glm::vec3(0.0f, 1.0f, 0.0f);
			if (s_placed.empty() && s_since >= static_cast<float>(std::atof(s_testSpheres)))
			{
				const std::array<uint32_t, 5> k_Seeds = {3, 9, 7, 8, 2};
				for (int row = 0; row < 3; ++row)
				{
					for (size_t i = 0; i < k_Seeds.size(); ++i)
					{
						const std::array<float, 3> k_Heights = {-2.5f, 3.0f, 11.0f};
						const glm::vec2 place((static_cast<float>(i) - 2.0f) * 6.5f, k_Heights[static_cast<size_t>(row)]);
						const auto made = CreateScriptObject(ObjectType::OneShotSpell, k_Seeds[i], eye + forward * 30.0f, 0.0f, 0.0f,
						                                     0.0f, 0.0f, 1.0f);
						s_placed.emplace_back(static_cast<uint32_t>(made), place);
					}
				}
				SPDLOG_LOGGER_INFO(spdlog::get("game"), "Test spheres: {} placed", s_placed.size());
				// ...and on the ground ahead, a reward chest (opened 8 s later) and a dispenser keeping a food sphere.
				const auto ahead = eye + glm::normalize(glm::vec3(forward.x, 0.0f, forward.z)) * 18.0f;
				s_testChest = static_cast<uint32_t>(CreateScriptObject(ObjectType::Reward, 5, ahead - right * 6.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f));
				s_chestShown = s_testChest;
				s_testDispenser = static_cast<uint32_t>(
				    CreateScriptObject(ObjectType::SpellDispenser, 126, ahead + right * 6.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f));
				g_dispensers[s_testDispenser].seed = 3;
			}
			// The chest and the dispenser (with its sphere) kept on the ground ahead too.
			const auto ahead = eye + glm::normalize(glm::vec3(forward.x, 0.0f, forward.z)) * 18.0f;
			auto onGround = [](glm::vec3 at) {
				at.y = Locator::terrainSystem::value().GetHeightAt(glm::vec2(at.x, at.z));
				return at;
			};
			if (const auto chest = static_cast<entt::entity>(s_chestShown);
			    s_chestShown != 0 && registry.Valid(chest) && registry.AllOf<Transform>(chest))
			{
				registry.Get<Transform>(chest).position = onGround(eye + glm::normalize(glm::vec3(forward.x, 0.0f, forward.z)) * 7.0f);
				if (std::getenv("OPENBLACK_TEST_CHEST_REST") != nullptr)
				{
					registry.Remove<openblack::ecs::components::Animated>(chest);
				}
			}
			if (const auto dispenser = static_cast<entt::entity>(s_testDispenser);
			    s_testDispenser != 0 && registry.Valid(dispenser) && registry.AllOf<Transform>(dispenser))
			{
				auto& at = registry.Get<Transform>(dispenser).position;
				const auto moved = onGround(ahead + right * 6.0f) - at;
				at += moved;
				const auto sphere = static_cast<entt::entity>(g_dispensers[s_testDispenser].sphere);
				if (g_dispensers[s_testDispenser].sphere != 0 && registry.Valid(sphere) && registry.AllOf<Transform>(sphere))
				{
					registry.Get<Transform>(sphere).position += moved;
				}
			}
			if (s_testChest != 0 && s_since >= static_cast<float>(std::atof(s_testSpheres)) + 8.0f)
			{
				OpenReward(s_testChest);
				s_testChest = 0;
			}
			for (const auto& [id, place] : s_placed)
			{
				const auto sphere = static_cast<entt::entity>(id);
				if (registry.Valid(sphere) && registry.AllOf<Transform>(sphere))
				{
					registry.Get<Transform>(sphere).position = eye + forward * 24.0f + right * place.x + up * place.y;
				}
			}
		}
	}
	// Miracle spheres: each holds its miracle's icon model (the original's I_ meshes: food the cornucopia SpellHornOfPlenty - Mike, I_Heal,
	// I_Fireball...), turning slowly inside the bubble (Mike, 2026-10-18). By spell seed (info.dat's order).
	{
		static const std::map<int, MeshId> k_Icons = {
		    {0, MeshId::I_Lightning2},    {1, MeshId::I_Forest},          {2, MeshId::I_Fireball},     {3, MeshId::SpellHornOfPlenty},
		    {4, MeshId::I_Shield},        {5, MeshId::I_Shield},          {6, MeshId::I_Lightning},    {7, MeshId::I_Heal},
		    {8, MeshId::I_OfferingWood},  {12, MeshId::SpellPhileFreeze}, {13, MeshId::SpellPhileSmall},
		    {14, MeshId::SpellPhileBig},  {16, MeshId::SpellPhile},       {17, MeshId::SpellPhileFat}, {19, MeshId::SpellPhileInvisible},
		    {20, MeshId::SpellPhileLove}, {21, MeshId::SpellPhileAngry},  {22, MeshId::SpellPhileHungry}, {23, MeshId::SpellPhileFrighten},
		    {25, MeshId::SpellPhileIll},  {27, MeshId::SpellPhileItchy},  {29, MeshId::I_Blast}};
		static std::unordered_map<uint32_t, entt::entity> s_icons;
		// One clock that only runs on: wrapped at a full turn, the tilts (at other rates) jumped back mid-turn (Mike,
		// 2026-10-18). The turn itself is wrapped where it is used.
		static double s_clock = 0.0;
		s_clock += seconds;
		const float s_spin = static_cast<float>(std::fmod(s_clock * 2.0, glm::two_pi<double>()));
		const auto& meshes = Locator::resources::value().GetMeshes();
		for (auto it = s_icons.begin(); it != s_icons.end();)
		{
			if (!g_oneShotSpells.contains(it->first) || !registry.Valid(static_cast<entt::entity>(it->first)))
			{
				if (registry.Valid(it->second))
				{
					registry.Destroy(it->second);
				}
				it = s_icons.erase(it);
				continue;
			}
			++it;
		}
		for (const auto& [id, seed] : g_oneShotSpells)
		{
			const auto sphere = static_cast<entt::entity>(id);
			const auto icon = k_Icons.find(seed);
			if (icon == k_Icons.end() || !registry.Valid(sphere) || !registry.AllOf<Transform>(sphere))
			{
				continue;
			}
			const auto iconMesh = resources::HashIdentifier(icon->second);
			if (!meshes.Contains(iconMesh))
			{
				continue;
			}
			auto& entity = s_icons.try_emplace(id, entt::null).first->second; // not [id]: a new one would be entity 0, the hand
			if (entity == entt::null || !registry.Valid(entity))
			{
				entity = registry.Create();
				registry.Assign<Transform>(entity, glm::vec3(0.0f), glm::mat3(1.0f), glm::vec3(1.0f));
				registry.Assign<openblack::ecs::components::Mesh>(entity, iconMesh, static_cast<int8_t>(0), static_cast<int8_t>(0));
			}
			const auto& at = registry.Get<Transform>(sphere);
			const auto* mesh = registry.TryGet<openblack::ecs::components::Mesh>(sphere);
			// At the original's size: its model at 0.6 of the bubble's scale (OneOffSpellSeed::GetSpellGraphicPos), the
			// bubble at 1. Centred on it.
			const float scale = 0.6f;
			const auto box = meshes.Handle(iconMesh)->GetBoundingBox();
			auto& transform = registry.Get<Transform>(entity);
			// Upright, turning about the up axis at spinSpeed, 2 radians a second (SpellSeedGraphic::DrawSpellGraphic's
			// YAngle) (Mike, 2026-10-19: tumbling, it left the bubble from some angles). Its middle at the bubble's
			// centre: by its origin, as GetSpellGraphicPos puts it, the icons sat a little high (Mike, 2026-10-19).
			const float phase = static_cast<float>(id % 7);
			transform.rotation = glm::mat3(glm::eulerAngleY(s_spin + phase));
			transform.scale = glm::vec3(id == g_heldObject ? 0.0f : scale); // taken: popped, the miracle in the hand
			transform.position = at.position - transform.rotation * (box.Center() * scale);
		}
		// Each sphere's own effect, as the original's (SpellSeedGraphic::CreateGraphic): its spell seed info's particle
		// type (+0x164) - water's SF_WaterOnHolder, a mist hanging in it; heal's chakra; fire's, lightning's, the
		// shields' and the creature spells' - run by the particle systems (src/PSys) at the sphere, magnitude 1.
		{
			static auto& s_effects = *new std::unordered_map<uint32_t, openblack::psys::Handle>();
			for (auto it = s_effects.begin(); it != s_effects.end();)
			{
				if (!g_oneShotSpells.contains(it->first) || !registry.Valid(static_cast<entt::entity>(it->first)) || it->first == g_heldObject)
				{
					openblack::psys::Destroy(it->second);
					it = s_effects.erase(it);
					continue;
				}
				++it;
			}
			const auto& infos = Locator::infoConstants::value().spellSeed;
			for (const auto& [id, seed] : g_oneShotSpells)
			{
				const auto sphere = static_cast<entt::entity>(id);
				if (seed < 0 || static_cast<size_t>(seed) >= infos.size() || !registry.Valid(sphere) || !registry.AllOf<Transform>(sphere) ||
				    id == g_heldObject)
				{
					continue;
				}
				auto [entry, made] = s_effects.try_emplace(id, 0);
				if (made)
				{
					openblack::psys::Start start;
					start.type = static_cast<openblack::psys::Type>(infos[static_cast<size_t>(seed)].field0x164);
					start.origin = registry.Get<Transform>(sphere).position;
					start.magnitude = 1.0f; // SpellSeedGraphic's Size, 1 for a sphere's
					start.humanCasting = false;
					entry->second = openblack::psys::Create(start);
				}
				openblack::psys::SetOrigin(entry->second, registry.Get<Transform>(sphere).position);
			}
		}
		// The bubble itself, as the original's (OneOffSpellSeed): Data/Spells/Meshes/O_Bibble_up.l3d, whose drawn part is
		// a disc turned to the camera (FaceCamera) showing one of the 16 frames of its skin - a 4x4 sheet of a glassy
		// sphere - stepping 18 frames a second (OneOffSpellSeed::UpdateFrame), drawn at SpellSeedAlpha (150 of 255).
		// Never freed: as a static unique_ptr it was destroyed at exit, after bgfx had shut down, and bgfx::destroy
		// crashed every quit and self-update restart (crash report 2026-10-18: textureDecRef from exit()).
		static openblack::graphics::Texture2D* s_bubbleTexture = nullptr;
		static bool s_triedBubble = false;
		if (!s_triedBubble)
		{
			s_triedBubble = true;
			try
			{
				auto& fs = Locator::filesystem::value();
				const auto bytes = fs.ReadAll(fs.GetPath<openblack::filesystem::Path::Data>() / "Spells" / "Meshes" / "O_bibble_up.l3d");
				openblack::l3d::L3DFile file;
				if (file.Open(bytes) == openblack::l3d::L3DResult::Success && !file.GetSkins().empty())
				{
					const auto& skin = file.GetSkins().front();
					constexpr int k_Side = openblack::l3d::L3DTexture::k_Width;
					std::vector<uint8_t> pixels(static_cast<size_t>(k_Side * k_Side * 4));
					for (size_t i = 0; i < skin.texels.size(); ++i)
					{
						const auto& texel = skin.texels[i];
						pixels[i * 4 + 0] = static_cast<uint8_t>(texel.r * 17);
						pixels[i * 4 + 1] = static_cast<uint8_t>(texel.g * 17);
						pixels[i * 4 + 2] = static_cast<uint8_t>(texel.b * 17);
						pixels[i * 4 + 3] = static_cast<uint8_t>(texel.a * 17);
					}
					s_bubbleTexture = new openblack::graphics::Texture2D("miracle bubble");
					s_bubbleTexture->Create(k_Side, k_Side, 1, openblack::graphics::TextureFormat::RGBA8,
					                        openblack::graphics::Wrapping::ClampEdge, openblack::graphics::Filter::Linear,
					                        bgfx::copy(pixels.data(), static_cast<uint32_t>(pixels.size())));
					SPDLOG_LOGGER_INFO(spdlog::get("game"), "Miracle bubble: the original's O_bibble_up.l3d flipbook");
				}
				else
				{
					SPDLOG_LOGGER_WARN(spdlog::get("game"), "Miracle bubble: O_bibble_up.l3d has no skin");
				}
			}
			catch (const std::exception& e)
			{
				SPDLOG_LOGGER_WARN(spdlog::get("game"), "Miracle bubble texture: {}", e.what());
			}
		}
		static std::unordered_map<uint32_t, entt::entity> s_bubbles;
		const auto& camera = Locator::camera::value();
		const auto toCamera = glm::normalize(camera.GetOrigin() - camera.GetFocus());
		for (auto it = s_bubbles.begin(); it != s_bubbles.end();)
		{
			if (!g_oneShotSpells.contains(it->first) || !registry.Valid(static_cast<entt::entity>(it->first)))
			{
				if (it->second != entt::null && registry.Valid(it->second))
				{
					registry.Destroy(it->second);
				}
				it = s_bubbles.erase(it);
				continue;
			}
			++it;
		}
		if (s_bubbleTexture)
		{
			std::vector<std::pair<uint32_t, glm::vec3>> spheres;
			for (const auto& [id, seed] : g_oneShotSpells)
			{
				const auto sphere = static_cast<entt::entity>(id);
				if (registry.Valid(sphere) && registry.AllOf<Transform>(sphere))
				{
					spheres.emplace_back(id, registry.Get<Transform>(sphere).position);
				}
			}
			for (const auto& [id, centre] : spheres)
			{
				auto& bubble = s_bubbles.try_emplace(id, entt::null).first->second; // not [id]: entity 0 is the hand
				if (bubble == entt::null || !registry.Valid(bubble))
				{
					bubble = registry.Create();
					auto& sprite = registry.Assign<openblack::ecs::components::Sprite>(
					    bubble, s_bubbleTexture->GetNativeHandle(), glm::vec2(0.0f), glm::vec2(0.25f),
					    glm::vec4(1.0f, 1.0f, 1.0f, 150.0f / 255.0f));
					sprite.darkens = true; // blended over what is behind, not added light
					// Its alpha is the circle: read as one channel (red), the whole square showed (Mike, 2026-10-18).
					sprite.fullColour = true;
					registry.Assign<Transform>(bubble, centre, glm::mat3(1.0f), glm::vec3(2.0f * k_BubbleRadius));
				}
				// Its frame of the sheet: 18 a second, round all 16 (each sphere as the original's, from the same clock).
				const int frame = static_cast<int>(std::fmod(s_clock * 18.0, 16.0));
				auto& sprite = registry.Get<openblack::ecs::components::Sprite>(bubble);
				// The part of the frame the original's disc shows: 0.016 to 0.242 across, 0.012 to 0.238 down.
				sprite.uvMin = glm::vec2(static_cast<float>(frame % 4), static_cast<float>(frame / 4)) * 0.25f + glm::vec2(0.016f, 0.012f);
				sprite.uvExtent = glm::vec2(0.226f);
				auto& transform = registry.Get<Transform>(bubble);
				// In front of everything inside: the icon reaches 1.2 radii from the centre, and at 0.6 the cornucopia's
				// near end came through the bubble (Mike, 2026-10-18).
				// Drawn nearer the camera by its radius twice over (OneOffSpellSeed::Draw, Object::GetRadius), the disc
				// at its own size.
				// Along its own line to the eye (OneOffSpellSeed::Draw: camera less centre), not the camera's view: the
				// view's direction slid the bubbles off their contents away from the middle of the screen.
				transform.position = centre + glm::normalize(camera.GetOrigin() - centre) * (2.0f * k_BubbleRadius);
				transform.scale = glm::vec3(id == g_heldObject ? 0.0f : k_BubbleDiscRadius);
			}
		}
	}
	// A shadow lies flat on the ground at `at`, tilted with its slope (sampled `span` either side), just above it
	// (Mike, 2026-10-18: turned to the camera, they stood up behind the villagers).
	auto groundShadow = [](const glm::vec3& at, float span) {
		const auto& land = Locator::terrainSystem::value();
		const float dx = land.GetHeightAt(glm::vec2(at.x + span, at.z)) - land.GetHeightAt(glm::vec2(at.x - span, at.z));
		const float dz = land.GetHeightAt(glm::vec2(at.x, at.z + span)) - land.GetHeightAt(glm::vec2(at.x, at.z - span));
		const auto up = glm::normalize(glm::vec3(-dx, 2.0f * span, -dz));
		const auto x = glm::normalize(glm::cross(up, glm::vec3(0.0f, 0.0f, 1.0f)));
		const auto z = glm::cross(x, up);
		return std::pair {glm::mat3(x, up, z), glm::vec3(at.x, land.GetHeightAt(glm::vec2(at.x, at.z)) + 0.1f, at.z)};
	};
	// Living things on the ground stand on a shadow (the original's human_shadow texture, darkening): near, the model
	// and its shadow; from k_ShadowFrom, the shadow alone (ShownAsShadow leaves the model out); from k_ShadowReach,
	// nothing (Mike, 2026-10-18). Villagers and animals that walk - not birds, the creature, the hand's, or anything
	// flying. Only the nearest k_MostShadows get one: with the whole land in view, hundreds were made at once - and made
	// inside the walk over the villagers' Transforms, whose storage that grew, so the walk read freed memory and the
	// game crashed (Mike, 2026-10-18, twice, zoomed right out). Now gathered first, made after.
	{
		using openblack::ecs::components::ShownAsShadow;
		static std::unordered_map<uint32_t, entt::entity> s_shadows;
		const auto& textures = Locator::resources::value().GetTextures();
		const auto shadowTexture = entt::hashed_string("raw/human_shadow").value();
		const auto eye = Locator::camera::value().GetOrigin();
		constexpr float k_ShadowFrom = 80.0f;   // a villager about 12 pixels tall at 720 lines
		constexpr float k_ShadowReach = 350.0f; // past this, not even the shadow
		constexpr size_t k_MostShadows = 150;
		constexpr float k_VillagerShadow = 1.2f;
		constexpr float k_AnimalShadow = 2.4f;
		const bool canShadow = textures.Contains(shadowTexture);
		// distance, (id, where, shadow size)
		std::vector<std::pair<float, std::tuple<uint32_t, glm::vec3, float>>> shadowed;
		std::vector<entt::entity> wasHidden;
		std::vector<uint32_t> hide;
		auto gather = [&](entt::entity entity, const Transform& where, float size) {
			const auto id = static_cast<uint32_t>(entity);
			if (registry.AllOf<ShownAsShadow>(entity))
			{
				wasHidden.push_back(entity);
			}
			if (id == g_heldObject || g_flying.contains(id) || registry.AllOf<openblack::ecs::components::Asleep>(entity))
			{
				return; // drawn as itself (or, asleep indoors, not at all)
			}
			const float d = glm::distance(eye, where.position);
			if (d >= k_ShadowReach || (canShadow && d >= k_ShadowFrom))
			{
				hide.push_back(id); // only its shadow, or past that nothing
			}
			// In the sea, no shadow (Mike, 2026-10-18).
			if (canShadow && d < k_ShadowReach && SeaDepthAt(glm::vec2(where.position.x, where.position.z)) <= 0.0f)
			{
				shadowed.push_back({d, {id, where.position, size}});
			}
		};
		registry.Each<const openblack::ecs::components::Villager, const Transform>(
		    [&](entt::entity entity, const openblack::ecs::components::Villager&, const Transform& where) {
			    gather(entity, where, k_VillagerShadow);
		    });
		registry.Each<const openblack::ecs::components::Animal, const Transform>(
		    [&](entt::entity entity, const openblack::ecs::components::Animal& animal, const Transform& where) {
			    // Birds (crow to the spell bat) fly: no shadow, always drawn.
			    if (animal.type >= AnimalInfo::Crow && animal.type <= AnimalInfo::SpellBat)
			    {
				    if (registry.AllOf<ShownAsShadow>(entity))
				    {
					    wasHidden.push_back(entity);
				    }
				    return;
			    }
			    gather(entity, where, k_AnimalShadow);
		    });
		if (shadowed.size() > k_MostShadows)
		{
			std::nth_element(shadowed.begin(), shadowed.begin() + static_cast<std::ptrdiff_t>(k_MostShadows), shadowed.end(),
			                 [](const auto& x, const auto& y) { return x.first < y.first; });
			shadowed.resize(k_MostShadows);
		}
		std::unordered_set<uint32_t> shown;
		for (const auto& [d, who] : shadowed)
		{
			const auto& [id, at, size] = who;
			shown.insert(id);
			auto& shadow = s_shadows.try_emplace(id, entt::null).first->second; // not [id]: a new one would be entity 0, the hand
			if (shadow == entt::null || !registry.Valid(shadow))
			{
				shadow = registry.Create();
				auto& sprite = registry.Assign<openblack::ecs::components::Sprite>(
				    shadow, textures.Handle(shadowTexture)->GetNativeHandle(), glm::vec2(0.0f), glm::vec2(1.0f), glm::vec4(0.0f, 0.0f, 0.0f, 0.5f));
				sprite.darkens = true;
				sprite.flat = true;
				registry.Assign<Transform>(shadow, at, glm::mat3(1.0f), glm::vec3(size));
			}
			auto& placed = registry.Get<Transform>(shadow);
			std::tie(placed.rotation, placed.position) = groundShadow(at, 0.5f);
		}
		const std::unordered_set<uint32_t> hidden(hide.begin(), hide.end());
		for (const auto entity : wasHidden)
		{
			if (!hidden.contains(static_cast<uint32_t>(entity)))
			{
				registry.Remove<ShownAsShadow>(entity); // near again (or picked up, or a bird): its model is back
			}
		}
		for (const auto id : hide)
		{
			registry.AssignOrReplace<ShownAsShadow>(static_cast<entt::entity>(id));
		}
		for (auto it = s_shadows.begin(); it != s_shadows.end();)
		{
			if (!shown.contains(it->first))
			{
				if (registry.Valid(it->second))
				{
					registry.Destroy(it->second);
				}
				it = s_shadows.erase(it);
				continue;
			}
			++it;
		}
	}
	// Trees likewise, further out (Mike, 2026-10-18): the tree near; from k_TreeShadowFrom - about where the animals'
	// shadows stop - a big shadow in its place; from k_TreeShadowReach, nothing. Each shadow is a draw of its own, so
	// only the nearest k_MostTreeShadows get one; the rest out there are left out with the furthest.
	{
		using openblack::ecs::components::ShownAsShadow;
		static std::unordered_map<uint32_t, entt::entity> s_treeShadows;
		const auto& textures = Locator::resources::value().GetTextures();
		const auto& meshes = Locator::resources::value().GetMeshes();
		const auto shadowTexture = entt::hashed_string("raw/human_shadow").value();
		const auto eye = Locator::camera::value().GetOrigin();
		constexpr float k_TreeShadowFrom = 350.0f;
		constexpr float k_TreeShadowReach = 1000.0f;
		constexpr size_t k_MostTreeShadows = 300;
		const bool canShadow = textures.Contains(shadowTexture);
		std::vector<std::pair<float, std::tuple<uint32_t, glm::vec3, float>>> shadowed; // distance, (id, where, size)
		std::vector<entt::entity> wasHidden;
		std::vector<uint32_t> hide;
		registry.Each<const openblack::ecs::components::Tree, const Transform, const openblack::ecs::components::Mesh>(
		    [&](entt::entity entity, const openblack::ecs::components::Tree&, const Transform& where,
		        const openblack::ecs::components::Mesh& mesh) {
			    const auto id = static_cast<uint32_t>(entity);
			    if (registry.AllOf<ShownAsShadow>(entity))
			    {
				    wasHidden.push_back(entity);
			    }
			    if (id == g_heldObject || g_flying.contains(id) || !canShadow)
			    {
				    return; // drawn as itself
			    }
			    const float d = glm::distance(eye, where.position);
			    if (d < k_TreeShadowFrom)
			    {
				    return;
			    }
			    hide.push_back(id);
			    if (d < k_TreeShadowReach && meshes.Contains(mesh.id))
			    {
				    // As wide as its crown: the mesh's box across, scaled.
				    const auto size = meshes.Handle(mesh.id)->GetBoundingBox().Size();
				    const float across = std::max(size.x, size.z) * std::max(where.scale.x, where.scale.z);
				    shadowed.push_back({d, {id, where.position, std::max(across, 2.0f)}});
			    }
		    });
		if (shadowed.size() > k_MostTreeShadows)
		{
			std::nth_element(shadowed.begin(), shadowed.begin() + static_cast<std::ptrdiff_t>(k_MostTreeShadows), shadowed.end(),
			                 [](const auto& x, const auto& y) { return x.first < y.first; });
			shadowed.resize(k_MostTreeShadows);
		}
		std::unordered_set<uint32_t> shown;
		for (const auto& [d, what] : shadowed)
		{
			const auto& [id, at, size] = what;
			shown.insert(id);
			auto& shadow = s_treeShadows.try_emplace(id, entt::null).first->second; // not [id]: entity 0 is the hand
			if (shadow == entt::null || !registry.Valid(shadow))
			{
				shadow = registry.Create();
				auto& sprite = registry.Assign<openblack::ecs::components::Sprite>(
				    shadow, textures.Handle(shadowTexture)->GetNativeHandle(), glm::vec2(0.0f), glm::vec2(1.0f), glm::vec4(0.0f, 0.0f, 0.0f, 0.55f));
				sprite.darkens = true;
				sprite.flat = true;
				registry.Assign<Transform>(shadow, at, glm::mat3(1.0f), glm::vec3(size));
			}
			auto& transform = registry.Get<Transform>(shadow);
			std::tie(transform.rotation, transform.position) = groundShadow(at, std::max(size * 0.5f, 1.0f));
			transform.position.y += 0.1f;
			transform.scale = glm::vec3(size);
		}
		const std::unordered_set<uint32_t> hidden(hide.begin(), hide.end());
		for (const auto entity : wasHidden)
		{
			if (!hidden.contains(static_cast<uint32_t>(entity)))
			{
				registry.Remove<ShownAsShadow>(entity);
			}
		}
		for (const auto id : hide)
		{
			registry.AssignOrReplace<ShownAsShadow>(static_cast<entt::entity>(id));
		}
		for (auto it = s_treeShadows.begin(); it != s_treeShadows.end();)
		{
			if (!shown.contains(it->first))
			{
				if (registry.Valid(it->second))
				{
					registry.Destroy(it->second);
				}
				it = s_treeShadows.erase(it);
				continue;
			}
			++it;
		}
	}
	// Spheres nobody holds settle softly, like bubbles: falling slowly, resting on the land and on each other, nudged
	// apart. Not a dispenser's sphere, the hand's, or one a script is keeping where it put it.
	{
		static std::unordered_map<uint32_t, glm::vec3> s_velocity;
		static std::unordered_set<uint32_t> s_scripted;
		static float s_scriptedAge = 1e9f;
		s_scriptedAge += seconds;
		if (s_scriptedAge > 2.0f)
		{
			s_scripted = ScriptedObjects();
			s_scriptedAge = 0.0f;
		}
		std::unordered_set<uint32_t> onDispensers;
		for (const auto& [id, dispenser] : g_dispensers)
		{
			onDispensers.insert(dispenser.sphere);
		}
		const auto& meshes = Locator::resources::value().GetMeshes();
		std::vector<std::pair<uint32_t, float>> free;
		for (const auto& [id, seed] : g_oneShotSpells)
		{
			const auto sphere = static_cast<entt::entity>(id);
			if (id == g_heldObject || onDispensers.contains(id) || s_scripted.contains(id) || g_flying.contains(id) ||
			    g_vortexSpirals.contains(id) || !registry.Valid(sphere) || !registry.AllOf<Transform>(sphere))
			{
				s_velocity.erase(id);
				continue;
			}
			const auto* mesh = registry.TryGet<openblack::ecs::components::Mesh>(sphere);
			const float radius = k_BubbleRadius; // the bubble image's, not Seed.L3D's (a NoDraw stand-in)
			free.emplace_back(id, radius);
		}
		constexpr float k_BubbleGravity = 3.0f;
		constexpr float k_Push = 12.0f;
		constexpr float k_Drag = 1.5f;
		for (size_t a = 0; a < free.size(); ++a)
		{
			auto& v = s_velocity[free[a].first];
			auto& pa = registry.Get<Transform>(static_cast<entt::entity>(free[a].first)).position;
			for (size_t b = a + 1; b < free.size(); ++b)
			{
				auto& pb = registry.Get<Transform>(static_cast<entt::entity>(free[b].first)).position;
				const auto apart = pa - pb;
				const float d = glm::length(apart);
				const float touching = free[a].second + free[b].second;
				if (d < touching)
				{
					// Mostly sideways, as bubbles in a bucket roll off each other; one right on top of another is
					// nudged off its own way (else they stood in a column).
					auto flat = glm::vec3(apart.x, apart.y * 0.25f, apart.z);
					if (glm::length(glm::vec2(apart.x, apart.z)) < 0.2f * touching)
					{
						const float angle = static_cast<float>((free[a].first * 2654435761u + free[b].first) % 628u) / 100.0f;
						flat = glm::vec3(std::cos(angle), 0.1f, std::sin(angle));
					}
					const auto push = glm::normalize(flat) * (touching - d) * k_Push * seconds;
					v += push;
					s_velocity[free[b].first] -= push;
				}
			}
			v.y -= k_BubbleGravity * seconds;
			v *= std::exp(-k_Drag * seconds);
			pa += v * seconds;
			const float floor = std::max(Locator::terrainSystem::value().GetHeightAt(glm::vec2(pa.x, pa.z)), 0.0f) + free[a].second;
			if (pa.y < floor)
			{
				pa.y = floor;
				v.y = std::abs(v.y) * 0.3f; // a soft bounce
			}
		}
	}
	// Scrolls turn slowly about the vertical, as the original's do; signposts stay as they were put.
	{
		constexpr float k_ScrollSpin = 0.6f; // radians a second
		static float s_spin = 0.0f;
		s_spin = std::fmod(s_spin + k_ScrollSpin * seconds, glm::two_pi<float>());
		for (const auto& [id, type] : g_highlights)
		{
			const auto entity = static_cast<entt::entity>(id);
			auto* transform = registry.Valid(entity) ? registry.TryGet<Transform>(entity) : nullptr;
			if (transform != nullptr && type != 1)
			{
				transform->rotation = glm::mat3(glm::eulerAngleY(s_spin));
			}
		}
	}
	for (auto it = g_walks.begin(); it != g_walks.end();)
	{
		if (it->first == g_heldObject)
		{
			++it; // in the hand: the script's move waits till it is let go of
			continue;
		}
		const auto entity = static_cast<entt::entity>(it->first);
		auto* transform = registry.Valid(entity) ? registry.TryGet<Transform>(entity) : nullptr;
		if (transform == nullptr)
		{
			it = g_walks.erase(it);
			continue;
		}
		auto& walk = it->second;
		const float before = walk.at;
		if (!walk.keyTimes.empty())
		{
			walk.timeMs = std::min(walk.timeMs + seconds * 1000.0f, walk.endMs);
			walk.at = TimedWalkDistance(walk, walk.timeMs);
		}
		else if (const float step = ScriptSpeed(it->first).value_or(FullSpeed(it->first)) * seconds; step > 0.0f)
		{
			walk.at = walk.end >= walk.at ? std::min(walk.at + step, walk.end) : std::max(walk.at - step, walk.end);
		}
		g_moverSpeeds[it->first] = {std::abs(walk.at - before) / std::max(seconds, 1e-4f), g_scriptFrame};
		if (walk.at != before)
		{
			const auto next = std::upper_bound(walk.lengths.begin(), walk.lengths.end(), walk.at);
			const size_t i = std::clamp<size_t>(static_cast<size_t>(next - walk.lengths.begin()), 1, walk.points.size() - 1);
			const float span = walk.lengths[i] - walk.lengths[i - 1];
			const float t = span > 0.0f ? (walk.at - walk.lengths[i - 1]) / span : 1.0f;
			const auto position = glm::mix(walk.points[i - 1], walk.points[i], std::clamp(t, 0.0f, 1.0f));
			PlaceMover(entity, *transform, position, walk.end >= walk.at ? walk.points[i] : walk.points[i - 1]);
			if (!walk.keyTimes.empty())
			{
				transform->position.y = std::max(transform->position.y, 0.0f); // a shark swims at the surface, its fin out
				// Turned as Whale's draw turns it: RotateY(GetYAngle(its move)), GetYAngle being atan2(z, x) - its
				// model's +X along the way it goes. FaceTowards turns people's -Z there, which left the shark swimming
				// sideways (Mike, 2026-10-22).
				const auto way = (walk.end >= walk.at ? walk.points[i] : walk.points[i - 1]) - transform->position;
				if (way.x != 0.0f || way.z != 0.0f)
				{
					transform->rotation = glm::mat3(glm::eulerAngleY(std::atan2(-way.z, way.x)));
				}
			}
		}
		const bool done = walk.keyTimes.empty() ? walk.at == walk.end : walk.timeMs >= walk.endMs;
		it = done ? g_walks.erase(it) : std::next(it);
	}

	UpdateCameraPath(seconds);

	// Grain fields: sown by farmers, grown as the original grows them, harvested; an emptied field shows nothing.
	{
		static constexpr float k_Stubble = 0.05f; // the wheat's height left on an empty field
		// Field::Process, every 10 game turns: a field all sown grows - by twice its sun effect, the growing one till
		// ripe (ageGrowth), the ripening one after (no rain in the port yet; the land's alignment, the original's
		// (alignment / 2 + 1) factor, counts 0) - and fills with food as it grows, its totalFoodInField at ageRecolt.
		static float s_fieldClock = 0.0f;
		s_fieldClock += seconds;
		const bool growTurn = s_fieldClock >= 1.0f;
		if (growTurn)
		{
			s_fieldClock -= 1.0f;
		}
		registry.Each<openblack::ecs::components::Field, Transform>(
		    [&registry, growTurn](entt::entity entity, openblack::ecs::components::Field& field, Transform& transform) {
			    if (field.capacity <= 0.0f)
			    {
				    return;
			    }
			    const auto& type = Locator::infoConstants::value().fieldType.at(field.fieldType);
			    if (growTurn && static_cast<float>(field.crops) >= type.timesToSow && field.growth < type.ageRecolt)
			    {
				    const float amount = 2.0f * (field.growth < type.ageGrowth ? type.effectSunWhenGrowing : type.effectSunWhenRipening);
				    field.growth += amount;
				    field.grain = std::min(field.grain + amount * type.totalFoodInField / std::max(type.ageRecolt, 1.0f), field.capacity);
			    }
			    // The wheat stands as tall as it has grown (sown, it is stubble).
			    const float tall = type.ageRecolt > 0.0f && static_cast<float>(field.crops) >= type.timesToSow ? field.growth / type.ageRecolt : 0.0f;
			    transform.scale.y = std::max(std::min(tall, field.grain / field.capacity + 0.05f), k_Stubble) * transform.scale.x;
			    // Empty, nothing of it shows, as in the original (Mike, 2026-10-18): left out like a far-off thing.
			    if (field.grain < 1.0f)
			    {
				    registry.AssignOrReplace<openblack::ecs::components::ShownAsShadow>(entity);
			    }
			    else
			    {
				    registry.Remove<openblack::ecs::components::ShownAsShadow>(entity);
			    }
		    });
	}

	// Loose grain piles as big as the grain in them (Mike, 2026-10-18: twice too big, and a handful as big as a heap):
	// by the cube root of how full they are (their volume goes with the amount), half the default size when full
	// (info.dat's maxAmountInPot). Not the village store's own pile, nor grain in the hand.
	{
		const auto& pots = Locator::infoConstants::value().pot;
		registry.Each<const openblack::ecs::components::Pot, Transform>(
		    [&pots](entt::entity entity, const openblack::ecs::components::Pot& pot, Transform& transform) {
			    if (pot.type == PotInfo::_COUNT || pot.type == PotInfo::StoragePitFoodPile || pot.type == PotInfo::HandFood ||
			        static_cast<uint32_t>(entity) == g_heldObject)
			    {
				    return;
			    }
			    const auto& info = pots.at(static_cast<size_t>(pot.type));
			    if (info.resourceType != ResourceType::Food || info.maxAmountInPot == 0)
			    {
				    return;
			    }
			    const float full = std::clamp(static_cast<float>(pot.amount) / static_cast<float>(info.maxAmountInPot), 0.0f, 1.0f);
			    transform.scale = glm::vec3(0.5f * std::max(std::cbrt(full), 0.15f));
		    });
	}

	// The day goes by: an hour an hour, from the Switch's clock until a script sets the time, then on from there.
	if (!g_gameTimeMove)
	{
		if (!g_gameTimeSet)
		{
			const auto now = std::time(nullptr);
			if (const auto* local = std::localtime(&now))
			{
				ApplyGameHour(static_cast<float>(local->tm_hour) + static_cast<float>(local->tm_min) / 60.0f);
			}
			g_gameTimeSet = true;
		}
		else
		{
			ApplyGameHour(g_gameHour + seconds / 3600.0f);
		}
	}
	if (g_gameTimeMove)
	{
		auto& move = *g_gameTimeMove;
		move.elapsed += seconds;
		const float t = move.seconds > 0.0f ? std::min(move.elapsed / move.seconds, 1.0f) : 1.0f;
		// Evenly in the sky's hours, as the original keeps its clock (GLandAlignement's time is fn_0086A110's).
		const float day = std::floor(move.from / 24.0f) * 24.0f;
		const float fromSky = SkyHour(std::fmod(move.from, 24.0f)) + day;
		const float toDay = std::floor(move.to / 24.0f) * 24.0f;
		const float toSky = SkyHour(std::fmod(move.to, 24.0f)) + toDay;
		const float sky = glm::mix(fromSky, toSky, t);
		const float skyDay = std::floor(sky / 24.0f) * 24.0f;
		ApplyGameHour(GameHourOfSky(sky - skyDay) + skyDay);
		if (t >= 1.0f)
		{
			g_gameTimeMove.reset();
		}
	}

	// Town centres' middle piece: the post with the god's hand and orb (BuildingVillageCentreSpellHand), its own
	// model on the centre's base (Mike's photos of the original). Made here, after the land is loaded - not with the
	// centre: a new object then shifted the numbers of all made after it, and saves (which keep objects by number)
	// pointed at the wrong things (2026-10-18: the food lesson's store became another building).
	{
		static std::unordered_set<uint32_t> s_withHand;
		auto& registry = Locator::entitiesRegistry::value();
		std::vector<std::pair<uint32_t, Transform>> centres;
		registry.Each<const openblack::ecs::components::Abode, const Transform>(
		    [&](entt::entity entity, const openblack::ecs::components::Abode& abode, const Transform& transform) {
			    if (abode.type == AbodeNumber::TownCentre && !s_withHand.contains(static_cast<uint32_t>(entity)))
			    {
				    centres.emplace_back(static_cast<uint32_t>(entity), transform);
			    }
		    });
		for (const auto& [id, transform] : centres)
		{
			s_withHand.insert(id);
			// On a number of its own, high up (entities' index is 20 bits): taking the next free one, it took one a
			// script's object saved under, which a save point could then not remake.
			static uint32_t s_next = 0xFFF00;
			auto hand = registry.Create(static_cast<entt::entity>(s_next++));
			if (static_cast<uint32_t>(hand) < 0xFFF00)
			{
				SPDLOG_LOGGER_WARN(spdlog::get("game"), "Town centre {}: its hand got number {}, not one of its own", id,
				                   static_cast<uint32_t>(hand));
			}
			registry.Assign<Transform>(hand, transform.position, transform.rotation, transform.scale);
			registry.Assign<openblack::ecs::components::Mesh>(hand, resources::HashIdentifier(MeshId::BuildingVillageCentreSpellHand),
			                                                  static_cast<int8_t>(0), static_cast<int8_t>(0));
		}
	}
	UpdateAnimalLife(seconds);
	UpdateAnimations(seconds);

	// Test hook: OPENBLACK_TEST_SCROLLS=1 puts a question, a silver and a gold scroll in front of the camera.
	{
		static bool made = false;
		static const auto scrollsStart = std::chrono::steady_clock::now();
		if (!made && std::getenv("OPENBLACK_TEST_SCROLLS") != nullptr &&
		    std::chrono::steady_clock::now() - scrollsStart > std::chrono::seconds(8))
		{
			made = true;
			auto& camera = Locator::camera::value();
			const auto ahead = camera.GetOrigin() + camera.GetForward() * 25.0f;
			const auto right = glm::normalize(glm::cross(camera.GetForward(), glm::vec3(0.0f, 1.0f, 0.0f)));
			for (int type = 1; type <= 3; ++type)
			{
				CreateScriptObject(ObjectType::Highlight, static_cast<uint32_t>(type),
				                   ahead + right * (static_cast<float>(type - 2) * 8.0f), 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
			}
		}
	}
	// Test hook: OPENBLACK_TEST_ENTER_TEMPLE=<seconds> goes into the Temple that long into the run.
	{
		static const float enterAfter = std::getenv("OPENBLACK_TEST_ENTER_TEMPLE") != nullptr
		                                    ? static_cast<float>(std::atof(std::getenv("OPENBLACK_TEST_ENTER_TEMPLE")))
		                                    : -1.0f;
		static const auto start = std::chrono::steady_clock::now();
		static bool entered = false;
		const float since = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
		if (!entered && enterAfter >= 0.0f && since > enterAfter)
		{
			entered = true;
			EnterTemple();
		}
		// ...and OPENBLACK_TEST_TEMPLE_ROOM=<n> long-presses room n's doorway on screen once it's in the main room.
		static const int pressRoom =
		    std::getenv("OPENBLACK_TEST_TEMPLE_ROOM") != nullptr ? std::atoi(std::getenv("OPENBLACK_TEST_TEMPLE_ROOM")) : -1;
		static bool pressed = false;
		if (entered && !pressed && pressRoom > 0 && since > enterAfter + 8.0f && static_cast<size_t>(pressRoom) < g_templeRooms.size())
		{
			pressed = true;
			auto& camera = Locator::camera::value();
			const auto door = g_templeRooms[static_cast<size_t>(pressRoom)].frames.front().first;
			LookAlong(camera.GetOrigin(), door); // turn to face it first, as a player would
			UpdateTemple(std::chrono::microseconds(0));
			camera.Update(std::chrono::microseconds(0));
			glm::vec3 onScreen;
			camera.ProjectWorldToScreen(door, glm::vec4(0.0f, 0.0f, 1.0f, 1.0f), onScreen);
			SPDLOG_LOGGER_INFO(spdlog::get("game"), "Test: long press on the {} doorway at ({:.2f}, {:.2f})",
			                   g_templeRooms[static_cast<size_t>(pressRoom)].name, onScreen.x, 1.0f - onScreen.y);
			TempleLongPress(glm::vec2(onScreen.x, 1.0f - onScreen.y));
		}
	}
	// Test hooks for the save points: OPENBLACK_TEST_SAVE_AT_TEXT=<line> saves point 1 when that line shows,
	// OPENBLACK_TEST_LOAD_AFTER=<seconds> loads point 1 that long into the run.
	{
		static const int saveAtText = std::getenv("OPENBLACK_TEST_SAVE_AT_TEXT") != nullptr ? std::atoi(std::getenv("OPENBLACK_TEST_SAVE_AT_TEXT")) : -1;
		static const float loadAfter = std::getenv("OPENBLACK_TEST_LOAD_AFTER") != nullptr ? static_cast<float>(std::atof(std::getenv("OPENBLACK_TEST_LOAD_AFTER"))) : -1.0f;
		static bool saved = false, loaded = false;
		static const auto start = std::chrono::steady_clock::now();
		if (!saved && saveAtText >= 0 && g_lastTextId == saveAtText)
		{
			saved = SaveTestPoint(1);
		}
		if (!loaded && loadAfter >= 0.0f && std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count() > loadAfter)
		{
			loaded = true;
			LoadTestPoint(1);
		}
	}

	// The player's own camera movement, for the GET_TOTAL_EVENTS the control tutorials watch.
	{
		static bool s_hasLast = false;
		static float s_lastYaw = 0.0f, s_lastPitch = 0.0f, s_lastDistance = 0.0f;
		const auto& camera = Locator::camera::value();
		const auto view = camera.GetFocus() - camera.GetOrigin();
		const float yaw = glm::degrees(std::atan2(view.x, view.z));
		const float pitch = glm::degrees(std::atan2(view.y, glm::length(glm::vec2(view.x, view.z))));
		const float distance = glm::length(view);
		const bool player = !camera.IsScriptControlled() && !HandDemoPlaying();
		if (s_hasLast && player)
		{
			float turned = std::abs(yaw - s_lastYaw);
			turned = std::min(turned, 360.0f - turned);
			g_totalEvents[25] += turned;
			g_totalEvents[28] += std::abs(pitch - s_lastPitch);
			g_totalEvents[29] += std::abs(distance - s_lastDistance);
		}
		s_lastYaw = yaw;
		s_lastPitch = pitch;
		s_lastDistance = distance;
		s_hasLast = true;
	}
	// Test hook: OPENBLACK_TEST_FOLLOW=1 keeps the camera close behind the lead of a flock (the Land 1 family's
	// father), as a player following them would, so the follow sequence can be checked without input.
	static const bool s_testFollow = std::getenv("OPENBLACK_TEST_FOLLOW") != nullptr;
	static bool s_testFollowDone = false;
	s_testFollowDone = s_testFollowDone || g_lastTextId == 6101; // "Let's move to the Temple": the chase is over
	// ...at 'Now you have a go' for pitching (5114), hold the tilt key (A) for 3 s as a player would...
	if (s_testFollow && s_testFollowDone)
	{
		static int s_tiltState = 0; // 0 waiting, 1 held, 2 done
		static auto s_tiltStart = std::chrono::steady_clock::now();
		auto sendKey = [](bool down) {
			SDL_Event e {};
			e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
			e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
			e.key.keysym.scancode = SDL_SCANCODE_A;
			e.key.keysym.sym = SDL_GetKeyFromScancode(SDL_SCANCODE_A);
			SDL_PushEvent(&e);
		};
		if (s_tiltState == 0 && g_lastTextId == 5114 && !Locator::camera::value().IsScriptControlled() && !HandDemoPlaying())
		{
			sendKey(true);
			s_tiltState = 1;
			s_tiltStart = std::chrono::steady_clock::now();
		}
		else if (s_tiltState == 1 && std::chrono::steady_clock::now() - s_tiltStart > std::chrono::seconds(3))
		{
			sendKey(false);
			s_tiltState = 2;
		}
	}
	// ...zoom out and back in when asked to try zooming (5695)...
	if (s_testFollow && s_testFollowDone)
	{
		static int s_zoomState = 0; // 0 waiting, 1 out, 2 in, 3 done
		static auto s_zoomStart = std::chrono::steady_clock::now();
		const auto zoomFor = std::chrono::steady_clock::now() - s_zoomStart;
		if (s_zoomState == 0 && g_lastTextId == 5695 && !Locator::camera::value().IsScriptControlled() && !HandDemoPlaying())
		{
			s_zoomState = 1;
			s_zoomStart = std::chrono::steady_clock::now();
		}
		else if (s_zoomState == 1 && zoomFor > std::chrono::seconds(2))
		{
			s_zoomState = 2;
		}
		else if (s_zoomState == 2 && zoomFor > std::chrono::seconds(4))
		{
			s_zoomState = 3;
		}
		openblack::g_heldZoomInput = s_zoomState == 1 ? 1.0f : s_zoomState == 2 ? -1.0f : 0.0f;
	}
	// ...then, once asked to look around (5109), orbit the camera round its focus as a player would.
	if (s_testFollow && s_testFollowDone && g_lastTextId == 5109 && !Locator::camera::value().IsScriptControlled())
	{
		auto& camera = Locator::camera::value();
		const auto focus = camera.GetFocus();
		const auto arm = camera.GetOrigin() - focus;
		const float turn = glm::radians(30.0f) * seconds;
		const auto rotated = glm::vec3(arm.x * std::cos(turn) - arm.z * std::sin(turn), arm.y,
		                               arm.x * std::sin(turn) + arm.z * std::cos(turn));
		camera.SetOrigin(focus + rotated);
		camera.SetFocus(focus);
	}
	if (s_testFollow && !s_testFollowDone)
	{
		for (const auto& [flockId, flock] : g_flocks)
		{
			const auto leader = static_cast<entt::entity>(flock.leader);
			if (flock.leader == 0 || !registry.Valid(leader) || !registry.AllOf<Transform>(leader))
			{
				continue;
			}
			const auto at = registry.Get<Transform>(leader).position;
			auto& camera = Locator::camera::value();
			camera.SetOrigin(at + glm::vec3(-9.0f, 9.0f, -9.0f)); // ~16 units: close enough that they run
			camera.SetFocus(at);
			break;
		}
	}
	// Poses and positions changed this frame: the renderer only rebuilds its draw lists for a dirty registry.
	registry.SetDirty();
	if (!g_pendingShots.empty() && std::chrono::steady_clock::now() >= g_pendingShots.front().second)
	{
		const auto& camera = Locator::camera::value();
		SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "Test screenshot {}: camera at ({:.1f}, {:.1f}, {:.1f}) looking at ({:.1f}, {:.1f}, {:.1f})",
		                   g_pendingShots.front().first, camera.GetOrigin().x, camera.GetOrigin().y, camera.GetOrigin().z,
		                   camera.GetFocus().x, camera.GetFocus().y, camera.GetFocus().z);
		Game::Instance()->RequestScreenshot(g_pendingShots.front().first); // one per frame
		g_pendingShots.erase(g_pendingShots.begin());
	}

	// Thrown and dropped things: ballistic until they reach the ground, then back into the physics world.
// People and animals fly in their thrown pose (Mike, 2026-10-17: "flying" mode until they hit the ground), and are
// hurt by the landing as hard as they hit: nothing under k_SafeLanding (a fall of ~7), dead by k_DeadlyLanding.
// Thresholds chosen, not the original's (its landing damage isn't decoded).
	for (auto it = g_flying.begin(); it != g_flying.end();)
	{
		const auto entity = static_cast<entt::entity>(it->first);
		auto* transform = registry.Valid(entity) ? registry.TryGet<Transform>(entity) : nullptr;
		if (transform == nullptr || it->first == g_heldObject)
		{
			it = g_flying.erase(it);
			continue;
		}
		auto& velocity = it->second;
		velocity.y -= k_Gravity * seconds;
		velocity -= AirDrag(entity) * glm::length(velocity) * velocity * seconds;
		auto position = transform->position + velocity * seconds;
		const float ground = SeabedAt(glm::vec2(position.x, position.z)); // the sea's bed out at sea
		const auto pedestal = IsGateTotem(entity) ? PedestalUnder(position) : std::nullopt;
		if (pedestal && position.y <= pedestal->top && transform->position.y >= pedestal->top - 1.0f &&
		    SetOnPedestal(entity, position))
		{
			it = g_flying.erase(it); // thrown or dropped onto the pedestal
			continue;
		}
		// Into a building: it takes the blow (a house hit hard enough falls), and what hit it glances off it.
		if (const auto hit = BuildingAt(position, entity); hit && glm::length(velocity) > 1.0f)
		{
			HitBuilding(*hit, glm::length(velocity), it->first);
			PlayThrowSound(ThrowSound::Roof, position);
			const auto& wall = registry.Get<Transform>(*hit).position;
			auto away = glm::vec2(position.x - wall.x, position.z - wall.z);
			away = glm::length(away) > 1e-3f ? glm::normalize(away) : glm::vec2(1.0f, 0.0f);
			auto flat = glm::vec2(velocity.x, velocity.z);
			flat = glm::dot(flat, away) < 0.0f ? (flat - 2.0f * glm::dot(flat, away) * away) * 0.5f : flat * 0.5f;
			velocity = glm::vec3(flat.x, std::min(velocity.y, 0.0f), flat.y);
			position = transform->position; // back out of the wall
		}
		// Into something else on the land (a person, an animal, a rock): it knocks it flying and is knocked aside.
		{
			auto& struck = g_lastStruck[it->first];
			struck.second = std::max(struck.second - seconds, 0.0f);
			const float speed = glm::length(velocity);
			if (speed > 6.0f)
			{
				const auto& meshes = Locator::resources::value().GetMeshes();
				std::optional<entt::entity> other;
				registry.Each<const openblack::ecs::components::Mobile, const Transform>(
				    [&](entt::entity thing, const openblack::ecs::components::Mobile&, const Transform& where) {
					    const auto id = static_cast<uint32_t>(thing);
					    if (other || thing == entity || id == g_heldObject || (struck.first == id && struck.second > 0.0f))
					    {
						    return;
					    }
					    const auto* mesh = registry.TryGet<openblack::ecs::components::Mesh>(thing);
					    const auto box = mesh != nullptr && meshes.Contains(mesh->id) ? meshes.Handle(mesh->id)->GetBoundingBox()
					                                                                  : openblack::AxisAlignedBoundingBox {};
					    const float reach = std::max(0.5f * std::max(box.Size().x, box.Size().z) * where.scale.x, 0.6f);
					    const float top = where.position.y + std::max(box.maxima.y * where.scale.y, 1.0f);
					    if (glm::distance(glm::vec2(where.position.x, where.position.z), glm::vec2(position.x, position.z)) < reach &&
					        position.y < top && position.y > where.position.y - 0.5f)
					    {
						    other = thing;
					    }
				    });
				if (other)
				{
					const auto otherId = static_cast<uint32_t>(*other);
					struck = {otherId, 0.4f};
					g_lastStruck[otherId] = {it->first, 0.4f};
					// The struck goes off with most of the blow, a little up; the thrower glances away with the rest.
					const bool big = registry.AllOf<openblack::ecs::components::Creature>(*other);
					if (!big && !g_flying.contains(otherId))
					{
						const auto killed = HurtByImpact(*other, speed * 0.6f);
						g_flying[otherId] = glm::vec3(velocity.x * 0.6f, std::abs(velocity.y) * 0.3f + 4.0f, velocity.z * 0.6f);
						SPDLOG_LOGGER_INFO(spdlog::get("game"), "Thrown {} struck {} at {:.1f} units/s{}", it->first, otherId, speed,
						                   killed ? ", killing it" : "");
					}
					HurtByImpact(entity, speed * 0.4f);
					PlayImpactSound(*other, position, speed);
					const auto& at = registry.Get<Transform>(*other).position;
					auto away = glm::vec2(position.x - at.x, position.z - at.z);
					away = glm::length(away) > 1e-3f ? glm::normalize(away) : glm::vec2(-velocity.x, -velocity.z) / std::max(speed, 1e-3f);
					const float keep = big ? 0.5f : 0.35f;
					velocity = glm::vec3(away.x * speed * keep, velocity.y * keep, away.y * speed * keep);
				}
			}
		}
		// The sea is the plane at 0, over its bed: what goes into it stops at the surface.
		const bool sea = SeaDepthAt(glm::vec2(position.x, position.z)) > 0.0f;
		const float surface = sea ? 0.0f : ground;
		const bool landed = position.y <= surface;
		if (landed)
		{
			position.y = surface;
		}
		// Hard on the land, it bounces (a few times at most), hurt each time.
		if (landed && !sea && -velocity.y > 8.0f && g_bounces[it->first] < 4)
		{
			const float impact = glm::length(velocity);
			++g_bounces[it->first];
			const bool killed = HurtByImpact(entity, impact);
			PlayImpactSound(entity, position, impact);
			velocity = glm::vec3(velocity.x * 0.6f, -velocity.y * 0.45f, velocity.z * 0.6f);
			position.y = ground + 0.05f;
			transform->position = position;
			MoveBody(entity, position);
			SPDLOG_LOGGER_INFO(spdlog::get("game"), "Thrown {} bounced at {:.1f} units/s ({}){}", it->first, impact, g_bounces[it->first],
			                   killed ? ", killed by it" : "");
			if (!killed)
			{
				++it;
				continue;
			}
		}
		transform->position = position;
		MoveBody(entity, position);
		if (const auto spin = g_spin.find(it->first); spin != g_spin.end() && glm::length(spin->second) > 0.0f)
		{
			transform->rotation = glm::mat3(glm::rotate(glm::mat4(1.0f), glm::length(spin->second) * seconds, glm::normalize(spin->second))) *
			                      transform->rotation;
		}
		if (!landed)
		{
			ShowThrown(entity, true, 0.0f);
			++it;
			continue;
		}
		g_spin.erase(it->first);
		g_bounces.erase(it->first);
		g_lastStruck.erase(it->first);
		// A tree or fence landing on a village store is shredded into its wood; a tree anywhere else lies where it fell.
		if (!sea && ShredWood(entity) && ShreddingAt(position))
		{
			it = g_flying.erase(it);
			ShredIntoStore(entity, position);
			continue;
		}
		if (!sea && registry.AllOf<openblack::ecs::components::Tree>(entity))
		{
			LayTreeDown(entity, position, velocity);
		}
		if (sea)
		{
			PlayThrowSound(ThrowSound::Splash, position);
		}
		else if (glm::length(velocity) > 3.0f && !registry.AllOf<openblack::ecs::components::Tree>(entity)) // a tree: LayTreeDown's
		{
			PlayImpactSound(entity, position, glm::length(velocity));
		}
		const bool living = registry.AnyOf<openblack::ecs::components::Villager, openblack::ecs::components::Animal>(entity);
		if (sea && !living && !registry.AllOf<openblack::ecs::components::Creature>(entity))
		{
			g_sinking[it->first] = 0.0f; // a rock: down to the bed
			SPDLOG_LOGGER_INFO(spdlog::get("game"), "{} went into the sea ({:.1f} deep)", it->first, -ground);
			it = g_flying.erase(it);
			continue;
		}
		if (sea && living)
		{
			// In the water: no landing hurt; in a water cell it starts to drown (Villager::HasSunk).
			if (IsWaterAt(glm::vec2(position.x, position.z)))
			{
				g_inDeepWater.try_emplace(it->first, 0.0f);
				SPDLOG_LOGGER_INFO(spdlog::get("game"), "{} is in the sea out of its depth ({:.1f} deep)", it->first, -ground);
			}
			else
			{
				position.y = ground; // stands in the shallows
				transform->position = position;
				ShowThrown(entity, false, 0.0f);
			}
		}
		else
		{
			ShowThrown(entity, false, glm::length(velocity));
		}
		if (auto* body = registry.TryGet<openblack::ecs::components::RigidBody>(entity))
		{
			Locator::dynamicsSystem::value().AddRigidBody(&body->handle);
		}
		if (auto* move = registry.TryGet<ScriptMove>(entity))
		{
			move->target = position;
		}
		if (auto* wallHug = registry.TryGet<WallHug>(entity))
		{
			wallHug->goal = glm::vec2(position.x, position.z);
			registry.Remove<MoveStateLinearTag, MoveStateOrbitTag, MoveStateExitCircleTag, MoveStateStepThroughTag,
			                MoveStateFinalStepTag, MoveStateArrivedTag, WallHugObjectReference>(entity);
		}
		SPDLOG_LOGGER_INFO(spdlog::get("game"), "Landed {} at ({:.1f}, {:.1f}, {:.1f})", it->first, position.x, position.y,
		                   position.z);
		it = g_flying.erase(it);
	}

	// Out of their depth: treading water (people), then drowned - a body that floats - unless taken out first.
	for (auto it = g_inDeepWater.begin(); it != g_inDeepWater.end();)
	{
		const auto entity = static_cast<entt::entity>(it->first);
		auto* transform = registry.Valid(entity) ? registry.TryGet<Transform>(entity) : nullptr;
		const float ground = transform != nullptr ? SeabedAt(glm::vec2(transform->position.x, transform->position.z)) : 0.0f;
		if (transform == nullptr || it->first == g_heldObject || g_flying.contains(it->first) ||
		    !IsWaterAt(glm::vec2(transform->position.x, transform->position.z)))
		{
			if (transform != nullptr && it->first != g_heldObject && !g_flying.contains(it->first))
			{
				g_scriptAnimations.erase(it->first); // out of its depth no longer
			}
			it = g_inDeepWater.erase(it);
			continue;
		}
		const bool person = registry.AllOf<openblack::ecs::components::Villager>(entity);
		it->second += seconds;
		transform->position.y = 0.0f; // at the surface
		if (person && !g_fallenDead.contains(it->first))
		{
			HoldForScript(entity, transform->position);
			g_scriptAnimations[it->first] = {static_cast<uint32_t>(AnimId::PDrowning), std::numeric_limits<float>::max()};
		}
		else if (!person && !g_fallenDead.contains(it->first) && AnimalThrownAnimations(entity))
		{
			if (auto* animated = registry.TryGet<openblack::ecs::components::Animated>(entity);
			    animated != nullptr && registry.Get<openblack::ecs::components::Mesh>(entity).id ==
			                               resources::HashIdentifier(MeshId::AnimalCow1))
			{
				animated->animation = static_cast<uint32_t>(AnimId::ACowDrowning);
			}
		}
		const float limit = person ? k_PersonDrownSeconds : k_AnimalDrownSeconds;
		if (it->second >= limit && !g_fallenDead.contains(it->first))
		{
			g_fallenDead.insert(it->first);
			if (auto* villager = registry.TryGet<openblack::ecs::components::Villager>(entity))
			{
				villager->life = 0.0f;
				g_scriptAnimations[it->first] = {static_cast<uint32_t>(AnimId::PDeadDrowned), std::numeric_limits<float>::max()};
			}
			else
			{
				g_animalHealth[it->first] = 0.0f;
			}
			SPDLOG_LOGGER_INFO(spdlog::get("game"), "{} drowned", it->first);
		}
		++it;
	}
	// Into the sea: down to its bed over a couple of seconds; then the script's (a quest's stone) lie there, the rest
	// are gone.
	{
		std::unordered_set<uint32_t> scripted;
		bool haveScripted = false;
		for (auto it = g_sinking.begin(); it != g_sinking.end();)
		{
			const auto entity = static_cast<entt::entity>(it->first);
			auto* transform = registry.Valid(entity) ? registry.TryGet<Transform>(entity) : nullptr;
			if (transform == nullptr || it->first == g_heldObject || g_flying.contains(it->first))
			{
				it = g_sinking.erase(it);
				continue;
			}
			it->second += seconds;
			const float bed = SeabedAt(glm::vec2(transform->position.x, transform->position.z));
			transform->position.y = glm::mix(0.0f, bed, std::min(it->second / 2.0f, 1.0f));
			MoveBody(entity, transform->position);
			if (it->second < 2.0f)
			{
				++it;
				continue;
			}
			if (!haveScripted)
			{
				scripted = ScriptedObjects();
				haveScripted = true;
			}
			if (!scripted.contains(it->first) && !g_scriptEntities.contains(it->first) && !g_scriptPlaced.contains(it->first))
			{
				SPDLOG_LOGGER_INFO(spdlog::get("game"), "{} sank and is gone", it->first);
				ForgetObject(it->first);
				registry.Destroy(entity);
			}
			it = g_sinking.erase(it);
		}
	}

	for (auto it = g_scriptMoves.begin(); it != g_scriptMoves.end();)
	{
		if (it->first == g_heldObject)
		{
			++it; // in the hand: the script's move waits till it is let go of
			continue;
		}
		const auto entity = static_cast<entt::entity>(it->first);
		auto* transform = registry.Valid(entity) ? registry.TryGet<Transform>(entity) : nullptr;
		if (transform == nullptr)
		{
			it = g_scriptMoves.erase(it);
			continue;
		}
		const auto toTarget = glm::vec2(it->second.x - transform->position.x, it->second.z - transform->position.z);
		const float distance = glm::length(toTarget);
		const auto speed = ScriptSpeed(it->first);
		const float step = (speed && *speed > 0.0f ? *speed : FullSpeed(it->first) * 0.4f) * seconds;
		g_moverSpeeds[it->first] = {distance <= step ? 0.0f : step / std::max(seconds, 1e-4f), g_scriptFrame};
		if (distance <= step)
		{
			PlaceMover(entity, *transform, it->second, it->second);
			transform->position = it->second; // exactly where the script said
			SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "MOVE_GAME_THING {} arrived", it->first);
			g_moveAnimations.erase(it->first);
			it = g_scriptMoves.erase(it);
			continue;
		}
		PlaceMover(entity, *transform, transform->position + glm::vec3(toTarget.x, 0.0f, toTarget.y) / distance * step,
		           it->second);
		++it;
	}

	for (auto it = g_followers.begin(); it != g_followers.end();)
	{
		if (it->first == g_heldObject)
		{
			++it; // in the hand: the script's move waits till it is let go of
			continue;
		}
		const auto entity = static_cast<entt::entity>(it->first);
		auto* transform = registry.Valid(entity) ? registry.TryGet<Transform>(entity) : nullptr;
		if (transform == nullptr)
		{
			it = g_followers.erase(it);
			continue;
		}
		const auto toTarget = glm::vec2(it->second.x - transform->position.x, it->second.z - transform->position.z);
		const float distance = glm::length(toTarget);
		// Close in at up to a run, faster when far behind so a member never loses the leader.
		const float speed = FullSpeed(it->first) * std::clamp(distance / 4.0f, 0.0f, 1.0f);
		g_moverSpeeds[it->first] = {distance > 0.05f ? speed : 0.0f, g_scriptFrame};
		if (distance > 0.05f)
		{
			const float step = std::min(distance, speed * seconds);
			const auto position = transform->position + glm::vec3(toTarget.x, 0.0f, toTarget.y) / distance * step;
			PlaceMover(entity, *transform, position, it->second);
		}
		++it;
	}
}

void FocusAndPositionFollow() // 178 FOCUS_AND_POSITION_FOLLOW
{
	[[maybe_unused]] const auto unk1 = Pop().intVal;
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void GetWalkPathPercentage() // 179 GET_WALK_PATH_PERCENTAGE
{
	[[maybe_unused]] const auto object = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushf(0.0f);
}

void CameraProperties() // 180 CAMERA_PROPERTIES
{
	// The follow's distance, zoom time scale (0: kept up with at once), heading in degrees and whether that is from
	// behind a walker (runblack.exe 0x6edff0 sets CameraModeFollow's ViewingDistance, ZoomTimeScale, Heading,
	// RelativeHeading).
	const auto enableBehind = static_cast<bool>(Pop().intVal);
	const auto angle = Popf();
	const auto speed = Popf();
	const auto distance = Popf();
	g_cameraFollow.distance = distance;
	g_cameraFollow.zoomTimeScale = speed;
	g_cameraFollow.heading = glm::radians(angle);
	g_cameraFollow.relativeHeading = enableBehind;
	SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "CAMERA_PROPERTIES distance {:.1f}, speed {:.2f}, heading {:.1f}, behind {}", distance,
	                   speed, angle, enableBehind);
}

void EnableDisableMusic() // 181 ENABLE_DISABLE_MUSIC
{
	[[maybe_unused]] const auto unk1 = Pop().intVal;
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void GetMusicObjDistance() // 182 GET_MUSIC_OBJ_DISTANCE
{
	[[maybe_unused]] const auto source = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushf(0.0f);
}

void GetMusicEnumDistance() // 183 GET_MUSIC_ENUM_DISTANCE
{
	[[maybe_unused]] const auto type = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushf(0.0f);
}

void SetMusicPlayPosition() // 184 SET_MUSIC_PLAY_POSITION
{
	[[maybe_unused]] const auto unk3 = Pop().intVal;
	[[maybe_unused]] const auto unk2 = Pop().intVal;
	[[maybe_unused]] const auto unk1 = Pop().intVal;
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void AttachObjectLeashToObject() // 185 ATTACH_OBJECT_LEASH_TO_OBJECT
{
	[[maybe_unused]] const auto unk1 = Pop().intVal;
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void AttachObjectLeashToHand() // 186 ATTACH_OBJECT_LEASH_TO_HAND
{
	// The script puts your creature on a leash itself (the leash type, or the regular one).
	const auto type = Pop().intVal;
	g_leash = {PlayersCreature(), type >= 2 && type <= 4 ? type : 2, 0};
}

void DetachObjectLeash() // 187 DETACH_OBJECT_LEASH
{
	const auto creature = Pop().uintVal;
	if (g_leash.creature == creature)
	{
		g_leash = {};
	}
}

void SetCreatureOnlyDesire() // 188 SET_CREATURE_ONLY_DESIRE
{
	[[maybe_unused]] const auto value = Popf();
	[[maybe_unused]] const auto desire = Pop().intVal;
	[[maybe_unused]] const auto creature = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void SetCreatureOnlyDesireOff() // 189 SET_CREATURE_ONLY_DESIRE_OFF
{
	[[maybe_unused]] const auto creature = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void RestartMusic() // 190 RESTART_MUSIC
{
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void MusicPlayed191() // 191 MUSIC_PLAYED
{
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushi(0);
}

void IsOfType() // 192 IS_OF_TYPE
{
	const auto subtype = Pop().intVal;
	const auto type = Pop().intVal;
	const auto object = Pop().uintVal;
	// By what the script made it as (subtype 5000 any). A sphere made in the hand (31) is a sphere (30) too: the town
	// lesson asks "is what the hand holds a ONE_SHOT_SPELL".
	bool is = false;
	if (const auto spell = g_oneShotSpells.find(object); spell != g_oneShotSpells.end())
	{
		is = (type == static_cast<int>(ObjectType::OneShotSpell) || type == static_cast<int>(ObjectType::OneShotSpellInHand)) &&
		     SubtypeMatches(subtype, spell->second);
	}
	else if (const auto record = g_scriptEntities.find(object); record != g_scriptEntities.end())
	{
		is = record->second.type == type && SubtypeMatches(subtype, static_cast<int>(record->second.subtype));
	}
	else
	{
		auto& registry = Locator::entitiesRegistry::value();
		const auto entity = static_cast<entt::entity>(object);
		if (object != 0 && registry.Valid(entity))
		{
			if (const auto* villager = registry.TryGet<openblack::ecs::components::Villager>(entity))
			{
				is = type == static_cast<int>(ObjectType::Villager) && SubtypeMatches(subtype, static_cast<int>(villager->number));
			}
			else if (const auto* animal = registry.TryGet<openblack::ecs::components::Animal>(entity))
			{
				is = type == static_cast<int>(ObjectType::Animal) && SubtypeMatches(subtype, static_cast<int>(animal->type));
			}
			else if (registry.AllOf<openblack::ecs::components::Creature>(entity))
			{
				is = type == static_cast<int>(ObjectType::Creature);
			}
		}
	}
	Pushb(is);
}

void ClearHitObject() // 193 CLEAR_HIT_OBJECT
{
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void GameThingHit() // 194 GAME_THING_HIT
{
	[[maybe_unused]] const auto object = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushb(false);
}

void SpellAtThing() // 195 SPELL_AT_THING
{
	[[maybe_unused]] const auto curl = Popf();
	[[maybe_unused]] const auto duration = Popf();
	[[maybe_unused]] const auto radius = Popf();
	[[maybe_unused]] const auto from = PopVec();
	[[maybe_unused]] const auto target = Pop().uintVal;
	[[maybe_unused]] const auto spell = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pusho(0);
}

void SpellAtPos() // 196 SPELL_AT_POS
{
	[[maybe_unused]] const auto curl = Popf();
	[[maybe_unused]] const auto duration = Popf();
	[[maybe_unused]] const auto radius = Popf();
	[[maybe_unused]] const auto from = PopVec();
	[[maybe_unused]] const auto target = PopVec();
	[[maybe_unused]] const auto spell = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pusho(0);
}

void CallPlayerCreature() // 197 CALL_PLAYER_CREATURE
{
	// The creature the player owns (CREATURE_SET_PLAYER gave it to them), or 0 before one is chosen. Land 1's
	// creature lessons ask this every turn: unanswered, none of them ever found the chosen creature. The scripts
	// number players from 1 (the lessons ask for 1.0, "you"); PlayerNames counts from 0.
	const auto player = static_cast<PlayerNames>(static_cast<int>(Popf()) - 1);
	uint32_t found = 0;
	if (player == PlayerNames::PLAYER_ONE)
	{
		found = PlayersCreature();
	}
	else
	{
		Locator::entitiesRegistry::value().Each<const openblack::ecs::components::Creature>(
		    [&found, player](entt::entity entity, const openblack::ecs::components::Creature& creature) {
			    if (found == 0 && creature.owner == player)
			    {
				    found = static_cast<uint32_t>(entity);
			    }
		    });
	}
	Pusho(found);
}

void GetSlowestSpeed() // 198 GET_SLOWEST_SPEED
{
	[[maybe_unused]] const auto flock = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushf(0.0f);
}

void GetObjectHeld199() // 199 GET_OBJECT_HELD
{
	// What the player's hand holds (0 if nothing): Land 1's LearnToEat waits for something held near your creature.
	Pusho(g_heldObject);
}

void HelpSystemOn() // 200 HELP_SYSTEM_ON
{
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushb(false);
}

void ShakeCamera() // 201 SHAKE_CAMERA
{
	[[maybe_unused]] const auto duration = Popf();
	[[maybe_unused]] const auto amplitude = Popf();
	[[maybe_unused]] const auto radius = Popf();
	[[maybe_unused]] const auto position = PopVec();
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void SetAnimationModify() // 202 SET_ANIMATION_MODIFY
{
	[[maybe_unused]] const auto creature = Pop().uintVal;
	[[maybe_unused]] const auto enable = static_cast<bool>(Pop().intVal);
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

// Sequence 1 is the original's intro movie, data\intro.bik (runblack.exe 0x68f450, the native at 0x6fc050): FollowUs
// plays it once the prayer has faded to black, before the sharks. Sequence 2 (the end of the game) goes
// elsewhere there and isn't done here. While it plays the game waits (Game.cpp); any button skips it.
void SetAviSequence() // 203 SET_AVI_SEQUENCE
{
	const auto aviSequence = Pop().intVal;
	const auto enable = static_cast<bool>(Pop().intVal);
	SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "SET_AVI_SEQUENCE {} {}", enable, aviSequence);
	if (!enable)
	{
		openblack::video::StopMovie();
		return;
	}
	if (aviSequence == 1)
	{
		auto& fs = Locator::filesystem::value();
		openblack::video::StartMovie(fs.GetPath<openblack::filesystem::Path::Data>(true) / "INTRO.bik");
	}
	// PSysGlobal::StartAVISequence: after the movie (or with none to play) the fade the script made for it is gone at
	// once, so the game comes back in full.
	SetupScreenFadeBackToNormal(0);
}

void PlayGesture() // 204 PLAY_GESTURE
{
	[[maybe_unused]] const auto unk4 = Pop().intVal;
	[[maybe_unused]] const auto unk3 = Pop().intVal;
	[[maybe_unused]] const auto unk2 = Pop().intVal;
	[[maybe_unused]] const auto unk1 = Pop().intVal;
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void DevFunction() // 205 DEV_FUNCTION
{
	[[maybe_unused]] const auto func = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void HasMouseWheel() // 206 HAS_MOUSE_WHEEL
{
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushb(false);
}

void NumMouseButtons() // 207 NUM_MOUSE_BUTTONS
{
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushf(0.0f);
}

void SetCreatureDevStage() // 208 SET_CREATURE_DEV_STAGE
{
	[[maybe_unused]] const auto stage = Pop().intVal;
	[[maybe_unused]] const auto creature = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void SetFixedCamRotation() // 209 SET_FIXED_CAM_ROTATION
{
	[[maybe_unused]] const auto unk3 = Pop().intVal;
	[[maybe_unused]] const auto unk2 = Pop().intVal;
	[[maybe_unused]] const auto unk1 = Pop().intVal;
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void SwapCreature() // 210 SWAP_CREATURE
{
	[[maybe_unused]] const auto toCreature = Pop().uintVal;
	[[maybe_unused]] const auto fromCreature = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void GetArena() // 211 GET_ARENA
{
	[[maybe_unused]] const auto unk4 = Pop().intVal;
	[[maybe_unused]] const auto unk3 = Pop().intVal;
	[[maybe_unused]] const auto unk2 = Pop().intVal;
	[[maybe_unused]] const auto unk1 = Pop().intVal;
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pusho(0);
}

void GetFootballPitch() // 212 GET_FOOTBALL_PITCH
{
	[[maybe_unused]] const auto town = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pusho(0);
}

void StopAllGames() // 213 STOP_ALL_GAMES
{
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void AttachToGame() // 214 ATTACH_TO_GAME
{
	[[maybe_unused]] const auto unk2 = Pop().intVal;
	[[maybe_unused]] const auto unk1 = Pop().intVal;
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void DetachFromGame() // 215 DETACH_FROM_GAME
{
	[[maybe_unused]] const auto unk2 = Pop().intVal;
	[[maybe_unused]] const auto unk1 = Pop().intVal;
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void DetachUndefinedFromGame() // 216 DETACH_UNDEFINED_FROM_GAME
{
	[[maybe_unused]] const auto unk1 = Pop().intVal;
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void SetOnlyForScripts() // 217 SET_ONLY_FOR_SCRIPTS
{
	[[maybe_unused]] const auto unk1 = Pop().intVal;
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void StartMatchWithReferee() // 218 START_MATCH_WITH_REFEREE
{
	[[maybe_unused]] const auto unk1 = Pop().intVal;
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void GameTeamSize() // 219 GAME_TEAM_SIZE
{
	[[maybe_unused]] const auto unk1 = Pop().intVal;
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void GameType() // 220 GAME_TYPE
{
	[[maybe_unused]] const auto object = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushi(0);
}

void GameSubType() // 221 GAME_SUB_TYPE
{
	[[maybe_unused]] const auto object = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushi(0);
}

void IsLeashed() // 222 IS_LEASHED
{
	const auto object = Pop().uintVal;
	Pushb(object != 0 && g_leash.creature == object && g_leash.type != 0);
}

void SetCreatureHome() // 223 SET_CREATURE_HOME
{
	// Where it goes back to (its pen): kept for when creatures move about on their own; nothing reads it yet.
	const auto position = PopVec();
	const auto creature = Pop().uintVal;
	g_creatureHomes[creature] = position;
	SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "SET_CREATURE_HOME: creature {} at ({:.1f}, {:.1f}, {:.1f})", creature,
	                   position.x, position.y, position.z);
}

void GetHitObject() // 224 GET_HIT_OBJECT
{
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pusho(0);
}

void GetObjectWhichHit() // 225 GET_OBJECT_WHICH_HIT
{
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pusho(0);
}

void GetNearestTownOfPlayer() // 226 GET_NEAREST_TOWN_OF_PLAYER
{
	[[maybe_unused]] const auto unk4 = Pop().intVal;
	[[maybe_unused]] const auto unk3 = Pop().intVal;
	[[maybe_unused]] const auto unk2 = Pop().intVal;
	[[maybe_unused]] const auto unk1 = Pop().intVal;
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pusho(0);
}

void SpellAtPoint() // 227 SPELL_AT_POINT
{
	[[maybe_unused]] const auto radius = Popf();
	[[maybe_unused]] const auto position = PopVec();
	[[maybe_unused]] const auto spell = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pusho(0);
}

void SetAttackOwnTown() // 228 SET_ATTACK_OWN_TOWN
{
	[[maybe_unused]] const auto unk1 = Pop().intVal;
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void IsFighting() // 229 IS_FIGHTING
{
	const auto object = Pop().uintVal;
	Pushb(g_fighters.contains(object));
}

void SetMagicRadius() // 230 SET_MAGIC_RADIUS
{
	[[maybe_unused]] const auto radius = Popf();
	[[maybe_unused]] const auto object = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void TempTextWithNumber() // 231 TEMP_TEXT_WITH_NUMBER
{
	[[maybe_unused]] const auto withInteraction = Pop().intVal;
	[[maybe_unused]] const auto value = Popf();
	[[maybe_unused]] const auto format = PopString();
	[[maybe_unused]] const auto singleLine = static_cast<bool>(Pop().intVal);
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void RunTextWithNumber() // 232 RUN_TEXT_WITH_NUMBER
{
	[[maybe_unused]] const auto withInteraction = Pop().intVal;
	[[maybe_unused]] const auto number = Popf();
	[[maybe_unused]] const auto string = Pop().intVal;
	[[maybe_unused]] const auto singleLine = static_cast<bool>(Pop().intVal);
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void CreatureSpellReversion() // 233 CREATURE_SPELL_REVERSION
{
	[[maybe_unused]] const auto unk1 = Pop().intVal;
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void GetDesire() // 234 GET_DESIRE
{
	[[maybe_unused]] const auto unk1 = Pop().intVal;
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushf(0.0f);
}

void GetEventsPerSecond() // 235 GET_EVENTS_PER_SECOND
{
	[[maybe_unused]] const auto type = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushf(0.0f);
}

void GetTimeSince() // 236 GET_TIME_SINCE
{
	[[maybe_unused]] const auto type = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushf(0.0f);
}

void GetTotalEvents() // 237 GET_TOTAL_EVENTS
{
	const auto type = Pop().intVal;
	const auto it = g_totalEvents.find(type);
	Pushf(it != g_totalEvents.end() ? it->second : 0.0f);
}

void UpdateSnapshot() // 238 UPDATE_SNAPSHOT
{
	[[maybe_unused]] const auto challengeId = Pop().intVal;
	[[maybe_unused]] const auto argc = Pop().intVal;
	[[maybe_unused]] const auto argv = PopVarArg(argc);
	[[maybe_unused]] const auto reminderScript = PopString();
	[[maybe_unused]] const auto titleStrID = Pop().intVal;
	[[maybe_unused]] const auto alignment = Popf();
	[[maybe_unused]] const auto success = Popf();
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

namespace
{
// A reward chest of `reward` at `position` for `town` (0: the nearest), falling from the sky if asked (Reward::Create).
uint32_t MakeReward(int reward, const glm::vec3& position, uint32_t town, bool fromSky)
{
	const auto entity = CreateScriptObject(ObjectType::Reward, static_cast<uint32_t>(std::max(reward, 0)), position, 0.0f, 0.0f,
	                                       Locator::rng::value().NextValue(0.0f, glm::two_pi<float>()), 0.0f, 1.0f);
	const auto id = static_cast<uint32_t>(entity);
	if (auto chest = g_rewards.find(id); chest != g_rewards.end())
	{
		chest->second.town = town;
		if (fromSky)
		{
			constexpr float k_SkyHeight = 40.0f;
			chest->second.falling = k_SkyHeight;
			Locator::entitiesRegistry::value().Get<Transform>(entity).position.y += k_SkyHeight;
		}
	}
	return id;
}
} // namespace

void CreateReward() // 239 CREATE_REWARD
{
	const auto fromSky = static_cast<bool>(Pop().intVal);
	const auto position = PopVec();
	const auto reward = Pop().intVal;
	Pusho(MakeReward(reward, position, 0, fromSky));
}

void CreateRewardInTown() // 240 CREATE_REWARD_IN_TOWN
{
	const auto fromSky = static_cast<bool>(Pop().intVal);
	const auto position = PopVec();
	const auto town = Pop().uintVal;
	const auto reward = Pop().intVal;
	Pusho(MakeReward(reward, position, town, fromSky));
}

void SetFade() // 241 SET_FADE
{
	const auto time = Popf();
	const auto blue = Popf();
	const auto green = Popf();
	const auto red = Popf();
	SetupScreenFadeTo(static_cast<uint8_t>(red), static_cast<uint8_t>(green), static_cast<uint8_t>(blue), static_cast<int>(time));
}

void SetFadeIn() // 242 SET_FADE_IN
{
	const auto duration = Popf();
	SetupScreenFadeBackToNormal(static_cast<int>(duration));
}

void FadeFinished() // 243 FADE_FINISHED
{
	Pushb(g_screenFade.rate == 0.0f); // fn_006EBB50
}

void SetPlayerMagic() // 244 SET_PLAYER_MAGIC
{
	[[maybe_unused]] const auto unk2 = Pop().intVal;
	[[maybe_unused]] const auto unk1 = Pop().intVal;
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void HasPlayerMagic() // 245 HAS_PLAYER_MAGIC
{
	[[maybe_unused]] const auto player = Popf();
	[[maybe_unused]] const auto spell = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushb(false);
}

void SpiritSpeaks() // 246 SPIRIT_SPEAKS
{
	[[maybe_unused]] const auto textID = Pop().intVal;
	[[maybe_unused]] const auto spirit = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushb(false);
}

void BeliefForPlayer() // 247 BELIEF_FOR_PLAYER
{
	const auto player = static_cast<int>(Popf());
	const auto object = Pop().uintVal;
	Pushf(TownBelief(object, player));
}

void GetHelp() // 248 GET_HELP
{
	const auto object = Pop().uintVal;
	// A reward's help text (its info.dat helpTextEnum, said by displayrewardhelp once it is opened); nothing else yet.
	float help = 0.0f;
	if (const auto chest = g_rewards.find(object); chest != g_rewards.end())
	{
		const auto& infos = Locator::infoConstants::value().reward;
		if (static_cast<size_t>(chest->second.type) < infos.size())
		{
			help = static_cast<float>(static_cast<int>(infos.at(static_cast<size_t>(chest->second.type)).helpTextEnum));
		}
	}
	Pushf(help);
}

void SetLeashWorks() // 249 SET_LEASH_WORKS
{
	[[maybe_unused]] const auto creature = Pop().uintVal;
	[[maybe_unused]] const auto enable = static_cast<bool>(Pop().intVal);
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void LoadMyCreature() // 250 LOAD_MY_CREATURE
{
	[[maybe_unused]] const auto position = PopVec();
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void ObjectRelativeBelief() // 251 OBJECT_RELATIVE_BELIEF
{
	[[maybe_unused]] const auto belief = Popf();
	[[maybe_unused]] const auto player = Popf();
	[[maybe_unused]] const auto object = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void CreateWithAngleAndScale() // 252 CREATE_WITH_ANGLE_AND_SCALE
{
	const auto position = PopVec();
	const auto subtype = Pop().intVal;
	const auto type = static_cast<ObjectType>(Pop().intVal);
	const auto scale = Popf();
	const auto angle = Popf();

	const entt::entity object = CreateScriptObject(type, subtype, position, 0.0f, 0.0f, angle, 0.0f, scale);

	Pusho(static_cast<uint32_t>(object));
}

void SetHelpSystem() // 253 SET_HELP_SYSTEM
{
	[[maybe_unused]] const auto enable = static_cast<bool>(Pop().intVal);
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void SetVirtualInfluence() // 254 SET_VIRTUAL_INFLUENCE
{
	[[maybe_unused]] const auto player = Popf();
	[[maybe_unused]] const auto enable = static_cast<bool>(Pop().intVal);
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void SetActive() // 255 SET_ACTIVE
{
	const auto object = Pop().uintVal;
	const auto active = static_cast<bool>(Pop().intVal);
	// On a scroll (highlight): taken up. Every quest's ChallengeHighlightNotify sets its scroll active once it is
	// clicked, and no script ever deletes a finished quest's scroll - so an active one is gone from the land: not
	// drawn, not clickable (it stays a valid object, for scripts that still name it).
	if (g_highlights.contains(object))
	{
		auto& registry = Locator::entitiesRegistry::value();
		const auto entity = static_cast<entt::entity>(object);
		if (active && registry.Valid(entity))
		{
			registry.Remove<openblack::ecs::components::Mesh>(entity);
			if (const auto sparkler = g_sparklers.find(object); sparkler != g_sparklers.end())
			{
				for (const auto sprite : sparkler->second.sprites)
				{
					if (registry.Valid(sprite))
					{
						registry.Destroy(sprite);
					}
				}
				g_sparklers.erase(sparkler);
			}
			g_takenHighlights.insert(object);
			SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "SET_ACTIVE scroll {}: taken up", object);
		}
	}
}

void ThingValid() // 256 THING_VALID
{
	const auto objId = Pop().uintVal;
	// TODO(Daniels118): is this the right way?
	bool valid = false;
	if (objId != 0)
	{
		auto& registry = Locator::entitiesRegistry::value();
		valid = registry.Valid(static_cast<entt::entity>(objId));
	}
	Pushb(valid);
}

void VortexFadeOut() // 257 VORTEX_FADE_OUT
{
	[[maybe_unused]] const auto vortex = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void RemoveReactionOfType() // 258 REMOVE_REACTION_OF_TYPE
{
	[[maybe_unused]] const auto reaction = Pop().intVal;
	[[maybe_unused]] const auto object = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void CreatureLearnEverythingExcluding() // 259 CREATURE_LEARN_EVERYTHING_EXCLUDING
{
	[[maybe_unused]] const auto unk1 = Pop().intVal;
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void PlayedPercentage() // 260 PLAYED_PERCENTAGE
{
	[[maybe_unused]] const auto object = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushf(0.0f);
}

void ObjectCastByObject() // 261 OBJECT_CAST_BY_OBJECT
{
	[[maybe_unused]] const auto caster = Pop().uintVal;
	[[maybe_unused]] const auto spellInstance = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushb(false);
}

void IsWindMagicAtPos() // 262 IS_WIND_MAGIC_AT_POS
{
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushb(false);
}

void CreateMist() // 263 CREATE_MIST
{
	[[maybe_unused]] const auto heightRatio = Popf();
	[[maybe_unused]] const auto transparency = Popf();
	[[maybe_unused]] const auto b = Popf();
	[[maybe_unused]] const auto g = Popf();
	[[maybe_unused]] const auto r = Popf();
	[[maybe_unused]] const auto scale = Popf();
	[[maybe_unused]] const auto pos = PopVec();
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pusho(0);
}

void SetMistFade() // 264 SET_MIST_FADE
{
	[[maybe_unused]] const auto duration = Popf();
	[[maybe_unused]] const auto endTransparency = Popf();
	[[maybe_unused]] const auto startTransparency = Popf();
	[[maybe_unused]] const auto endScale = Popf();
	[[maybe_unused]] const auto startScale = Popf();
	[[maybe_unused]] const auto mist = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void GetObjectFade() // 265 GET_OBJECT_FADE
{
	[[maybe_unused]] const auto object = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushf(0.0f);
}

// Hand demos: the tutorials' recorded hand, Data/HandDemo/<name>.hnd. The file is a 0x1c-byte header then
// 124-byte frames; the fields used here (found by comparing the recordings with the scripts):
//   +24 / +28   hand screen position, 0..1
//   +32..+40    camera origin          +44..+52  camera focus
//   +64         1 on a trigger frame (HAND_DEMO_TRIGGER), where the script shows the next line
//   +68         time in milliseconds   +96       button event (1 press, 2 release)
// With pause, the demo holds on each trigger frame until the line shown there has been read.
namespace
{
struct HandDemoFrame
{
	uint32_t ms;
	glm::vec2 screen;
	glm::vec3 origin;
	glm::vec3 focus;
	bool trigger;
};

struct HandDemo
{
	std::vector<HandDemoFrame> frames;
	bool playing = false;
	bool withPause = false;
	size_t frame = 0;
	float demoMs = 0.0f;
	std::chrono::steady_clock::time_point lastUpdate {};
	bool paused = false;
	std::chrono::steady_clock::time_point pausedAt {};
	bool triggerPending = false;
	bool drivesCamera = true;
	bool cameraWasScripted = false;
	uint32_t scriptMoveCount = 0;
} g_handDemo;

std::vector<HandDemoFrame> LoadHandDemo(const std::string& name)
{
	auto& fileSystem = Locator::filesystem::value();
	const auto dir = fileSystem.GetPath<openblack::filesystem::Path::Data>(true) / "HandDemo";
	auto lower = [](std::string text) {
		std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
		return text;
	};
	const auto wanted = lower(std::filesystem::path(name).stem().string());
	std::filesystem::path found;
	std::error_code ec;
	for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
	{
		if (lower(entry.path().stem().string()) == wanted && lower(entry.path().extension().string()) == ".hnd")
		{
			found = entry.path();
			break;
		}
	}
	std::vector<HandDemoFrame> frames;
	if (found.empty())
	{
		SPDLOG_LOGGER_WARN(spdlog::get("scripting"), "Hand demo {} not found in {}", name, dir.string());
		return frames;
	}
	const auto data = fileSystem.ReadAll(found);
	constexpr size_t k_Header = 0x1c;
	constexpr size_t k_Frame = 124;
	auto f32 = [&](size_t at) {
		float v;
		std::memcpy(&v, &data[at], sizeof(v));
		return v;
	};
	auto u32 = [&](size_t at) {
		uint32_t v;
		std::memcpy(&v, &data[at], sizeof(v));
		return v;
	};
	for (size_t at = k_Header; at + k_Frame <= data.size(); at += k_Frame)
	{
		frames.push_back({u32(at + 68), {f32(at + 24), f32(at + 28)}, {f32(at + 32), f32(at + 36), f32(at + 40)},
		                  {f32(at + 44), f32(at + 48), f32(at + 52)}, u32(at + 64) == 1});
	}
	SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "Hand demo {}: {} frames, {:.1f} s", found.filename().string(),
	                   frames.size(), frames.empty() ? 0.0f : (frames.back().ms - frames.front().ms) / 1000.0f);
	return frames;
}

void EndHandDemo()
{
	if (g_handDemo.playing)
	{
		// Back to whoever held it: the script if it has asked for the camera, otherwise the player. (Only doing
		// this when the demo drove the camera to the end left the player locked out after the pitch demo.)
		Locator::camera::value().SetScriptControl(g_scriptHasCamera);
	}
	g_handDemo.playing = false;
	g_handDemo.paused = false;
}
} // namespace

bool HandDemoPlaying()
{
	return g_handDemo.playing;
}

std::optional<glm::vec2> UpdateHandDemo()
{
	auto& demo = g_handDemo;
	if (!demo.playing)
	{
		return std::nullopt;
	}
	const auto now = ScriptNow(); // the tutorial hands keep pace with the play speed
	const float dtMs = std::chrono::duration<float, std::milli>(now - demo.lastUpdate).count();
	demo.lastUpdate = now;

	if (demo.paused)
	{
		// Resume once the line the script showed at this trigger has been read (or after a while if it showed none).
		const bool lineRead = g_lastTextShown > demo.pausedAt && now - g_lastTextShown >= k_TextReadTime;
		if (!demo.triggerPending && (lineRead || now - demo.pausedAt > std::chrono::seconds(10)))
		{
			demo.paused = false;
		}
	}
	else
	{
		demo.demoMs += dtMs;
		const uint32_t target = demo.frames.front().ms + static_cast<uint32_t>(demo.demoMs);
		while (demo.frame + 1 < demo.frames.size() && demo.frames[demo.frame + 1].ms <= target)
		{
			++demo.frame;
			if (demo.frames[demo.frame].trigger)
			{
				demo.triggerPending = true;
				if (demo.withPause)
				{
					demo.paused = true;
					demo.pausedAt = now;
					demo.demoMs = static_cast<float>(demo.frames[demo.frame].ms - demo.frames.front().ms);
					break;
				}
			}
		}
		if (demo.frame + 1 >= demo.frames.size())
		{
			EndHandDemo();
			return std::nullopt;
		}
	}

	// Between the current frame and the next, so the hand moves smoothly at any frame rate.
	const auto& a = demo.frames[demo.frame];
	const auto& b = demo.frames[std::min(demo.frame + 1, demo.frames.size() - 1)];
	const float span = static_cast<float>(b.ms - a.ms);
	const float t = demo.paused || span <= 0.0f
	                    ? 0.0f
	                    : std::clamp((demo.frames.front().ms + demo.demoMs - a.ms) / span, 0.0f, 1.0f);

	auto& camera = Locator::camera::value();
	if (demo.drivesCamera && camera.GetScriptMoveCount() != demo.scriptMoveCount)
	{
		demo.drivesCamera = false; // the script moved the camera itself: leave it to the script
	}
	if (demo.drivesCamera)
	{
		camera.SetOrigin(glm::mix(a.origin, b.origin, t));
		camera.SetFocus(glm::mix(a.focus, b.focus, t));
	}
	return glm::mix(a.screen, b.screen, t);
}

void PlayHandDemo() // 266 PLAY_HAND_DEMO
{
	[[maybe_unused]] const auto withoutHandModify = static_cast<bool>(Pop().intVal);
	const auto withPause = static_cast<bool>(Pop().intVal);
	const auto name = PopString();
	EndHandDemo();
	auto frames = LoadHandDemo(name);
	if (frames.size() < 2)
	{
		return;
	}
	auto& camera = Locator::camera::value();
	g_handDemo = HandDemo {};
	g_handDemo.frames = std::move(frames);
	g_handDemo.playing = true;
	g_handDemo.withPause = withPause;
	g_handDemo.lastUpdate = ScriptNow();
	g_handDemo.cameraWasScripted = camera.IsScriptControlled();
	camera.SetScriptControl(true); // the player's camera controls would fight the recording
	g_handDemo.scriptMoveCount = camera.GetScriptMoveCount();
}

void IsPlayingHandDemo() // 267 IS_PLAYING_HAND_DEMO
{
	// A demo held on a trigger counts as not playing: scripts wait on this for it to carry on.
	Pushb(g_handDemo.playing && !g_handDemo.paused);
}

void GetArsePosition() // 268 GET_ARSE_POSITION
{
	[[maybe_unused]] const auto object = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushv(0.0f); // x
	Pushv(0.0f); // y
	Pushv(0.0f); // z
}

void IsLeashedToObject() // 269 IS_LEASHED_TO_OBJECT
{
	const auto target = Pop().uintVal;
	const auto object = Pop().uintVal;
	Pushb(object != 0 && g_leash.creature == object && g_leash.type != 0 && g_leash.tiedTo == target);
}

void GetInteractionMagnitude() // 270 GET_INTERACTION_MAGNITUDE
{
	// The last slap (below 0) or stroke (above 0) the creature had from the hand (UpdateCreatureInteraction).
	const auto creature = Pop().uintVal;
	const auto it = g_interactionMagnitude.find(creature);
	Pushf(it != g_interactionMagnitude.end() ? it->second : 0.0f);
}

void IsCreatureAvailable() // 271 IS_CREATURE_AVAILABLE
{
	[[maybe_unused]] const auto type = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushb(false);
}

void CreateHighlight() // 272 CREATE_HIGHLIGHT
{
	[[maybe_unused]] const auto challengeID = Pop().intVal;
	const auto position = PopVec();
	const auto type = Pop().intVal;
	// Land 1's FollowUs waits (CALL_NEAR type HIGHLIGHT) for the "Did you know" scroll DidYouKnow makes here.
	const auto entity = CreateScriptObject(ObjectType::Highlight, static_cast<uint32_t>(type), position, 0.0f, 0.0f, 0.0f,
	                                       0.0f, 1.0f);
	SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "Highlight {} (type {}) at ({:.1f}, {:.1f}, {:.1f})",
	                   static_cast<uint32_t>(entity), type, position.x, position.y, position.z);
	Pusho(static_cast<uint32_t>(entity));
}

void GetObjectHeld273() // 273 GET_OBJECT_HELD
{
	// What a creature holds (given from the hand), 0 if nothing.
	const auto creature = Pop().uintVal;
	const auto it = g_creatureHolding.find(creature);
	Pusho(it != g_creatureHolding.end() ? it->second : 0u);
}

void GetActionCount() // 274 GET_ACTION_COUNT
{
	// How many times the creature has done the action (so far only eating what it was given counts).
	const auto creature = Pop().uintVal;
	const auto action = Pop().intVal;
	const auto it = g_creatureActionCounts.find({creature, action});
	Pushf(it != g_creatureActionCounts.end() ? it->second : 0.0f);
}

void GetObjectLeashType() // 275 GET_OBJECT_LEASH_TYPE
{
	const auto object = Pop().uintVal;
	Pushi(object != 0 && g_leash.creature == object ? g_leash.type : 0);
}

void SetFocusFollow() // 276 SET_FOCUS_FOLLOW
{
	const auto target = Pop().uintVal;
	CameraFollowFocus(target);
}

void SetPositionFollow() // 277 SET_POSITION_FOLLOW
{
	const auto target = Pop().uintVal;
	CameraFollowPosition(target, std::nullopt, true);
}

void SetFocusAndPositionFollow() // 278 SET_FOCUS_AND_POSITION_FOLLOW
{
	const auto distance = Popf();
	const auto target = Pop().uintVal;
	CameraFollowPosition(target, distance, true);
}

void SetCameraLens() // 279 SET_CAMERA_LENS
{
	// As the original (runblack.exe 0x6ee2e0): back to the usual lens, over the given time.
	const auto time = Popf();
	Locator::camera::value().MoveLens(Locator::config::value().cameraXFov,
	                                  std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::duration<float>(time)));
}

void MoveCameraLens() // 280 MOVE_CAMERA_LENS
{
	// The lens is the field of view across the screen, in degrees (runblack.exe 0x6ee280: GCamera::SetCameraFov).
	const auto time = Popf();
	const auto lens = Popf();
	Locator::camera::value().MoveLens(lens, std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::duration<float>(time)));
}

void CreatureReaction() // 281 CREATURE_REACTION
{
	[[maybe_unused]] const auto unk1 = Pop().intVal;
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void CreatureInDevScript() // 282 CREATURE_IN_DEV_SCRIPT
{
	[[maybe_unused]] const auto creature = Pop().uintVal;
	[[maybe_unused]] const auto enable = static_cast<bool>(Pop().intVal);
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void StoreCameraDetails() // 283 STORE_CAMERA_DETAILS
{
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void RestoreCameraDetails() // 284 RESTORE_CAMERA_DETAILS
{
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void StartAngleSound285() // 285 START_ANGLE_SOUND
{
	[[maybe_unused]] const auto enable = static_cast<bool>(Pop().intVal);
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void SetCameraPosFocLens() // 286 SET_CAMERA_POS_FOC_LENS
{
	[[maybe_unused]] const auto unk6 = Pop().intVal;
	[[maybe_unused]] const auto unk5 = Pop().intVal;
	[[maybe_unused]] const auto unk4 = Pop().intVal;
	[[maybe_unused]] const auto unk3 = Pop().intVal;
	[[maybe_unused]] const auto unk2 = Pop().intVal;
	[[maybe_unused]] const auto unk1 = Pop().intVal;
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void MoveCameraPosFocLens() // 287 MOVE_CAMERA_POS_FOC_LENS
{
	[[maybe_unused]] const auto unk7 = Pop().intVal;
	[[maybe_unused]] const auto unk6 = Pop().intVal;
	[[maybe_unused]] const auto unk5 = Pop().intVal;
	[[maybe_unused]] const auto unk4 = Pop().intVal;
	[[maybe_unused]] const auto unk3 = Pop().intVal;
	[[maybe_unused]] const auto unk2 = Pop().intVal;
	[[maybe_unused]] const auto unk1 = Pop().intVal;
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void GameTimeOnOff() // 288 GAME_TIME_ON_OFF
{
	[[maybe_unused]] const auto enable = static_cast<bool>(Pop().intVal);
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void MoveGameTime() // 289 MOVE_GAME_TIME
{
	const auto duration = Popf();
	const auto hourOfTheDay = Popf();
	// Forwards round the clock: 23 -> 10 passes through midnight.
	float to = hourOfTheDay;
	while (to < g_gameHour)
	{
		to += 24.0f;
	}
	// The duration is not in seconds: the original's opening (MOVE_GAME_TIME 12 460, from 7.3) reaches full day 39
	// seconds in - sky hour 8.25, 27% of the way from 6.875 to 12 (the move goes evenly in sky hours, see SkyHour) -
	// so it takes about 145 seconds in all, 0.316 of the number.
	g_gameTimeMove = GameTimeMove {g_gameHour, to, std::max(duration * 0.316f, 0.0f), 0.0f};
}

void SetHighGraphicsDetail() // 290 SET_HIGH_GRAPHICS_DETAIL
{
	[[maybe_unused]] const auto object = Pop().uintVal;
	[[maybe_unused]] const auto enable = static_cast<bool>(Pop().intVal);
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void SetSkeleton() // 291 SET_SKELETON
{
	[[maybe_unused]] const auto object = Pop().uintVal;
	[[maybe_unused]] const auto enable = static_cast<bool>(Pop().intVal);
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void IsSkeleton() // 292 IS_SKELETON
{
	[[maybe_unused]] const auto object = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushb(false);
}

void PlayerSpellCastTime() // 293 PLAYER_SPELL_CAST_TIME
{
	[[maybe_unused]] const auto player = Popf();
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushf(0.0f);
}

void PlayerSpellLastCast() // 294 PLAYER_SPELL_LAST_CAST
{
	[[maybe_unused]] const auto player = Popf();
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushi(0);
}

void GetLastSpellCastPos() // 295 GET_LAST_SPELL_CAST_POS
{
	[[maybe_unused]] const auto player = Popf();
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushv(0.0f); // x
	Pushv(0.0f); // y
	Pushv(0.0f); // z
}

void AddSpotVisualTargetPos() // 296 ADD_SPOT_VISUAL_TARGET_POS
{
	[[maybe_unused]] const auto position = PopVec();
	[[maybe_unused]] const auto object = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void AddSpotVisualTargetObject() // 297 ADD_SPOT_VISUAL_TARGET_OBJECT
{
	[[maybe_unused]] const auto target = Pop().uintVal;
	[[maybe_unused]] const auto object = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void SetIndestructable() // 298 SET_INDESTRUCTABLE
{
	[[maybe_unused]] const auto object = Pop().uintVal;
	[[maybe_unused]] const auto indestructible = static_cast<bool>(Pop().intVal);
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void SetGraphicsClipping() // 299 SET_GRAPHICS_CLIPPING
{
	[[maybe_unused]] const auto unk1 = Pop().intVal;
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void SpiritAppear() // 300 SPIRIT_APPEAR
{
	const auto spirit = Pop().intVal;
	SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "SPIRIT_APPEAR {}", spirit);
	helpspirits::Appear(AdvisorOf(spirit));
}

void SpiritDisappear() // 301 SPIRIT_DISAPPEAR
{
	const auto spirit = Pop().intVal;
	SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "SPIRIT_DISAPPEAR {}", spirit);
	helpspirits::Disappear(AdvisorOf(spirit));
}

void SetFocusOnObject() // 302 SET_FOCUS_ON_OBJECT
{
	[[maybe_unused]] const auto target = Pop().uintVal;
	[[maybe_unused]] const auto object = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void ReleaseObjectFocus() // 303 RELEASE_OBJECT_FOCUS
{
	[[maybe_unused]] const auto creature = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void ImmersionExists() // 304 IMMERSION_EXISTS
{
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushb(false);
}

void SetDrawLeash() // 305 SET_DRAW_LEASH
{
	// Whether the leash is drawn: nothing draws it yet.
	[[maybe_unused]] const auto enable = static_cast<bool>(Pop().intVal);
}

void SetDrawHighlight() // 306 SET_DRAW_HIGHLIGHT
{
	[[maybe_unused]] const auto enable = static_cast<bool>(Pop().intVal);
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void SetOpenClose() // 307 SET_OPEN_CLOSE
{
	const auto object = Pop().uintVal;
	const auto open = static_cast<bool>(Pop().intVal);
	if (open)
	{
		g_openedStatics.insert(object);
	}
	else
	{
		g_openedStatics.erase(object);
	}
	SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "SET_OPEN_CLOSE {} {}", object, open ? "open" : "shut");
}

void SetIntroBuilding() // 308 SET_INTRO_BUILDING
{
	[[maybe_unused]] const auto enable = static_cast<bool>(Pop().intVal);
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void CreatureForceFriends() // 309 CREATURE_FORCE_FRIENDS
{
	[[maybe_unused]] const auto targetCreature = Pop().uintVal;
	[[maybe_unused]] const auto creature = Pop().uintVal;
	[[maybe_unused]] const auto enable = static_cast<bool>(Pop().intVal);
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void MoveComputerPlayerPosition() // 310 MOVE_COMPUTER_PLAYER_POSITION
{
	[[maybe_unused]] const auto withFixedHeight = static_cast<bool>(Pop().intVal);
	[[maybe_unused]] const auto speed = Popf();
	[[maybe_unused]] const auto position = PopVec();
	[[maybe_unused]] const auto player = Popf();
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void EnableDisableComputerPlayer311() // 311 ENABLE_DISABLE_COMPUTER_PLAYER
{
	[[maybe_unused]] const auto player = Popf();
	[[maybe_unused]] const auto enable = static_cast<bool>(Pop().intVal);
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void GetComputerPlayerPosition() // 312 GET_COMPUTER_PLAYER_POSITION
{
	[[maybe_unused]] const auto player = Popf();
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushv(0.0f); // x
	Pushv(0.0f); // y
	Pushv(0.0f); // z
}

void SetComputerPlayerPosition() // 313 SET_COMPUTER_PLAYER_POSITION
{
	[[maybe_unused]] const auto withFixedHeight = static_cast<bool>(Pop().intVal);
	[[maybe_unused]] const auto position = PopVec();
	[[maybe_unused]] const auto player = Popf();
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void GetStoredCameraPosition() // 314 GET_STORED_CAMERA_POSITION
{
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushv(0.0f); // x
	Pushv(0.0f); // y
	Pushv(0.0f); // z
}

void GetStoredCameraFocus() // 315 GET_STORED_CAMERA_FOCUS
{
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushv(0.0f); // x
	Pushv(0.0f); // y
	Pushv(0.0f); // z
}

void CallNearInState() // 316 CALL_NEAR_IN_STATE
{
	[[maybe_unused]] const auto excludingScripted = static_cast<bool>(Pop().intVal);
	[[maybe_unused]] const auto radius = Popf();
	[[maybe_unused]] const auto position = PopVec();
	[[maybe_unused]] const auto state = Pop().intVal;
	[[maybe_unused]] const auto subtype = Pop().intVal;
	[[maybe_unused]] const auto type = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pusho(0);
}

void SetCreatureSound() // 317 SET_CREATURE_SOUND
{
	[[maybe_unused]] const auto enable = static_cast<bool>(Pop().intVal);
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void CreatureInteractingWith() // 318 CREATURE_INTERACTING_WITH
{
	[[maybe_unused]] const auto unk1 = Pop().intVal;
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushb(false);
}

void SetSunDraw() // 319 SET_SUN_DRAW
{
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void ObjectInfoBits() // 320 OBJECT_INFO_BITS
{
	// On the gate stone pedestal: which gate totems are on it, a bit each - the Ape (Land 1's GateKey1) 1, the Tiger
	// (GateKey2) 2, the Cow (GateKey3) 4. ChooseYourCreature waits for 7.
	const auto object = Pop().uintVal;
	int bits = 0;
	auto& registry = Locator::entitiesRegistry::value();
	// Only ChooseYourCreature asks, and only about the pedestal: a handle to something else (a save point from
	// before CALL found the pedestal holds 0) means the pedestal still - there is one in the land.
	const bool isPedestal =
	    registry.Valid(static_cast<entt::entity>(object)) &&
	    registry.AllOf<openblack::ecs::components::AnimatedStatic>(static_cast<entt::entity>(object));
	for (const auto& [pedestal, set] : g_pedestalSetBits)
	{
		if (!isPedestal || pedestal == object)
		{
			bits |= set;
		}
	}
	for (const auto& [pedestal, stones] : g_pedestalStones)
	{
		if (isPedestal && pedestal != object)
		{
			continue;
		}
		for (const auto stone : stones)
		{
			const auto* mobile = registry.Valid(static_cast<entt::entity>(stone))
			                         ? registry.TryGet<openblack::ecs::components::MobileStatic>(static_cast<entt::entity>(stone))
			                         : nullptr;
			if (mobile == nullptr)
			{
				continue;
			}
			bits |= mobile->type == MobileStaticInfo::GateTotemApe     ? 1
			        : mobile->type == MobileStaticInfo::GateTotemTiger ? 2
			        : mobile->type == MobileStaticInfo::GateTotemCow   ? 4
			                                                           : 0;
		}
	}
	Pushf(static_cast<float>(bits));
}

void SetHurtByFire() // 321 SET_HURT_BY_FIRE
{
	[[maybe_unused]] const auto object = Pop().uintVal;
	[[maybe_unused]] const auto enable = static_cast<bool>(Pop().intVal);
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void ConfinedObject() // 322 CONFINED_OBJECT
{
	[[maybe_unused]] const auto unk4 = Pop().intVal;
	[[maybe_unused]] const auto unk3 = Pop().intVal;
	[[maybe_unused]] const auto unk2 = Pop().intVal;
	[[maybe_unused]] const auto unk1 = Pop().intVal;
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void ClearConfinedObject() // 323 CLEAR_CONFINED_OBJECT
{
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void GetObjectFlock() // 324 GET_OBJECT_FLOCK
{
	[[maybe_unused]] const auto member = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pusho(0);
}

void SetPlayerBelief() // 325 SET_PLAYER_BELIEF
{
	const auto belief = Popf();
	const auto player = static_cast<int>(Popf());
	const auto object = Pop().uintVal;
	// Land 1's SetAztecTownBeliefToZero holds the Aztecs' belief in you at 0 until the guide's lesson.
	if (std::abs(TownBelief(object, player) - belief) > 1e-4f)
	{
		SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "SET_PLAYER_BELIEF town {} player {}: {:.3f}", object, player, belief);
	}
	SetTownBelief(object, player, belief);
}

// The opening's god, after the movie (FollowUs: PLAY_JC_SPECIAL 0, 1, 2, 4, 5, 5): runblack.exe 1.20's JCMisc.cpp
// Intro, whose fn_005DF9C0 starts each effect and fn_005DF640 moves them each frame. A fire ball comes down out of
// the sky into the sea where the boy swims, the camera behind it; a hand (Data/Misc/hand_intro.l3d) rises where it
// fell and picks him up (hand_intro2.anm, "Hand_Pick_Up_Swimmer"); then on the beach the hand puts him down by his
// parents (hand_intro.anm, "Hand_Put_Down_Swimmer"), the boy held at its EBone. Without it the boy was simply back on
// the sand when the screen came up from black.
namespace
{
enum SJC : int // ScriptEnumstwo.h's SCRIPT_JC_SPECIAL
{
	SJC_CREATE_FIRE_BALL = 0,
	SJC_CAMERA_FOLLOW_FIRE_BALL = 1,
	SJC_CANCEL_CAMERA_FOLLOW_FIRE_BALL = 2,
	SJC_CREATE_HAND_WITH_KID = 4,
	SJC_ACTIVATED_HAND = 5,
	SJC_I_AM_THE_LITTLE_BOY = 7,
	SJC_START_HAND_WITH_BOY = 13,
	SJC_CLEAN_INTRO = 18,
};

// Intro's numbers (its statics at 0xd19a28/0xd19a38 and the constants beside them).
const glm::vec3 k_IntroFireBallTarget {1416.337f, 0.0f, 2053.503f}; // where the boy swims
const glm::vec3 k_IntroFireBallHeading {-1.0f, -0.24f, -1.0f};    // normalised: down from the north-east sky
const glm::vec3 k_IntroPickUpHand {1416.337f, -1.5f, 2053.503f};   // the hand risen where the fire ball fell
constexpr float k_IntroPickUpAngle = glm::pi<float>();
const glm::vec3 k_IntroPutDownHand {1494.379f, -2.5f, 2091.474f}; // on the beach by the mother and father
constexpr float k_IntroPutDownAngle = -0.7853982f - glm::radians(15.0f);
constexpr float k_IntroHandScale = 0.008f;
constexpr float k_IntroBoyCarriedMs = 1817.0f;  // held at the EBone this long into the put-down
constexpr float k_IntroBoyBelowEBone = 0.65f;   // and this far below it
constexpr float k_IntroSecondActivateMs = 2379.0f; // a second ACTIVATED_HAND skips on to here (0x94b)

// The fire ball (fn_00827F20, drawn by fn_008283D0): 20 LH3DSprites of Data/Textures/misc0 on one line, flying from
// 4000 behind the target at 0.45 a millisecond and stopping 10 short of it.
constexpr float k_FireBallStartDistance = 4000.0f;
constexpr float k_FireBallTravel = 3990.0f;
constexpr float k_FireBallSpeedPerMs = 0.45f;
constexpr float k_FireBallTrailStep = 6.0f;
constexpr float k_FireBallOnTopAfterMs = 8237.0f; // 0x202d: from here drawn over everything (Z func always)
constexpr float k_FireBallHoldMs = 500.0f;        // landed: the core alone, held
constexpr float k_FireBallFadeMs = 1000.0f;       // then fading from 255 to 50
constexpr float k_FireBallCameraBack = 150.0f;    // the following camera, behind the ball
constexpr float k_FireBallCameraUp = 30.0f;
constexpr int k_FireBallSprites = 20;

struct IntroFireBall
{
	enum class State
	{
		Flying,
		Landed,
		Fading,
		Gone,
	} state = State::Flying;
	float ageMs = 0.0f;
	float timerMs = 0.0f;
	std::array<entt::entity, k_FireBallSprites> sprites {};
	std::array<float, k_FireBallSprites> size {};
	std::array<float, k_FireBallSprites> alpha {};
	float streakAngle = 0.0f;
};

struct IntroState
{
	// fn_005DF640's 0xbf2afc: -1 nothing, 0 the fire ball flying, 12 it has landed and the hand is up, 4 on the beach.
	int state = -1;
	std::optional<IntroFireBall> fireBall;
	bool cameraFollows = false;
	entt::entity hand = entt::null;
	uint32_t handAnimation = 0;
	bool pickUpPlaying = false; // 0xd19cb0: the pick-up runs once the fire ball has gone
	bool pickUpDone = false;    // 0xd19c94
	bool activated = false;     // 0xd19ca4: the put-down running
	bool carrying = false;      // 0xd19c34: the boy drawn in the hand
	float handMs = 0.0f;
	uint32_t littleBoy = 0;     // THING_JC_SPECIAL I_AM_THE_LITTLE_BOY
	std::optional<glm::vec3> boyPutDown; // where the scripts stood him while he was carried
	// hand_intro.l3d's EBone: a 3x4 matrix at its footprint offset + 4, its bone at + 0x304 (fn_005DFCE0).
	int eBone = -1;
	glm::vec3 eBoneOffset {0.0f};
	bool eBoneRead = false;
};
IntroState g_intro;

void ReadIntroEBone()
{
	auto& intro = g_intro;
	if (intro.eBoneRead)
	{
		return;
	}
	intro.eBoneRead = true;
	try
	{
		auto& fs = Locator::filesystem::value();
		const auto data = fs.ReadAll(fs.GetPath<openblack::filesystem::Path::Misc>() / "hand_intro.l3d");
		uint32_t base = 0;
		if (data.size() >= 0x4c)
		{
			std::memcpy(&base, data.data() + 0x48, sizeof(base));
		}
		if (base != 0 && base + 0x308 <= data.size())
		{
			std::array<float, 12> matrix {};
			std::memcpy(matrix.data(), data.data() + base + 4, sizeof(matrix));
			int32_t bone = -1;
			std::memcpy(&bone, data.data() + base + 0x304, sizeof(bone));
			intro.eBone = bone;
			intro.eBoneOffset = glm::vec3(matrix[9], matrix[10], matrix[11]);
			SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "Intro hand EBone on bone {} at ({:.2f}, {:.2f}, {:.2f})", bone,
			                   intro.eBoneOffset.x, intro.eBoneOffset.y, intro.eBoneOffset.z);
		}
	}
	catch (const std::exception& e)
	{
		SPDLOG_LOGGER_WARN(spdlog::get("scripting"), "Intro hand EBone: {}", e.what());
	}
}

// The hands' lengths in milliseconds: their .anm header's word 0x20, which Intro steps through by the game's time
// (as the chest's coffre.anm, k_ChestOpenSeconds).
float IntroAnimationMs(uint32_t animation)
{
	if (animation == entt::hashed_string("hand_intro").value())
	{
		return 3066.0f; // Hand_Put_Down_Swimmer, 30 frames
	}
	if (animation == entt::hashed_string("hand_intro2").value())
	{
		return 766.0f; // Hand_Pick_Up_Swimmer, 8 frames
	}
	return 0.0f;
}

// The hand's bones `ms` into its animation, relative to their parents (as the chest's).
std::vector<glm::mat4> IntroHandPose(uint32_t animation, float ms)
{
	const auto& animations = Locator::resources::value().GetAnimations();
	if (!animations.Contains(animation) || animations.Handle(animation)->GetFrames().empty())
	{
		return {};
	}
	const auto& anim = *animations.Handle(animation);
	const float duration = std::max(1.0f, IntroAnimationMs(animation));
	const float last = static_cast<float>(anim.GetFrames().back().time);
	return anim.GetBoneMatrices(static_cast<uint32_t>(std::clamp(ms / duration, 0.0f, 1.0f) * last));
}

void PlaceIntroHand(uint32_t animation, const glm::vec3& position, float angle)
{
	auto& intro = g_intro;
	auto& registry = Locator::entitiesRegistry::value();
	const auto mesh = entt::hashed_string("hand_intro").value();
	if (!Locator::resources::value().GetMeshes().Contains(mesh))
	{
		SPDLOG_LOGGER_WARN(spdlog::get("scripting"), "Intro hand: Data/Misc/hand_intro.l3d not loaded");
		return;
	}
	if (!registry.Valid(intro.hand))
	{
		intro.hand = registry.Create();
		registry.Assign<openblack::ecs::components::Mesh>(intro.hand, mesh, static_cast<int8_t>(0), static_cast<int8_t>(0));
		registry.Assign<openblack::ecs::components::Animated>(intro.hand, 0u, 0.0f, std::vector<glm::mat4> {});
		registry.Assign<Transform>(intro.hand, position, glm::mat3(1.0f), glm::vec3(k_IntroHandScale));
	}
	auto& transform = registry.Get<Transform>(intro.hand);
	transform.position = position;
	transform.rotation = glm::mat3(glm::eulerAngleY(angle));
	transform.scale = glm::vec3(k_IntroHandScale);
	intro.handAnimation = animation;
	intro.handMs = 0.0f;
	registry.Get<openblack::ecs::components::Animated>(intro.hand).localPose = IntroHandPose(animation, 0.0f);
}

void RemoveIntroFireBall()
{
	auto& intro = g_intro;
	if (!intro.fireBall)
	{
		return;
	}
	auto& registry = Locator::entitiesRegistry::value();
	for (const auto sprite : intro.fireBall->sprites)
	{
		if (registry.Valid(sprite))
		{
			registry.Destroy(sprite);
		}
	}
	intro.fireBall.reset();
}

// Intro::ReleaseAll: the hand and the fire ball gone, the boy back where the scripts put him.
void ReleaseIntro()
{
	auto& intro = g_intro;
	auto& registry = Locator::entitiesRegistry::value();
	RemoveIntroFireBall();
	if (registry.Valid(intro.hand))
	{
		registry.Destroy(intro.hand);
	}
	intro.hand = entt::null;
	if (intro.boyPutDown && registry.Valid(static_cast<entt::entity>(intro.littleBoy)) &&
	    registry.AllOf<Transform>(static_cast<entt::entity>(intro.littleBoy)))
	{
		registry.Get<Transform>(static_cast<entt::entity>(intro.littleBoy)).position = *intro.boyPutDown;
	}
	intro.boyPutDown.reset();
	intro.state = -1;
	intro.cameraFollows = false;
	intro.pickUpPlaying = false;
	intro.activated = false;
	intro.carrying = false;
}

void CreateIntroFireBall()
{
	auto& intro = g_intro;
	RemoveIntroFireBall();
	auto* sheet = SpriteSheet("misc0");
	if (sheet == nullptr)
	{
		return;
	}
	auto& registry = Locator::entitiesRegistry::value();
	auto& ball = intro.fireBall.emplace();
	// Its sprites (fn_00827F20): the 18 of the tail shrink from the head back and fade (alpha (19 - i) / 20); the first
	// six are drawn twice as big; the 19th is a big faint halo at the head and the 20th a long thin streak through it.
	for (int i = 0; i < k_FireBallSprites; ++i)
	{
		const float n = static_cast<float>(19 - i);
		float size = n * 15.0f * 0.05f + 3.0f;
		if (i <= 5)
		{
			size *= 2.0f;
		}
		ball.size[i] = size;
		ball.alpha[i] = std::floor(n * 255.0f / 20.0f) / 255.0f;
		// misc0's three fire puffs, 32 texels each along its row 192 (the sprite flags' low bits): the big soft one at
		// the head, the small hard one for the streak, the middling one down the tail.
		int tile = i <= 5 ? 0 : 2;
		if (i == 18)
		{
			size = 80.0f;
			ball.size[i] = size;
			ball.alpha[i] = 40.0f / 255.0f;
		}
		if (i == 19)
		{
			tile = 1;
			ball.size[i] = 350.0f;
			ball.alpha[i] = 14.0f / 255.0f;
		}
		const auto entity = registry.Create();
		auto& sprite = registry.Assign<openblack::ecs::components::Sprite>(
		    entity, sheet->GetNativeHandle(), glm::vec2(static_cast<float>(tile) * 0.125f + 0.002f, 0.75f + 0.002f),
		    glm::vec2(0.121f), glm::vec4(1.0f, 1.0f, 1.0f, ball.alpha[i]));
		sprite.fullColour = true;
		registry.Assign<Transform>(entity, k_IntroFireBallTarget, glm::mat3(1.0f), glm::vec3(0.0f));
		ball.sprites[i] = entity;
	}
	ball.streakAngle = -glm::pi<float>() / 8.0f;
}

glm::vec3 IntroFireBallHead(const IntroFireBall& ball)
{
	const auto heading = glm::normalize(k_IntroFireBallHeading);
	const auto start = k_IntroFireBallTarget - heading * k_FireBallStartDistance;
	return start + heading * std::min(ball.ageMs * k_FireBallSpeedPerMs, k_FireBallTravel);
}

// fn_008283D0, each frame: the ball along its line; landed, only its core (three times, turned at random) holding
// then fading; and the following camera behind it.
void UpdateIntroFireBall(float ms)
{
	auto& intro = g_intro;
	auto& ball = *intro.fireBall;
	auto& registry = Locator::entitiesRegistry::value();
	const auto heading = glm::normalize(k_IntroFireBallHeading);
	if (ball.state == IntroFireBall::State::Flying && ball.ageMs * k_FireBallSpeedPerMs >= k_FireBallTravel)
	{
		ball.state = IntroFireBall::State::Landed;
		ball.timerMs = k_FireBallHoldMs;
	}
	else if (ball.state == IntroFireBall::State::Landed)
	{
		ball.timerMs -= ms;
		if (ball.timerMs < 0.0f)
		{
			ball.state = IntroFireBall::State::Fading;
			ball.timerMs = k_FireBallFadeMs;
		}
	}
	else if (ball.state == IntroFireBall::State::Fading)
	{
		ball.timerMs -= ms;
		if (ball.timerMs < 0.0f)
		{
			ball.state = IntroFireBall::State::Gone;
		}
	}
	const auto head = IntroFireBallHead(ball);
	const bool flying = ball.state == IntroFireBall::State::Flying;
	const bool onTop = !flying || ball.ageMs > k_FireBallOnTopAfterMs;
	for (int i = 0; i < k_FireBallSprites; ++i)
	{
		const auto entity = ball.sprites[i];
		if (!registry.Valid(entity))
		{
			continue;
		}
		auto& sprite = registry.Get<openblack::ecs::components::Sprite>(entity);
		auto& transform = registry.Get<Transform>(entity);
		sprite.onTop = onTop;
		float size = ball.size[i];
		float alpha = ball.alpha[i];
		float angle = Locator::rng::value().NextValue(0.0f, glm::two_pi<float>());
		glm::vec2 stretch(1.0f);
		auto position = head;
		if (flying)
		{
			if (i < 18)
			{
				position += heading * (-static_cast<float>(i) * k_FireBallTrailStep);
			}
			if (i < 5)
			{
				// The head's five flicker: (57 - 3i) / 4 + 3, give or take 2, every frame.
				size = static_cast<float>(57 - 3 * i) * 5.0f * 0.05f + 3.0f + Locator::rng::value().NextValue(-2.0f, 2.0f);
			}
			if (i == 19)
			{
				ball.streakAngle += ms * 0.00015707963f;
				angle = ball.streakAngle;
				stretch = glm::vec2(1.0f, 0.05f);
			}
		}
		else if (i < 3)
		{
			// Landed: sprite 0 drawn three times, at random turns - its alpha 255, then down to 50 as it fades.
			size = ball.size[0];
			alpha = ball.state == IntroFireBall::State::Fading
			            ? (std::max(ball.timerMs, 0.0f) * (255.0f - 50.0f) / k_FireBallFadeMs + 50.0f) / 255.0f
			            : ball.alpha[0];
			if (ball.state == IntroFireBall::State::Gone)
			{
				alpha = 0.0f;
			}
		}
		else
		{
			alpha = 0.0f;
		}
		sprite.tint.a = alpha;
		transform.position = position;
		transform.rotation = glm::mat3(glm::eulerAngleZ(angle));
		// The sprite's size is its width; the drawn plane is 2 across.
		transform.scale = alpha > 0.0f ? glm::vec3(size * 0.5f * stretch.x, size * 0.5f * stretch.y, 1.0f) : glm::vec3(0.0f);
	}
	if (flying)
	{
		ball.ageMs += ms;
	}
	// SJC_CAMERA_FOLLOW_FIRE_BALL: LH3DTech's camera put 150 back along its line and 30 up, looking at the target
	// (0xea9ec8 = 2), while the ball flies.
	if (intro.cameraFollows && intro.state == 0)
	{
		auto& cam = Locator::camera::value();
		cam.SetOrigin(IntroFireBallHead(ball) - heading * k_FireBallCameraBack + glm::vec3(0.0f, k_FireBallCameraUp, 0.0f));
		cam.SetFocus(k_IntroFireBallTarget);
	}
}

// fn_005DFCE0: the hand's EBone in the world, `ms` into the put-down - where the boy is drawn.
std::optional<glm::vec3> IntroBoyInHand()
{
	auto& intro = g_intro;
	auto& registry = Locator::entitiesRegistry::value();
	if (!registry.Valid(intro.hand) || !intro.activated || intro.handMs > k_IntroBoyCarriedMs || intro.eBone < 0)
	{
		return std::nullopt;
	}
	const auto& meshes = Locator::resources::value().GetMeshes();
	const auto meshId = entt::hashed_string("hand_intro").value();
	if (!meshes.Contains(meshId))
	{
		return std::nullopt;
	}
	const auto& mesh = *meshes.Handle(meshId);
	const auto& pose = registry.Get<openblack::ecs::components::Animated>(intro.hand).localPose;
	const auto& parents = mesh.GetBoneParents();
	if (pose.size() != parents.size() || static_cast<size_t>(intro.eBone) >= pose.size())
	{
		return std::nullopt;
	}
	// The bones composed as the renderer does (RenderingSystem), parents first.
	std::vector<glm::mat4> model(pose.size());
	for (size_t i = 0; i < pose.size(); ++i)
	{
		model[i] = parents[i] != std::numeric_limits<uint32_t>::max() ? model[parents[i]] * pose[i] : pose[i];
	}
	const auto& transform = registry.Get<Transform>(intro.hand);
	const auto world = glm::scale(glm::translate(glm::mat4(1.0f), transform.position) * glm::mat4(transform.rotation), transform.scale);
	auto point = glm::vec3(world * model[intro.eBone] * glm::vec4(intro.eBoneOffset, 1.0f));
	point.y -= k_IntroBoyBelowEBone;
	return point;
}

void UpdateIntroHand(float ms)
{
	auto& intro = g_intro;
	auto& registry = Locator::entitiesRegistry::value();
	if (!registry.Valid(intro.hand))
	{
		return;
	}
	const float duration = IntroAnimationMs(intro.handAnimation);
	const bool runs = intro.state == 12 ? intro.pickUpPlaying : intro.activated;
	if (runs)
	{
		intro.handMs += ms;
		if (intro.handMs >= duration)
		{
			intro.handMs = std::max(0.0f, duration - 1.0f);
			if (intro.state == 12)
			{
				intro.pickUpDone = true;
			}
			else
			{
				// The put-down done: the hand has gone back up out of sight.
				ReleaseIntro();
				return;
			}
		}
	}
	registry.Get<openblack::ecs::components::Animated>(intro.hand).localPose = IntroHandPose(intro.handAnimation, intro.handMs);
}

void UpdateIntro(float seconds)
{
	auto& intro = g_intro;
	if (intro.state == -1)
	{
		return;
	}
	const float ms = seconds * 1000.0f;
	if (intro.fireBall)
	{
		UpdateIntroFireBall(ms);
		if (intro.fireBall->state == IntroFireBall::State::Gone)
		{
			RemoveIntroFireBall();
			if (intro.state == 12)
			{
				intro.pickUpPlaying = true;
			}
		}
		else if (intro.state == 0 && intro.fireBall->state != IntroFireBall::State::Flying)
		{
			// Landed: the hand rises where it fell, still until the fire ball has gone.
			intro.state = 12;
			intro.pickUpDone = false;
			intro.pickUpPlaying = false;
			PlaceIntroHand(entt::hashed_string("hand_intro2").value(), k_IntroPickUpHand, k_IntroPickUpAngle);
			SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "Intro: the fire ball has landed, the hand rises");
		}
	}
	UpdateIntroHand(ms);
	// GLandscape::Draw: the little boy drawn in the hand while it carries him.
	auto& registry = Locator::entitiesRegistry::value();
	const auto boy = static_cast<entt::entity>(intro.littleBoy);
	if (intro.littleBoy != 0 && registry.Valid(boy) && registry.AllOf<Transform>(boy))
	{
		auto& transform = registry.Get<Transform>(boy);
		const auto held = intro.carrying ? IntroBoyInHand() : std::nullopt;
		if (held)
		{
			if (!intro.boyPutDown)
			{
				intro.boyPutDown = transform.position;
			}
			transform.position = *held;
		}
		else if (intro.boyPutDown)
		{
			transform.position = *intro.boyPutDown;
			intro.boyPutDown.reset();
		}
	}
}

// A shark's wake: the water's circles (GWater's, 1024 of them), one dropped at its fin every 50 ms of drawing
// (fn_00775170, from Whale's draw fn_00774E30: a counter shared by every shark, plus each frame's milliseconds,
// a circle each time it passes 50) - on the sea at its mesh's EBone (bone 0, (-0.08, 0.00, 0.30): SharkBoned's,
// AllMeshes.g3d mesh 31), turned to its heading. Each (fn_005E5100) ages at half the game's time to 700 - 1.4 s -
// growing from nothing to 10 across (its size age x 10 / 700, its height ratio 1) and fading from 0x90 of white to
// nothing (alpha (255 - age x 255 / 700) x 0x90 / 256), lying flat (sprite flag 0x40) in the additive smoke material
// (g_smoke_additif_mat: Data/Textures/smoke.raw) as its sprite 49, a white V of wake, its point along the way the shark
// swims (the heading Whale's draw takes from its move).
struct WaterCircle
{
	entt::entity sprite = entt::null;
	float age = 0.0f;
};
std::vector<WaterCircle> g_waterCircles;

void UpdateSharkWakes(float seconds)
{
	constexpr float k_DropEveryMs = 50.0f;
	constexpr float k_CircleAge = 700.0f;
	constexpr float k_AgeRate = 0.5f;
	constexpr float k_CircleGrowth = 10.0f;
	constexpr float k_CircleAlpha = 144.0f / 256.0f;
	constexpr size_t k_MostCircles = 1024;
	static const glm::vec3 s_fin(-0.07914868f, -0.00280821f, 0.30429077f);
	static float s_counter = 0.0f;
	auto& registry = Locator::entitiesRegistry::value();
	const float ms = seconds * 1000.0f;
	for (auto it = g_waterCircles.begin(); it != g_waterCircles.end();)
	{
		it->age += ms * k_AgeRate;
		if (it->age >= k_CircleAge || !registry.Valid(it->sprite))
		{
			if (registry.Valid(it->sprite))
			{
				registry.Destroy(it->sprite);
			}
			it = g_waterCircles.erase(it);
			continue;
		}
		const float size = it->age * k_CircleGrowth / k_CircleAge;
		registry.Get<openblack::ecs::components::Sprite>(it->sprite).tint.a = (1.0f - it->age / k_CircleAge) * k_CircleAlpha;
		registry.Get<Transform>(it->sprite).scale = glm::vec3(size * 0.5f); // the drawn plane is 2 across
		++it;
	}
	if (ms <= 0.0f)
	{
		return;
	}
	const auto sharkMesh = resources::HashIdentifier(MeshId::SharkBoned);
	const auto& animations = Locator::resources::value().GetAnimations();
	static std::unordered_map<entt::entity, std::pair<glm::vec3, float>> s_headings; // where each was, and its heading
	std::erase_if(s_headings, [&](const auto& shark) { return !registry.Valid(shark.first); });
	std::vector<std::pair<glm::vec3, float>> drops;
	registry.Each<const openblack::ecs::components::Mesh, const Transform, const openblack::ecs::components::Animated>(
	    [&](entt::entity entity, const openblack::ecs::components::Mesh& mesh, const Transform& transform,
	        const openblack::ecs::components::Animated& animated) {
		    if (mesh.id != sharkMesh)
		    {
			    return;
		    }
		    auto& [was, heading] = s_headings.try_emplace(entity, transform.position, 0.0f).first->second;
		    if (const auto moved = transform.position - was; moved.x != 0.0f || moved.z != 0.0f)
		    {
			    heading = std::atan2(-moved.z, moved.x); // eulerAngleY(heading) turns the sprite's +x (the V's point) that way
		    }
		    was = transform.position;
		    if (s_counter > k_DropEveryMs)
		    {
			    s_counter = std::fmod(s_counter, k_DropEveryMs);
			    // Bone 0, the root, as the swim poses it.
			    glm::mat4 bone(1.0f);
			    if (const auto id = resources::HashIdentifier(animated.animation); animations.Contains(id))
			    {
				    const auto posed = animations.Handle(id)->GetBoneMatrices(static_cast<uint32_t>(animated.time));
				    if (!posed.empty())
				    {
					    bone = posed[0];
				    }
			    }
			    const auto world =
			        glm::scale(glm::translate(glm::mat4(1.0f), transform.position) * glm::mat4(transform.rotation), transform.scale);
			    auto at = glm::vec3(world * bone * glm::vec4(s_fin, 1.0f));
			    at.y = 0.05f; // on the sea (0), just above it
			    drops.emplace_back(at, heading);
		    }
		    s_counter += ms;
	    });
	auto* sheet = drops.empty() ? nullptr : SpriteSheet("smoke");
	for (const auto& [at, heading] : drops)
	{
		if (sheet == nullptr || g_waterCircles.size() >= k_MostCircles)
		{
			break;
		}
		const auto entity = registry.Create();
		auto& sprite = registry.Assign<openblack::ecs::components::Sprite>(
		    entity, sheet->GetNativeHandle(), glm::vec2(1.0f * 0.125f + 0.002f, 0.75f + 0.002f), glm::vec2(0.121f),
		    glm::vec4(1.0f, 1.0f, 1.0f, k_CircleAlpha));
		sprite.fullColour = true;
		sprite.flat = true;
		registry.Assign<Transform>(entity, at, glm::mat3(glm::eulerAngleY(heading)), glm::vec3(0.0f));
		g_waterCircles.push_back({entity, 0.0f});
	}
}

// The sun (LH3DSky): Data/WeatherSystem/sun.l3d, one square 9928 across, on Data/Textures/sun.raw's rayed sun (its u
// 0.5-0.75, v 0-0.25; LH3DDivers::Open loads it) in an additive material that writes no depth (type 0xd).
// It stands at (-30000, 7500 e, -30000) - the sky's hour (fn_0086A160) put to 6-12 and back, e = (that - 6) / 6, so
// on the horizon at 6 and 18, highest at noon (fn_0086C020) - drawn with the land, coloured 0x957c63 at its
// strength: 255 from 6 to 18, rising from 3 and dying by 21 (85 an hour). Then, over everything (PostDrawSun), its
// glare, 1.8 times as big, coloured 0xa06a35 at that strength times how much of it is seen: five points of it tested
// against the land in front (fn_0086BD00, the depth buffer's), eased towards a hundredth of the way a millisecond.
// The opening's dusk puts it just over the sea behind the family, as in the original's shots from 54 to 84 s.
void UpdateSun(float seconds)
{
	constexpr glm::vec2 k_SunAt(-30000.0f, -30000.0f);
	constexpr float k_SunHighest = 7500.0f;
	constexpr float k_SunHalfSize = 4964.119f;
	constexpr float k_GlareScale = 1.8f;
	constexpr float k_Ease = 0.01f; // a millisecond
	static entt::entity s_disc = entt::null;
	static entt::entity s_glare = entt::null;
	static float s_seen = 0.0f; // 0-255
	auto& registry = Locator::entitiesRegistry::value();
	auto* sheet = SpriteSheet("sun");
	if (sheet == nullptr || !Locator::skySystem::has_value())
	{
		return;
	}
	auto make = [&](entt::entity& entity, glm::vec3 tint, bool over) {
		if (entity == entt::null || !registry.Valid(entity))
		{
			entity = registry.Create();
			auto& sprite = registry.Assign<openblack::ecs::components::Sprite>(entity, sheet->GetNativeHandle(), glm::vec2(0.5f, 0.0f),
			                                                                   glm::vec2(0.25f), glm::vec4(tint, 0.0f));
			sprite.fullColour = true;
			sprite.onTop = over;
			registry.Assign<Transform>(entity, glm::vec3(0.0f), glm::mat3(1.0f), glm::vec3(0.0f));
		}
		return entity;
	};
	make(s_disc, glm::vec3(0x95, 0x7c, 0x63) / 255.0f, false);
	make(s_glare, glm::vec3(0xa0, 0x6a, 0x35) / 255.0f, true);

	const float hour = Locator::skySystem::value().GetTime();
	float h = std::clamp(hour, 6.0f, 18.0f);
	h = h > 12.0f ? 24.0f - h : h;
	const float elevation = (h - 6.0f) / 6.0f;
	float strength = 255.0f;
	if (hour < 6.0f)
	{
		strength = hour < 3.0f ? 0.0f : (hour - 3.0f) * 85.0f;
	}
	else if (hour > 18.0f)
	{
		strength = hour > 21.0f ? 0.0f : 255.0f - (hour - 18.0f) * 85.0f;
	}
	const auto sun = glm::vec3(k_SunAt.x, k_SunHighest * elevation, k_SunAt.y);
	if (InTemple())
	{
		strength = 0.0f; // LH3DSky::g_b_we_are_inside_citadel
	}

	// How much of it the land hides: its middle and four points about it, each looked for along the line from the
	// camera, over the land near enough to stand in the way.
	const auto eye = Locator::camera::value().GetOrigin();
	const auto& land = Locator::terrainSystem::value();
	auto seen = [&](glm::vec3 point) {
		const auto way = glm::normalize(point - eye);
		constexpr float k_Step = 8.0f;
		constexpr float k_Far = 4000.0f;
		for (float d = k_Step; d < k_Far; d += k_Step)
		{
			const auto at = eye + way * d;
			if (at.y < land.GetHeightAt(glm::vec2(at.x, at.z)))
			{
				return false;
			}
		}
		return true;
	};
	const auto toSun = glm::normalize(sun - eye);
	const auto side = glm::normalize(glm::cross(toSun, glm::vec3(0.0f, 1.0f, 0.0f)));
	const auto up = glm::cross(side, toSun);
	const float spread = k_SunHalfSize * 0.25f;
	int count = 0;
	if (strength > 0.0f)
	{
		for (const auto& offset : {glm::vec3(0.0f), side * spread, -side * spread, up * spread, -up * spread})
		{
			count += seen(sun + offset) ? 1 : 0;
		}
	}
	const float target = static_cast<float>(count) * 255.0f / 5.0f;
	s_seen = std::clamp(s_seen + (target - s_seen) * std::min(seconds * 1000.0f * k_Ease, 1.0f), 0.0f, 255.0f);

	auto place = [&](entt::entity entity, float size, float alpha) {
		registry.Get<openblack::ecs::components::Sprite>(entity).tint.a = alpha;
		auto& transform = registry.Get<Transform>(entity);
		transform.position = sun;
		transform.scale = alpha > 0.0f ? glm::vec3(size, size, 1.0f) : glm::vec3(0.0f);
	};
	place(s_disc, k_SunHalfSize, strength / 255.0f);
	place(s_glare, k_SunHalfSize * k_GlareScale, s_seen * strength / (255.0f * 255.0f));
}
} // namespace

void PlayJcSpecial() // 326 PLAY_JC_SPECIAL
{
	const auto feature = Pop().intVal;
	auto& intro = g_intro;
	SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "PLAY_JC_SPECIAL {}", feature);
	switch (feature)
	{
	case SJC_CREATE_FIRE_BALL:
		intro.state = 0;
		CreateIntroFireBall();
		break;
	case SJC_CAMERA_FOLLOW_FIRE_BALL:
		if (intro.state == 0)
		{
			intro.cameraFollows = true;
		}
		break;
	case SJC_CANCEL_CAMERA_FOLLOW_FIRE_BALL:
		if (intro.state == 0)
		{
			intro.cameraFollows = false;
		}
		break;
	case SJC_CREATE_HAND_WITH_KID:
		// The same hand (made now if it wasn't), on the beach with the put-down, waiting for ACTIVATED_HAND.
		ReadIntroEBone();
		RemoveIntroFireBall();
		intro.cameraFollows = false;
		PlaceIntroHand(entt::hashed_string("hand_intro").value(), k_IntroPutDownHand, k_IntroPutDownAngle);
		intro.activated = false;
		intro.carrying = false;
		intro.state = 4;
		break;
	case SJC_ACTIVATED_HAND:
		if (intro.state != 4)
		{
			break;
		}
		if (!intro.activated)
		{
			intro.activated = true;
			intro.carrying = true;
		}
		else
		{
			// The second: on to the hand's leaving, the boy let go.
			intro.handMs = k_IntroSecondActivateMs;
			intro.carrying = false;
		}
		break;
	case SJC_CLEAN_INTRO:
		ReleaseIntro();
		break;
	default:
		// 3 clouds, 6 the little boat, 14/15 bookmarks shown or hidden: not yet.
		SPDLOG_LOGGER_WARN(spdlog::get("scripting"), "PLAY_JC_SPECIAL {} not implemented", feature);
		break;
	}
}

void IsPlayingJcSpecial() // 327 IS_PLAYING_JC_SPECIAL
{
	// GScript::IsPlayingJCSpecial: 1 for all but START_HAND_WITH_BOY, which is whether the pick-up has finished.
	const auto feature = Pop().intVal;
	Pushb(feature == SJC_START_HAND_WITH_BOY ? g_intro.pickUpDone : true);
}

void VortexParameters() // 328 VORTEX_PARAMETERS
{
	[[maybe_unused]] const auto flock = Pop().uintVal;
	[[maybe_unused]] const auto radius = Popf();
	[[maybe_unused]] const auto distance = Popf();
	[[maybe_unused]] const auto position = PopVec();
	[[maybe_unused]] const auto town = Pop().uintVal;
	[[maybe_unused]] const auto vortex = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void LoadCreature() // 329 LOAD_CREATURE
{
	[[maybe_unused]] const auto position = PopVec();
	[[maybe_unused]] const auto player = Popf();
	[[maybe_unused]] const auto mindFilename = PopString();
	[[maybe_unused]] const auto type = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void IsSpellCharging() // 330 IS_SPELL_CHARGING
{
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushb(false);
}

void IsThatSpellCharging() // 331 IS_THAT_SPELL_CHARGING
{
	[[maybe_unused]] const auto unk1 = Pop().intVal;
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushb(false);
}

void OpposingCreature() // 332 OPPOSING_CREATURE
{
	[[maybe_unused]] const auto god = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushi(0);
}

void FlockWithinLimits() // 333 FLOCK_WITHIN_LIMITS
{
	[[maybe_unused]] const auto object = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushb(false);
}

void HighlightProperties() // 334 HIGHLIGHT_PROPERTIES
{
	[[maybe_unused]] const auto category = Pop().intVal;
	[[maybe_unused]] const auto text = Pop().intVal;
	[[maybe_unused]] const auto object = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void LastMusicLine() // 335 LAST_MUSIC_LINE
{
	const auto line = Popf();
	// Whether the song has reached its line `line`. Land 1's Missionaries show each verse's 12 lines on it, and their
	// cutscene moves on by them; unanswered, each line waited out its 10 s fallback and the scene ran 20 s past the
	// 40 s song. The packs keep no line times, so the 12 lines are spread evenly over the song: line n from (n-1)/12.
	constexpr float k_VerseLines = 12.0f;
	const bool verse = g_musicNumber >= 51 && g_musicNumber <= 53;
	const float elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - g_musicStart).count();
	const bool reached = verse && g_musicSeconds > 0.0f && elapsed >= (line - 1.0f) / k_VerseLines * g_musicSeconds;
	if (reached)
	{
		static float s_lastLogged = 0.0f;
		if (s_lastLogged != line)
		{
			s_lastLogged = line;
			SPDLOG_LOGGER_INFO(spdlog::get("audio"), "LAST_MUSIC_LINE {}: reached at {:.1f} s of {:.1f}", line, elapsed, g_musicSeconds);
		}
	}
	Pushb(reached);
}

void HandDemoTrigger() // 336 HAND_DEMO_TRIGGER
{
	const bool triggered = g_handDemo.triggerPending;
	g_handDemo.triggerPending = false;
	Pushb(triggered);
}

void GetBellyPosition() // 337 GET_BELLY_POSITION
{
	[[maybe_unused]] const auto object = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushv(0.0f); // x
	Pushv(0.0f); // y
	Pushv(0.0f); // z
}

void SetCreatureCreedProperties() // 338 SET_CREATURE_CREED_PROPERTIES
{
	[[maybe_unused]] const auto time = Popf();
	[[maybe_unused]] const auto power = Popf();
	[[maybe_unused]] const auto scale = Popf();
	[[maybe_unused]] const auto handGlow = Pop().intVal;
	[[maybe_unused]] const auto creature = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void GameThingCanViewCamera() // 339 GAME_THING_CAN_VIEW_CAMERA
{
	[[maybe_unused]] const auto degrees = Popf();
	[[maybe_unused]] const auto object = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushb(false);
}

// Lines said aloud without a subtitle (GAME_PLAY_SAY_SOUND_EFFECT): the number is a text's, spoken with its voice
// (the text table, TextSamples.inc). Land 1's opening: the family calling the boy back from the water, which was
// silent, so the first heard was "you saved our boy" (Mike, 2026-10-18). Kept to answer SAY_SOUND_EFFECT_PLAYING.
std::unordered_map<int32_t, entt::entity> g_saidLines;

void GamePlaySaySoundEffect() // 340 GAME_PLAY_SAY_SOUND_EFFECT
{
	const auto withPosition = static_cast<bool>(Pop().intVal);
	const auto position = PopVec();
	const auto sound = Pop().intVal;
	[[maybe_unused]] const auto extra = static_cast<bool>(Pop().intVal);
	if (sound < 0 || static_cast<size_t>(sound) >= k_TextSamples.size() || k_TextSamples.at(static_cast<size_t>(sound)).first == 0)
	{
		SPDLOG_LOGGER_WARN(spdlog::get("audio"), "GAME_PLAY_SAY_SOUND_EFFECT {}: no voice for it", sound);
		return;
	}
	const auto& [bank, sample] = k_TextSamples.at(static_cast<size_t>(sound));
	g_saidLines[sound] = PlayBankSound(bank, sample, withPosition ? &position : nullptr);
	SPDLOG_LOGGER_INFO(spdlog::get("audio"), "GAME_PLAY_SAY_SOUND_EFFECT {}{}", sound, withPosition ? " (where it is said)" : "");
}

void SetTownDesireBoost() // 341 SET_TOWN_DESIRE_BOOST
{
	[[maybe_unused]] const auto boost = Popf();
	[[maybe_unused]] const auto desire = Pop().intVal;
	[[maybe_unused]] const auto object = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void IsLockedInteraction() // 342 IS_LOCKED_INTERACTION
{
	// Whether the hand is in a locked interaction with it: facing the creature, the action button held on it.
	const auto object = Pop().uintVal;
	Pushb(g_interaction && g_interaction->creature == object);
}

void SetCreatureName() // 343 SET_CREATURE_NAME
{
	[[maybe_unused]] const auto textID = Pop().intVal;
	[[maybe_unused]] const auto creature = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void ComputerPlayerReady() // 344 COMPUTER_PLAYER_READY
{
	[[maybe_unused]] const auto player = Popf();
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushb(false);
}

void EnableDisableComputerPlayer345() // 345 ENABLE_DISABLE_COMPUTER_PLAYER
{
	[[maybe_unused]] const auto player = Popf();
	[[maybe_unused]] const auto pause = static_cast<bool>(Pop().intVal);
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void ClearActorMind() // 346 CLEAR_ACTOR_MIND
{
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void EnterExitCitadel() // 347 ENTER_EXIT_CITADEL
{
	const auto enter = static_cast<bool>(Pop().intVal);
	if (Locator::temple::has_value())
	{
		auto& temple = Locator::temple::value();
		const auto active = temple.Active();
		if (enter != active)
		{
			if (enter)
			{
				temple.Activate();
			}
			else
			{
				temple.Deactivate();
			}
		}
	}
	else
	{
		SPDLOG_LOGGER_DEBUG(spdlog::get("game"), "No temple");
	}
}

void StartAngleSound348() // 348 START_ANGLE_SOUND
{
	[[maybe_unused]] const auto enable = static_cast<bool>(Pop().intVal);
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void ThingJcSpecial() // 349 THING_JC_SPECIAL
{
	const auto target = Pop().uintVal;
	const auto feature = Pop().intVal;
	[[maybe_unused]] const auto enable = static_cast<bool>(Pop().intVal);
	// GScript::ThingJCSpecial (on a SET_HIGH_GRAPHICS_DETAIL villager): I_AM_THE_LITTLE_BOY marks the one the opening's
	// hand carries (its SuperVillager's flag 1, read by GLandscape::Draw); CLEAN_INTRO is Intro::ReleaseAll.
	switch (feature)
	{
	case SJC_I_AM_THE_LITTLE_BOY:
		g_intro.littleBoy = target;
		break;
	case SJC_CLEAN_INTRO:
		ReleaseIntro();
		break;
	default:
		// 8 blend angle, 9-11 turns, 16/17, 19 always visible: not yet.
		SPDLOG_LOGGER_WARN(spdlog::get("scripting"), "THING_JC_SPECIAL {} on {} not implemented", feature, target);
		break;
	}
}

void MusicPlayed350() // 350 MUSIC_PLAYED
{
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushi(0);
}

void UpdateSnapshotPicture() // 351 UPDATE_SNAPSHOT_PICTURE
{
	[[maybe_unused]] const auto challengeID = Pop().intVal;
	[[maybe_unused]] const auto takingPicture = static_cast<bool>(Pop().intVal);
	[[maybe_unused]] const auto titleStrID = Pop().intVal;
	[[maybe_unused]] const auto alignment = Popf();
	[[maybe_unused]] const auto success = Popf();
	[[maybe_unused]] const auto focus = PopVec();
	[[maybe_unused]] const auto position = PopVec();
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void StopScriptsInFilesExcluding() // 352 STOP_SCRIPTS_IN_FILES_EXCLUDING
{
	const auto scriptNames = PopString();
	const auto sourceFilenames = PopString();

	const auto names = GetUniqueWords(scriptNames);
	const auto filenames = GetUniqueWords(sourceFilenames);
	auto& lhvm = Locator::vm::value();
	lhvm.StopScripts([&names, &filenames](const std::string& name, const std::string& filename) -> bool {
		return filenames.contains(filename) && !names.contains(name);
	});
}

void CreateRandomVillagerOfTribe() // 353 CREATE_RANDOM_VILLAGER_OF_TRIBE
{
	[[maybe_unused]] const auto position = PopVec();
	[[maybe_unused]] const auto tribe = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pusho(0);
}

void ToggleLeash() // 354 TOGGLE_LEASH
{
	[[maybe_unused]] const auto player = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void GameSetMana() // 355 GAME_SET_MANA
{
	[[maybe_unused]] const auto mana = Popf();
	[[maybe_unused]] const auto object = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void SetMagicProperties() // 356 SET_MAGIC_PROPERTIES
{
	[[maybe_unused]] const auto duration = Popf();
	const auto magicType = Pop().intVal;
	const auto object = Pop().uintVal;
	// On a miracle dispenser (GiveSpellDispenserReward): which miracle it gives - MagicType numbering (Land 1's
	// Pied Piper gives its GlobalHealSpell as 10, HEAL; Land 4 its shield dispenser as 19, SHIELD).
	const int seed = SpellSeedOfMagic(magicType);
	g_dispensers[object].seed = seed;
	SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "SET_MAGIC_PROPERTIES {}: magic {} (spell seed {})", object, magicType, seed);
}

void SetGameSound() // 357 SET_GAME_SOUND
{
	[[maybe_unused]] const auto enable = static_cast<bool>(Pop().intVal);
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void SexIsMale() // 358 SEX_IS_MALE
{
	[[maybe_unused]] const auto object = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushb(false);
}

void GetFirstHelp() // 359 GET_FIRST_HELP
{
	[[maybe_unused]] const auto object = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushf(0.0f);
}

void GetLastHelp() // 360 GET_LAST_HELP
{
	[[maybe_unused]] const auto object = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushf(0.0f);
}

void IsActive() // 361 IS_ACTIVE
{
	[[maybe_unused]] const auto object = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushb(false);
}

void SetBookmarkPosition() // 362 SET_BOOKMARK_POSITION
{
	[[maybe_unused]] const auto unk3 = Pop().intVal;
	[[maybe_unused]] const auto unk2 = Pop().intVal;
	[[maybe_unused]] const auto unk1 = Pop().intVal;
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void SetScaffoldProperties() // 363 SET_SCAFFOLD_PROPERTIES
{
	[[maybe_unused]] const auto destroy = static_cast<bool>(Pop().intVal);
	[[maybe_unused]] const auto size = Popf();
	[[maybe_unused]] const auto type = Pop().intVal;
	[[maybe_unused]] const auto object = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void SetComputerPlayerPersonality() // 364 SET_COMPUTER_PLAYER_PERSONALITY
{
	[[maybe_unused]] const auto probability = Popf();
	[[maybe_unused]] const auto aspect = PopString();
	[[maybe_unused]] const auto player = Popf();
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void SetComputerPlayerSuppression() // 365 SET_COMPUTER_PLAYER_SUPPRESSION
{
	[[maybe_unused]] const auto unk2 = Pop().intVal;
	[[maybe_unused]] const auto unk1 = Pop().intVal;
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void ForceComputerPlayerAction() // 366 FORCE_COMPUTER_PLAYER_ACTION
{
	[[maybe_unused]] const auto obj2 = Pop().uintVal;
	[[maybe_unused]] const auto obj1 = Pop().uintVal;
	[[maybe_unused]] const auto action = PopString();
	[[maybe_unused]] const auto player = Popf();
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void QueueComputerPlayerAction() // 367 QUEUE_COMPUTER_PLAYER_ACTION
{
	[[maybe_unused]] const auto obj2 = Pop().uintVal;
	[[maybe_unused]] const auto obj1 = Pop().uintVal;
	[[maybe_unused]] const auto action = PopString();
	[[maybe_unused]] const auto player = Popf();
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void GetTownWithId() // 368 GET_TOWN_WITH_ID
{
	[[maybe_unused]] const auto id = Popf();
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pusho(0);
}

void SetDisciple() // 369 SET_DISCIPLE
{
	[[maybe_unused]] const auto withSound = static_cast<bool>(Pop().intVal);
	[[maybe_unused]] const auto discipleType = Pop().intVal;
	[[maybe_unused]] const auto object = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void ReleaseComputerPlayer() // 370 RELEASE_COMPUTER_PLAYER
{
	[[maybe_unused]] const auto player = Popf();
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void SetComputerPlayerSpeed() // 371 SET_COMPUTER_PLAYER_SPEED
{
	[[maybe_unused]] const auto speed = Popf();
	[[maybe_unused]] const auto player = Popf();
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void SetFocusFollowComputerPlayer() // 372 SET_FOCUS_FOLLOW_COMPUTER_PLAYER
{
	[[maybe_unused]] const auto player = Popf();
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void SetPositionFollowComputerPlayer() // 373 SET_POSITION_FOLLOW_COMPUTER_PLAYER
{
	[[maybe_unused]] const auto player = Popf();
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void CallComputerPlayer() // 374 CALL_COMPUTER_PLAYER
{
	[[maybe_unused]] const auto player = Popf();
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pusho(0);
}

void CallBuildingInTown() // 375 CALL_BUILDING_IN_TOWN
{
	[[maybe_unused]] const auto unk3 = Pop().intVal;
	[[maybe_unused]] const auto unk2 = Pop().intVal;
	[[maybe_unused]] const auto unk1 = Pop().intVal;
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushi(0);
}

void SetCanBuildWorshipsite() // 376 SET_CAN_BUILD_WORSHIPSITE
{
	[[maybe_unused]] const auto object = Pop().uintVal;
	[[maybe_unused]] const auto enable = static_cast<bool>(Pop().intVal);
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void GetFacingCameraPosition() // 377 GET_FACING_CAMERA_POSITION
{
	[[maybe_unused]] const auto distance = Popf();
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushv(0.0f); // x
	Pushv(0.0f); // y
	Pushv(0.0f); // z
}

void SetComputerPlayerAttitude() // 378 SET_COMPUTER_PLAYER_ATTITUDE
{
	[[maybe_unused]] const auto attitude = Popf();
	[[maybe_unused]] const auto player2 = Popf();
	[[maybe_unused]] const auto player1 = Popf();
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void GetComputerPlayerAttitude() // 379 GET_COMPUTER_PLAYER_ATTITUDE
{
	[[maybe_unused]] const auto player2 = Popf();
	[[maybe_unused]] const auto player1 = Popf();
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushf(0.0f);
}

void LoadComputerPlayerPersonality() // 380 LOAD_COMPUTER_PLAYER_PERSONALITY
{
	[[maybe_unused]] const auto unk1 = Pop().intVal;
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void SaveComputerPlayerPersonality() // 381 SAVE_COMPUTER_PLAYER_PERSONALITY
{
	[[maybe_unused]] const auto unk1 = Pop().intVal;
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void SetPlayerAlly() // 382 SET_PLAYER_ALLY
{
	[[maybe_unused]] const auto percentage = Popf();
	[[maybe_unused]] const auto player2 = Popf();
	[[maybe_unused]] const auto player1 = Popf();
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void CallFlying() // 383 CALL_FLYING
{
	[[maybe_unused]] const auto excluding = static_cast<bool>(Pop().intVal);
	[[maybe_unused]] const auto radius = Popf();
	[[maybe_unused]] const auto position = PopVec();
	[[maybe_unused]] const auto subtype = Pop().intVal;
	[[maybe_unused]] const auto type = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pusho(0);
}

void SetObjectFadeIn() // 384 SET_OBJECT_FADE_IN
{
	[[maybe_unused]] const auto time = Popf();
	[[maybe_unused]] const auto object = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void IsAffectedBySpell() // 385 IS_AFFECTED_BY_SPELL
{
	[[maybe_unused]] const auto object = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushb(false);
}

void SetMagicInObject() // 386 SET_MAGIC_IN_OBJECT
{
	[[maybe_unused]] const auto object = Pop().uintVal;
	[[maybe_unused]] const auto MAGIC_TYPE = Pop().intVal;
	[[maybe_unused]] const auto enable = static_cast<bool>(Pop().intVal);
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void IdAdultSize() // 387 ID_ADULT_SIZE
{
	[[maybe_unused]] const auto container = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushf(0.0f);
}

void ObjectCapacity() // 388 OBJECT_CAPACITY
{
	[[maybe_unused]] const auto container = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushf(0.0f);
}

void ObjectAdultCapacity() // 389 OBJECT_ADULT_CAPACITY
{
	[[maybe_unused]] const auto container = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushf(0.0f);
}

void SetCreatureAutoFighting() // 390 SET_CREATURE_AUTO_FIGHTING
{
	const auto creature = Pop().uintVal;
	const auto enable = static_cast<bool>(Pop().intVal);
	if (auto it = g_fighters.find(creature); it != g_fighters.end())
	{
		it->second.autoFight = enable;
	}
	SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "SET_CREATURE_AUTO_FIGHTING {} {}", creature, enable);
}

void IsAutoFighting() // 391 IS_AUTO_FIGHTING
{
	const auto creature = Pop().uintVal;
	const auto it = g_fighters.find(creature);
	Pushb(it != g_fighters.end() && it->second.autoFight);
}

void SetCreatureQueueFightMove() // 392 SET_CREATURE_QUEUE_FIGHT_MOVE
{
	const auto move = Pop().intVal;
	const auto creature = Pop().uintVal;
	// The nth attack of ctrspec27.txt's: 1-2 high, 3-4 middle, 5-6 low.
	if (auto it = g_fighters.find(creature); it != g_fighters.end() && move >= 1)
	{
		it->second.queue.push_back({false, std::min((move - 1) / 2, 2)});
	}
	SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "SET_CREATURE_QUEUE_FIGHT_MOVE {} {}", creature, move);
}

void SetCreatureQueueFightSpell() // 393 SET_CREATURE_QUEUE_FIGHT_SPELL
{
	[[maybe_unused]] const auto spell = Pop().intVal;
	[[maybe_unused]] const auto creature = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void SetCreatureQueueFightStep() // 394 SET_CREATURE_QUEUE_FIGHT_STEP
{
	[[maybe_unused]] const auto step = Pop().intVal;
	[[maybe_unused]] const auto creature = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void GetCreatureFightAction() // 395 GET_CREATURE_FIGHT_ACTION
{
	const auto creature = Pop().uintVal;
	const auto it = g_fighters.find(creature);
	Pushi(it != g_fighters.end() ? it->second.action : 0);
}

void CreatureFightQueueHits() // 396 CREATURE_FIGHT_QUEUE_HITS
{
	const auto creature = Pop().uintVal;
	const auto it = g_fighters.find(creature);
	Pushf(it != g_fighters.end() ? static_cast<float>(it->second.queue.size()) : 0.0f);
}

void SquareRoot() // 397 SQUARE_ROOT
{
	const auto value = Popf();
	auto root = 0.0f;
	if (value > 0.0f)
	{
		root = std::sqrt(value);
	}
	Pushf(root);
}

void GetPlayerAlly() // 398 GET_PLAYER_ALLY
{
	[[maybe_unused]] const auto player2 = Popf();
	[[maybe_unused]] const auto player1 = Popf();
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushf(0.0f);
}

void SetPlayerWindResistance() // 399 SET_PLAYER_WIND_RESISTANCE
{
	[[maybe_unused]] const auto unk1 = Pop().intVal;
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushi(0);
}

void GetPlayerWindResistance() // 400 GET_PLAYER_WIND_RESISTANCE
{
	[[maybe_unused]] const auto unk1 = Pop().intVal;
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushi(0);
}

void PauseUnpauseClimateSystem() // 401 PAUSE_UNPAUSE_CLIMATE_SYSTEM
{
	[[maybe_unused]] const auto enable = static_cast<bool>(Pop().intVal);
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void PauseUnpauseStormCreationInClimateSystem() // 402 PAUSE_UNPAUSE_STORM_CREATION_IN_CLIMATE_SYSTEM
{
	[[maybe_unused]] const auto enable = static_cast<bool>(Pop().intVal);
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void GetManaForSpell() // 403 GET_MANA_FOR_SPELL
{
	[[maybe_unused]] const auto spell = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushf(0.0f);
}

void KillStormsInArea() // 404 KILL_STORMS_IN_AREA
{
	[[maybe_unused]] const auto radius = Popf();
	[[maybe_unused]] const auto position = PopVec();
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void InsideTemple() // 405 INSIDE_TEMPLE
{
	Pushb(InTemple()); // Land 1's CitadelGuideCinema waits for the player to click the door and go in
}

void RestartObject() // 406 RESTART_OBJECT
{
	[[maybe_unused]] const auto object = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void SetGameTimeProperties() // 407 SET_GAME_TIME_PROPERTIES
{
	[[maybe_unused]] const auto unk2 = Pop().intVal;
	[[maybe_unused]] const auto unk1 = Pop().intVal;
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void ResetGameTimeProperties() // 408 RESET_GAME_TIME_PROPERTIES
{
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void SoundExists() // 409 SOUND_EXISTS
{
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushb(false);
}

void GetTownWorshipDeaths() // 410 GET_TOWN_WORSHIP_DEATHS
{
	[[maybe_unused]] const auto town = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushf(0.0f);
}

void GameClearDialogue() // 411 GAME_CLEAR_DIALOGUE
{
	openblack::subtitles::Clear();
}

void GameCloseDialogue() // 412 GAME_CLOSE_DIALOGUE
{
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void GetHandState() // 413 GET_HAND_STATE
{
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushi(0);
}

void SetInterfaceCitadel() // 414 SET_INTERFACE_CITADEL
{
	g_citadelInterface = static_cast<bool>(Pop().intVal); // whether the Temple's door takes the player in
}

void MapScriptFunction() // 415 MAP_SCRIPT_FUNCTION
{
	[[maybe_unused]] const auto command = PopString();
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void WithinRotation() // 416 WITHIN_ROTATION
{
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushb(false);
}

void GetPlayerTownTotal() // 417 GET_PLAYER_TOWN_TOTAL
{
	[[maybe_unused]] const auto player = Popf();
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushf(0.0f);
}

void SpiritScreenPoint() // 418 SPIRIT_SCREEN_POINT
{
	[[maybe_unused]] const auto unk2 = Pop().intVal;
	[[maybe_unused]] const auto unk1 = Pop().intVal;
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void KeyDown() // 419 KEY_DOWN
{
	[[maybe_unused]] const auto key = Pop().intVal;
	// TODO(Daniels118): implement this (translate key to physical key code)
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushb(false);
}

void SetFightExit() // 420 SET_FIGHT_EXIT
{
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void GetObjectClicked() // 421 GET_OBJECT_CLICKED
{
	Pusho(g_clickedThing);
}

void GetMana() // 422 GET_MANA
{
	[[maybe_unused]] const auto worshipSite = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushf(0.0f);
}

void ClearPlayerSpellCharging() // 423 CLEAR_PLAYER_SPELL_CHARGING
{
	[[maybe_unused]] const auto player = Popf();
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void StopSoundEffect() // 424 STOP_SOUND_EFFECT
{
	[[maybe_unused]] const auto soundbank = Pop().intVal;
	[[maybe_unused]] const auto sound = Pop().intVal;
	[[maybe_unused]] const auto alwaysFalse = static_cast<bool>(Pop().intVal);
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void GetTotemStatue() // 425 GET_TOTEM_STATUE
{
	[[maybe_unused]] const auto town = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pusho(0);
}

void SetSetOnFire() // 426 SET_SET_ON_FIRE
{
	[[maybe_unused]] const auto object = Pop().uintVal;
	[[maybe_unused]] const auto enable = static_cast<bool>(Pop().intVal);
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void SetLandBalance() // 427 SET_LAND_BALANCE
{
	[[maybe_unused]] const auto unk1 = Pop().intVal;
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void SetObjectBeliefScale() // 428 SET_OBJECT_BELIEF_SCALE
{
	[[maybe_unused]] const auto unk1 = Pop().intVal;
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void StartImmersion() // 429 START_IMMERSION
{
	[[maybe_unused]] const auto effect = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void StopImmersion() // 430 STOP_IMMERSION
{
	[[maybe_unused]] const auto effect = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void StopAllImmersion() // 431 STOP_ALL_IMMERSION
{
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void SetCreatureInTemple() // 432 SET_CREATURE_IN_TEMPLE
{
	[[maybe_unused]] const auto enable = static_cast<bool>(Pop().intVal);
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void GameDrawText() // 433 GAME_DRAW_TEXT
{
	[[maybe_unused]] const auto fade = Popf();
	[[maybe_unused]] const auto size = Popf();
	[[maybe_unused]] const auto height = Popf();
	[[maybe_unused]] const auto width = Popf();
	[[maybe_unused]] const auto down = Popf();
	[[maybe_unused]] const auto across = Popf();
	[[maybe_unused]] const auto textID = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void GameDrawTempText() // 434 GAME_DRAW_TEMP_TEXT
{
	[[maybe_unused]] const auto fade = Popf();
	[[maybe_unused]] const auto size = Popf();
	[[maybe_unused]] const auto height = Popf();
	[[maybe_unused]] const auto width = Popf();
	[[maybe_unused]] const auto down = Popf();
	[[maybe_unused]] const auto across = Popf();
	[[maybe_unused]] const auto string = PopString();
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void FadeAllDrawText() // 435 FADE_ALL_DRAW_TEXT
{
	[[maybe_unused]] const auto time = Popf();
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void SetDrawTextColour() // 436 SET_DRAW_TEXT_COLOUR
{
	[[maybe_unused]] const auto blue = Popf();
	[[maybe_unused]] const auto green = Popf();
	[[maybe_unused]] const auto red = Popf();
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void SetClippingWindow() // 437 SET_CLIPPING_WINDOW
{
	[[maybe_unused]] const auto time = Popf();
	[[maybe_unused]] const auto height = Popf();
	[[maybe_unused]] const auto width = Popf();
	[[maybe_unused]] const auto down = Popf();
	[[maybe_unused]] const auto across = Popf();
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void ClearClippingWindow() // 438 CLEAR_CLIPPING_WINDOW
{
	[[maybe_unused]] const auto time = Popf();
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void SaveGameInSlot() // 439 SAVE_GAME_IN_SLOT
{
	[[maybe_unused]] const auto slot = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void SetObjectCarrying() // 440 SET_OBJECT_CARRYING
{
	[[maybe_unused]] const auto carriedObj = Pop().intVal;
	[[maybe_unused]] const auto object = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void PosValidForCreature() // 441 POS_VALID_FOR_CREATURE
{
	[[maybe_unused]] const auto position = PopVec();
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushb(false);
}

void GetTimeSinceObjectAttacked() // 442 GET_TIME_SINCE_OBJECT_ATTACKED
{
	[[maybe_unused]] const auto town = Pop().uintVal;
	[[maybe_unused]] const auto player = Popf();
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushf(0.0f);
}

void GetTownAndVillagerHealthTotal() // 443 GET_TOWN_AND_VILLAGER_HEALTH_TOTAL
{
	[[maybe_unused]] const auto town = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushf(0.0f);
}

void GameAddForBuilding() // 444 GAME_ADD_FOR_BUILDING
{
	[[maybe_unused]] const auto unk1 = Pop().intVal;
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void EnableDisableAlignmentMusic() // 445 ENABLE_DISABLE_ALIGNMENT_MUSIC
{
	[[maybe_unused]] const auto enable = static_cast<bool>(Pop().intVal);
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void GetDeadLiving() // 446 GET_DEAD_LIVING
{
	[[maybe_unused]] const auto radius = Popf();
	[[maybe_unused]] const auto position = PopVec();
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pusho(0);
}

void AttachSoundTag() // 447 ATTACH_SOUND_TAG
{
	[[maybe_unused]] const auto target = Pop().uintVal;
	[[maybe_unused]] const auto soundbank = Pop().intVal;
	[[maybe_unused]] const auto sound = Pop().intVal;
	[[maybe_unused]] const auto threeD = static_cast<bool>(Pop().intVal);
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void DetachSoundTag() // 448 DETACH_SOUND_TAG
{
	[[maybe_unused]] const auto target = Pop().uintVal;
	[[maybe_unused]] const auto soundbank = Pop().intVal;
	[[maybe_unused]] const auto sound = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void GetSacrificeTotal() // 449 GET_SACRIFICE_TOTAL
{
	[[maybe_unused]] const auto worshipSite = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushf(0.0f);
}

void GameSoundPlaying() // 450 GAME_SOUND_PLAYING
{
	[[maybe_unused]] const auto soundbank = Pop().intVal;
	[[maybe_unused]] const auto sound = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushb(false);
}

void GetTemplePosition() // 451 GET_TEMPLE_POSITION
{
	[[maybe_unused]] const auto player = Popf();
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushv(0.0f); // x
	Pushv(0.0f); // y
	Pushv(0.0f); // z
}

void CreatureAutoscale() // 452 CREATURE_AUTOSCALE
{
	const auto size = Popf();
	const auto creature = Pop().uintVal;
	const auto enable = static_cast<bool>(Pop().intVal);
	// Sized against the player's creature: Land 1's ogre guardian stands 1.2 times yours.
	if (enable)
	{
		SizeCreatureAs(static_cast<entt::entity>(creature), PlayersCreature(), size);
		SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "CREATURE_AUTOSCALE creature {}: {:.1f} x the height of yours", creature, size);
	}
}

void GetSpellIconInTemple() // 453 GET_SPELL_ICON_IN_TEMPLE
{
	[[maybe_unused]] const auto temple = Pop().uintVal;
	[[maybe_unused]] const auto spell = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pusho(0);
}

void GameClearComputerPlayerActions() // 454 GAME_CLEAR_COMPUTER_PLAYER_ACTIONS
{
	[[maybe_unused]] const auto player = Popf();
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void GetFirstInContainer() // 455 GET_FIRST_IN_CONTAINER
{
	[[maybe_unused]] const auto container = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pusho(0);
}

void GetNextInContainer() // 456 GET_NEXT_IN_CONTAINER
{
	[[maybe_unused]] const auto after = Pop().uintVal;
	[[maybe_unused]] const auto container = Pop().uintVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pusho(0);
}

void GetTempleEntrancePosition() // 457 GET_TEMPLE_ENTRANCE_POSITION
{
	const auto height = Popf();
	[[maybe_unused]] const auto radius = Popf();
	[[maybe_unused]] const auto player = Popf();
	const auto door = TempleDoor().value_or(glm::vec3(0.0f));
	Pushv(door.x);
	Pushv(door.y + height);
	Pushv(door.z);
}

void SaySoundEffectPlaying() // 458 SAY_SOUND_EFFECT_PLAYING
{
	const auto sound = Pop().intVal;
	[[maybe_unused]] const auto alwaysFalse = static_cast<bool>(Pop().intVal);
	const auto said = g_saidLines.find(sound);
	const bool playing = said != g_saidLines.end() && said->second != entt::null && Locator::audio::value().EmitterExists(said->second);
	if (!playing && said != g_saidLines.end())
	{
		g_saidLines.erase(said);
	}
	Pushb(playing);
}

void SetHandDemoKeys() // 459 SET_HAND_DEMO_KEYS
{
	[[maybe_unused]] const auto unk0 = Pop().intVal;
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
}

void CanSkipTutorial() // 460 CAN_SKIP_TUTORIAL
{
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushb(false);
}

void CanSkipCreatureTraining() // 461 CAN_SKIP_CREATURE_TRAINING
{
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushb(false);
}

void IsKeepingOldCreature() // 462 IS_KEEPING_OLD_CREATURE
{
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushb(false);
}

void CurrentProfileHasCreature() // 463 CURRENT_PROFILE_HAS_CREATURE
{
	// TODO(Daniels118): implement this
	SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "CHLApi Function {}() not implemented.", __func__);
	Pushb(false);
}

void CHLApi::InitFunctionsTable0()
{
	CREATE_FUNCTION_BINDING("NONE", 0, 0, None);
	CREATE_FUNCTION_BINDING("SET_CAMERA_POSITION", 3, 0, SetCameraPosition);
	CREATE_FUNCTION_BINDING("SET_CAMERA_FOCUS", 3, 0, SetCameraFocus);
	CREATE_FUNCTION_BINDING("MOVE_CAMERA_POSITION", 4, 0, MoveCameraPosition);
	CREATE_FUNCTION_BINDING("MOVE_CAMERA_FOCUS", 4, 0, MoveCameraFocus);
	CREATE_FUNCTION_BINDING("GET_CAMERA_POSITION", 0, 3, GetCameraPosition);
	CREATE_FUNCTION_BINDING("GET_CAMERA_FOCUS", 0, 3, GetCameraFocus);
	CREATE_FUNCTION_BINDING("SPIRIT_EJECT", 1, 0, SpiritEject);
	CREATE_FUNCTION_BINDING("SPIRIT_HOME", 1, 0, SpiritHome);
	CREATE_FUNCTION_BINDING("SPIRIT_POINT_POS", 5, 0, SpiritPointPos);
	CREATE_FUNCTION_BINDING("SPIRIT_POINT_GAME_THING", 3, 0, SpiritPointGameThing);
	CREATE_FUNCTION_BINDING("GAME_THING_FIELD_OF_VIEW", 1, 1, GameThingFieldOfView);
	CREATE_FUNCTION_BINDING("POS_FIELD_OF_VIEW", 3, 1, PosFieldOfView);
	CREATE_FUNCTION_BINDING("RUN_TEXT", 3, 0, RunText);
	CREATE_FUNCTION_BINDING("TEMP_TEXT", 3, 0, TempText);
	CREATE_FUNCTION_BINDING("TEXT_READ", 0, 1, TextRead);
	CREATE_FUNCTION_BINDING("GAME_THING_CLICKED", 1, 1, GameThingClicked);
	CREATE_FUNCTION_BINDING("SET_SCRIPT_STATE", 2, 0, SetScriptState);
	CREATE_FUNCTION_BINDING("SET_SCRIPT_STATE_POS", 4, 0, SetScriptStatePos);
	CREATE_FUNCTION_BINDING("SET_SCRIPT_FLOAT", 2, 0, SetScriptFloat);
	CREATE_FUNCTION_BINDING("SET_SCRIPT_ULONG", 3, 0, SetScriptUlong);
	CREATE_FUNCTION_BINDING("GET_PROPERTY", 2, 1, GetProperty);
	CREATE_FUNCTION_BINDING("SET_PROPERTY", 3, 0, SetProperty);
	CREATE_FUNCTION_BINDING("GET_POSITION", 1, 3, GetPosition);
	CREATE_FUNCTION_BINDING("SET_POSITION", 4, 0, SetPosition);
	CREATE_FUNCTION_BINDING("GET_DISTANCE", 6, 1, GetDistance);
	CREATE_FUNCTION_BINDING("CALL", 6, 1, Call);
	CREATE_FUNCTION_BINDING("CREATE", 5, 1, Create);
	CREATE_FUNCTION_BINDING("RANDOM", 2, 1, Random);
	CREATE_FUNCTION_BINDING("DLL_GETTIME", 0, 1, DllGettime);
	CREATE_FUNCTION_BINDING("START_CAMERA_CONTROL", 0, 1, StartCameraControl);
	CREATE_FUNCTION_BINDING("END_CAMERA_CONTROL", 0, 0, EndCameraControl);
	CREATE_FUNCTION_BINDING("SET_WIDESCREEN", 1, 0, SetWidescreen);
	CREATE_FUNCTION_BINDING("MOVE_GAME_THING", 5, 0, MoveGameThing);
	CREATE_FUNCTION_BINDING("SET_FOCUS", 4, 0, SetFocus);
	CREATE_FUNCTION_BINDING("HAS_CAMERA_ARRIVED", 0, 1, HasCameraArrived);
	CREATE_FUNCTION_BINDING("FLOCK_CREATE", 3, 1, FlockCreate);
	CREATE_FUNCTION_BINDING("FLOCK_ATTACH", 3, 1, FlockAttach);
	CREATE_FUNCTION_BINDING("FLOCK_DETACH", 2, 1, FlockDetach);
	CREATE_FUNCTION_BINDING("FLOCK_DISBAND", 1, 0, FlockDisband);
	CREATE_FUNCTION_BINDING("ID_SIZE", 1, 1, IdSize);
	CREATE_FUNCTION_BINDING("FLOCK_MEMBER", 2, 1, FlockMember);
	CREATE_FUNCTION_BINDING("GET_HAND_POSITION", 0, 3, GetHandPosition);
	CREATE_FUNCTION_BINDING("PLAY_SOUND_EFFECT", 6, 0, PlaySoundEffect);
	CREATE_FUNCTION_BINDING("START_MUSIC", 1, 0, StartMusic);
	CREATE_FUNCTION_BINDING("STOP_MUSIC", 0, 0, StopMusic);
	CREATE_FUNCTION_BINDING("ATTACH_MUSIC", 2, 0, AttachMusic);
	CREATE_FUNCTION_BINDING("DETACH_MUSIC", 1, 0, DetachMusic);
	CREATE_FUNCTION_BINDING("OBJECT_DELETE", 2, 0, ObjectDelete);
	CREATE_FUNCTION_BINDING("FOCUS_FOLLOW", 1, 0, FocusFollow);
	CREATE_FUNCTION_BINDING("POSITION_FOLLOW", 1, 0, PositionFollow);
	CREATE_FUNCTION_BINDING("CALL_NEAR", 7, 1, CallNear);
	CREATE_FUNCTION_BINDING("SPECIAL_EFFECT_POSITION", 5, 1, SpecialEffectPosition);
	CREATE_FUNCTION_BINDING("SPECIAL_EFFECT_OBJECT", 3, 1, SpecialEffectObject);
	CREATE_FUNCTION_BINDING("DANCE_CREATE", 6, 1, DanceCreate);
	CREATE_FUNCTION_BINDING("CALL_IN", 4, 1, CallIn);
	CREATE_FUNCTION_BINDING("CHANGE_INNER_OUTER_PROPERTIES", 4, 0, ChangeInnerOuterProperties);
	CREATE_FUNCTION_BINDING("SNAPSHOT", -1, 0, Snapshot);
	CREATE_FUNCTION_BINDING("GET_ALIGNMENT", 1, 1, GetAlignment);
	CREATE_FUNCTION_BINDING("SET_ALIGNMENT", 2, 0, SetAlignment);
	CREATE_FUNCTION_BINDING("INFLUENCE_OBJECT", 4, 1, InfluenceObject);
	CREATE_FUNCTION_BINDING("INFLUENCE_POSITION", 6, 1, InfluencePosition);
	CREATE_FUNCTION_BINDING("GET_INFLUENCE", 5, 1, GetInfluence);
	CREATE_FUNCTION_BINDING("SET_INTERFACE_INTERACTION", 1, 0, SetInterfaceInteraction);
	CREATE_FUNCTION_BINDING("PLAYED", 1, 1, Played);
	CREATE_FUNCTION_BINDING("RANDOM_ULONG", 2, 1, RandomUlong);
	CREATE_FUNCTION_BINDING("SET_GAMESPEED", 1, 0, SetGamespeed);
	CREATE_FUNCTION_BINDING("CALL_IN_NEAR", 8, 1, CallInNear);
	CREATE_FUNCTION_BINDING("OVERRIDE_STATE_ANIMATION", 2, 0, OverrideStateAnimation);
	CREATE_FUNCTION_BINDING("CREATURE_CREATE_RELATIVE_TO_CREATURE", 6, 1, CreatureCreateRelativeToCreature);
	CREATE_FUNCTION_BINDING("CREATURE_LEARN_EVERYTHING", 1, 0, CreatureLearnEverything);
	CREATE_FUNCTION_BINDING("CREATURE_SET_KNOWS_ACTION", 4, 0, CreatureSetKnowsAction);
	CREATE_FUNCTION_BINDING("CREATURE_SET_AGENDA_PRIORITY", 2, 0, CreatureSetAgendaPriority);
	CREATE_FUNCTION_BINDING("CREATURE_TURN_OFF_ALL_DESIRES", 1, 0, CreatureTurnOffAllDesires);
	CREATE_FUNCTION_BINDING("CREATURE_LEARN_DISTINCTION_ABOUT_ACTIVITY_OBJECT", 4, 0,
	                        CreatureLearnDistinctionAboutActivityObject);
	CREATE_FUNCTION_BINDING("CREATURE_DO_ACTION", 4, 0, CreatureDoAction);
	CREATE_FUNCTION_BINDING("IN_CREATURE_HAND", 2, 1, InCreatureHand);
	CREATE_FUNCTION_BINDING("CREATURE_SET_DESIRE_VALUE", 3, 0, CreatureSetDesireValue);
	CREATE_FUNCTION_BINDING("CREATURE_SET_DESIRE_ACTIVATED", 3, 0, CreatureSetDesireActivated78);
	CREATE_FUNCTION_BINDING("CREATURE_SET_DESIRE_ACTIVATED", 2, 0, CreatureSetDesireActivated79);
	CREATE_FUNCTION_BINDING("CREATURE_SET_DESIRE_MAXIMUM", 3, 0, CreatureSetDesireMaximum);
	CREATE_FUNCTION_BINDING("CONVERT_CAMERA_POSITION", 1, 3, ConvertCameraPosition);
	CREATE_FUNCTION_BINDING("CONVERT_CAMERA_FOCUS", 1, 3, ConvertCameraFocus);
	CREATE_FUNCTION_BINDING("CREATURE_SET_PLAYER", 1, 0, CreatureSetPlayer);
	CREATE_FUNCTION_BINDING("START_COUNTDOWN_TIMER", 1, 0, StartCountdownTimer);
	CREATE_FUNCTION_BINDING("CREATURE_INITIALISE_NUM_TIMES_PERFORMED_ACTION", 2, 0, CreatureInitialiseNumTimesPerformedAction);
	CREATE_FUNCTION_BINDING("CREATURE_GET_NUM_TIMES_ACTION_PERFORMED", 2, 1, CreatureGetNumTimesActionPerformed);
	CREATE_FUNCTION_BINDING("REMOVE_COUNTDOWN_TIMER", 0, 0, RemoveCountdownTimer);
	CREATE_FUNCTION_BINDING("GET_OBJECT_DROPPED", 1, 1, GetObjectDropped);
	CREATE_FUNCTION_BINDING("CLEAR_DROPPED_BY_OBJECT", 1, 0, ClearDroppedByObject);
	CREATE_FUNCTION_BINDING("CREATE_REACTION", 2, 0, CreateReaction);
	CREATE_FUNCTION_BINDING("REMOVE_REACTION", 1, 0, RemoveReaction);
	CREATE_FUNCTION_BINDING("GET_COUNTDOWN_TIMER", 0, 1, GetCountdownTimer);
	CREATE_FUNCTION_BINDING("START_DUAL_CAMERA", 2, 0, StartDualCamera);
	CREATE_FUNCTION_BINDING("UPDATE_DUAL_CAMERA", 2, 0, UpdateDualCamera);
	CREATE_FUNCTION_BINDING("RELEASE_DUAL_CAMERA", 0, 0, ReleaseDualCamera);
	CREATE_FUNCTION_BINDING("SET_CREATURE_HELP", 1, 0, SetCreatureHelp);
	CREATE_FUNCTION_BINDING("GET_TARGET_OBJECT", 1, 1, GetTargetObject);
	CREATE_FUNCTION_BINDING("CREATURE_DESIRE_IS", 2, 1, CreatureDesireIs);
	CREATE_FUNCTION_BINDING("COUNTDOWN_TIMER_EXISTS", 0, 1, CountdownTimerExists);
	CREATE_FUNCTION_BINDING("LOOK_GAME_THING", 2, 0, LookGameThing);
	CREATE_FUNCTION_BINDING("GET_OBJECT_DESTINATION", 1, 3, GetObjectDestination);
	CREATE_FUNCTION_BINDING("CREATURE_FORCE_FINISH", 1, 0, CreatureForceFinish);
	CREATE_FUNCTION_BINDING("HIDE_COUNTDOWN_TIMER", 0, 0, HideCountdownTimer);
	CREATE_FUNCTION_BINDING("GET_ACTION_TEXT_FOR_OBJECT", 1, 1, GetActionTextForObject);
	CREATE_FUNCTION_BINDING("CREATE_DUAL_CAMERA_WITH_POINT", 4, 0, CreateDualCameraWithPoint);
	CREATE_FUNCTION_BINDING("SET_CAMERA_TO_FACE_OBJECT", 2, 0, SetCameraToFaceObject);
	CREATE_FUNCTION_BINDING("MOVE_CAMERA_TO_FACE_OBJECT", 3, 0, MoveCameraToFaceObject);
}

void CHLApi::InitFunctionsTable1()
{
	CREATE_FUNCTION_BINDING("GET_MOON_PERCENTAGE", 0, 1, GetMoonPercentage);
	CREATE_FUNCTION_BINDING("POPULATE_CONTAINER", 4, 0, PopulateContainer);
	CREATE_FUNCTION_BINDING("ADD_REFERENCE", 1, 1, AddReference);
	CREATE_FUNCTION_BINDING("REMOVE_REFERENCE", 1, 1, RemoveReference);
	CREATE_FUNCTION_BINDING("SET_GAME_TIME", 1, 0, SetGameTime);
	CREATE_FUNCTION_BINDING("GET_GAME_TIME", 0, 1, GetGameTime);
	CREATE_FUNCTION_BINDING("GET_REAL_TIME", 0, 1, GetRealTime);
	CREATE_FUNCTION_BINDING("GET_REAL_DAY", 0, 1, GetRealDay115);
	CREATE_FUNCTION_BINDING("GET_REAL_DAY", 0, 1, GetRealDay116);
	CREATE_FUNCTION_BINDING("GET_REAL_MONTH", 0, 1, GetRealMonth);
	CREATE_FUNCTION_BINDING("GET_REAL_YEAR", 0, 1, GetRealYear);
	CREATE_FUNCTION_BINDING("RUN_CAMERA_PATH", 1, 0, RunCameraPath);
	CREATE_FUNCTION_BINDING("START_DIALOGUE", 0, 1, StartDialogue);
	CREATE_FUNCTION_BINDING("END_DIALOGUE", 0, 0, EndDialogue);
	CREATE_FUNCTION_BINDING("IS_DIALOGUE_READY", 0, 1, IsDialogueReady);
	CREATE_FUNCTION_BINDING("CHANGE_WEATHER_PROPERTIES", 6, 0, ChangeWeatherProperties);
	CREATE_FUNCTION_BINDING("CHANGE_LIGHTNING_PROPERTIES", 5, 0, ChangeLightningProperties);
	CREATE_FUNCTION_BINDING("CHANGE_TIME_FADE_PROPERTIES", 3, 0, ChangeTimeFadeProperties);
	CREATE_FUNCTION_BINDING("CHANGE_CLOUD_PROPERTIES", 4, 0, ChangeCloudProperties);
	CREATE_FUNCTION_BINDING("SET_HEADING_AND_SPEED", 5, 0, SetHeadingAndSpeed);
	CREATE_FUNCTION_BINDING("START_GAME_SPEED", 0, 0, StartGameSpeed);
	CREATE_FUNCTION_BINDING("END_GAME_SPEED", 0, 0, EndGameSpeed);
	CREATE_FUNCTION_BINDING("BUILD_BUILDING", 4, 0, BuildBuilding);
	CREATE_FUNCTION_BINDING("SET_AFFECTED_BY_WIND", 2, 0, SetAffectedByWind);
	CREATE_FUNCTION_BINDING("WIDESCREEN_TRANSISTION_FINISHED", 0, 1, WidescreenTransistionFinished);
	CREATE_FUNCTION_BINDING("GET_RESOURCE", 2, 1, GetResource);
	CREATE_FUNCTION_BINDING("ADD_RESOURCE", 3, 1, AddResource);
	CREATE_FUNCTION_BINDING("REMOVE_RESOURCE", 3, 1, RemoveResource);
	CREATE_FUNCTION_BINDING("GET_TARGET_RELATIVE_POS", 8, 3, GetTargetRelativePos);
	CREATE_FUNCTION_BINDING("STOP_POINTING", 1, 0, StopPointing);
	CREATE_FUNCTION_BINDING("STOP_LOOKING", 1, 0, StopLooking);
	CREATE_FUNCTION_BINDING("LOOK_AT_POSITION", 4, 0, LookAtPosition);
	CREATE_FUNCTION_BINDING("PLAY_SPIRIT_ANIM", 5, 0, PlaySpiritAnim);
	CREATE_FUNCTION_BINDING("CALL_IN_NOT_NEAR", 8, 1, CallInNotNear);
	CREATE_FUNCTION_BINDING("SET_CAMERA_ZONE", 1, 0, SetCameraZone);
	CREATE_FUNCTION_BINDING("GET_OBJECT_STATE", 1, 1, GetObjectState);
	CREATE_FUNCTION_BINDING("REVEAL_COUNTDOWN_TIMER", 0, 0, RevealCountdownTimer);
	CREATE_FUNCTION_BINDING("SET_TIMER_TIME", 2, 0, SetTimerTime);
	CREATE_FUNCTION_BINDING("CREATE_TIMER", 1, 1, CreateTimer);
	CREATE_FUNCTION_BINDING("GET_TIMER_TIME_REMAINING", 1, 1, GetTimerTimeRemaining);
	CREATE_FUNCTION_BINDING("GET_TIMER_TIME_SINCE_SET", 1, 1, GetTimerTimeSinceSet);
	CREATE_FUNCTION_BINDING("MOVE_MUSIC", 2, 0, MoveMusic);
	CREATE_FUNCTION_BINDING("GET_INCLUSION_DISTANCE", 0, 1, GetInclusionDistance);
	CREATE_FUNCTION_BINDING("GET_LAND_HEIGHT", 3, 1, GetLandHeight);
	CREATE_FUNCTION_BINDING("LOAD_MAP", 1, 0, LoadMap);
	CREATE_FUNCTION_BINDING("STOP_ALL_SCRIPTS_EXCLUDING", 1, 0, StopAllScriptsExcluding);
	CREATE_FUNCTION_BINDING("STOP_ALL_SCRIPTS_IN_FILES_EXCLUDING", 1, 0, StopAllScriptsInFilesExcluding);
	CREATE_FUNCTION_BINDING("STOP_SCRIPT", 1, 0, StopScript);
	CREATE_FUNCTION_BINDING("CLEAR_CLICKED_OBJECT", 0, 0, ClearClickedObject);
	CREATE_FUNCTION_BINDING("CLEAR_CLICKED_POSITION", 0, 0, ClearClickedPosition);
	CREATE_FUNCTION_BINDING("POSITION_CLICKED", 4, 1, PositionClicked);
	CREATE_FUNCTION_BINDING("RELEASE_FROM_SCRIPT", 1, 0, ReleaseFromScript);
	CREATE_FUNCTION_BINDING("GET_OBJECT_HAND_IS_OVER", 0, 1, GetObjectHandIsOver);
	CREATE_FUNCTION_BINDING("ID_POISONED_SIZE", 1, 1, IdPoisonedSize);
	CREATE_FUNCTION_BINDING("IS_POISONED", 1, 1, IsPoisoned);
	CREATE_FUNCTION_BINDING("CALL_POISONED_IN", 4, 1, CallPoisonedIn);
	CREATE_FUNCTION_BINDING("CALL_NOT_POISONED_IN", 4, 1, CallNotPoisonedIn);
	CREATE_FUNCTION_BINDING("SPIRIT_PLAYED", 1, 1, SpiritPlayed);
	CREATE_FUNCTION_BINDING("CLING_SPIRIT", 3, 0, ClingSpirit);
	CREATE_FUNCTION_BINDING("FLY_SPIRIT", 3, 0, FlySpirit);
	CREATE_FUNCTION_BINDING("SET_ID_MOVEABLE", 2, 0, SetIdMoveable);
	CREATE_FUNCTION_BINDING("SET_ID_PICKUPABLE", 2, 0, SetIdPickupable);
	CREATE_FUNCTION_BINDING("IS_ON_FIRE", 1, 1, IsOnFire);
	CREATE_FUNCTION_BINDING("IS_FIRE_NEAR", 4, 1, IsFireNear);
	CREATE_FUNCTION_BINDING("STOP_SCRIPTS_IN_FILES", 1, 0, StopScriptsInFiles);
	CREATE_FUNCTION_BINDING("SET_POISONED", 2, 0, SetPoisoned);
	CREATE_FUNCTION_BINDING("SET_TEMPERATURE", 2, 0, SetTemperature);
	CREATE_FUNCTION_BINDING("SET_ON_FIRE", 3, 0, SetOnFire);
	CREATE_FUNCTION_BINDING("SET_TARGET", 5, 0, SetTarget);
	CREATE_FUNCTION_BINDING("WALK_PATH", 5, 0, WalkPath);
	CREATE_FUNCTION_BINDING("FOCUS_AND_POSITION_FOLLOW", 2, 0, FocusAndPositionFollow);
	CREATE_FUNCTION_BINDING("GET_WALK_PATH_PERCENTAGE", 1, 1, GetWalkPathPercentage);
	CREATE_FUNCTION_BINDING("CAMERA_PROPERTIES", 4, 0, CameraProperties);
	CREATE_FUNCTION_BINDING("ENABLE_DISABLE_MUSIC", 2, 0, EnableDisableMusic);
	CREATE_FUNCTION_BINDING("GET_MUSIC_OBJ_DISTANCE", 1, 1, GetMusicObjDistance);
	CREATE_FUNCTION_BINDING("GET_MUSIC_ENUM_DISTANCE", 1, 1, GetMusicEnumDistance);
	CREATE_FUNCTION_BINDING("SET_MUSIC_PLAY_POSITION", 4, 0, SetMusicPlayPosition);
	CREATE_FUNCTION_BINDING("ATTACH_OBJECT_LEASH_TO_OBJECT", 2, 0, AttachObjectLeashToObject);
	CREATE_FUNCTION_BINDING("ATTACH_OBJECT_LEASH_TO_HAND", 1, 0, AttachObjectLeashToHand);
	CREATE_FUNCTION_BINDING("DETACH_OBJECT_LEASH", 1, 0, DetachObjectLeash);
	CREATE_FUNCTION_BINDING("SET_CREATURE_ONLY_DESIRE", 3, 0, SetCreatureOnlyDesire);
	CREATE_FUNCTION_BINDING("SET_CREATURE_ONLY_DESIRE_OFF", 1, 0, SetCreatureOnlyDesireOff);
	CREATE_FUNCTION_BINDING("RESTART_MUSIC", 1, 0, RestartMusic);
	CREATE_FUNCTION_BINDING("MUSIC_PLAYED", 1, 1, MusicPlayed191);
	CREATE_FUNCTION_BINDING("IS_OF_TYPE", 3, 1, IsOfType);
	CREATE_FUNCTION_BINDING("CLEAR_HIT_OBJECT", 0, 0, ClearHitObject);
	CREATE_FUNCTION_BINDING("GAME_THING_HIT", 1, 1, GameThingHit);
	CREATE_FUNCTION_BINDING("SPELL_AT_THING", 8, 1, SpellAtThing);
	CREATE_FUNCTION_BINDING("SPELL_AT_POS", 10, 1, SpellAtPos);
	CREATE_FUNCTION_BINDING("CALL_PLAYER_CREATURE", 1, 1, CallPlayerCreature);
	CREATE_FUNCTION_BINDING("GET_SLOWEST_SPEED", 1, 1, GetSlowestSpeed);
	CREATE_FUNCTION_BINDING("GET_OBJECT_HELD", 0, 1, GetObjectHeld199);
}

void CHLApi::InitFunctionsTable2()
{
	CREATE_FUNCTION_BINDING("HELP_SYSTEM_ON", 0, 1, HelpSystemOn);
	CREATE_FUNCTION_BINDING("SHAKE_CAMERA", 6, 0, ShakeCamera);
	CREATE_FUNCTION_BINDING("SET_ANIMATION_MODIFY", 2, 0, SetAnimationModify);
	CREATE_FUNCTION_BINDING("SET_AVI_SEQUENCE", 2, 0, SetAviSequence);
	CREATE_FUNCTION_BINDING("PLAY_GESTURE", 5, 0, PlayGesture);
	CREATE_FUNCTION_BINDING("DEV_FUNCTION", 1, 0, DevFunction);
	CREATE_FUNCTION_BINDING("HAS_MOUSE_WHEEL", 0, 1, HasMouseWheel);
	CREATE_FUNCTION_BINDING("NUM_MOUSE_BUTTONS", 0, 1, NumMouseButtons);
	CREATE_FUNCTION_BINDING("SET_CREATURE_DEV_STAGE", 2, 0, SetCreatureDevStage);
	CREATE_FUNCTION_BINDING("SET_FIXED_CAM_ROTATION", 4, 0, SetFixedCamRotation);
	CREATE_FUNCTION_BINDING("SWAP_CREATURE", 2, 0, SwapCreature);
	CREATE_FUNCTION_BINDING("GET_ARENA", 5, 1, GetArena);
	CREATE_FUNCTION_BINDING("GET_FOOTBALL_PITCH", 1, 1, GetFootballPitch);
	CREATE_FUNCTION_BINDING("STOP_ALL_GAMES", 1, 0, StopAllGames);
	CREATE_FUNCTION_BINDING("ATTACH_TO_GAME", 3, 0, AttachToGame);
	CREATE_FUNCTION_BINDING("DETACH_FROM_GAME", 3, 0, DetachFromGame);
	CREATE_FUNCTION_BINDING("DETACH_UNDEFINED_FROM_GAME", 2, 0, DetachUndefinedFromGame);
	CREATE_FUNCTION_BINDING("SET_ONLY_FOR_SCRIPTS", 2, 0, SetOnlyForScripts);
	CREATE_FUNCTION_BINDING("START_MATCH_WITH_REFEREE", 2, 0, StartMatchWithReferee);
	CREATE_FUNCTION_BINDING("GAME_TEAM_SIZE", 2, 0, GameTeamSize);
	CREATE_FUNCTION_BINDING("GAME_TYPE", 1, 1, GameType);
	CREATE_FUNCTION_BINDING("GAME_SUB_TYPE", 1, 1, GameSubType);
	CREATE_FUNCTION_BINDING("IS_LEASHED", 1, 1, IsLeashed);
	CREATE_FUNCTION_BINDING("SET_CREATURE_HOME", 4, 0, SetCreatureHome);
	CREATE_FUNCTION_BINDING("GET_HIT_OBJECT", 0, 1, GetHitObject);
	CREATE_FUNCTION_BINDING("GET_OBJECT_WHICH_HIT", 0, 1, GetObjectWhichHit);
	CREATE_FUNCTION_BINDING("GET_NEAREST_TOWN_OF_PLAYER", 5, 1, GetNearestTownOfPlayer);
	CREATE_FUNCTION_BINDING("SPELL_AT_POINT", 5, 1, SpellAtPoint);
	CREATE_FUNCTION_BINDING("SET_ATTACK_OWN_TOWN", 2, 0, SetAttackOwnTown);
	CREATE_FUNCTION_BINDING("IS_FIGHTING", 1, 1, IsFighting);
	CREATE_FUNCTION_BINDING("SET_MAGIC_RADIUS", 2, 0, SetMagicRadius);
	CREATE_FUNCTION_BINDING("TEMP_TEXT_WITH_NUMBER", 4, 0, TempTextWithNumber);
	CREATE_FUNCTION_BINDING("RUN_TEXT_WITH_NUMBER", 4, 0, RunTextWithNumber);
	CREATE_FUNCTION_BINDING("CREATURE_SPELL_REVERSION", 2, 0, CreatureSpellReversion);
	CREATE_FUNCTION_BINDING("GET_DESIRE", 2, 1, GetDesire);
	CREATE_FUNCTION_BINDING("GET_EVENTS_PER_SECOND", 1, 1, GetEventsPerSecond);
	CREATE_FUNCTION_BINDING("GET_TIME_SINCE", 1, 1, GetTimeSince);
	CREATE_FUNCTION_BINDING("GET_TOTAL_EVENTS", 1, 1, GetTotalEvents);
	CREATE_FUNCTION_BINDING("UPDATE_SNAPSHOT", -1, 0, UpdateSnapshot);
	CREATE_FUNCTION_BINDING("CREATE_REWARD", 5, 1, CreateReward);
	CREATE_FUNCTION_BINDING("CREATE_REWARD_IN_TOWN", 6, 1, CreateRewardInTown);
	CREATE_FUNCTION_BINDING("SET_FADE", 4, 0, SetFade);
	CREATE_FUNCTION_BINDING("SET_FADE_IN", 1, 0, SetFadeIn);
	CREATE_FUNCTION_BINDING("FADE_FINISHED", 0, 1, FadeFinished);
	CREATE_FUNCTION_BINDING("SET_PLAYER_MAGIC", 3, 0, SetPlayerMagic);
	CREATE_FUNCTION_BINDING("HAS_PLAYER_MAGIC", 2, 1, HasPlayerMagic);
	CREATE_FUNCTION_BINDING("SPIRIT_SPEAKS", 2, 1, SpiritSpeaks);
	CREATE_FUNCTION_BINDING("BELIEF_FOR_PLAYER", 2, 1, BeliefForPlayer);
	CREATE_FUNCTION_BINDING("GET_HELP", 1, 1, GetHelp);
	CREATE_FUNCTION_BINDING("SET_LEASH_WORKS", 2, 0, SetLeashWorks);
	CREATE_FUNCTION_BINDING("LOAD_MY_CREATURE", 3, 0, LoadMyCreature);
	CREATE_FUNCTION_BINDING("OBJECT_RELATIVE_BELIEF", 3, 0, ObjectRelativeBelief);
	CREATE_FUNCTION_BINDING("CREATE_WITH_ANGLE_AND_SCALE", 7, 1, CreateWithAngleAndScale);
	CREATE_FUNCTION_BINDING("SET_HELP_SYSTEM", 1, 0, SetHelpSystem);
	CREATE_FUNCTION_BINDING("SET_VIRTUAL_INFLUENCE", 2, 0, SetVirtualInfluence);
	CREATE_FUNCTION_BINDING("SET_ACTIVE", 2, 0, SetActive);
	CREATE_FUNCTION_BINDING("THING_VALID", 1, 1, ThingValid);
	CREATE_FUNCTION_BINDING("VORTEX_FADE_OUT", 1, 0, VortexFadeOut);
	CREATE_FUNCTION_BINDING("REMOVE_REACTION_OF_TYPE", 2, 0, RemoveReactionOfType);
	CREATE_FUNCTION_BINDING("CREATURE_LEARN_EVERYTHING_EXCLUDING", 2, 0, CreatureLearnEverythingExcluding);
	CREATE_FUNCTION_BINDING("PLAYED_PERCENTAGE", 1, 1, PlayedPercentage);
	CREATE_FUNCTION_BINDING("OBJECT_CAST_BY_OBJECT", 2, 1, ObjectCastByObject);
	CREATE_FUNCTION_BINDING("IS_WIND_MAGIC_AT_POS", 1, 1, IsWindMagicAtPos);
	CREATE_FUNCTION_BINDING("CREATE_MIST", 9, 1, CreateMist);
	CREATE_FUNCTION_BINDING("SET_MIST_FADE", 6, 0, SetMistFade);
	CREATE_FUNCTION_BINDING("GET_OBJECT_FADE", 1, 1, GetObjectFade);
	CREATE_FUNCTION_BINDING("PLAY_HAND_DEMO", 3, 0, PlayHandDemo);
	CREATE_FUNCTION_BINDING("IS_PLAYING_HAND_DEMO", 0, 1, IsPlayingHandDemo);
	CREATE_FUNCTION_BINDING("GET_ARSE_POSITION", 1, 3, GetArsePosition);
	CREATE_FUNCTION_BINDING("IS_LEASHED_TO_OBJECT", 2, 1, IsLeashedToObject);
	CREATE_FUNCTION_BINDING("GET_INTERACTION_MAGNITUDE", 1, 1, GetInteractionMagnitude);
	CREATE_FUNCTION_BINDING("IS_CREATURE_AVAILABLE", 1, 1, IsCreatureAvailable);
	CREATE_FUNCTION_BINDING("CREATE_HIGHLIGHT", 5, 1, CreateHighlight);
	CREATE_FUNCTION_BINDING("GET_OBJECT_HELD", 1, 1, GetObjectHeld273);
	CREATE_FUNCTION_BINDING("GET_ACTION_COUNT", 2, 1, GetActionCount);
	CREATE_FUNCTION_BINDING("GET_OBJECT_LEASH_TYPE", 1, 1, GetObjectLeashType);
	CREATE_FUNCTION_BINDING("SET_FOCUS_FOLLOW", 1, 0, SetFocusFollow);
	CREATE_FUNCTION_BINDING("SET_POSITION_FOLLOW", 1, 0, SetPositionFollow);
	CREATE_FUNCTION_BINDING("SET_FOCUS_AND_POSITION_FOLLOW", 2, 0, SetFocusAndPositionFollow);
	CREATE_FUNCTION_BINDING("SET_CAMERA_LENS", 1, 0, SetCameraLens);
	CREATE_FUNCTION_BINDING("MOVE_CAMERA_LENS", 2, 0, MoveCameraLens);
	CREATE_FUNCTION_BINDING("CREATURE_REACTION", 2, 0, CreatureReaction);
	CREATE_FUNCTION_BINDING("CREATURE_IN_DEV_SCRIPT", 2, 0, CreatureInDevScript);
	CREATE_FUNCTION_BINDING("STORE_CAMERA_DETAILS", 0, 0, StoreCameraDetails);
	CREATE_FUNCTION_BINDING("RESTORE_CAMERA_DETAILS", 0, 0, RestoreCameraDetails);
	CREATE_FUNCTION_BINDING("START_ANGLE_SOUND", 1, 0, StartAngleSound285);
	CREATE_FUNCTION_BINDING("SET_CAMERA_POS_FOC_LENS", 7, 0, SetCameraPosFocLens);
	CREATE_FUNCTION_BINDING("MOVE_CAMERA_POS_FOC_LENS", 8, 0, MoveCameraPosFocLens);
	CREATE_FUNCTION_BINDING("GAME_TIME_ON_OFF", 1, 0, GameTimeOnOff);
	CREATE_FUNCTION_BINDING("MOVE_GAME_TIME", 2, 0, MoveGameTime);
	CREATE_FUNCTION_BINDING("SET_HIGH_GRAPHICS_DETAIL", 2, 0, SetHighGraphicsDetail);
	CREATE_FUNCTION_BINDING("SET_SKELETON", 2, 0, SetSkeleton);
	CREATE_FUNCTION_BINDING("IS_SKELETON", 1, 1, IsSkeleton);
	CREATE_FUNCTION_BINDING("PLAYER_SPELL_CAST_TIME", 1, 1, PlayerSpellCastTime);
	CREATE_FUNCTION_BINDING("PLAYER_SPELL_LAST_CAST", 1, 1, PlayerSpellLastCast);
	CREATE_FUNCTION_BINDING("GET_LAST_SPELL_CAST_POS", 1, 3, GetLastSpellCastPos);
	CREATE_FUNCTION_BINDING("ADD_SPOT_VISUAL_TARGET_POS", 4, 0, AddSpotVisualTargetPos);
	CREATE_FUNCTION_BINDING("ADD_SPOT_VISUAL_TARGET_OBJECT", 2, 0, AddSpotVisualTargetObject);
	CREATE_FUNCTION_BINDING("SET_INDESTRUCTABLE", 2, 0, SetIndestructable);
	CREATE_FUNCTION_BINDING("SET_GRAPHICS_CLIPPING", 2, 0, SetGraphicsClipping);
}

void CHLApi::InitFunctionsTable3()
{
	CREATE_FUNCTION_BINDING("SPIRIT_APPEAR", 1, 0, SpiritAppear);
	CREATE_FUNCTION_BINDING("SPIRIT_DISAPPEAR", 1, 0, SpiritDisappear);
	CREATE_FUNCTION_BINDING("SET_FOCUS_ON_OBJECT", 2, 0, SetFocusOnObject);
	CREATE_FUNCTION_BINDING("RELEASE_OBJECT_FOCUS", 1, 0, ReleaseObjectFocus);
	CREATE_FUNCTION_BINDING("IMMERSION_EXISTS", 0, 1, ImmersionExists);
	CREATE_FUNCTION_BINDING("SET_DRAW_LEASH", 1, 0, SetDrawLeash);
	CREATE_FUNCTION_BINDING("SET_DRAW_HIGHLIGHT", 1, 0, SetDrawHighlight);
	CREATE_FUNCTION_BINDING("SET_OPEN_CLOSE", 2, 0, SetOpenClose);
	CREATE_FUNCTION_BINDING("SET_INTRO_BUILDING", 1, 0, SetIntroBuilding);
	CREATE_FUNCTION_BINDING("CREATURE_FORCE_FRIENDS", 3, 0, CreatureForceFriends);
	CREATE_FUNCTION_BINDING("MOVE_COMPUTER_PLAYER_POSITION", 6, 0, MoveComputerPlayerPosition);
	CREATE_FUNCTION_BINDING("ENABLE_DISABLE_COMPUTER_PLAYER", 2, 0, EnableDisableComputerPlayer311);
	CREATE_FUNCTION_BINDING("GET_COMPUTER_PLAYER_POSITION", 1, 3, GetComputerPlayerPosition);
	CREATE_FUNCTION_BINDING("SET_COMPUTER_PLAYER_POSITION", 5, 0, SetComputerPlayerPosition);
	CREATE_FUNCTION_BINDING("GET_STORED_CAMERA_POSITION", 0, 3, GetStoredCameraPosition);
	CREATE_FUNCTION_BINDING("GET_STORED_CAMERA_FOCUS", 0, 3, GetStoredCameraFocus);
	CREATE_FUNCTION_BINDING("CALL_NEAR_IN_STATE", 8, 1, CallNearInState);
	CREATE_FUNCTION_BINDING("SET_CREATURE_SOUND", 1, 0, SetCreatureSound);
	CREATE_FUNCTION_BINDING("CREATURE_INTERACTING_WITH", 2, 1, CreatureInteractingWith);
	CREATE_FUNCTION_BINDING("SET_SUN_DRAW", 1, 0, SetSunDraw);
	CREATE_FUNCTION_BINDING("OBJECT_INFO_BITS", 1, 1, ObjectInfoBits);
	CREATE_FUNCTION_BINDING("SET_HURT_BY_FIRE", 2, 0, SetHurtByFire);
	CREATE_FUNCTION_BINDING("CONFINED_OBJECT", 5, 0, ConfinedObject);
	CREATE_FUNCTION_BINDING("CLEAR_CONFINED_OBJECT", 1, 0, ClearConfinedObject);
	CREATE_FUNCTION_BINDING("GET_OBJECT_FLOCK", 1, 1, GetObjectFlock);
	CREATE_FUNCTION_BINDING("SET_PLAYER_BELIEF", 3, 0, SetPlayerBelief);
	CREATE_FUNCTION_BINDING("PLAY_JC_SPECIAL", 1, 0, PlayJcSpecial);
	CREATE_FUNCTION_BINDING("IS_PLAYING_JC_SPECIAL", 1, 1, IsPlayingJcSpecial);
	CREATE_FUNCTION_BINDING("VORTEX_PARAMETERS", 8, 0, VortexParameters);
	CREATE_FUNCTION_BINDING("LOAD_CREATURE", 6, 0, LoadCreature);
	CREATE_FUNCTION_BINDING("IS_SPELL_CHARGING", 1, 1, IsSpellCharging);
	CREATE_FUNCTION_BINDING("IS_THAT_SPELL_CHARGING", 2, 1, IsThatSpellCharging);
	CREATE_FUNCTION_BINDING("OPPOSING_CREATURE", 1, 1, OpposingCreature);
	CREATE_FUNCTION_BINDING("FLOCK_WITHIN_LIMITS", 1, 1, FlockWithinLimits);
	CREATE_FUNCTION_BINDING("HIGHLIGHT_PROPERTIES", 3, 0, HighlightProperties);
	CREATE_FUNCTION_BINDING("LAST_MUSIC_LINE", 1, 1, LastMusicLine);
	CREATE_FUNCTION_BINDING("HAND_DEMO_TRIGGER", 0, 1, HandDemoTrigger);
	CREATE_FUNCTION_BINDING("GET_BELLY_POSITION", 1, 3, GetBellyPosition);
	CREATE_FUNCTION_BINDING("SET_CREATURE_CREED_PROPERTIES", 5, 0, SetCreatureCreedProperties);
	CREATE_FUNCTION_BINDING("GAME_THING_CAN_VIEW_CAMERA", 2, 1, GameThingCanViewCamera);
	CREATE_FUNCTION_BINDING("GAME_PLAY_SAY_SOUND_EFFECT", 6, 0, GamePlaySaySoundEffect);
	CREATE_FUNCTION_BINDING("SET_TOWN_DESIRE_BOOST", 3, 0, SetTownDesireBoost);
	CREATE_FUNCTION_BINDING("IS_LOCKED_INTERACTION", 1, 1, IsLockedInteraction);
	CREATE_FUNCTION_BINDING("SET_CREATURE_NAME", 2, 0, SetCreatureName);
	CREATE_FUNCTION_BINDING("COMPUTER_PLAYER_READY", 1, 1, ComputerPlayerReady);
	CREATE_FUNCTION_BINDING("ENABLE_DISABLE_COMPUTER_PLAYER", 2, 0, EnableDisableComputerPlayer345);
	CREATE_FUNCTION_BINDING("CLEAR_ACTOR_MIND", 1, 0, ClearActorMind);
	CREATE_FUNCTION_BINDING("ENTER_EXIT_CITADEL", 1, 0, EnterExitCitadel);
	CREATE_FUNCTION_BINDING("START_ANGLE_SOUND", 1, 0, StartAngleSound348);
	CREATE_FUNCTION_BINDING("THING_JC_SPECIAL", 3, 0, ThingJcSpecial);
	CREATE_FUNCTION_BINDING("MUSIC_PLAYED", 1, 1, MusicPlayed350);
	CREATE_FUNCTION_BINDING("UPDATE_SNAPSHOT_PICTURE", 11, 0, UpdateSnapshotPicture);
	CREATE_FUNCTION_BINDING("STOP_SCRIPTS_IN_FILES_EXCLUDING", 2, 0, StopScriptsInFilesExcluding);
	CREATE_FUNCTION_BINDING("CREATE_RANDOM_VILLAGER_OF_TRIBE", 4, 1, CreateRandomVillagerOfTribe);
	CREATE_FUNCTION_BINDING("TOGGLE_LEASH", 1, 0, ToggleLeash);
	CREATE_FUNCTION_BINDING("GAME_SET_MANA", 2, 0, GameSetMana);
	CREATE_FUNCTION_BINDING("SET_MAGIC_PROPERTIES", 3, 0, SetMagicProperties);
	CREATE_FUNCTION_BINDING("SET_GAME_SOUND", 1, 0, SetGameSound);
	CREATE_FUNCTION_BINDING("SEX_IS_MALE", 1, 1, SexIsMale);
	CREATE_FUNCTION_BINDING("GET_FIRST_HELP", 1, 1, GetFirstHelp);
	CREATE_FUNCTION_BINDING("GET_LAST_HELP", 1, 1, GetLastHelp);
	CREATE_FUNCTION_BINDING("IS_ACTIVE", 1, 1, IsActive);
	CREATE_FUNCTION_BINDING("SET_BOOKMARK_POSITION", 4, 0, SetBookmarkPosition);
	CREATE_FUNCTION_BINDING("SET_SCAFFOLD_PROPERTIES", 4, 0, SetScaffoldProperties);
	CREATE_FUNCTION_BINDING("SET_COMPUTER_PLAYER_PERSONALITY", 3, 0, SetComputerPlayerPersonality);
	CREATE_FUNCTION_BINDING("SET_COMPUTER_PLAYER_SUPPRESSION", 3, 0, SetComputerPlayerSuppression);
	CREATE_FUNCTION_BINDING("FORCE_COMPUTER_PLAYER_ACTION", 4, 0, ForceComputerPlayerAction);
	CREATE_FUNCTION_BINDING("QUEUE_COMPUTER_PLAYER_ACTION", 4, 0, QueueComputerPlayerAction);
	CREATE_FUNCTION_BINDING("GET_TOWN_WITH_ID", 1, 1, GetTownWithId);
	CREATE_FUNCTION_BINDING("SET_DISCIPLE", 3, 0, SetDisciple);
	CREATE_FUNCTION_BINDING("RELEASE_COMPUTER_PLAYER", 1, 0, ReleaseComputerPlayer);
	CREATE_FUNCTION_BINDING("SET_COMPUTER_PLAYER_SPEED", 2, 0, SetComputerPlayerSpeed);
	CREATE_FUNCTION_BINDING("SET_FOCUS_FOLLOW_COMPUTER_PLAYER", 1, 0, SetFocusFollowComputerPlayer);
	CREATE_FUNCTION_BINDING("SET_POSITION_FOLLOW_COMPUTER_PLAYER", 1, 0, SetPositionFollowComputerPlayer);
	CREATE_FUNCTION_BINDING("CALL_COMPUTER_PLAYER", 1, 1, CallComputerPlayer);
	CREATE_FUNCTION_BINDING("CALL_BUILDING_IN_TOWN", 4, 1, CallBuildingInTown);
	CREATE_FUNCTION_BINDING("SET_CAN_BUILD_WORSHIPSITE", 2, 0, SetCanBuildWorshipsite);
	CREATE_FUNCTION_BINDING("GET_FACING_CAMERA_POSITION", 1, 3, GetFacingCameraPosition);
	CREATE_FUNCTION_BINDING("SET_COMPUTER_PLAYER_ATTITUDE", 3, 0, SetComputerPlayerAttitude);
	CREATE_FUNCTION_BINDING("GET_COMPUTER_PLAYER_ATTITUDE", 2, 1, GetComputerPlayerAttitude);
	CREATE_FUNCTION_BINDING("LOAD_COMPUTER_PLAYER_PERSONALITY", 2, 0, LoadComputerPlayerPersonality);
	CREATE_FUNCTION_BINDING("SAVE_COMPUTER_PLAYER_PERSONALITY", 2, 0, SaveComputerPlayerPersonality);
	CREATE_FUNCTION_BINDING("SET_PLAYER_ALLY", 3, 0, SetPlayerAlly);
	CREATE_FUNCTION_BINDING("CALL_FLYING", 7, 1, CallFlying);
	CREATE_FUNCTION_BINDING("SET_OBJECT_FADE_IN", 2, 0, SetObjectFadeIn);
	CREATE_FUNCTION_BINDING("IS_AFFECTED_BY_SPELL", 1, 1, IsAffectedBySpell);
	CREATE_FUNCTION_BINDING("SET_MAGIC_IN_OBJECT", 3, 0, SetMagicInObject);
	CREATE_FUNCTION_BINDING("ID_ADULT_SIZE", 1, 1, IdAdultSize);
	CREATE_FUNCTION_BINDING("OBJECT_CAPACITY", 1, 1, ObjectCapacity);
	CREATE_FUNCTION_BINDING("OBJECT_ADULT_CAPACITY", 1, 1, ObjectAdultCapacity);
	CREATE_FUNCTION_BINDING("SET_CREATURE_AUTO_FIGHTING", 2, 0, SetCreatureAutoFighting);
	CREATE_FUNCTION_BINDING("IS_AUTO_FIGHTING", 1, 1, IsAutoFighting);
	CREATE_FUNCTION_BINDING("SET_CREATURE_QUEUE_FIGHT_MOVE", 2, 0, SetCreatureQueueFightMove);
	CREATE_FUNCTION_BINDING("SET_CREATURE_QUEUE_FIGHT_SPELL", 2, 0, SetCreatureQueueFightSpell);
	CREATE_FUNCTION_BINDING("SET_CREATURE_QUEUE_FIGHT_STEP", 2, 0, SetCreatureQueueFightStep);
	CREATE_FUNCTION_BINDING("GET_CREATURE_FIGHT_ACTION", 1, 1, GetCreatureFightAction);
	CREATE_FUNCTION_BINDING("CREATURE_FIGHT_QUEUE_HITS", 1, 1, CreatureFightQueueHits);
	CREATE_FUNCTION_BINDING("SQUARE_ROOT", 1, 1, SquareRoot);
	CREATE_FUNCTION_BINDING("GET_PLAYER_ALLY", 2, 1, GetPlayerAlly);
	CREATE_FUNCTION_BINDING("SET_PLAYER_WIND_RESISTANCE", 2, 1, SetPlayerWindResistance);
}

void CHLApi::InitFunctionsTable4()
{
	CREATE_FUNCTION_BINDING("GET_PLAYER_WIND_RESISTANCE", 2, 1, GetPlayerWindResistance);
	CREATE_FUNCTION_BINDING("PAUSE_UNPAUSE_CLIMATE_SYSTEM", 1, 0, PauseUnpauseClimateSystem);
	CREATE_FUNCTION_BINDING("PAUSE_UNPAUSE_STORM_CREATION_IN_CLIMATE_SYSTEM", 1, 0, PauseUnpauseStormCreationInClimateSystem);
	CREATE_FUNCTION_BINDING("GET_MANA_FOR_SPELL", 1, 1, GetManaForSpell);
	CREATE_FUNCTION_BINDING("KILL_STORMS_IN_AREA", 4, 0, KillStormsInArea);
	CREATE_FUNCTION_BINDING("INSIDE_TEMPLE", 0, 1, InsideTemple);
	CREATE_FUNCTION_BINDING("RESTART_OBJECT", 1, 0, RestartObject);
	CREATE_FUNCTION_BINDING("SET_GAME_TIME_PROPERTIES", 3, 0, SetGameTimeProperties);
	CREATE_FUNCTION_BINDING("RESET_GAME_TIME_PROPERTIES", 0, 0, ResetGameTimeProperties);
	CREATE_FUNCTION_BINDING("SOUND_EXISTS", 0, 1, SoundExists);
	CREATE_FUNCTION_BINDING("GET_TOWN_WORSHIP_DEATHS", 1, 1, GetTownWorshipDeaths);
	CREATE_FUNCTION_BINDING("GAME_CLEAR_DIALOGUE", 0, 0, GameClearDialogue);
	CREATE_FUNCTION_BINDING("GAME_CLOSE_DIALOGUE", 0, 0, GameCloseDialogue);
	CREATE_FUNCTION_BINDING("GET_HAND_STATE", 0, 1, GetHandState);
	CREATE_FUNCTION_BINDING("SET_INTERFACE_CITADEL", 1, 0, SetInterfaceCitadel);
	CREATE_FUNCTION_BINDING("MAP_SCRIPT_FUNCTION", 1, 0, MapScriptFunction);
	CREATE_FUNCTION_BINDING("WITHIN_ROTATION", 0, 1, WithinRotation);
	CREATE_FUNCTION_BINDING("GET_PLAYER_TOWN_TOTAL", 1, 1, GetPlayerTownTotal);
	CREATE_FUNCTION_BINDING("SPIRIT_SCREEN_POINT", 3, 0, SpiritScreenPoint);
	CREATE_FUNCTION_BINDING("KEY_DOWN", 1, 1, KeyDown);
	CREATE_FUNCTION_BINDING("SET_FIGHT_EXIT", 1, 0, SetFightExit);
	CREATE_FUNCTION_BINDING("GET_OBJECT_CLICKED", 0, 1, GetObjectClicked);
	CREATE_FUNCTION_BINDING("GET_MANA", 1, 1, GetMana);
	CREATE_FUNCTION_BINDING("CLEAR_PLAYER_SPELL_CHARGING", 1, 0, ClearPlayerSpellCharging);
	CREATE_FUNCTION_BINDING("STOP_SOUND_EFFECT", 3, 0, StopSoundEffect);
	CREATE_FUNCTION_BINDING("GET_TOTEM_STATUE", 1, 1, GetTotemStatue);
	CREATE_FUNCTION_BINDING("SET_SET_ON_FIRE", 2, 0, SetSetOnFire);
	CREATE_FUNCTION_BINDING("SET_LAND_BALANCE", 2, 0, SetLandBalance);
	CREATE_FUNCTION_BINDING("SET_OBJECT_BELIEF_SCALE", 2, 0, SetObjectBeliefScale);
	CREATE_FUNCTION_BINDING("START_IMMERSION", 1, 0, StartImmersion);
	CREATE_FUNCTION_BINDING("STOP_IMMERSION", 1, 0, StopImmersion);
	CREATE_FUNCTION_BINDING("STOP_ALL_IMMERSION", 0, 0, StopAllImmersion);
	CREATE_FUNCTION_BINDING("SET_CREATURE_IN_TEMPLE", 1, 0, SetCreatureInTemple);
	CREATE_FUNCTION_BINDING("GAME_DRAW_TEXT", 7, 0, GameDrawText);
	CREATE_FUNCTION_BINDING("GAME_DRAW_TEMP_TEXT", 7, 0, GameDrawTempText);
	CREATE_FUNCTION_BINDING("FADE_ALL_DRAW_TEXT", 1, 0, FadeAllDrawText);
	CREATE_FUNCTION_BINDING("SET_DRAW_TEXT_COLOUR", 3, 0, SetDrawTextColour);
	CREATE_FUNCTION_BINDING("SET_CLIPPING_WINDOW", 5, 0, SetClippingWindow);
	CREATE_FUNCTION_BINDING("CLEAR_CLIPPING_WINDOW", 1, 0, ClearClippingWindow);
	CREATE_FUNCTION_BINDING("SAVE_GAME_IN_SLOT", 1, 0, SaveGameInSlot);
	CREATE_FUNCTION_BINDING("SET_OBJECT_CARRYING", 2, 0, SetObjectCarrying);
	CREATE_FUNCTION_BINDING("POS_VALID_FOR_CREATURE", 3, 1, PosValidForCreature);
	CREATE_FUNCTION_BINDING("GET_TIME_SINCE_OBJECT_ATTACKED", 2, 1, GetTimeSinceObjectAttacked);
	CREATE_FUNCTION_BINDING("GET_TOWN_AND_VILLAGER_HEALTH_TOTAL", 1, 1, GetTownAndVillagerHealthTotal);
	CREATE_FUNCTION_BINDING("GAME_ADD_FOR_BUILDING", 2, 0, GameAddForBuilding);
	CREATE_FUNCTION_BINDING("ENABLE_DISABLE_ALIGNMENT_MUSIC", 1, 0, EnableDisableAlignmentMusic);
	CREATE_FUNCTION_BINDING("GET_DEAD_LIVING", 4, 1, GetDeadLiving);
	CREATE_FUNCTION_BINDING("ATTACH_SOUND_TAG", 4, 0, AttachSoundTag);
	CREATE_FUNCTION_BINDING("DETACH_SOUND_TAG", 3, 0, DetachSoundTag);
	CREATE_FUNCTION_BINDING("GET_SACRIFICE_TOTAL", 1, 1, GetSacrificeTotal);
	CREATE_FUNCTION_BINDING("GAME_SOUND_PLAYING", 2, 1, GameSoundPlaying);
	CREATE_FUNCTION_BINDING("GET_TEMPLE_POSITION", 1, 3, GetTemplePosition);
	CREATE_FUNCTION_BINDING("CREATURE_AUTOSCALE", 3, 0, CreatureAutoscale);
	CREATE_FUNCTION_BINDING("GET_SPELL_ICON_IN_TEMPLE", 2, 1, GetSpellIconInTemple);
	CREATE_FUNCTION_BINDING("GAME_CLEAR_COMPUTER_PLAYER_ACTIONS", 1, 0, GameClearComputerPlayerActions);
	CREATE_FUNCTION_BINDING("GET_FIRST_IN_CONTAINER", 1, 1, GetFirstInContainer);
	CREATE_FUNCTION_BINDING("GET_NEXT_IN_CONTAINER", 2, 1, GetNextInContainer);
	CREATE_FUNCTION_BINDING("GET_TEMPLE_ENTRANCE_POSITION", 3, 3, GetTempleEntrancePosition);
	CREATE_FUNCTION_BINDING("SAY_SOUND_EFFECT_PLAYING", 2, 1, SaySoundEffectPlaying);
	CREATE_FUNCTION_BINDING("SET_HAND_DEMO_KEYS", 1, 0, SetHandDemoKeys);
	CREATE_FUNCTION_BINDING("CAN_SKIP_TUTORIAL", 0, 1, CanSkipTutorial);
	CREATE_FUNCTION_BINDING("CAN_SKIP_CREATURE_TRAINING", 0, 1, CanSkipCreatureTraining);
	CREATE_FUNCTION_BINDING("IS_KEEPING_OLD_CREATURE", 0, 1, IsKeepingOldCreature);
	CREATE_FUNCTION_BINDING("CURRENT_PROFILE_HAS_CREATURE", 0, 1, CurrentProfileHasCreature);
}


// Test save points (D-pad on the Switch, keypad 1-4 on a desktop): a light snapshot for getting back to a
// point in the story while testing, not the game's own saves. A point holds the script engine's state
// (LHVM::SaveState), the objects the scripts created (remade under the same ids on load, since the scripts
// hold their ids), and this file's own state: walks, flocks, speeds, timers, animations, the camera.
namespace
{
std::filesystem::path TestPointDir(int slot)
{
	const char* dir = std::getenv("OPENBLACK_SAVEPOINT_DIR");
	return std::filesystem::path(dir != nullptr ? dir : "savepoints") / fmt::format("point{}", slot);
}

std::string MatrixText(const glm::mat3& m)
{
	return fmt::format("{} {} {} {} {} {} {} {} {}", m[0][0], m[0][1], m[0][2], m[1][0], m[1][1], m[1][2], m[2][0], m[2][1],
	                   m[2][2]);
}
} // namespace

bool SaveTestPoint(int slot)
{
	using namespace openblack::ecs::components;
	auto& registry = Locator::entitiesRegistry::value();
	const auto dir = TestPointDir(slot);
	std::error_code ec;
	std::filesystem::create_directories(dir, ec);
	if (Locator::vm::value().SaveRuntime(dir / "vm.bin") != EXIT_SUCCESS)
	{
		SPDLOG_LOGGER_ERROR(spdlog::get("game"), "Test save point {}: couldn't write the script state", slot);
		return false;
	}

	std::ofstream out(dir / "state.txt");
	const auto now = ScriptNow();
	const auto& camera = Locator::camera::value();
	out << fmt::format("camera {} {} {} {} {} {} {}\n", camera.GetOrigin().x, camera.GetOrigin().y, camera.GetOrigin().z,
	                   camera.GetFocus().x, camera.GetFocus().y, camera.GetFocus().z, camera.IsScriptControlled() ? 1 : 0);
	out << fmt::format("hour {}\n", g_gameHour);
	out << fmt::format("dialogue {} {} {}\n", g_dialogueActive ? 1 : 0, g_lastTextId, g_dialogueTask);
	for (const auto& [id, record] : g_scriptEntities)
	{
		const auto entity = static_cast<entt::entity>(id);
		const auto* transform = registry.Valid(entity) ? registry.TryGet<Transform>(entity) : nullptr;
		if (transform == nullptr)
		{
			continue;
		}
		out << fmt::format("entity {} {} {} {} {} {} {} {} {}\n", id, record.type, record.subtype, record.age,
		                   transform->position.x, transform->position.y, transform->position.z,
		                   MatrixText(transform->rotation), transform->scale.x);
		if (const auto* mesh = registry.TryGet<Mesh>(entity))
		{
			out << fmt::format("mesh {} {}\n", id, mesh->id);
		}
		if (const auto* villager = registry.TryGet<Villager>(entity))
		{
			out << fmt::format("villager {} {} {}\n", id, villager->age, villager->life);
		}
		if (const auto* move = registry.TryGet<ScriptMove>(entity))
		{
			out << fmt::format("held {} {} {} {} {}\n", id, move->target.x, move->target.y, move->target.z, move->arrived ? 1 : 0);
		}
	}
	for (const auto& [key, value] : g_objectProperties)
	{
		out << fmt::format("property {} {}\n", key, value);
	}
	for (const auto& [id, walk] : g_walks)
	{
		out << fmt::format("walk {} {} {} {}\n", id, walk.track, walk.at, walk.end);
	}
	for (const auto& [id, target] : g_followers)
	{
		out << fmt::format("follow {} {} {} {}\n", id, target.x, target.y, target.z);
	}
	for (const auto& [id, target] : g_scriptMoves)
	{
		out << fmt::format("move {} {} {} {}\n", id, target.x, target.y, target.z);
	}
	for (const auto& [id, flock] : g_flocks)
	{
		if (g_dances.contains(id))
		{
			continue; // a dance isn't saved: loaded, its dancers would be held in it for ever
		}
		out << fmt::format("flock {} {}", id, flock.leader);
		for (const auto member : flock.members)
		{
			out << ' ' << member;
		}
		out << '\n';
	}
	for (const auto& [id, animation] : g_scriptAnimations)
	{
		out << fmt::format("animation {} {} {}\n", id, animation.animation, animation.playsLeft);
	}
	for (const auto& [id, end] : g_timers)
	{
		out << fmt::format("timer {} {}\n", id, std::chrono::duration<float>(end - now).count());
	}
	// Creatures: whose each is, its pen and its order - remade unowned, the chosen creature was nobody's after a load
	// and every lesson lost it.
	Locator::entitiesRegistry::value().Each<const openblack::ecs::components::Creature>(
	    [&out](entt::entity entity, const openblack::ecs::components::Creature& creature) {
		    out << fmt::format("creature {} {}\n", static_cast<uint32_t>(entity), static_cast<int>(creature.owner));
	    });
	for (const auto& [id, home] : g_creatureHomes)
	{
		out << fmt::format("creaturehome {} {} {} {}\n", id, home.x, home.y, home.z);
	}
	for (const auto& [id, order] : g_creatureOrders)
	{
		out << fmt::format("creatureorder {} {} {} {}\n", id, order.action, order.target, order.with);
	}
	out << fmt::format("nexttimer {}\n", g_nextTimer);
	for (const auto& [type, value] : g_totalEvents)
	{
		out << fmt::format("event {} {}\n", type, value);
	}
	for (const auto id : g_notPickupable)
	{
		out << fmt::format("notpickupable {}\n", id);
	}
	// How often each creature has done what the lessons count (eating): a load kept the count from after the save,
	// and the feeding lesson, loaded from before the feeding, passed at once.
	for (const auto& [key, count] : g_creatureActionCounts)
	{
		out << fmt::format("actioncount {} {} {}\n", key.first, key.second, count);
	}
	for (const auto& [town, beliefs] : g_townBelief)
	{
		for (const auto& [player, value] : beliefs)
		{
			out << fmt::format("townbelief {} {} {}\n", town, player, value);
		}
	}
	for (const auto& [town, owner] : g_townOwner)
	{
		out << fmt::format("townowner {} {}\n", town, owner);
	}
	for (const auto& [id, dispenser] : g_dispensers)
	{
		out << fmt::format("dispenser {} {} {}\n", id, dispenser.seed, dispenser.sphere);
	}
	// A fight under way (its health is a property, saved above): who against whom, on their own or not, and the ring.
	for (const auto& [id, fighter] : g_fighters)
	{
		out << fmt::format("fighter {} {} {} {} {} {} {}\n", id, fighter.opponent, fighter.autoFight ? 1 : 0, g_arenaCentre.x,
		                   g_arenaCentre.y, g_arenaCentre.z, g_arenaRadius);
	}
	for (const auto id : g_deadForever)
	{
		out << fmt::format("deadforever {}\n", id);
	}
	SPDLOG_LOGGER_INFO(spdlog::get("game"), "Test save point {} saved to {} ({} script objects)", slot, dir.string(),
	                   g_scriptEntities.size());
	return true;
}

bool LoadTestPoint(int slot)
{
	ExitTemple(); // a save point is always outside
	using namespace openblack::ecs::components;
	auto& registry = Locator::entitiesRegistry::value();
	const auto dir = TestPointDir(slot);
	std::ifstream in(dir / "state.txt");
	if (!in || Locator::vm::value().LoadRuntime(dir / "vm.bin") != EXIT_SUCCESS)
	{
		SPDLOG_LOGGER_WARN(spdlog::get("game"), "Test save point {}: nothing saved at {}", slot, dir.string());
		return false;
	}
	EndHandDemo();
	g_heldObject = 0;
	g_walks.clear();
	g_followers.clear();
	g_scriptMoves.clear();
	g_flying.clear();
	g_flocks.clear();
	g_dances.clear();
	g_objectProperties.clear();
	g_scriptAnimations.clear();
	g_creatureOrders.clear();
	g_holdDone.clear();
	EndCameraPath();
	g_creatureHomes.clear();
	g_creaturePlaying.clear();
	g_creatureLastPosition.clear();
	g_creatureGait.clear();
	g_interactionMagnitude.clear();
	EndCreatureInteraction();
	g_timers.clear();
	g_totalEvents.clear();
	g_notPickupable.clear();
	// Not in a save point: what is on the gate stone pedestal (the stones on it are the script's, remade from the
	// save), and which gates scripts have opened. Kept, a stone set in later in the session counted before its time.
	for (auto& [pedestal, copies] : g_pedestalSetStones)
	{
		for (const auto copy : copies)
		{
			if (registry.Valid(copy))
			{
				registry.Destroy(copy);
			}
		}
	}
	g_pedestalSetStones.clear();
	g_pedestalSetBits.clear();
	g_pedestalStones.clear();
	g_openedStatics.clear();
	g_plinthRiders.clear();
	g_creatureActionCounts.clear();
	g_townBelief.clear();
	g_townOwner.clear();
	g_dispensers.clear();
	for (const auto& [id, own] : g_fightNormalScale)
	{
		if (auto* transform = Locator::entitiesRegistry::value().TryGet<Transform>(static_cast<entt::entity>(id)))
		{
			transform->scale = glm::vec3(own);
		}
	}
	g_fightNormalScale.clear();
	g_scaleTweens.clear();
	g_fighters.clear();
	g_vortexSpirals.clear();
	g_dying.clear();
	g_deadForever.clear();
	g_oneShotSpells.clear();
	g_scriptPlaced.clear();
	g_takenHighlights.clear();
	ReleaseHeldCreatures();
	g_followerPopups.clear();
	ReleaseIntro();
	g_intro.littleBoy = 0;
	g_dialogueTask = 0;
	g_gameTimeMove.reset();
	const auto now = ScriptNow();
	auto seconds = [](float value) {
		return std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<float>(value));
	};

	std::string line;
	int remade = 0;
	while (std::getline(in, line))
	{
		std::istringstream fields(line);
		std::string kind;
		fields >> kind;
		uint32_t id = 0;
		if (kind == "camera")
		{
			glm::vec3 origin, focus;
			int scripted;
			fields >> origin.x >> origin.y >> origin.z >> focus.x >> focus.y >> focus.z >> scripted;
			auto& camera = Locator::camera::value();
			camera.SetScriptControl(scripted != 0);
			camera.SetOrigin(origin);
			camera.SetFocus(focus);
		}
		else if (kind == "hour")
		{
			float hour;
			fields >> hour;
			ApplyGameHour(hour);
		}
		else if (kind == "creature")
		{
			uint32_t id;
			int owner;
			fields >> id >> owner;
			auto& registry = Locator::entitiesRegistry::value();
			const auto entity = static_cast<entt::entity>(id);
			if (auto* creature = registry.Valid(entity) ? registry.TryGet<openblack::ecs::components::Creature>(entity) : nullptr)
			{
				creature->owner = static_cast<PlayerNames>(owner);
			}
		}
		else if (kind == "creaturehome")
		{
			uint32_t id;
			glm::vec3 home;
			fields >> id >> home.x >> home.y >> home.z;
			g_creatureHomes[id] = home;
		}
		else if (kind == "creatureorder")
		{
			uint32_t id;
			CreatureOrder order {};
			fields >> id >> order.action >> order.target >> order.with;
			g_creatureOrders[id] = order;
		}
		else if (kind == "dialogue")
		{
			int active;
			fields >> active >> g_lastTextId;
			g_dialogueTask = 0;
			fields >> g_dialogueTask; // absent in saves made before the holder was kept
			g_dialogueActive = active != 0;
			g_lastTextShown = now - std::chrono::seconds(30); // any line up counts as read
		}
		else if (kind == "entity")
		{
			ScriptEntityRecord record {};
			glm::vec3 position;
			glm::mat3 rotation;
			float scale;
			fields >> id >> record.type >> record.subtype >> record.age >> position.x >> position.y >> position.z;
			for (int c = 0; c < 3; ++c)
			{
				for (int r = 0; r < 3; ++r)
				{
					fields >> rotation[c][r];
				}
			}
			fields >> scale;
			const auto entity = static_cast<entt::entity>(id);
			if (!registry.Valid(entity))
			{
				const auto made = CreateScriptObjectOn(entity, static_cast<ObjectType>(record.type), record.subtype, position,
				                                       0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
				if (made != entity)
				{
					// Not on its old handle: don't leave the stray (it doubled Land 1's gate totems, the script making
					// its own again for the handle it lost).
					// 0 is how a failed make says so (CREATE's "no object") - and it is the hand's own entity: destroyed
					// here, the hand was gone after a load and the next frame crashed (2026-10-18).
					if (made != entt::null && made != static_cast<entt::entity>(0) && registry.Valid(made))
					{
						registry.Destroy(made);
					}
					SPDLOG_LOGGER_WARN(spdlog::get("game"), "Test save point {}: couldn't remake object {} (type {})", slot, id,
					                   record.type);
					continue;
				}
				++remade;
			}
			g_scriptEntities[id] = record;
			if (auto* transform = registry.TryGet<Transform>(entity))
			{
				transform->position = position;
				transform->rotation = rotation;
				// A creature keeps the size it is made at now: a save keeps the size of when it was made, and Land 1's
				// guide creature came back at the ten times it once was, not the five it is.
				const bool creature = registry.AllOf<openblack::ecs::components::Creature>(entity) && g_creatureBaseScale.contains(id);
				if (!creature)
				{
					transform->scale = glm::vec3(scale);
				}
				else if (std::abs(transform->scale.x - scale) > 1e-4f)
				{
					SPDLOG_LOGGER_INFO(spdlog::get("game"), "Test save point {}: creature {} kept at x{:.3f}, not the saved x{:.3f}", slot, id,
					                   transform->scale.x, scale);
				}
			}
		}
		else if (kind == "mesh")
		{
			entt::id_type mesh;
			fields >> id >> mesh;
			if (auto* component = registry.TryGet<Mesh>(static_cast<entt::entity>(id)))
			{
				component->id = mesh;
			}
		}
		else if (kind == "villager")
		{
			uint32_t age;
			float life;
			fields >> id >> age >> life;
			if (auto* villager = registry.TryGet<Villager>(static_cast<entt::entity>(id)))
			{
				villager->age = age;
				villager->life = life > 1.0f ? life / 100.0f : life; // saves before 2026-10-19 kept 0-100
				const auto type = openblack::GVillagerInfo::Find(villager->tribe, villager->number);
				villager->lifeStage = VillagerArchetype::IsChild(type, age) ? Villager::LifeStage::Child : Villager::LifeStage::Adult;
				if (auto* transform = registry.TryGet<Transform>(static_cast<entt::entity>(id)))
				{
					transform->scale = glm::vec3(VillagerArchetype::ScaleForAge(type, age));
				}
			}
		}
		else if (kind == "held")
		{
			glm::vec3 target;
			int arrived;
			fields >> id >> target.x >> target.y >> target.z >> arrived;
			const auto entity = static_cast<entt::entity>(id);
			if (registry.Valid(entity))
			{
				registry.AssignOrReplace<ScriptMove>(entity, target, 0u, arrived != 0);
			}
		}
		else if (kind == "property")
		{
			uint64_t key;
			float value;
			fields >> key >> value;
			g_objectProperties[key] = value;
		}
		else if (kind == "walk")
		{
			int32_t track;
			float at, end;
			fields >> id >> track >> at >> end;
			auto curve = LoadTrackGround(track);
			if (curve.size() >= 2)
			{
				ScriptWalk walk;
				walk.lengths.push_back(0.0f);
				for (size_t i = 1; i < curve.size(); ++i)
				{
					walk.lengths.push_back(walk.lengths.back() + glm::distance(curve[i - 1], curve[i]));
				}
				walk.points = std::move(curve);
				walk.at = at;
				walk.end = end;
				walk.track = track;
				g_walks[id] = std::move(walk);
			}
		}
		else if (kind == "follow" || kind == "move")
		{
			glm::vec3 target;
			fields >> id >> target.x >> target.y >> target.z;
			(kind == "follow" ? g_followers : g_scriptMoves)[id] = target;
		}
		else if (kind == "flock")
		{
			Flock flock;
			fields >> id >> flock.leader;
			for (uint32_t member; fields >> member;)
			{
				flock.members.push_back(member);
			}
			g_flocks[id] = std::move(flock);
		}
		else if (kind == "animation")
		{
			ScriptAnimation animation {};
			fields >> id >> animation.animation >> animation.playsLeft;
			g_scriptAnimations[id] = animation;
		}
		else if (kind == "timer")
		{
			float remaining;
			fields >> id >> remaining;
			g_timers[id] = now + seconds(remaining);
		}
		else if (kind == "nexttimer")
		{
			fields >> g_nextTimer;
		}
		else if (kind == "event")
		{
			int32_t type;
			float value;
			fields >> type >> value;
			g_totalEvents[type] = value;
		}
		else if (kind == "notpickupable")
		{
			fields >> id;
			g_notPickupable.insert(id);
		}
		else if (kind == "townbelief")
		{
			int player;
			float value;
			fields >> id >> player >> value;
			g_townBelief[id][player] = value;
		}
		else if (kind == "deadforever")
		{
			fields >> id;
			g_deadForever.insert(id);
		}
		else if (kind == "fighter")
		{
			Fighter fighter;
			int autoFight;
			fields >> id >> fighter.opponent >> autoFight >> g_arenaCentre.x >> g_arenaCentre.y >> g_arenaCentre.z >> g_arenaRadius;
			fighter.autoFight = autoFight != 0;
			g_fighters[id] = fighter;
		}
		else if (kind == "dispenser")
		{
			Dispenser dispenser;
			fields >> id >> dispenser.seed >> dispenser.sphere;
			g_dispensers[id] = dispenser;
		}
		else if (kind == "townowner")
		{
			int owner;
			fields >> id >> owner;
			g_townOwner[id] = owner;
		}
		else if (kind == "actioncount")
		{
			int32_t action;
			float count;
			fields >> id >> action >> count;
			g_creatureActionCounts[{id, action}] = count;
		}
	}
	registry.SetDirty();
	SPDLOG_LOGGER_INFO(spdlog::get("game"), "Test save point {} loaded from {} ({} objects remade)", slot, dir.string(), remade);
	return true;
}

} // namespace openblack::chlapi
