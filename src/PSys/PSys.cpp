/*******************************************************************************
 * Copyright (c) 2018-2026 openblack developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/openblack/openblack
 *
 * openblack is licensed under the GNU General Public License version 3.
 *******************************************************************************/

#include "PSys/PSys.h"

#include <algorithm>
#include <cstring>
#include <set>
#include <sstream>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/matrix.hpp>
#include <spdlog/spdlog.h>

#include "3D/LandIslandInterface.h"
#include "Camera/Camera.h"
#include "Common/RandomNumberManager.h"
#include "Common/Zip.h"
#include "ECS/Systems/HandSystemInterface.h"
#include "FileSystem/FileSystemInterface.h"
#include "Locator.h"
#include "PSys/PSysCore.h"

namespace openblack::psys
{
namespace core
{
namespace
{
// runblack.exe's table of effect files, by particle type (the stores into 0xd4ebb0's array at BW1W120 0068ea00);
// those not given have none. Food's 9 and 10 (poisoned) are the same file.
const std::map<int, const char*> k_Files = {
    {2, "FireWorks"},
    {4, "Forest"},
    {6, "LightningStormPush"},
    {7, "LightningBolt"},
    {9, "Food"},
    {10, "Food"},
    {11, "BeamExplosionSingle"},
    {12, "BeamExplosionMany"},
    {13, "BeamExplosionLoads"},
    {14, "BeamExplosionCitadel"},
    {15, "Wood"},
    {16, "Water"},
    {17, "WaterPU1"},
    {18, "WaterOnHolder"},
    {19, "WaterInHand"},
    {20, "WaterInHandPU1"},
    {21, "HealChakra"},
    {22, "ManaPathNew"},
    {23, "ExplodeObject"},
    {24, "BeliefSprite"},
    {25, "TownBelief"},
    {26, "FailedApply"},
    {28, "SpellSelection"},
    {29, "GripLandscape"},
    {30, "MagicObjectCreated"},
    {31, "CreatureGestureChain"},
    {32, "TeleportVillager"},
    {33, "CreatureTarget"},
    {34, "SimpleBeamCreatureCast"},
    {35, "Gesture"},
    {36, "Fireworks2"},
    {37, "OnFire"},
    {38, "VolFX"},
    {39, "VolFXArtifact"},
    {40, "VolFXCitadel"},
    {43, "FireBallThrow"},
    {44, "FireBallInHand"},
    {47, "FireBallOnHolder"},
    {48, "GestureChain"},
    {50, "HealChakraInHand"},
    {51, "HealChakraOnHolder"},
    {52, "FireBallThrowPU"},
    {53, "FireBallThrowPU2"},
    {54, "HealChakraPU"},
    {55, "LightningStormInHand"},
    {56, "LightningStormInHandPU1"},
    {57, "LightningStormInHandPU2"},
    {58, "LightningStormOnHolder"},
    {59, "LightningBoltInHand"},
    {60, "LightningBoltOnHolder"},
    {61, "LightningStrike"},
    {62, "LightningSingleStrike"},
    {64, "DefenseSphere"},
    {65, "DefenseSphereInHand"},
    {66, "DefenseSphereOnHolder"},
    {67, "PhysicalShieldFX"},
    {68, "LightningBoltPUOne"},
    {69, "LightningBoltPUTwo"},
    {70, "LightningBoltInHandPUOne"},
    {71, "LightningBoltInHandPUTwo"},
    {73, "TeleportVortex"},
    {74, "TeleportOnHolder"},
    {75, "TeleportInHand"},
    {76, "LandscapeVortexObjectMover"},
    {77, "LandscapeVortexLightMap"},
    {78, "LandscapeVortexInBefore"},
    {79, "LandscapeVortexInAfter"},
    {81, "LandscapeVortexOutAfter"},
    {82, "LandscapeVolcanoBefore"},
    {83, "LandscapeVolcanoAfter"},
    {84, "LandscapeVolcanoLightMap"},
    {85, "CreatureSpellGeneric"},
    {87, "CreatureSpellItch"},
    {88, "CreatureSpellItchInHand"},
    {89, "CreatureSpellItchOnHolder"},
    {90, "CreatureSpellFreeze"},
    {92, "CreatureSpellFreezeOnHolder"},
    {93, "CreatureSpellCompassion"},
    {95, "CreatureSpellCompassionOnHolder"},
    {99, "ScriptHighlightGlintsGold"},
    {100, "ScriptHighlightGlintsSilver"},
    {101, "ScriptHighlightGlintsBronze"},
    {102, "ScriptHighlightActiveGold"},
    {103, "ScriptHighlightActiveSilver"},
    {106, "StormCast"},
    {107, "MultiPickUpFood"},
    {108, "MultiPickUpFoodPoisoned"},
    {109, "MultiPickUpFoodFish"},
    {110, "MultiPickUpWood"},
    {111, "MultiPutDownFoodPoisoned"},
    {112, "MultiPutDownWood"},
    {113, "MultiPutDownFood"},
    {114, "Steam"},
    {115, "Smoke"},
    {116, "Bonfire"},
    {117, "EvilSmoke"},
    {119, "SimpleBeam"},
    {120, "SimpleBeamCitadel"},
    {121, "SimpleBeamCreatureSwap"},
    {122, "FlockFlyingCastGood"},
    {123, "FlockFlyingCastEvil"},
    {124, "FlockFlyingRainGood"},
    {125, "FlockFlyingRainEvil"},
    {126, "FlockGroundDust"},
    {127, "Butterflies"},
    {128, "ButterfliesOnObject"},
    {129, "Flies"},
    {130, "FliesOnObject"},
    {131, "MagicObjectCreated2"},
    {134, "PlayerIconFountain"},
    {135, "SmokeExplode"},
    {137, "HighlightOnObject"},
    {138, "BeamExplosionFX"},
    {139, "Flash"},
    {140, "TickerTape"},
    {141, "ForestCreated"},
    {142, "SingingStonesHeal"},
    {143, "SparklesFromObject"},
    {144, "SpellDispenserVortex"},
    {145, "SeeThisBeam"},
};

// An enum header of the game's (Data/SoundAction.h, Data/AllMeshes.h): its names' values.
std::map<std::string, int, std::less<>> ReadEnum(const char* file)
{
	std::map<std::string, int, std::less<>> values;
	try
	{
		auto& fs = Locator::filesystem::value();
		const auto bytes = fs.ReadAll(fs.GetPath<filesystem::Path::Data>() / file);
		std::istringstream lines(std::string(bytes.begin(), bytes.end()));
		std::string line;
		while (std::getline(lines, line))
		{
			const auto equals = line.find('=');
			if (equals == std::string::npos)
			{
				continue;
			}
			std::istringstream name(line.substr(0, equals));
			std::string word;
			name >> word;
			const auto digits = line.find_first_of("-0123456789", equals);
			if (word.empty() || digits == std::string::npos)
			{
				continue;
			}
			values[word] = std::atoi(line.c_str() + digits);
		}
	}
	catch (const std::exception& e)
	{
		SPDLOG_LOGGER_WARN(spdlog::get("game"), "Effects: {}: {}", file, e.what());
	}
	return values;
}

const std::map<std::string, int, std::less<>>& SoundActions()
{
	static const auto s_values = ReadEnum("SoundAction.h");
	return s_values;
}

const std::map<std::string, int, std::less<>>& MeshEnums()
{
	static const auto s_values = ReadEnum("AllMeshes.h");
	return s_values;
}

std::unique_ptr<FileData> LoadFile(const std::string& name)
{
	auto& fs = Locator::filesystem::value();
	const auto path = fs.GetPath<filesystem::Path::Data>() / "Spells" / "ZSpellFiles" / ("SF_" + name + "_txt.zzz");
	const auto bytes = fs.ReadAll(path);
	if (bytes.size() < 4)
	{
		return nullptr;
	}
	uint32_t size = 0;
	std::memcpy(&size, bytes.data(), sizeof(size));
	const auto text = zip::Inflate(std::vector<uint8_t>(bytes.begin() + 4, bytes.end()), size);
	// Its objects and their properties, as PersistenceStreamer::Read makes them: each in the file's order.
	struct Read
	{
		std::string type;
		std::string name;
		Properties properties;
	};
	std::vector<Read> reads;
	Properties header;
	std::istringstream lines(std::string(text.begin(), text.end()));
	std::string line;
	Read* current = nullptr;
	while (std::getline(lines, line))
	{
		std::istringstream words(line);
		std::string first;
		if (!(words >> first))
		{
			continue;
		}
		if (first == "BEGINCLASS")
		{
			reads.emplace_back();
			words >> reads.back().type >> reads.back().name;
			current = &reads.back();
		}
		else if (first == "ENDCLASS")
		{
			current = nullptr;
		}
		else if (first == "PROPERTY")
		{
			std::string property;
			std::string type;
			words >> property >> type;
			std::vector<std::string> values;
			for (std::string word; words >> word;)
			{
				values.push_back(word);
			}
			auto& into = current != nullptr ? current->properties : header;
			into.values.try_emplace(property, std::move(values)); // the first of a twice-given one, as the streamer's
		}
	}
	auto file = std::make_unique<FileData>();
	file->name = name;
	const auto created = header.Ints("InitiallyCreated");
	const auto hierarchies = header.Ints("Hierarchies");
	for (int g = 0; g < k_MaxGroups; ++g)
	{
		file->initiallyCreated.at(static_cast<size_t>(g)) = static_cast<size_t>(g) < created.size() && created[static_cast<size_t>(g)] != 0;
		file->hierarchies.at(static_cast<size_t>(g)) = static_cast<size_t>(g) < hierarchies.size() && hierarchies[static_cast<size_t>(g)] != 0;
	}
	file->deleteOnCloseDown = header.Bool("DeleteOnCloseDown", false);
	file->maxSpellAge = header.Float("MaxSpellAge", -1.0f);
	std::map<std::string, Object*, std::less<>> byName;
	std::set<std::string> unknown;
	std::vector<std::pair<Object*, Properties*>> made;
	for (auto& read : reads)
	{
		auto object = MakeObject(read.type);
		if (!object)
		{
			unknown.insert(read.type);
			continue;
		}
		object->name = read.name;
		byName[read.name] = object.get();
		made.emplace_back(object.get(), &read.properties);
		file->objects.push_back(std::move(object));
	}
	for (auto& [object, properties] : made)
	{
		properties->objects = &byName;
		ReadProperties(*object, *properties);
	}
	// PSysFileData::OnLoaded: each rule to its group's list, the float providers to theirs.
	for (const auto& object : file->objects)
	{
		if (const auto* modifier = dynamic_cast<const Modifier*>(object.get()))
		{
			if (modifier->group >= 0 && modifier->group < k_MaxGroups)
			{
				file->groupModifiers.at(static_cast<size_t>(modifier->group)).push_back(modifier);
			}
		}
		else if (auto* provider = dynamic_cast<FloatProvider*>(object.get()))
		{
			file->providers.push_back(provider);
		}
	}
	for (const auto& type : unknown)
	{
		SPDLOG_LOGGER_WARN(spdlog::get("game"), "Effect SF_{}: {} is not run yet", name, type);
	}
	SPDLOG_LOGGER_INFO(spdlog::get("game"), "Effect SF_{}: {} objects", name, file->objects.size());
	return file;
}

const FileData* File(Type type)
{
	static std::map<std::string, std::unique_ptr<FileData>> s_files;
	const auto name = k_Files.find(static_cast<int>(type));
	if (name == k_Files.end())
	{
		return nullptr;
	}
	auto [it, made] = s_files.try_emplace(name->second);
	if (made)
	{
		try
		{
			it->second = LoadFile(name->second);
		}
		catch (const std::exception& e)
		{
			SPDLOG_LOGGER_WARN(spdlog::get("game"), "Effect SF_{}: {}", name->second, e.what());
		}
	}
	return it->second.get();
}

const std::vector<std::string>* Words(const Properties& properties, std::string_view name)
{
	const auto it = properties.values.find(name);
	return it == properties.values.end() || it->second.empty() ? nullptr : &it->second;
}

struct System
{
	std::unique_ptr<Manager> manager;
};
// Never freed: freeing it at exit, after the registry and bgfx have gone, would take its meshes and sounds with them.
std::map<Handle, System>& g_systems = *new std::map<Handle, System>();
Handle g_nextHandle = 1;
float g_sinceTurn = 0.0f;

void ProcessCollection(Manager& manager, Collection& collection);
void PostProcessCollection(Collection& collection, const glm::mat3* parentMatrix, float parentScale);
bool AnyModifier(const Collection& collection, uint32_t mask);
void DrawCollection(const Collection& collection, float interpolation);
} // namespace

// Properties

float Properties::Float(std::string_view name, float fallback) const
{
	const auto* words = Words(*this, name);
	return words != nullptr ? std::strtof((*words)[0].c_str(), nullptr) : fallback;
}

int Properties::Int(std::string_view name, int fallback) const
{
	const auto* words = Words(*this, name);
	return words != nullptr ? std::atoi((*words)[0].c_str()) : fallback;
}

bool Properties::Bool(std::string_view name, bool fallback) const
{
	return Int(name, fallback ? 1 : 0) != 0;
}

std::string Properties::String(std::string_view name) const
{
	const auto* words = Words(*this, name);
	return words == nullptr || (*words)[0] == "NULL_STRING" ? std::string() : (*words)[0];
}

std::vector<int> Properties::Ints(std::string_view name) const
{
	// ARRAY SIZE <n> <v1> .. <vn>
	std::vector<int> values;
	const auto* words = Words(*this, name);
	if (words != nullptr && words->size() >= 2 && (*words)[0] == "SIZE")
	{
		for (size_t i = 2; i < words->size(); ++i)
		{
			values.push_back(std::atoi((*words)[i].c_str()));
		}
	}
	return values;
}

std::vector<float> Properties::Floats(std::string_view name) const
{
	std::vector<float> values;
	const auto* words = Words(*this, name);
	if (words != nullptr && words->size() >= 2 && (*words)[0] == "SIZE")
	{
		for (size_t i = 2; i < words->size(); ++i)
		{
			values.push_back(std::strtof((*words)[i].c_str(), nullptr));
		}
	}
	return values;
}

SoundAction Properties::Sound(std::string_view name) const
{
	// <ACTION> LOOPING <0|1> ONLYONE <0|1> SOFTRELEASE <0|1> USESURFACE <0|1>
	SoundAction sound;
	const auto* words = Words(*this, name);
	if (words == nullptr)
	{
		return sound;
	}
	if (const auto action = SoundActions().find((*words)[0]); action != SoundActions().end())
	{
		sound.action = action->second;
	}
	for (size_t i = 1; i + 1 < words->size(); i += 2)
	{
		const bool on = (*words)[i + 1] != "0";
		const auto& key = (*words)[i];
		if (key == "LOOPING")
		{
			sound.looping = on;
		}
		else if (key == "ONLYONE")
		{
			sound.onlyOne = on;
		}
		else if (key == "SOFTRELEASE")
		{
			sound.softRelease = on;
		}
		else if (key == "USESURFACE")
		{
			sound.useSurface = on;
		}
	}
	return sound;
}

int Properties::MeshEnum(std::string_view name) const
{
	const auto* words = Words(*this, name);
	if (words == nullptr)
	{
		return -1;
	}
	const auto mesh = MeshEnums().find((*words)[0]);
	return mesh == MeshEnums().end() ? -1 : mesh->second;
}

// Atom

float Atom::Age() const
{
	return collection != nullptr ? collection->manager.age - birthTime : 0.0f;
}

void Atom::SetAge(float age)
{
	if (collection != nullptr)
	{
		birthTime = collection->manager.age - age;
	}
}

Atom* Atom::ParentAtom() const
{
	return collection != nullptr ? collection->parentAtom : nullptr;
}

void Atom::LocalToGlobal(glm::vec3& pos) const
{
	if (collection != nullptr)
	{
		collection->LocalToGlobal(pos);
	}
}

void Atom::GlobalToLocal(glm::vec3& pos) const
{
	if (collection != nullptr)
	{
		collection->GlobalToLocal(pos);
	}
}

glm::vec3 Atom::GlobalPos() const
{
	auto pos = localPos;
	LocalToGlobal(pos);
	return pos;
}

glm::mat3 Atom::ScaledRotation() const
{
	// fn_00673db0: its rotation, each axis by its scale, the up axis by its stretch too.
	const float s = CombinedScale();
	return glm::mat3(rotation[0] * s, rotation[1] * (s * stretchHeight), rotation[2] * s);
}

void Atom::SetFrame(int frame)
{
	frameTime = static_cast<float>(frame);
	lastFrameTime = frameTime;
}

void Atom::SetAngleY(float angle)
{
	rotation = glm::mat3(glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 1.0f, 0.0f)));
}

