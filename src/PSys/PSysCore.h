/*******************************************************************************
 * Copyright (c) 2018-2026 openblack developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/openblack/openblack
 *
 * openblack is licensed under the GNU General Public License version 3.
 *******************************************************************************/

#pragma once

// The particle systems' insides, as runblack.exe's (bw1-decomp's PSysAtomCore.h, PSysManager.h, PSysBaseModifiers.h,
// PSysEvents.h, ParticleCreator.h; what the decomp leaves empty read from the exe, its addresses given).

#include <array>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <glm/mat3x3.hpp>
#include <glm/vec3.hpp>

#include "PSys/PSys.h"

namespace openblack::psys::core
{
constexpr int k_MaxGroups = 25;
// PSysManager::TimeStep: the game turn the systems are run in (0xd01a38's 100 ms, times 0.001).
constexpr float k_TimeStep = 0.1f;
constexpr float k_OneOverTimeStep = 1.0f / k_TimeStep;

enum ManagerState : uint32_t
{
	CloseDownState = 1 << 0,
	EnabledState = 1 << 1,
};

// PSysSoundAction: an action of a sound bank's animation sound table (LHAudioAnimArrayTable), played at an atom.
struct SoundAction
{
	int action = -1; // PSYS_SOUND_ACTION_NONE
	int level = 2;   // IMPACT_SOUND_LEVEL: 1 heavy, 2 medium, 3 light (the tables' large, medium and small sounds)
	bool looping = false;
	bool onlyOne = false;
	bool softRelease = false;
	bool useSurface = false;
};

class Manager;
class Collection;
struct Atom;
class Modifier;
class Creator;

// What a rule keeps for a collection or an atom (BaseCollectionModifierData, BaseAtomModifierData).
struct ModifierData
{
	virtual ~ModifierData() = default;
};

// A sound an atom plays (PSysSound): its bank's emitter, kept where the atom is.
struct PlayingSound
{
	int action = -1;
	uint32_t emitter = 0;
};

// RenderParticle: what an atom is drawn as.
struct Particle
{
	enum class Kind : uint8_t
	{
		None, // Particle3DPnt: nothing
		Sprite,
		Mist,
		Mesh,
	};
	Kind kind = Kind::None;
	const Creator* creator = nullptr;
	// Particle3DSprite: its creator's sheet, cell grid, origin and blending
	const std::string* texture = nullptr;
	int perRow = 8;
	float originX = 0.0f;
	float originY = 0.0f;
	bool additive = false;
	uint8_t alpha = 255;      // its creator's ScaleAlpha
	int frameOffset = 0;      // FileOffset: its first frame's place on the sheet
	bool centreAtBase = false;
	bool ignoreRotation = false;
	bool stretched = false;   // StretchVertically not 1: drawn at the atom's stretch, interpolated
	float previousStretch = 1.0f;
	float stretch = 1.0f;
	// LH3DMist
	float mistRatio = 3.0f;
	float mistCounter = 0.0f;
	// A mesh: the entity drawn for it
	uint32_t meshEntity = 0;
	uint32_t mesh = 0; // MeshId
	float lowestPoint = 0.0f; // RenderParticle::GetLowestPoint, from its model's bottom (at scale 1)
};

// AtomCore.
struct Atom
{
	Collection* collection = nullptr;
	std::list<std::unique_ptr<Collection>> subCollections; // newest first, as the original's
	std::map<const Modifier*, std::unique_ptr<ModifierData>> data;
	std::vector<PlayingSound> sounds;
	glm::vec3 velocity {0.0f};
	float birthTime = -1.0f;
	glm::mat3 rotation {1.0f}; // RotationMatrix: its right, up and at
	float baseScale = 1.0f;
	float scale = 1.0f;
	float stretchHeight = 1.0f;
	glm::vec3 localPos {0.0f};
	uint32_t colour = 0xffffffff; // ARGB
	uint32_t specular = 0;
	bool flag0 = false; // UR_WillowWisp's DrawOnFirstUpdate
	bool hasBeenDeflected = false;
	bool inUse = false;
	// What it is drawn from (CalculateDrawPosInfo, now and the turn before), interpolated between them.
	glm::vec3 drawPos {0.0f};
	glm::vec3 lastDrawPos {0.0f};
	glm::mat3 drawMatrix {1.0f};
	glm::mat3 lastDrawMatrix {1.0f};
	float drawScale = 1.0f;
	float lastDrawScale = 1.0f;
	uint8_t drawAlpha = 255;
	bool drawn = false; // has a turn's draw info
	// PSysAnimInfo
	float frameTime = 0.0f;
	float lastFrameTime = 0.0f;
	float frameRate = 1.0f;
	int frameCount = 1;
	bool playAnim = false;
	bool loopAnim = false;
	float gravityScale = 1.0f;
	// DrawOffsetLT: drawn where the hand is now, not where it was at the turn (the hand moves every frame).
	bool followsHand = false;
	Particle particle;
	uint8_t randomSeed = 0;

