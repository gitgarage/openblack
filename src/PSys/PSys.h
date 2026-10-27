/*******************************************************************************
 * Copyright (c) 2018-2026 openblack developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/openblack/openblack
 *
 * openblack is licensed under the GNU General Public License version 3.
 *******************************************************************************/

#pragma once

#include <cstdint>
#include <functional>

#include <glm/vec3.hpp>

// The original's particle systems (runblack.exe's PSys*, reconstructed in bw1-decomp): every miracle's look and sound -
// the food falling from the hand, the rain cone, the chakra round the healed - is a file of rules in
// Data/Spells/ZSpellFiles (SF_<name>_txt.zzz: zlib text, its length before it), run here as the original runs them.
//
// A system is groups of rules (each rule's Group); a group's collection of atoms is made when the system starts
// (InitiallyCreated) or under an atom a rule makes (its NextGroups). Each game turn (100 ms) every collection's rules
// run in the file's order, each kept while it says so; an atom is drawn as its creator's particle (a sprite from a sheet,
// a mist, a mesh, nothing), and a rule can tell the spell something happened (an atom landed: food there).
namespace openblack::psys
{
// The original's particle types: the index of runblack.exe's table of SF_ files (its init at BW1W120 0068ea00), as
// info.dat's magic infos name them (particleType, particleTypeInHand).
enum class Type : int32_t
{
	None = 0,
	Firework = 2,
	Forest = 4,
	LightningStormPush = 6,
	LightningBolt = 7,
	Food = 9,
	FoodPoisoned = 10,
	BeamExplosionSingle = 11,
	Wood = 15,
	Water = 16,
	WaterPuOne = 17,
	WaterOnHolder = 18,
	WaterInHand = 19,
	WaterInHandPuOne = 20,
	Heal = 21,
	SpellFail = 26,
	SpellSelection = 28,
	MagicObjectCreated = 30,
	Fireball = 43,
	FireballInHand = 44,
	FireballOnHolder = 47,
	HealInHand = 50,
	HealOnHolder = 51,
	HealPuOne = 54,
	LightningStormInHand = 55,
	LightningStormOnHolder = 58,
	LightningBoltInHand = 59,
	LightningBoltOnHolder = 60,
	Shield = 64,
	ShieldInHand = 65,
	ShieldOnHolder = 66,
	TeleportOnHolder = 74,
	CreatureSpellItchyOnHolder = 89,
	CreatureSpellFreezeOnHolder = 92,
	CreatureSpellCompassionOnHolder = 95,
	FoodPickup = 107,
	FoodPickupPoisoned = 108,
	FoodPickupFish = 109,
	WoodPickup = 110,
	FoodPutdownPoisoned = 111,
	WoodPutdown = 112,
	FoodPutdown = 113,
};

// SPELL_EVENT_TYPE: what a rule tells the spell.
enum class EventType : int32_t
{
	Atom = 0,
	Spell = 1,
	Heal = 2,
	Landed = 3, // LandscapeCollide: an atom fell to the land
};

struct Event
{
	EventType type = EventType::Atom;
	glm::vec3 position {0.0f};
	glm::vec3 velocity {0.0f};
	float strength = 1.0f;
	uint32_t target = 0; // what it is about (a healed villager), if anything
};

// A running system; 0 is none.
using Handle = uint32_t;

struct Start
{
	Type type = Type::None;
	glm::vec3 origin {0.0f};  // the spell's point (FollowOrigin, the base collections' atoms start there)
	float magnitude = 1.0f;   // PSysManager::Magnitude (MagnitudeFloatProvider; the hand's scale for an effect in it)
	bool humanCasting = true; // cast by your hand: the hand's point is where it is cast from
	std::function<bool(const Event&)> onEvent; // the spell's part (false: not taken)
	bool enabled = true;      // PSYS_MANAGER_STATE_ENABLED from the start
};

Handle Create(const Start& start);
// The spell moved (a sphere carried, a heal on a villager who walks): the base collections' origin.
void SetOrigin(Handle system, const glm::vec3& origin);
void SetMagnitude(Handle system, float magnitude);
// ProcessInfo's strength (StrengthFloatProvider; how hard it is being cast, 0 - 1).
void SetStrength(Handle system, float strength);
// One of its targets (SpellTargets: UR_HealSpellChakra's villagers), for its rules to take.
void AddTarget(Handle system, uint32_t object);
// PSYS_MANAGER_STATE_ENABLED: being cast (an emitter waiting on EventConditionTrueWhenEnabled emits).
void SetEnabled(Handle system, bool enabled);
// PSYS_MANAGER_STATE_CLOSE_DOWN: over; its rules wind down and it ends once they have.
void CloseDown(Handle system);
[[nodiscard]] bool Exists(Handle system);
// A hand pour's (UR_HandSprinkle, HandStateGrain::StartTimedPour): how far up the hand is now, and how long into the
// system its pour is over (when the spell closes it down; -1 for none).
[[nodiscard]] float HandRaise(Handle system);
[[nodiscard]] float PourTime(Handle system);
[[nodiscard]] float Age(Handle system);
[[nodiscard]] bool IsClosingDown(Handle system);
// Gone at once, without winding down.
void Destroy(Handle system);

// Each frame, with the game's time: runs every system's game turns (100 ms each) as they fall due.
void Update(float seconds);
// Each frame, from the renderer: every system's particles, interpolated between their last two turns, with the
// particle shader `program` (a bgfx program handle's index).
void Draw(uint16_t viewId, uint16_t program);
// Everything gone (a new land).
void Clear();
// How many systems and atoms are running, for the log.
void Stats(int& systems, int& atoms);
} // namespace openblack::psys