void Atom::SetAngleZ(float angle)
{
	rotation = glm::mat3(glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 0.0f, 1.0f)));
}

void Atom::RandomiseOrientation()
{
	// AtomCore::RandomiseOrientation: turned a random way about each axis.
	const auto r = glm::rotate(glm::mat4(1.0f), Random(6.2831853f), glm::vec3(1.0f, 0.0f, 0.0f)) *
	               glm::rotate(glm::mat4(1.0f), Random(6.2831853f), glm::vec3(0.0f, 1.0f, 0.0f)) *
	               glm::rotate(glm::mat4(1.0f), Random(6.2831853f), glm::vec3(0.0f, 0.0f, 1.0f));
	rotation = glm::mat3(r);
}

void Atom::AddSubCollections(const std::vector<int>& groups)
{
	for (const int group : groups)
	{
		AddSubCollection(group);
	}
}

void Atom::AddSubCollection(int group)
{
	if (group != -1 && collection != nullptr)
	{
		collection->manager.CreateCollection(group, this);
	}
}

void Atom::StartSound(const SoundAction& sound)
{
	if (sound.action < 0 || collection == nullptr)
	{
		return;
	}
	if (sound.onlyOne && SoundOf(sound.action) != nullptr)
	{
		return;
	}
	const auto emitter = PlaySound(sound, GlobalPos());
	if (emitter != 0)
	{
		sounds.push_back({sound.action, emitter});
	}
}