	[[nodiscard]] float Age() const;
	void SetAge(float age);
	void SetAlpha(uint8_t alpha) { colour = (colour & 0x00ffffffu) | (static_cast<uint32_t>(alpha) << 24); }
	[[nodiscard]] uint8_t Alpha() const { return static_cast<uint8_t>(colour >> 24); }
	[[nodiscard]] float CombinedScale() const { return scale * baseScale; }
	[[nodiscard]] Atom* ParentAtom() const;
	void LocalToGlobal(glm::vec3& pos) const;
	void GlobalToLocal(glm::vec3& pos) const;
	[[nodiscard]] glm::vec3 GlobalPos() const;
	// Its matrix (rotation by scale, the up axis by stretch too) and its point, in its collection's coordinates.
	[[nodiscard]] glm::mat3 ScaledRotation() const;
	void SetFrame(int frame);
	void SetAngleY(float angle);
	void SetAngleZ(float angle);
	void RandomiseOrientation();
	void AddSubCollections(const std::vector<int>& groups);
	void AddSubCollection(int group);
	void StartSound(const SoundAction& sound);
	PlayingSound* SoundOf(int action);
	void StopSound(PlayingSound* sound);
	void StopAllSounds();
	void GetLastGlobalMovement(glm::vec3& movement) const;
	template <class T>
	T& Data(const Modifier* modifier)
	{
		auto& slot = data[modifier];
		if (!slot)
		{
			slot = std::make_unique<T>();
		}
		return static_cast<T&>(*slot);
	}
	[[nodiscard]] ModifierData* FindData(const Modifier* modifier) const
	{
		const auto it = data.find(modifier);
		return it == data.end() ? nullptr : it->second.get();
	}
};

// AtomCollection.
class Collection
{
public:
	Collection(Manager& manager, Atom* parent, int group);
	~Collection();

	[[nodiscard]] float Age() const;
	[[nodiscard]] const glm::vec3& ParentPos() const;
	[[nodiscard]] const glm::vec3& ParentVel() const;
	void LocalToGlobal(glm::vec3& pos) const;
	void GlobalToLocal(glm::vec3& pos) const;
	// A new atom in it (CommonInitNewAtom, InitNewAtom): where it starts, then a collection under it for each group.
	Atom* NewAtom(const std::vector<int>& nextGroups);
	Atom* NewAtom(int nextGroup);
	void DeleteAtom(Atom* atom);
	template <class T>
	T& Data(const Modifier* modifier)
	{
		auto& slot = data[modifier];
		if (!slot)
		{
			slot = std::make_unique<T>();
		}
		return static_cast<T&>(*slot);
	}
	template <class T>
	const T* FindModifier() const
	{
		for (const auto* modifier : modifiers)
		{
			if (const auto* found = dynamic_cast<const T*>(modifier))
			{
				return found;
			}
		}
		return nullptr;
	}

	Manager& manager;
	Atom* parentAtom;
	int group;
	float birthTime;
	bool inGlobalCoords = true;
	bool interpolateDrawPos = true;
	bool shouldBeEmitting = true;
	bool transmitsTransform = false;
	uint8_t alpha = 255;
	std::list<const Modifier*> modifiers; // its group's rules, in the file's order
	std::map<const Modifier*, std::unique_ptr<ModifierData>> data;
	std::list<std::unique_ptr<Atom>> atoms; // newest first
};

struct FileData;

// PSysManager.
class Manager
{
public:
	Manager(const FileData& file, const Start& start);
	~Manager();