PlayingSound* Atom::SoundOf(int action)
{
	for (auto& sound : sounds)
	{
		if (sound.action == action)
		{
			return &sound;
		}
	}
	return nullptr;
}

void Atom::StopSound(PlayingSound* sound)
{
	if (sound == nullptr)
	{
		return;
	}
	core::StopSound(sound->emitter);
	sounds.erase(sounds.begin() + (sound - sounds.data()));
}

void Atom::StopAllSounds()
{
	for (const auto& sound : sounds)
	{
		core::StopSound(sound.emitter);
	}
	sounds.clear();
}

void Atom::GetLastGlobalMovement(glm::vec3& movement) const
{
	movement = drawPos - lastDrawPos;
}

// Collection

Collection::Collection(Manager& owner, Atom* parent, int ofGroup)
    : manager(owner)
    , parentAtom(parent)
    , group(ofGroup)
    , birthTime(owner.age)
{
	transmitsTransform = manager.file.hierarchies.at(static_cast<size_t>(group));
	// In the world's coordinates unless an atom above it carries its own (its collection's group transmits).
	for (const Atom* atom = parentAtom; atom != nullptr; atom = atom->ParentAtom())
	{
		if (atom->collection != nullptr && atom->collection->transmitsTransform)
		{
			inGlobalCoords = false;
		}
	}
	for (const auto* modifier : manager.file.groupModifiers.at(static_cast<size_t>(group)))
	{
		modifiers.push_back(modifier);
	}
}

Collection::~Collection()
{
	for (auto& atom : atoms)
	{
		atom->StopAllSounds();
		ForgetAtom(*atom);
		--manager.atomCount;
	}
}

float Collection::Age() const
{
	return manager.age - birthTime;
}

const glm::vec3& Collection::ParentPos() const
{
	return parentAtom != nullptr ? parentAtom->localPos : manager.Origin();
}

const glm::vec3& Collection::ParentVel() const
{
	static const glm::vec3 k_Zero(0.0f);
	return parentAtom != nullptr ? parentAtom->velocity : k_Zero;
}

void Collection::LocalToGlobal(glm::vec3& pos) const
{
	// AtomCollection::LocalToGlobal (BW1W120 006751d0): through each atom above that carries its own.
	if (inGlobalCoords)
	{
		return;
	}
	for (const Atom* atom = parentAtom; atom != nullptr; atom = atom->ParentAtom())
	{
		if (atom->collection != nullptr && atom->collection->transmitsTransform)
		{
			pos = atom->ScaledRotation() * pos + atom->localPos;
		}
	}
}

void Collection::GlobalToLocal(glm::vec3& pos) const
{
	// AtomCollection::GlobalToLocal (BW1W120 00675410): the inverse, from the top down.
	if (inGlobalCoords)
	{
		return;
	}
	std::vector<const Atom*> carriers;
	for (const Atom* atom = parentAtom; atom != nullptr; atom = atom->ParentAtom())
	{
		if (atom->collection != nullptr && atom->collection->transmitsTransform)
		{
			carriers.push_back(atom);
		}
	}
	for (auto it = carriers.rbegin(); it != carriers.rend(); ++it)
	{
		pos = glm::inverse((*it)->ScaledRotation()) * (pos - (*it)->localPos);
	}
}