	void Process();
	[[nodiscard]] bool Finished() const;
	[[nodiscard]] bool IsInState(uint32_t state) const { return (_state & state) != 0; }
	void SetState(uint32_t state);
	void ClearState(uint32_t state) { _state &= ~state; }
	[[nodiscard]] const glm::vec3& Origin() const { return origin; }
	// Where the spell is being cast from: the hand's point for your hand's (ProcessInfo's position).
	[[nodiscard]] glm::vec3 GesturePos() const;
	[[nodiscard]] bool IsHumanPlayerCasting() const { return humanCasting; }
	[[nodiscard]] bool IsMyInterfaceCasting() const { return humanCasting; }
	bool SpellEvent(const Event& event) const;
	// Atom::AddSubCollection's and the start's: a collection of `group` (none for -1).
	Collection* CreateCollection(int group, Atom* parent);
	Atom* CreateAtomInBaseGroup(int group, const std::vector<int>& nextGroups);
	uint32_t TakeTarget();
	[[nodiscard]] size_t TargetCount() const { return targets.size(); }

	const FileData& file;
	std::list<std::unique_ptr<Collection>> collections; // the base ones, newest first
	glm::vec3 origin {0.0f};
	glm::vec3 lastOrigin {0.0f};
	float age = 0.0f;
	float closeDownAge = 0.0f;
	float magnitude = 1.0f;
	float strength = 1.0f;
	bool humanCasting = true;
	glm::vec3 initialVelocity {0.0f};
	glm::vec3 initialDirection {0.0f, 0.0f, 1.0f};
	float initialSpeed = 0.0f;
	std::function<bool(const Event&)> onEvent;
	std::vector<uint32_t> targets;
	uint32_t object = 0;
	int atomCount = 0;
	// HandStateGrain::StartTimedPour's: how far the hand is raised as it pours (UR_HandSprinkle's KeyPoints over its
	// TotalTime, times HeightToRaise), and when the pour is over (FracToCloseDownOn of TotalTime).
	float handRaise = 0.0f;
	float pourCloseTime = -1.0f;

private:
	uint32_t _state = 0;
};

struct Properties;

// Persistent: every object of an effect file, its properties read from the file (DefineProperties' names).
class Object
{
public:
	virtual ~Object() = default;
	virtual void Read(const Properties& /*properties*/) {}
	std::string name;
};

// FloatProvider: a value set once a turn for the system being run (UpdateParams), read by the rules.
class FloatProvider: public Object
{
public:
	virtual void UpdateParams(const Manager& manager) = 0;
	[[nodiscard]] float Value() const { return value; }

	float value = 0.0f;
};

// TEventCondition.
class Condition: public Object
{
public:
	// ConditionAlwaysTrueForCollection: a condition on atoms (true of every collection).
	[[nodiscard]] virtual bool OnAtoms() const = 0;
	virtual bool TrueForCollection(Collection& /*collection*/) { return false; }
	virtual bool TrueForAtom(Atom& /*atom*/) { return false; }
	// TestConditionOnAtom (BW1W120 00670b10): an atom condition on it, a collection condition on its collection.
	bool TestOnAtom(Atom& atom);

	bool invertResponse = false;
};

// AtomCollectionModifier: a rule.
class Modifier: public Object
{
public:
	// Run each turn on a collection of its group (fn_00675ad0): its collection condition first; false when it is done
	// there (it is taken off the collection).
	bool Run(Collection& collection) const;
	// AtomCollectionModifier::ModifyAtomCollection (BW1W120 00675b10): ModifyAtomCore on each atom its atom
	// condition is true of (each atom, without one).
	virtual bool ModifyAtomCollection(Collection& collection) const;
	virtual void ModifyAtomCore(Atom& /*atom*/) const {}