Atom* Collection::NewAtom(const std::vector<int>& nextGroups)
{
	// AtomCollection::CommonInitNewAtom (BW1W120 00674c60): still, born now, in use; at the system's origin in a base
	// collection, at its parent's own point under a parent that carries it, else where its parent is.
	auto atom = std::make_unique<Atom>();
	atom->collection = this;
	atom->randomSeed = static_cast<uint8_t>(RandomInt(256));
	atom->SetAge(0.0f);
	atom->inUse = true;
	if (parentAtom == nullptr)
	{
		atom->localPos = manager.Origin();
	}
	else if (parentAtom->collection != nullptr && parentAtom->collection->transmitsTransform)
	{
		atom->localPos = glm::vec3(0.0f);
	}
	else
	{
		atom->localPos = parentAtom->GlobalPos();
	}
	auto* made = atom.get();
	atoms.push_front(std::move(atom));
	++manager.atomCount;
	made->AddSubCollections(nextGroups);
	return made;
}

Atom* Collection::NewAtom(int nextGroup)
{
	return NewAtom(nextGroup == -1 ? std::vector<int> {} : std::vector<int> {nextGroup});
}

void Collection::DeleteAtom(Atom* atom)
{
	for (auto it = atoms.begin(); it != atoms.end(); ++it)
	{
		if (it->get() == atom)
		{
			atom->StopAllSounds();
			ForgetAtom(*atom);
			--manager.atomCount;
			atoms.erase(it);
			return;
		}
	}
}