	uint32_t flags = 0; // 2 makes atoms, 4 keeps making them (fn_00673260: the system is not over while one is left)
	int group = -1;
	bool removeOnCloseDown = false;
	Condition* condition = nullptr;
};

class Creator;

// AtomCreateRule.
class CreateRule: public Modifier
{
public:
	CreateRule() { flags |= 6; }

	Creator* creator = nullptr;
	std::vector<int> nextGroups;
};

// ParticleCreator: what an atom it makes is drawn as.
class Creator: public Object
{
public:
	// CreateParticle, and its common start (fn_006a85e0: the atom's colour and loop from the creator).
	virtual void CreateParticle(Atom& atom) const;

	bool loopAnim = true;
	bool usePlayerColour = false;
	float usePlayerColourBlend = 1.0f;
	int colourA = 255;
	int colourR = 255;
	int colourG = 255;
	int colourB = 255;
	int specR = 0;
	int specG = 0;
	int specB = 0;
	float initialScale = 1.0f;
};

// An effect file, read: its objects, each group's rules in order, its float providers.
struct FileData
{
	std::string name;
	std::array<bool, k_MaxGroups> initiallyCreated {};
	std::array<bool, k_MaxGroups> hierarchies {};
	bool deleteOnCloseDown = false;
	float maxSpellAge = -1.0f;
	std::vector<std::unique_ptr<Object>> objects;
	std::array<std::vector<const Modifier*>, k_MaxGroups> groupModifiers;
	std::vector<FloatProvider*> providers;
};

// An effect file's properties, by name, each its words after the type (PROPERTY <name> <type> <words...>).
struct Properties
{
	std::map<std::string, std::vector<std::string>, std::less<>> values;
	std::map<std::string, Object*, std::less<>>* objects = nullptr;

	[[nodiscard]] float Float(std::string_view name, float fallback) const;
	[[nodiscard]] int Int(std::string_view name, int fallback) const;
	[[nodiscard]] bool Bool(std::string_view name, bool fallback) const;
	[[nodiscard]] std::string String(std::string_view name) const;
	[[nodiscard]] std::vector<int> Ints(std::string_view name) const;
	[[nodiscard]] std::vector<float> Floats(std::string_view name) const;
	[[nodiscard]] SoundAction Sound(std::string_view name) const;
	[[nodiscard]] int MeshEnum(std::string_view name) const;
	template <class T>
	T* Pointer(std::string_view name) const
	{
		const auto it = values.find(name);
		if (it == values.end() || it->second.empty() || objects == nullptr)
		{
			return nullptr;
		}
		const auto object = objects->find(it->second[0]);
		return object == objects->end() ? nullptr : dynamic_cast<T*>(object->second);
	}
};

// The rules' and creators' classes, by name: a new one (none for a class not run yet).
std::unique_ptr<Object> MakeObject(const std::string& type);
inline void ReadProperties(Object& object, const Properties& properties)
{
	object.Read(properties);
}

// PSysManager::PSysFloatRand, PSysRand, PSysRandR3.
float Random(float range);
float Random(float min, float max);
int RandomInt(int range);
glm::vec3 RandomR3();

// The world the systems run in.
float Altitude(const glm::vec3& point);
glm::vec3 Normal(const glm::vec3& point);
glm::vec3 HandPos();
glm::vec3 CameraPos();
bool IsWater(const glm::vec3& point);

// Sounds: an action's sample in the spell sounds' bank (Audio/SFX/Game/spells.sad), started at a point, moved, stopped.
uint32_t PlaySound(const SoundAction& sound, const glm::vec3& at);
void MoveSound(uint32_t emitter, const glm::vec3& at);
void StopSound(uint32_t emitter);

// The objects a system is about (a heal's villagers): where they are, how wide and high, whether in the hand.
bool ObjectPos(uint32_t object, glm::vec3& pos);
float ObjectRadius(uint32_t object);
float ObjectHeight(uint32_t object);
bool ObjectInHand(uint32_t object);
void SetObjectSpecular(uint32_t object, uint32_t colour);

// Drawing: each atom with a particle, every frame, then all of them at once.
void DrawAtom(const Atom& atom, float interpolation);
void FlushDrawing(uint16_t viewId, uint16_t program);
void ForgetAtom(Atom& atom);
} // namespace openblack::psys::core