// Manager

Manager::Manager(const FileData& data, const Start& start)
    : file(data)
    , origin(start.origin)
    , lastOrigin(start.origin)
    , magnitude(start.magnitude)
    , humanCasting(start.humanCasting)
    , onEvent(start.onEvent)
{
	if (start.enabled)
	{
		_state |= EnabledState;
	}
	// fn_00673070: a base collection of each group made from the start.
	for (int g = 0; g < k_MaxGroups; ++g)
	{
		if (file.initiallyCreated.at(static_cast<size_t>(g)))
		{
			CreateCollection(g, nullptr);
		}
	}
}

Manager::~Manager()
{
	collections.clear();
}

void Manager::SetState(uint32_t state)
{
	// PSysManager::SetState (BW1W120 00672ff0): closing down is timed from when it began.
	if ((state & CloseDownState) != 0 && !IsInState(CloseDownState))
	{
		closeDownAge = age;
	}
	_state |= state;
}

glm::vec3 Manager::GesturePos() const
{
	return humanCasting ? HandPos() + glm::vec3(0.0f, handRaise, 0.0f) : origin;
}

bool Manager::SpellEvent(const Event& event) const
{
	return onEvent ? onEvent(event) : true;
}

Collection* Manager::CreateCollection(int group, Atom* parent)
{
	// PSysManager::CreateCollection (BW1W120 00673110).
	if (group < 0 || group >= k_MaxGroups)
	{
		return nullptr;
	}
	auto collection = std::make_unique<Collection>(*this, parent, group);
	auto* made = collection.get();
	if (parent != nullptr)
	{
		parent->subCollections.push_front(std::move(collection));
	}
	else
	{
		collections.push_front(std::move(collection));
	}
	return made;
}

Atom* Manager::CreateAtomInBaseGroup(int group, const std::vector<int>& nextGroups)
{
	for (auto& collection : collections)
	{
		if (collection->group == group)
		{
			return collection->NewAtom(nextGroups);
		}
	}
	return nullptr;
}

uint32_t Manager::TakeTarget()
{
	if (targets.empty())
	{
		return 0;
	}
	const auto target = targets.front();
	targets.erase(targets.begin());
	return target;
}

void Manager::Process()
{
	// fn_00673340: the float providers for this system, each collection's rules, the atoms' draw info, then a turn older.
	for (auto* provider : file.providers)
	{
		provider->UpdateParams(*this);
	}
	for (auto& collection : collections)
	{
		ProcessCollection(*this, *collection);
	}
	for (auto& collection : collections)
	{
		PostProcessCollection(*collection, nullptr, 1.0f);
	}
	lastOrigin = origin;
	age += k_TimeStep;
}

bool Manager::Finished() const
{
	// fn_00673290: no atoms left and nothing left to make them (or, closing down, nothing that keeps making them); or
	// older than the file's MaxSpellAge.
	if (file.deleteOnCloseDown && IsInState(CloseDownState))
	{
		return true;
	}
	if (atomCount == 0)
	{
		const auto any = [this](uint32_t mask) {
			return std::any_of(collections.begin(), collections.end(), [mask](const auto& c) { return AnyModifier(*c, mask); });
		};
		if (IsInState(CloseDownState) && !any(4))
		{
			return true;
		}
		if (!any(6))
		{
			return true;
		}
	}
	return file.maxSpellAge > 0.0f && age > file.maxSpellAge;
}

// Modifier, Condition, Creator

bool Condition::TestOnAtom(Atom& atom)
{
	if (OnAtoms())
	{
		return TrueForAtom(atom) != invertResponse;
	}
	return atom.collection != nullptr && TrueForCollection(*atom.collection) != invertResponse;
}

bool Modifier::Run(Collection& collection) const
{
	if (condition != nullptr && !condition->OnAtoms() && condition->TrueForCollection(collection) == condition->invertResponse)
	{
		return true; // its condition is not met here this turn: kept, not run
	}
	return ModifyAtomCollection(collection);
}

bool Modifier::ModifyAtomCollection(Collection& collection) const
{
	const bool onAtoms = condition != nullptr && condition->OnAtoms();
	for (auto it = collection.atoms.begin(); it != collection.atoms.end();)
	{
		auto next = std::next(it);
		Atom& atom = **it;
		if (!onAtoms || condition->TrueForAtom(atom) != condition->invertResponse)
		{
			ModifyAtomCore(atom);
		}
		it = next;
	}
	return true;
}

void Creator::CreateParticle(Atom& atom) const
{
	// fn_006a85e0: the creator's colour, and whether its frames go round.
	atom.colour = (static_cast<uint32_t>(colourA & 0xff) << 24) | (static_cast<uint32_t>(colourR & 0xff) << 16) |
	              (static_cast<uint32_t>(colourG & 0xff) << 8) | static_cast<uint32_t>(colourB & 0xff);
	atom.specular = (static_cast<uint32_t>(specR & 0xff) << 16) | (static_cast<uint32_t>(specG & 0xff) << 8) |
	                static_cast<uint32_t>(specB & 0xff);
	atom.loopAnim = loopAnim;
	atom.particle.creator = this;
}

// Randomness and the world

float Random(float range)
{
	return range <= 0.0f ? 0.0f : Locator::rng::value().NextValue(0.0f, range);
}

float Random(float min, float max)
{
	return min + Random(max - min);
}

int RandomInt(int range)
{
	return range <= 1 ? 0 : std::min(static_cast<int>(Random(static_cast<float>(range))), range - 1);
}

glm::vec3 RandomR3()
{
	// PSysManager::PSysRandR3: each of x, y and z from -1 to 1.
	return {Random(2.0f) - 1.0f, Random(2.0f) - 1.0f, Random(2.0f) - 1.0f};
}

float Altitude(const glm::vec3& point)
{
	return Locator::terrainSystem::value().GetHeightAt(glm::vec2(point.x, point.z));
}

glm::vec3 Normal(const glm::vec3& point)
{
	return Locator::terrainSystem::value().GetNormalAt(glm::vec2(point.x, point.z));
}

glm::vec3 HandPos()
{
	using Side = ecs::systems::HandSystemInterface::Side;
	const auto positions = Locator::handSystem::value().GetPlayerHandPositions();
	return positions[static_cast<size_t>(Side::Left)].value_or(positions[static_cast<size_t>(Side::Right)].value_or(glm::vec3(0.0f)));
}

glm::vec3 CameraPos()
{
	return Locator::camera::value().GetOrigin();
}

namespace
{
void ProcessCollection(Manager& manager, Collection& collection)
{
	// fn_006750e0: its rules in order, each taken off once done with it (or, RemoveOnCloseDown, once the system
	// closes down); then the collections under its atoms.
	for (auto it = collection.modifiers.begin(); it != collection.modifiers.end();)
	{
		const auto* modifier = *it;
		bool keep = modifier->Run(collection);
		if (keep && modifier->removeOnCloseDown && manager.IsInState(CloseDownState))
		{
			keep = false;
		}
		it = keep ? std::next(it) : collection.modifiers.erase(it);
	}
	for (auto& atom : collection.atoms)
	{
		for (auto& sub : atom->subCollections)
		{
			ProcessCollection(manager, *sub);
		}
	}
}

void PostProcessCollection(Collection& collection, const glm::mat3* parentMatrix, float parentScale)
{
	// fn_00673ea0 for each atom: where it is drawn now (its matrix under its carrier's), what it was the turn before, its
	// alpha with its collection's, its frame on.
	for (auto& atomPtr : collection.atoms)
	{
		auto& atom = *atomPtr;
		atom.lastDrawPos = atom.drawPos;
		atom.lastDrawMatrix = atom.drawMatrix;
		atom.lastDrawScale = atom.drawScale;
		atom.drawPos = atom.GlobalPos();
		atom.drawMatrix = parentMatrix != nullptr ? *parentMatrix * atom.ScaledRotation() : atom.ScaledRotation();
		atom.drawScale = atom.CombinedScale() * parentScale;
		if (!atom.drawn)
		{
			atom.drawn = true;
			atom.lastDrawPos = atom.drawPos;
			atom.lastDrawMatrix = atom.drawMatrix;
			atom.lastDrawScale = atom.drawScale;
		}
		atom.drawAlpha = static_cast<uint8_t>((static_cast<uint32_t>(atom.Alpha()) * collection.alpha) / 255u);
		atom.lastFrameTime = atom.frameTime;
		if (atom.playAnim)
		{
			atom.frameTime += k_TimeStep * atom.frameRate;
			const auto count = static_cast<float>(std::max(atom.frameCount, 1));
			if (atom.loopAnim)
			{
				atom.frameTime -= count * std::floor(atom.frameTime / count);
			}
			else
			{
				atom.frameTime = std::clamp(atom.frameTime, 0.0f, count - 1.0f);
			}
		}
		if (atom.particle.stretched)
		{
			atom.particle.previousStretch = atom.particle.stretch;
			atom.particle.stretch = atom.stretchHeight;
		}
		for (const auto& sound : atom.sounds)
		{
			MoveSound(sound.emitter, atom.drawPos);
		}
		const bool carries = collection.transmitsTransform;
		for (auto& sub : atom.subCollections)
		{
			PostProcessCollection(*sub, carries ? &atom.drawMatrix : parentMatrix, carries ? atom.drawScale : parentScale);
		}
	}
}

bool AnyModifier(const Collection& collection, uint32_t mask)
{
	// fn_00674e50.
	for (const auto* modifier : collection.modifiers)
	{
		if ((modifier->flags & mask) != 0)
		{
			return true;
		}
	}
	for (const auto& atom : collection.atoms)
	{
		for (const auto& sub : atom->subCollections)
		{
			if (AnyModifier(*sub, mask))
			{
				return true;
			}
		}
	}
	return false;
}

void DrawCollection(const Collection& collection, float interpolation)
{
	for (const auto& atom : collection.atoms)
	{
		if (atom->particle.kind != Particle::Kind::None && atom->inUse && atom->drawn)
		{
			DrawAtom(*atom, collection.interpolateDrawPos ? interpolation : 1.0f);
		}
		for (const auto& sub : atom->subCollections)
		{
			DrawCollection(*sub, interpolation);
		}
	}
}
} // namespace
} // namespace core

namespace
{
core::Manager* Find(Handle system)
{
	const auto it = core::g_systems.find(system);
	return it == core::g_systems.end() ? nullptr : it->second.manager.get();
}
} // namespace

Handle Create(const Start& start)
{
	const auto* file = core::File(start.type);
	if (file == nullptr)
	{
		return 0;
	}
	const auto handle = core::g_nextHandle++;
	core::g_systems[handle].manager = std::make_unique<core::Manager>(*file, start);
	return handle;
}

void SetOrigin(Handle system, const glm::vec3& origin)
{
	if (auto* manager = Find(system))
	{
		manager->origin = origin;
	}
}

void SetMagnitude(Handle system, float magnitude)
{
	if (auto* manager = Find(system))
	{
		manager->magnitude = magnitude;
	}
}

void SetStrength(Handle system, float strength)
{
	if (auto* manager = Find(system))
	{
		manager->strength = strength;
	}
}

void AddTarget(Handle system, uint32_t object)
{
	if (auto* manager = Find(system))
	{
		manager->targets.push_back(object);
	}
}

void SetEnabled(Handle system, bool enabled)
{
	if (auto* manager = Find(system))
	{
		if (enabled)
		{
			manager->SetState(core::EnabledState);
		}
		else
		{
			manager->ClearState(core::EnabledState);
		}
	}
}

void CloseDown(Handle system)
{
	if (auto* manager = Find(system))
	{
		manager->ClearState(core::EnabledState);
		manager->SetState(core::CloseDownState);
	}
}

float HandRaise(Handle system)
{
	const auto* manager = Find(system);
	return manager != nullptr ? manager->handRaise : 0.0f;
}

float PourTime(Handle system)
{
	const auto* manager = Find(system);
	return manager != nullptr ? manager->pourCloseTime : -1.0f;
}

float Age(Handle system)
{
	const auto* manager = Find(system);
	return manager != nullptr ? manager->age : 0.0f;
}

bool Exists(Handle system)
{
	return Find(system) != nullptr;
}

bool IsClosingDown(Handle system)
{
	const auto* manager = Find(system);
	return manager != nullptr && manager->IsInState(core::CloseDownState);
}

void Destroy(Handle system)
{
	core::g_systems.erase(system);
}

void Update(float seconds)
{
	core::g_sinceTurn += seconds;
	// (A long frame - loading - runs at most a second of turns.)
	core::g_sinceTurn = std::min(core::g_sinceTurn, 1.0f);
	while (core::g_sinceTurn >= core::k_TimeStep)
	{
		core::g_sinceTurn -= core::k_TimeStep;
		for (auto it = core::g_systems.begin(); it != core::g_systems.end();)
		{
			auto& manager = *it->second.manager;
			manager.Process();
			it = manager.Finished() ? core::g_systems.erase(it) : std::next(it);
		}
	}
}

void Draw(uint16_t viewId, uint16_t program)
{
	const float interpolation = std::clamp(core::g_sinceTurn / core::k_TimeStep, 0.0f, 1.0f);
	for (const auto& [handle, system] : core::g_systems)
	{
		for (const auto& collection : system.manager->collections)
		{
			core::DrawCollection(*collection, interpolation);
		}
	}
	core::FlushDrawing(viewId, program);
}

void Clear()
{
	core::g_systems.clear();
}

void Stats(int& systems, int& atoms)
{
	systems = static_cast<int>(core::g_systems.size());
	atoms = 0;
	for (const auto& [handle, system] : core::g_systems)
	{
		atoms += system.manager->atomCount;
	}
}
} // namespace openblack::psys
