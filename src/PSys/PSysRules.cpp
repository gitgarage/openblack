/*******************************************************************************
 * Copyright (c) 2018-2026 openblack developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/openblack/openblack
 *
 * openblack is licensed under the GNU General Public License version 3.
 *******************************************************************************/

// The particle systems' classes: bw1-decomp's PSysModifiers.cpp, its defaults (PSysRules.h, PSysBaseModifiers.h,
// PSysEvents.h) and property names (PSysProperties.cpp); what it leaves empty read from runblack.exe, its address given.

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "PSys/PSysCore.h"

namespace openblack::psys::core
{
namespace
{
constexpr float k_TwoPi = 6.2831853f;
constexpr float k_HalfPi = 1.5707963f;

float Lerp(float a, float b, float t)
{
	return a + (b - a) * t;
}

glm::mat3 RotationAbout(int axis, float angle)
{
	const glm::vec3 axes[3] = {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
	return glm::mat3(glm::rotate(glm::mat4(1.0f), angle, axes[axis]));
}

// LHMatrix::PostRotateX/Y/Z: a turn after the matrix's own.
void PostRotate(glm::mat3& matrix, int axis, float angle)
{
	matrix = RotationAbout(axis, angle) * matrix;
}

// The common base properties: AtomCollectionModifier::DefineProperties (Group, RemoveOnCloseDown, Condition) and
// AtomCreateRule's (PCreator, NextGroups).
void ReadModifier(Modifier& modifier, const Properties& p)
{
	modifier.group = p.Int("Group", modifier.group);
	modifier.removeOnCloseDown = p.Bool("RemoveOnCloseDown", modifier.removeOnCloseDown);
	modifier.condition = p.Pointer<Condition>("Condition");
}

void ReadCreateRule(CreateRule& rule, const Properties& p)
{
	ReadModifier(rule, p);
	rule.creator = p.Pointer<Creator>("PCreator");
	rule.nextGroups = p.Ints("NextGroups");
}

void ReadCreator(Creator& creator, const Properties& p)
{
	creator.usePlayerColour = p.Bool("UsePlayerColor", creator.usePlayerColour);
	creator.usePlayerColourBlend = p.Float("UsePlayerColorBlend", creator.usePlayerColourBlend);
	creator.loopAnim = p.Bool("LoopAnim", creator.loopAnim);
	creator.colourA = p.Int("ColorA", creator.colourA);
	creator.colourR = p.Int("ColorR", creator.colourR);
	creator.colourG = p.Int("ColorG", creator.colourG);
	creator.colourB = p.Int("ColorB", creator.colourB);
	creator.specR = p.Int("SpecColorR", creator.specR);
	creator.specG = p.Int("SpecColorG", creator.specG);
	creator.specB = p.Int("SpecColorB", creator.specB);
	creator.initialScale = p.Float("InitialScale", creator.initialScale);
}

float ValueOf(const FloatProvider* provider, float fallback)
{
	return provider != nullptr ? provider->Value() : fallback;
}

// ---------------------------------------------------------------------------------------------------------- creators

// ParticlePointCreator::CreateParticle (BW1W120 006a8770): nothing drawn.
class PointCreator: public Creator
{
public:
	void Read(const Properties& p) override { ReadCreator(*this, p); }
};

class SpriteCreator: public Creator
{
public:
	void Read(const Properties& p) override
	{
		ReadCreator(*this, p);
		useLandscapeColour = p.Bool("UseLandscapeColor", false);
		frameRate = p.Float("FrameRate", frameRate);
		playAnim = p.Bool("PlayAnim", playAnim);
		useAdditiveAlpha = p.Bool("UseAdditiveAlpha", false);
		numFrames = std::max(p.Int("NumFrames", 1), 1);
		initFrame = p.Int("InitFrame", 0);
		fileOffset = p.Int("FileOffset", 0);
		texture = p.String("TextureFileName");
		randomiseScale = p.Bool("RandomiseScale", false);
		randomiseInitFrame = p.Bool("RandomiseInitFrame", false);
		setHorizontal = p.Bool("SetHorozontal", false);
		setVertical = p.Bool("SetVertical", false);
		centreAtBase = p.Bool("CentreAtBase", false);
		randomiseFrameDirection = p.Bool("RandomiseFrameDirection", false);
		ignoreRotation = p.Bool("IgnoreRotation", false);
		stretchVertically = p.Float("StretchVertically", 1.0f);
		spriteOriginX = p.Float("SpriteOriginX", 0.0f);
		spriteOriginY = p.Float("SpriteOriginY", 0.0f);
		spritesPerRow = std::max(p.Int("NumSpritesPerRow", 8), 1);
		scaleAlpha = p.Int("ScaleAlpha", 255);
	}

	void CreateParticle(Atom& atom) const override
	{
		// ParticleSpriteCreator::CreateParticle3DSprite (BW1W120 006aa0d0).
		Creator::CreateParticle(atom);
		auto& particle = atom.particle;
		particle.kind = Particle::Kind::Sprite;
		if (stretchVertically != 1.0f)
		{
			atom.stretchHeight = stretchVertically;
			particle.stretched = true;
			particle.stretch = particle.previousStretch = stretchVertically;
		}
		particle.texture = &texture;
		particle.perRow = spritesPerRow;
		particle.originX = spriteOriginX;
		particle.originY = spriteOriginY;
		particle.additive = useAdditiveAlpha;
		particle.alpha = static_cast<uint8_t>(std::clamp(scaleAlpha, 0, 255));
		particle.frameOffset = fileOffset;
		particle.centreAtBase = centreAtBase;
		particle.ignoreRotation = ignoreRotation;
		atom.baseScale = initialScale * (randomiseScale ? Random(0.7f) + 0.3f : 1.0f);
		atom.SetFrame(randomiseInitFrame ? RandomInt(numFrames) : initFrame);
		float rate = frameRate;
		if (randomiseFrameDirection && RandomInt(256) > 128)
		{
			rate = -rate;
		}
		atom.frameRate = rate;
		atom.frameCount = numFrames;
		atom.playAnim = playAnim;
		atom.loopAnim = loopAnim;
	}

	bool useLandscapeColour = false;
	float frameRate = 1.0f;
	bool playAnim = true;
	bool useAdditiveAlpha = false;
	int numFrames = 1;
	int initFrame = 0;
	int fileOffset = 0;
	std::string texture;
	bool randomiseScale = false;
	bool randomiseInitFrame = false;
	bool setHorizontal = false;
	bool setVertical = false;
	bool centreAtBase = false;
	bool randomiseFrameDirection = false;
	bool ignoreRotation = false;
	float stretchVertically = 1.0f;
	float spriteOriginX = 0.0f;
	float spriteOriginY = 0.0f;
	int spritesPerRow = 8;
	int scaleAlpha = 255;
};

// ParticleMistCreator: an LH3DMist - a frame of the smoke sheet turned to the camera, flattened by its ratio seen from
// the side (ParticleMistCreator::CreateLH3DMist: its ratio, or a random 5 to 7.5 without one).
class MistCreator: public Creator
{
public:
	void Read(const Properties& p) override
	{
		ReadCreator(*this, p);
		randomiseScale = p.Bool("RandomiseScale", false);
		ratio = p.Float("Ratio", 0.0f);
		initialScaleMin = p.Float("InitialScaleMin", 0.0f);
		texture = p.String("TextureFileName");
	}

	void CreateParticle(Atom& atom) const override
	{
		Creator::CreateParticle(atom);
		atom.particle.kind = Particle::Kind::Mist;
		atom.particle.mistRatio = ratio != 0.0f ? ratio : 5.0f + Random(2.5f);
		atom.particle.mistCounter = std::floor(Random(16.0f));
		atom.baseScale = randomiseScale && initialScaleMin > 0.0f ? Random(initialScaleMin, initialScale) : initialScale;
	}

	bool randomiseScale = false;
	float ratio = 0.0f;
	float initialScaleMin = 0.0f;
	std::string texture;
};

// ParticleMeshCreator, ParticleMeshCreatorAnimTextured: a model of AllMeshes.g3d (its MeshEnum).
class MeshCreator: public Creator
{
public:
	void Read(const Properties& p) override
	{
		ReadCreator(*this, p);
		mesh = p.MeshEnum("MeshEnum");
		useAdditiveAlpha = p.Bool("UseAdditiveAlpha", false);
		useGlobalAlpha = p.Bool("UseGlobalAlpha", false);
		playAnim = p.Bool("PlayAnim", false);
		frameRate = p.Float("FrameRate", 1.0f);
	}

	void CreateParticle(Atom& atom) const override
	{
		Creator::CreateParticle(atom);
		atom.baseScale = initialScale;
		if (mesh >= 0)
		{
			atom.particle.kind = Particle::Kind::Mesh;
			atom.particle.mesh = static_cast<uint32_t>(mesh);
		}
	}

	int mesh = -1;
	bool useAdditiveAlpha = false;
	bool useGlobalAlpha = false;
	bool playAnim = false;
	float frameRate = 1.0f;
};

// ---------------------------------------------------------------------------------------------------- float providers

void ReadRange(const Properties& p, float& scaleBy, float& minimum, float& maximum)
{
	scaleBy = p.Float("ScaleBy", scaleBy);
	minimum = p.Float("Minimum", minimum);
	maximum = p.Float("Maximum", maximum);
}

class ConstFloatProvider: public FloatProvider
{
public:
	void Read(const Properties& p) override { constValue = p.Float("ConstValue", 0.0f); }
	void UpdateParams(const Manager& /*manager*/) override { value = constValue; }
	float constValue = 0.0f;
};

class MagnitudeFloatProvider: public FloatProvider
{
public:
	void Read(const Properties& p) override { ReadRange(p, scaleBy, minimum, maximum); }
	void UpdateParams(const Manager& manager) override { value = std::clamp(manager.magnitude * scaleBy, minimum, maximum); }
	float scaleBy = 1.0f;
	float minimum = -1000000.0f;
	float maximum = 1000000.0f;
};

class StrengthFloatProvider: public FloatProvider
{
public:
	void Read(const Properties& p) override { ReadRange(p, scaleBy, minimum, maximum); }
	void UpdateParams(const Manager& manager) override { value = std::clamp(manager.strength * scaleBy, minimum, maximum); }
	float scaleBy = 1.0f;
	float minimum = -1000000.0f;
	float maximum = 1000000.0f;
};

class MagnitudeTimesStrengthFloatProvider: public FloatProvider
{
public:
	void Read(const Properties& p) override { ReadRange(p, scaleBy, minimum, maximum); }
	void UpdateParams(const Manager& manager) override
	{
		value = std::clamp(manager.magnitude * manager.strength * scaleBy, minimum, maximum);
	}
	float scaleBy = 1.0f;
	float minimum = 0.0f;
	float maximum = 100.0f;
};

// RenderHandScaleFloatProvider::UpdateParams (BW1W120 0069a750): the magnitude (the hand's scale for an effect in it).
class RenderHandScaleFloatProvider: public FloatProvider
{
public:
	void Read(const Properties& p) override { scaleBy = p.Float("ScaleBy", 1.0f); }
	void UpdateParams(const Manager& manager) override { value = manager.magnitude * scaleBy; }
	float scaleBy = 1.0f;
};

// RenderHandScaleTimesStrengthFloatProvider::UpdateParams (BW1W120 0069a770).
class RenderHandScaleTimesStrengthFloatProvider: public FloatProvider
{
public:
	void Read(const Properties& p) override { scaleBy = p.Float("ScaleBy", 1.0f); }
	void UpdateParams(const Manager& manager) override { value = manager.magnitude * manager.strength * scaleBy; }
	float scaleBy = 1.0f;
};

// --------------------------------------------------------------------------------------------------------- conditions

class CollectionCondition: public Condition
{
public:
	void Read(const Properties& p) override { invertResponse = p.Bool("InvertResponse", false); }
	[[nodiscard]] bool OnAtoms() const override { return false; }
};

class AtomCondition: public Condition
{
public:
	void Read(const Properties& p) override { invertResponse = p.Bool("InvertResponse", false); }
	[[nodiscard]] bool OnAtoms() const override { return true; }
};

// BW1W120 0067d7f0, 0067d810: the system closing down, being cast.
class EventConditionTrueOnCloseDown: public CollectionCondition
{
public:
	bool TrueForCollection(Collection& c) override { return c.manager.IsInState(CloseDownState); }
};

class EventConditionTrueWhenEnabled: public CollectionCondition
{
public:
	bool TrueForCollection(Collection& c) override { return c.manager.IsInState(EnabledState); }
};

class EventConditionNever: public CollectionCondition
{
};

// BW1W120 0067d830.
class EC_CollectionShouldBeEmitting: public CollectionCondition
{
public:
	bool TrueForCollection(Collection& c) override { return c.shouldBeEmitting; }
};

// BW1W120 0067d840: older than DelayTime.
class EventConditionCollectionDelay: public CollectionCondition
{
public:
	void Read(const Properties& p) override
	{
		CollectionCondition::Read(p);
		delayTime = p.Float("DelayTime", 0.0f);
	}
	bool TrueForCollection(Collection& c) override { return c.Age() > delayTime; }
	float delayTime = 0.0f;
};

// BW1W120 0067d870: from StartTime to StopTime.
class EventConditionCollectionLimitedTime: public CollectionCondition
{
public:
	void Read(const Properties& p) override
	{
		CollectionCondition::Read(p);
		startTime = p.Float("StartTime", 0.0f);
		stopTime = p.Float("StopTime", 10.0f);
	}
	bool TrueForCollection(Collection& c) override { return c.Age() >= startTime && c.Age() < stopTime; }
	float startTime = 0.0f;
	float stopTime = 10.0f;
};

// BW1W120 0067d9c0: a chance of Freq a second.
class EventConditionRandom: public CollectionCondition
{
public:
	void Read(const Properties& p) override
	{
		CollectionCondition::Read(p);
		freq = p.Float("Freq", 1.0f);
	}
	bool TrueForCollection(Collection& /*c*/) override { return Random(1.0f) < k_TimeStep * freq; }
	float freq = 1.0f;
};

// BW1W120 0067d8b0.
class EventConditionAtomDelay: public AtomCondition
{
public:
	void Read(const Properties& p) override
	{
		AtomCondition::Read(p);
		delayTime = p.Float("DelayTime", 0.0f);
	}
	bool TrueForAtom(Atom& a) override { return a.Age() > delayTime; }
	float delayTime = 0.0f;
};

// BW1W120 0067d980.
class EventConditionAtomLimitedTime: public AtomCondition
{
public:
	void Read(const Properties& p) override
	{
		AtomCondition::Read(p);
		startTime = p.Float("StartTime", 0.0f);
		stopTime = p.Float("StopTime", 10.0f);
	}
	bool TrueForAtom(Atom& a) override { return a.Age() >= startTime && a.Age() < stopTime; }
	float startTime = 0.0f;
	float stopTime = 10.0f;
};

// BW1W120 0067db50, 0067db70.
class EventConditionAtomInUse: public AtomCondition
{
public:
	bool TrueForAtom(Atom& a) override { return a.inUse; }
};

class EventConditionAtomHasBeenDeflected: public AtomCondition
{
public:
	bool TrueForAtom(Atom& a) override { return a.hasBeenDeflected; }
};

// BW1W120 0067dab0: slower than CutOffSpeed.
class EventConditionAtomBelowSpeed: public AtomCondition
{
public:
	void Read(const Properties& p) override
	{
		AtomCondition::Read(p);
		cutOffSpeed = p.Float("CutOffSpeed", 0.1f);
	}
	bool TrueForAtom(Atom& a) override { return glm::length(a.velocity) < cutOffSpeed; }
	float cutOffSpeed = 0.1f;
};

// BW1W120 0067dba0: lower over the land than CutOffHeight.
class EventConditionAtomBelowHeight: public AtomCondition
{
public:
	void Read(const Properties& p) override
	{
		AtomCondition::Read(p);
		cutOffHeight = p.Float("CutOffHeight", 0.0f);
	}
	bool TrueForAtom(Atom& a) override
	{
		const auto pos = a.GlobalPos();
		return pos.y - Altitude(pos) - cutOffHeight < 0.0f;
	}
	float cutOffHeight = 0.0f;
};

// BW1W120 0067dc80.
class EC_AtomAlphaAbove: public AtomCondition
{
public:
	void Read(const Properties& p) override
	{
		AtomCondition::Read(p);
		alphaValue = p.Int("AlphaValue", 0);
	}
	bool TrueForAtom(Atom& a) override { return static_cast<int>(a.Alpha()) > alphaValue; }
	int alphaValue = 0;
};

// BW1W120 0067daf0: it, or an atom above it, has been deflected.
class EC_DeflectionInAtomsHierarchy: public AtomCondition
{
public:
	bool TrueForAtom(Atom& a) override
	{
		for (const Atom* atom = &a; atom != nullptr; atom = atom->ParentAtom())
		{
			if (atom->hasBeenDeflected)
			{
				return true;
			}
		}
		return false;
	}
};

// ------------------------------------------------------------------------------------------------------------ rules

// EventAlways::ModifyAtomCore: an event at each atom.
class EventAlways: public Modifier
{
public:
	void Read(const Properties& p) override { ReadModifier(*this, p); }
	void ModifyAtomCore(Atom& atom) const override
	{
		Event event;
		event.type = EventType::Atom;
		event.position = atom.GlobalPos();
		atom.GetLastGlobalMovement(event.velocity);
		atom.collection->manager.SpellEvent(event);
	}
};

class StartStopSoundOnCondition: public Modifier
{
public:
	StartStopSoundOnCondition() { flags |= 0x10; }
	void Read(const Properties& p) override
	{
		ReadModifier(*this, p);
		sound = p.Sound("Sound");
		soundCondition = p.Pointer<Condition>("SoundCondition");
		fadeStep = p.Int("FadeStep", 0);
	}
	void ModifyAtomCore(Atom& atom) const override
	{
		if (soundCondition != nullptr && !soundCondition->TestOnAtom(atom))
		{
			atom.StopSound(atom.SoundOf(sound.action));
		}
		else if (atom.SoundOf(sound.action) == nullptr)
		{
			atom.StartSound(sound);
		}
	}
	SoundAction sound;
	Condition* soundCondition = nullptr;
	int fadeStep = 0;
};

class AddSoundToAtom: public Modifier
{
public:
	struct Data: ModifierData
	{
		bool added = false;
	};
	void Read(const Properties& p) override
	{
		ReadModifier(*this, p);
		sound = p.Sound("Sound");
		soundCondition = p.Pointer<Condition>("SoundCondition");
		delay = p.Float("Delay", 0.0f);
		stopOtherSoundsFirst = p.Bool("StopOtherSoundsFirst", false);
	}
	void ModifyAtomCore(Atom& atom) const override
	{
		auto& data = atom.Data<Data>(this);
		if (!data.added && atom.Age() >= delay && (soundCondition == nullptr || soundCondition->TestOnAtom(atom)))
		{
			if (stopOtherSoundsFirst)
			{
				atom.StopAllSounds();
			}
			atom.StartSound(sound);
			data.added = true;
		}
	}
	SoundAction sound;
	Condition* soundCondition = nullptr;
	float delay = 0.0f;
	bool stopOtherSoundsFirst = false;
};

class RemoveSoundFromAtom: public Modifier
{
public:
	struct Data: ModifierData
	{
		bool removed = false;
	};
	void Read(const Properties& p) override
	{
		ReadModifier(*this, p);
		sound = p.Sound("Sound");
		soundCondition = p.Pointer<Condition>("SoundCondition");
	}
	void ModifyAtomCore(Atom& atom) const override
	{
		auto& data = atom.Data<Data>(this);
		if (!data.removed && (soundCondition == nullptr || soundCondition->TestOnAtom(atom)))
		{
			atom.StopSound(atom.SoundOf(sound.action));
			data.removed = true;
		}
	}
	SoundAction sound;
	Condition* soundCondition = nullptr;
};

// LandscapeCollide::ModifyAtomCore (BW1W120 0067d6e0): an atom gone below the land is taken off, telling the spell
// where it landed (SendEvent).
class LandscapeCollide: public Modifier
{
public:
	LandscapeCollide() { flags |= 0x60; }
	void Read(const Properties& p) override
	{
		ReadModifier(*this, p);
		sendEvent = p.Bool("SendEvent", true);
	}
	void ModifyAtomCore(Atom& atom) const override
	{
		const auto pos = atom.GlobalPos();
		if (pos.y >= Altitude(pos))
		{
			return;
		}
		if (sendEvent)
		{
			Event event;
			event.type = EventType::Landed;
			event.position = pos;
			atom.GetLastGlobalMovement(event.velocity);
			event.strength = 1.0f;
			atom.collection->manager.SpellEvent(event);
		}
		atom.collection->DeleteAtom(&atom);
	}
	bool sendEvent = true;
};

class RemoveRule: public Modifier
{
public:
	RemoveRule() { flags |= 0x20; }
	void ModifyAtomCore(Atom& atom) const override
	{
		if (ShouldRemove(*atom.collection, atom))
		{
			atom.collection->DeleteAtom(&atom);
		}
	}
	virtual bool ShouldRemove(Collection& collection, Atom& atom) const = 0;
};

class RemoveRuleOldAgeOnly: public RemoveRule
{
public:
	void Read(const Properties& p) override
	{
		ReadModifier(*this, p);
		minAtoms = p.Int("MinAtoms", 0);
		dieAge = p.Float("DieAge", 0.0f);
	}
	bool ShouldRemove(Collection& /*collection*/, Atom& atom) const override { return atom.Age() > dieAge; }
	int minAtoms = 0;
	float dieAge = 0.0f;
};

class RemoveRuleAfterCloseDown: public RemoveRule
{
public:
	void Read(const Properties& p) override
	{
		ReadModifier(*this, p);
		delay = p.Float("Delay", 0.0f);
	}
	bool ShouldRemove(Collection& collection, Atom& /*atom*/) const override
	{
		const auto& manager = collection.manager;
		return manager.IsInState(CloseDownState) && manager.age - manager.closeDownAge > delay;
	}
	float delay = 0.0f;
};

class RemoveRuleAfterConditionTrue: public RemoveRule
{
public:
	struct Data: ModifierData
	{
		bool met = false;
		float metAge = 0.0f;
	};
	void Read(const Properties& p) override
	{
		ReadModifier(*this, p);
		conditionForRemove = p.Pointer<Condition>("ConditionForRemove");
		delay = p.Float("Delay", 0.0f);
	}
	bool ShouldRemove(Collection& /*collection*/, Atom& atom) const override
	{
		auto& data = atom.Data<Data>(this);
		if (!data.met && (conditionForRemove == nullptr || conditionForRemove->TestOnAtom(atom)))
		{
			data.met = true;
			data.metAge = atom.Age();
		}
		return data.met && atom.Age() - data.metAge > delay;
	}
	Condition* conditionForRemove = nullptr;
	float delay = 0.0f;
};

class RemoveRuleProb: public RemoveRule
{
public:
	void Read(const Properties& p) override
	{
		ReadModifier(*this, p);
		minAtoms = p.Int("MinAtoms", 0);
		removeFreq = p.Float("RemoveFreq", 1.0f);
	}
	bool ShouldRemove(Collection& collection, Atom& /*atom*/) const override
	{
		return static_cast<int>(collection.atoms.size()) > minAtoms && Random(1.0f) < removeFreq * k_TimeStep;
	}
	int minAtoms = 0;
	float removeFreq = 1.0f;
};

class UpdateRuleGravity: public Modifier
{
public:
	UpdateRuleGravity() { flags |= 8; }
	void Read(const Properties& p) override
	{
		ReadModifier(*this, p);
		maxSpeed = p.Float("MaxSpeed", maxSpeed);
		damping = p.Float("Damping", damping);
		useDamping = p.Bool("UseDamping", useDamping);
		gravity = p.Float("Gravity", gravity);
		useWind = p.Bool("UseWind", useWind);
		windMagnification = p.Float("WindMagnification", windMagnification);
	}
	void ModifyAtomCore(Atom& atom) const override
	{
		auto velocity = atom.velocity;
		if (useDamping)
		{
			velocity *= 1.0f - k_TimeStep * damping;
		}
		velocity.y -= gravity * atom.gravityScale * std::clamp(velocity.y + maxSpeed, 0.0f, 1.0f) * k_TimeStep;
		atom.velocity = velocity;
		atom.localPos += velocity * k_TimeStep;
	}
	float maxSpeed = 100.0f;
	float gravity = 10.0f;
	float damping = 0.0f;
	float windMagnification = 100.0f;
	bool useDamping = false;
	bool useWind = false;
};

// UpdateRuleGravityWithFloor::ModifyAtomCollection: bouncing off the land (the wind and shields not yet).
class UpdateRuleGravityWithFloor: public UpdateRuleGravity
{
public:
	void Read(const Properties& p) override
	{
		UpdateRuleGravity::Read(p);
		dampingHorizontalBounce = p.Float("DampingHorozontalBounce", 0.5f);
		dampingVerticalBounce = p.Float("DampingVerticalBounce", 0.5f);
		groundDrag = p.Float("GroundDrag", 0.0f);
		impactSound = p.Sound("ImpactSound");
		impactSpeedSmall = p.Float("ImpactSpeedSmall", 5.0f);
		impactSpeedMedium = p.Float("ImpactSpeedMedium", 20.0f);
		impactSpeedLarge = p.Float("ImpactSpeedLarge", 40.0f);
		minAlphaForImpactSound = p.Int("MinAlphaForImpactSoundOrRipple", 60);
	}
	bool ModifyAtomCollection(Collection& collection) const override
	{
		for (auto& atomPtr : collection.atoms)
		{
			auto& atom = *atomPtr;
			auto velocity = atom.velocity;
			if (useDamping)
			{
				velocity *= 1.0f - k_TimeStep * damping;
			}
			atom.velocity = velocity;
			atom.localPos += velocity * k_TimeStep;
			auto global = atom.GlobalPos();
			const float altitude = Altitude(global);
			const float lowest = global.y + atom.particle.lowestPoint * atom.CombinedScale();
			if (lowest >= altitude)
			{
				velocity.y -= gravity * atom.gravityScale * std::clamp(velocity.y + maxSpeed, 0.0f, 1.0f) * k_TimeStep;
			}
			else
			{
				global.y += altitude - lowest;
				atom.GlobalToLocal(global);
				atom.localPos = global;
				const auto normal = Normal(global);
				const float speed = glm::dot(normal, velocity);
				if (speed < 0.0f)
				{
					if (atom.Alpha() >= minAlphaForImpactSound && std::abs(speed) >= impactSpeedSmall && impactSound.action >= 0 &&
					    atom.SoundOf(impactSound.action) == nullptr)
					{
						auto sound = impactSound;
						sound.level = std::abs(speed) < impactSpeedMedium ? 3 : std::abs(speed) < impactSpeedLarge ? 2 : 1;
						atom.StartSound(sound);
					}
					auto normalVelocity = normal * speed;
					auto tangent = velocity - normalVelocity;
					auto direction = tangent;
					if (glm::dot(direction, direction) < 0.0001f)
					{
						direction = glm::vec3(0.0001f, 0.0f, 0.0f);
					}
					const float length = glm::length(direction);
					direction /= length;
					tangent -= direction * std::clamp(k_TimeStep * groundDrag, 0.0f, length);
					tangent *= dampingHorizontalBounce;
					normalVelocity *= -dampingVerticalBounce;
					velocity = normalVelocity + tangent;
				}
			}
			atom.velocity = velocity;
		}
		return true;
	}
	float dampingHorizontalBounce = 0.5f;
	float dampingVerticalBounce = 0.5f;
	float groundDrag = 0.0f;
	SoundAction impactSound;
	float impactSpeedSmall = 5.0f;
	float impactSpeedMedium = 20.0f;
	float impactSpeedLarge = 40.0f;
	int minAlphaForImpactSound = 60;
};

class UR_UpdatePosnFromVelocity: public Modifier
{
public:
	UR_UpdatePosnFromVelocity() { flags |= 8; }
	void Read(const Properties& p) override { ReadModifier(*this, p); }
	void ModifyAtomCore(Atom& atom) const override { atom.localPos += atom.velocity * k_TimeStep; }
};

class UR_ChangeScale: public Modifier
{
public:
	UR_ChangeScale() { flags |= 8; }
	void Read(const Properties& p) override
	{
		ReadModifier(*this, p);
		startTime = p.Float("StartTime", 0.0f);
		stopTime = p.Float("StopTime", 5.0f);
		startScale = p.Float("StartScale", 1.0f);
		stopScale = p.Float("StopScale", 1.0f);
	}
	void ModifyAtomCore(Atom& atom) const override
	{
		const float age = atom.Age();
		if (age >= startTime)
		{
			if (age <= stopTime)
			{
				atom.scale = Lerp(startScale, stopScale, (age - startTime) / (stopTime - startTime));
			}
			else if (age - k_TimeStep < stopTime)
			{
				atom.scale = stopScale;
			}
		}
	}
	float startTime = 0.0f;
	float stopTime = 5.0f;
	float startScale = 1.0f;
	float stopScale = 1.0f;
};

class UR_ChangeScaleXYZ: public Modifier
{
public:
	UR_ChangeScaleXYZ() { flags |= 8; }
	void Read(const Properties& p) override
	{
		ReadModifier(*this, p);
		startTime = p.Float("StartTime", 0.0f);
		stopTime = p.Float("StopTime", 5.0f);
		startScaleXZ = p.Float("StartScaleXZ", 1.0f);
		stopScaleXZ = p.Float("StopScaleXZ", 1.0f);
		startScaleY = p.Float("StartScaleY", 1.0f);
		stopScaleY = p.Float("StopScaleY", 1.0f);
	}
	void ModifyAtomCore(Atom& atom) const override
	{
		const float age = atom.Age();
		if (age < startTime)
		{
			return;
		}
		float xz = stopScaleXZ;
		float y = stopScaleY;
		if (age <= stopTime)
		{
			const float t = (age - startTime) / (stopTime - startTime);
			xz = Lerp(startScaleXZ, stopScaleXZ, t);
			y = Lerp(startScaleY, stopScaleY, t);
		}
		else if (age - k_TimeStep > stopTime)
		{
			return;
		}
		atom.scale = xz;
		atom.stretchHeight = xz > 0.0001f ? y / xz : 0.0f;
	}
	float startTime = 0.0f;
	float stopTime = 5.0f;
	float startScaleXZ = 1.0f;
	float stopScaleXZ = 1.0f;
	float startScaleY = 1.0f;
	float stopScaleY = 1.0f;
};

class AR_FadeAlpha: public Modifier
{
public:
	AR_FadeAlpha() { flags |= 0x10; }
	void Read(const Properties& p) override
	{
		ReadModifier(*this, p);
		startTime = p.Float("StartTime", 0.0f);
		stopTime = p.Float("StopTime", 5.0f);
		startAlpha = p.Int("StartAlpha", 0);
		stopAlpha = p.Int("StopAlpha", 255);
	}
	void ModifyAtomCore(Atom& atom) const override
	{
		const float age = atom.Age();
		if (age >= startTime)
		{
			if (age <= stopTime)
			{
				atom.SetAlpha(static_cast<uint8_t>(
				    Lerp(static_cast<float>(startAlpha), static_cast<float>(stopAlpha), (age - startTime) / (stopTime - startTime))));
			}
			else if (age - k_TimeStep <= stopTime)
			{
				atom.SetAlpha(static_cast<uint8_t>(stopAlpha));
			}
		}
	}
	float startTime = 0.0f;
	float stopTime = 5.0f;
	int startAlpha = 0;
	int stopAlpha = 255;
};

class AR_FadeCollectionAlpha: public Modifier
{
public:
	AR_FadeCollectionAlpha() { flags |= 0x10; }
	void Read(const Properties& p) override
	{
		ReadModifier(*this, p);
		startTime = p.Float("StartTime", 0.0f);
		stopTime = p.Float("StopTime", 5.0f);
		startAlpha = p.Int("StartAlpha", 0);
		stopAlpha = p.Int("StopAlpha", 255);
		timesAreAfterCloseDown = p.Bool("TimesAreAfterCloseDown", false);
		setAlphaAfterStopTime = p.Bool("SetAlphaAfterStopTime", false);
	}
	bool ModifyAtomCollection(Collection& collection) const override
	{
		float age = collection.Age();
		if (timesAreAfterCloseDown)
		{
			const auto& manager = collection.manager;
			if (!manager.IsInState(CloseDownState))
			{
				return true;
			}
			age = std::max(manager.age - manager.closeDownAge, 0.0f);
		}
		if (age >= startTime && age <= stopTime)
		{
			auto alpha = static_cast<uint8_t>(
			    Lerp(static_cast<float>(startAlpha), static_cast<float>(stopAlpha), (age - startTime) / (stopTime - startTime)));
			if (age + k_TimeStep > stopTime)
			{
				alpha = static_cast<uint8_t>(stopAlpha);
			}
			collection.alpha = alpha;
		}
		else if (setAlphaAfterStopTime && age > stopTime)
		{
			collection.alpha = static_cast<uint8_t>(stopAlpha);
		}
		return true;
	}
	float startTime = 0.0f;
	float stopTime = 5.0f;
	int startAlpha = 0;
	int stopAlpha = 255;
	bool timesAreAfterCloseDown = false;
	bool setAlphaAfterStopTime = false;
};

class AR_FadeOutOnceConditionTrue: public Modifier
{
public:
	struct Data: ModifierData
	{
		bool met = false;
		float startTime = 0.0f;
		float startAlpha = 255.0f;
		float startScale = 1.0f;
	};
	AR_FadeOutOnceConditionTrue() { flags |= 0x10; }
	void Read(const Properties& p) override
	{
		ReadModifier(*this, p);
		timeToFadeOut = p.Float("TimeToFadeOut", 2.0f);
		fadeAlpha = p.Bool("FadeAlpha", true);
		shrinkScale = p.Bool("ShrinkScale", false);
		conditionStartFadeOut = p.Pointer<Condition>("ConditionStartFadeOut");
	}
	void ModifyAtomCore(Atom& atom) const override
	{
		auto& data = atom.Data<Data>(this);
		if (!data.met && (conditionStartFadeOut == nullptr || conditionStartFadeOut->TestOnAtom(atom)))
		{
			data.met = true;
			data.startTime = atom.Age();
			data.startScale = atom.scale;
			data.startAlpha = atom.Alpha();
		}
		if (data.met && (fadeAlpha || shrinkScale))
		{
			const float t = std::clamp((atom.Age() - data.startTime) / timeToFadeOut, 0.0f, 1.0f);
			if (fadeAlpha)
			{
				atom.SetAlpha(static_cast<uint8_t>(Lerp(data.startAlpha, 0.0f, t)));
			}
			if (shrinkScale)
			{
				atom.scale = Lerp(data.startScale, 0.0f, t);
			}
		}
	}
	float timeToFadeOut = 2.0f;
	Condition* conditionStartFadeOut = nullptr;
	bool fadeAlpha = true;
	bool shrinkScale = false;
};

class AppearanceRuleFadeOut: public Modifier
{
public:
	AppearanceRuleFadeOut() { flags |= 0x10; }
	void Read(const Properties& p) override
	{
		ReadModifier(*this, p);
		vanishAge = p.Float("VanishAge", 1.0f);
	}
	void ModifyAtomCore(Atom& atom) const override
	{
		atom.scale = std::clamp(1.0f - atom.Age() * 0.5f / vanishAge, 0.0f, 1.0f);
		atom.SetAlpha(static_cast<uint8_t>(std::clamp(1.0f - atom.Age() / vanishAge, 0.0f, 1.0f) * 255.0f));
	}
	float vanishAge = 1.0f;
};

class AppearanceRuleTumble: public Modifier
{
public:
	AppearanceRuleTumble() { flags |= 0x10; }
	void Read(const Properties& p) override
	{
		ReadModifier(*this, p);
		tumbleSpeed = p.Float("TumbleSpeed", 0.0f);
		maxTumbleSpeed = p.Float("MaxTumbleSpeed", 0.0f);
		restrictMaxRotation = p.Bool("RestrictMaxRotation", false);
	}
	void ModifyAtomCore(Atom& atom) const override
	{
		float angle = tumbleSpeed * glm::length(atom.velocity);
		if (restrictMaxRotation)
		{
			angle = std::clamp(angle, -maxTumbleSpeed, maxTumbleSpeed);
		}
		angle *= k_TimeStep;
		PostRotate(atom.rotation, std::abs(atom.velocity.x) > std::abs(atom.velocity.z) ? 2 : 0, angle);
	}
	float tumbleSpeed = 0.0f;
	float maxTumbleSpeed = 0.0f;
	bool restrictMaxRotation = false;
};

class UpdateRuleRotatePrincipalAxis: public Modifier
{
public:
	UpdateRuleRotatePrincipalAxis() { flags |= 8; }
	void Read(const Properties& p) override
	{
		ReadModifier(*this, p);
		angularVel = p.Float("AngularVel", 0.0f);
		const int chosen = p.Int("AxisChosen", 1);
		axis = chosen >= 0 && chosen <= 2 ? chosen : 1;
	}
	void ModifyAtomCore(Atom& atom) const override { PostRotate(atom.rotation, axis, k_TimeStep * angularVel); }
	int axis = 1;
	float angularVel = 0.0f;
};

class UR_OrientSpriteWithRandomAngle: public Modifier
{
public:
	struct Data: ModifierData
	{
		bool first = true;
		float angle = 0.0f;
	};
	UR_OrientSpriteWithRandomAngle() { flags |= 8; }
	void Read(const Properties& p) override
	{
		ReadModifier(*this, p);
		randomAngle = p.Float("RandomAngle", 0.0f);
		defaultAngle = p.Float("DefaultAngle", 0.0f);
	}
	bool ModifyAtomCollection(Collection& collection) const override
	{
		for (auto& atom : collection.atoms)
		{
			auto& data = atom->Data<Data>(this);
			if (data.first)
			{
				data.first = false;
				data.angle = (1.0f - Random(2.0f)) * randomAngle + defaultAngle;
			}
			atom->SetAngleY(data.angle);
		}
		return true;
	}
	float randomAngle = 0.0f;
	float defaultAngle = 0.0f;
};

class SetScale: public Modifier
{
public:
	SetScale() { flags |= 8; }
	void Read(const Properties& p) override
	{
		ReadModifier(*this, p);
		scale = p.Pointer<FloatProvider>("Scale");
	}
	void ModifyAtomCore(Atom& atom) const override
	{
		if (scale != nullptr)
		{
			atom.scale = scale->Value();
		}
	}
	FloatProvider* scale = nullptr;
};

class SetAtomAlpha: public Modifier
{
public:
	void Read(const Properties& p) override
	{
		ReadModifier(*this, p);
		alpha = p.Pointer<FloatProvider>("Alpha");
	}
	void ModifyAtomCore(Atom& atom) const override
	{
		if (alpha != nullptr)
		{
			atom.SetAlpha(static_cast<uint8_t>(std::clamp(alpha->Value(), 0.0f, 255.0f)));
		}
	}
	FloatProvider* alpha = nullptr;
};

class SetCollectionAlpha: public Modifier
{
public:
	void Read(const Properties& p) override
	{
		ReadModifier(*this, p);
		alpha = p.Pointer<FloatProvider>("Alpha");
	}
	bool ModifyAtomCollection(Collection& collection) const override
	{
		if (alpha == nullptr)
		{
			return false;
		}
		collection.alpha = static_cast<uint8_t>(std::clamp(alpha->Value(), 0.0f, 255.0f));
		return true;
	}
	FloatProvider* alpha = nullptr;
};

class SetAtomHasBeenDeflected: public Modifier
{
public:
	void Read(const Properties& p) override { ReadModifier(*this, p); }
	void ModifyAtomCore(Atom& atom) const override { atom.hasBeenDeflected = true; }
};

class SetPSysCloseDown: public Modifier
{
public:
	void Read(const Properties& p) override { ReadModifier(*this, p); }
	void ModifyAtomCore(Atom& atom) const override { atom.collection->manager.SetState(CloseDownState); }
};

class FollowOrigin: public Modifier
{
public:
	FollowOrigin() { flags |= 8; }
	void Read(const Properties& p) override { ReadModifier(*this, p); }
	void ModifyAtomCore(Atom& atom) const override { atom.localPos = atom.collection->manager.Origin(); }
};

class ForceConstantHeight: public Modifier
{
public:
	void Read(const Properties& p) override
	{
		ReadModifier(*this, p);
		height = p.Float("Height", 0.0f);
	}
	void ModifyAtomCore(Atom& atom) const override
	{
		auto pos = atom.GlobalPos();
		pos.y = height + Altitude(pos);
		atom.GlobalToLocal(pos);
		atom.localPos = pos;
	}
	float height = 0.0f;
};

class ForceConstantAltitude: public Modifier
{
public:
	void Read(const Properties& p) override
	{
		ReadModifier(*this, p);
		altitude = p.Float("Altitude", 0.0f);
	}
	void ModifyAtomCore(Atom& atom) const override
	{
		auto pos = atom.GlobalPos();
		pos.y = altitude;
		atom.GlobalToLocal(pos);
		atom.localPos = pos;
	}
	float altitude = 0.0f;
};

class UR_FollowParent: public Modifier
{
public:
	UR_FollowParent() { flags |= 8; }
	void Read(const Properties& p) override
	{
		ReadModifier(*this, p);
		useParentsDrawOffset = p.Bool("UseParentsDrawOffset", false);
	}
	void ModifyAtomCore(Atom& atom) const override
	{
		atom.localPos = atom.collection->ParentPos();
		atom.velocity = atom.collection->ParentVel();
		if (useParentsDrawOffset)
		{
			if (const auto* parent = atom.ParentAtom(); parent != nullptr && parent->followsHand)
			{
				atom.collection->interpolateDrawPos = parent->collection->interpolateDrawPos;
				atom.followsHand = true;
			}
		}
	}
	bool useParentsDrawOffset = false;
};

// UR_FollowLocalHand::ModifyAtomCore (BW1W120 0069a6a0): at the hand, moving as it moves.
class UR_FollowLocalHand: public Modifier
{
public:
	UR_FollowLocalHand() { flags |= 8; }
	void Read(const Properties& p) override
	{
		ReadModifier(*this, p);
		useGraspPos = p.Bool("UseGraspPos", true);
	}
	void ModifyAtomCore(Atom& atom) const override
	{
		auto pos = atom.collection->manager.GesturePos();
		atom.GlobalToLocal(pos);
		atom.velocity = (pos - atom.localPos) * k_OneOverTimeStep;
		atom.localPos = pos;
	}
	bool useGraspPos = true;
};

class UR_FollowCastPosn: public Modifier
{
public:
	UR_FollowCastPosn() { flags |= 8; }
	void Read(const Properties& p) override { ReadModifier(*this, p); }
	void ModifyAtomCore(Atom& atom) const override
	{
		auto& manager = atom.collection->manager;
		auto pos = manager.GesturePos();
		atom.GlobalToLocal(pos);
		atom.localPos = pos;
		if (manager.IsMyInterfaceCasting())
		{
			atom.collection->interpolateDrawPos = false;
			atom.followsHand = true;
		}
	}
};

class UR_HealInHand: public Modifier
{
public:
	UR_HealInHand() { flags |= 8; }
	void Read(const Properties& p) override
	{
		ReadModifier(*this, p);
		wiggleFreq = p.Float("WiggleFreq", 1.0f);
	}
	bool ModifyAtomCollection(Collection& collection) const override
	{
		const auto pos = collection.ParentPos();
		int index = 0;
		for (auto& atom : collection.atoms)
		{
			const float side = index % 2 == 1 ? -1.0f : 1.0f;
			atom->localPos = pos * (std::sin(collection.Age() * wiggleFreq * k_TwoPi) * side);
			++index;
		}
		return true;
	}
	float wiggleFreq = 1.0f;
};

class UR_SphereSurfaceTracer: public Modifier
{
public:
	struct Data: ModifierData
	{
		bool first = true;
		float theta = 0.0f;
		float phi = 0.0f;
	};
	UR_SphereSurfaceTracer() { flags |= 8; }
	void Read(const Properties& p) override
	{
		ReadModifier(*this, p);
		thetaSpeed = p.Float("ThetaSpeed", 1.0f);
		phiSpeed = p.Float("PhiSpeed", 1.0f);
		sphereRadius = p.Float("SphereRadius", 1.0f);
		scaleSphereRadius = p.Pointer<FloatProvider>("ScaleSphereRadius");
		scaleAlpha = p.Pointer<FloatProvider>("ScaleAlpha");
		alpha = p.Int("Alpha", 255);
		scaleX = p.Float("ScaleX", 1.0f);
		scaleY = p.Float("ScaleY", 1.0f);
		scaleZ = p.Float("ScaleZ", 1.0f);
		orientToSurface = p.Bool("OrientToSurface", false);
	}
	void ModifyAtomCore(Atom& atom) const override
	{
		auto& data = atom.Data<Data>(this);
		if (data.first)
		{
			data.first = false;
			data.theta = Random(k_TwoPi);
			data.phi = Random(k_TwoPi);
			Random(k_TwoPi); // Psi
		}
		const float theta = std::fmod(atom.Age() * thetaSpeed + data.theta, k_TwoPi);
		const float phi = std::fmod(atom.Age() * phiSpeed + data.phi, k_TwoPi);
		const float radius = sphereRadius * ValueOf(scaleSphereRadius, 1.0f);
		auto pos = glm::vec3(scaleX * std::cos(theta) * std::cos(phi), scaleY * std::sin(phi), scaleZ * std::sin(theta) * std::cos(phi)) * radius;
		if (atom.collection->inGlobalCoords)
		{
			pos += atom.collection->ParentPos();
		}
		atom.velocity = (pos - atom.localPos) * k_OneOverTimeStep;
		atom.localPos = pos;
		auto a = static_cast<uint8_t>(alpha);
		if (scaleAlpha != nullptr)
		{
			a = static_cast<uint8_t>(std::clamp(scaleAlpha->Value() * static_cast<float>(a), 0.0f, 255.0f));
		}
		atom.SetAlpha(a);
		if (orientToSurface)
		{
			atom.SetAngleZ(k_HalfPi - phi);
			PostRotate(atom.rotation, 1, theta);
		}
	}
	float thetaSpeed = 1.0f;
	float phiSpeed = 1.0f;
	float sphereRadius = 1.0f;
	FloatProvider* scaleSphereRadius = nullptr;
	FloatProvider* scaleAlpha = nullptr;
	int alpha = 255;
	float scaleX = 1.0f;
	float scaleY = 1.0f;
	float scaleZ = 1.0f;
	bool orientToSurface = false;
};

class UR_ScaleByCameraDist: public Modifier
{
public:
	void Read(const Properties& p) override
	{
		ReadModifier(*this, p);
		cameraDistMin = p.Float("CameraDistMin", 10.0f);
		cameraDistMax = p.Float("CameraDistMax", 100.0f);
		scaleAtMin = p.Float("ScaleAtCameraDistMin", 0.1f);
		scaleAtMax = p.Float("ScaleAtCameraDistMax", 1.0f);
	}
	bool ModifyAtomCollection(Collection& collection) const override
	{
		if (cameraDistMin == cameraDistMax)
		{
			return false;
		}
		const auto camera = CameraPos();
		for (auto& atom : collection.atoms)
		{
			const float distance = glm::distance(camera, atom->GlobalPos());
			atom->scale = distance < cameraDistMin   ? scaleAtMin
			              : distance > cameraDistMax ? scaleAtMax
			                                         : Lerp(scaleAtMin, scaleAtMax, (distance - cameraDistMin) / (cameraDistMax - cameraDistMin));
		}
		return true;
	}
	float cameraDistMin = 10.0f;
	float cameraDistMax = 100.0f;
	float scaleAtMin = 0.1f;
	float scaleAtMax = 1.0f;
};

// ------------------------------------------------------------------------------------------------------ create rules

class CreateRuleAnAtom: public CreateRule
{
public:
	void Read(const Properties& p) override
	{
		ReadCreateRule(*this, p);
		offset = glm::vec3(p.Float("OffsetX", 0.0f), p.Float("OffsetY", 0.0f), p.Float("OffsetZ", 0.0f));
		soundOfCreate = p.Sound("SoundOfCreate");
		soundRadius = p.Pointer<FloatProvider>("SoundRadiusFP");
		soundRadiusSmall = p.Float("SoundRadiusSmall", 200.0f);
		soundRadiusMedium = p.Float("SoundRadiusMedium", 500.0f);
		initScale = p.Pointer<FloatProvider>("InitScaleFP");
	}
	bool ModifyAtomCollection(Collection& collection) const override
	{
		if (creator == nullptr)
		{
			return false;
		}
		auto* atom = collection.NewAtom(nextGroups);
		creator->CreateParticle(*atom);
		if (initScale != nullptr)
		{
			atom->baseScale *= initScale->Value();
		}
		atom->localPos += offset;
		auto sound = soundOfCreate;
		if (soundRadius != nullptr)
		{
			const float radius = soundRadius->Value();
			sound.level = radius < soundRadiusSmall ? 3 : radius < soundRadiusMedium ? 2 : 1;
		}
		atom->StartSound(sound);
		return false;
	}
	glm::vec3 offset {0.0f};
	SoundAction soundOfCreate;
	FloatProvider* soundRadius = nullptr;
	float soundRadiusSmall = 200.0f;
	float soundRadiusMedium = 500.0f;
	FloatProvider* initScale = nullptr;
};

class CreateRuleSphere: public CreateRule
{
public:
	void Read(const Properties& p) override
	{
		ReadCreateRule(*this, p);
		numAtoms = p.Int("NumAtoms", 1);
		radius = p.Float("Radius", 0.0f);
		soundOfCreate = p.Sound("SoundOfCreate");
		setInitFrameFromIndex = p.Bool("SetInitFrameFromIndex", false);
		initScale = p.Pointer<FloatProvider>("InitScaleFP");
		radiusScale = p.Pointer<FloatProvider>("RadiusScaleFP");
	}
	bool ModifyAtomCollection(Collection& collection) const override
	{
		if (creator == nullptr)
		{
			return false;
		}
		const float r = radius * ValueOf(radiusScale, 1.0f);
		for (int i = 0; i < numAtoms; ++i)
		{
			auto* atom = collection.NewAtom(nextGroups);
			creator->CreateParticle(*atom);
			if (initScale != nullptr)
			{
				atom->baseScale *= initScale->Value();
			}
			if (setInitFrameFromIndex)
			{
				atom->SetFrame((numAtoms - i - 1) % std::max(atom->frameCount, 1));
			}
			glm::vec3 offset(0.0f);
			if (r != 0.0f)
			{
				offset = RandomR3() * r;
			}
			atom->localPos += offset;
			if (i == 0)
			{
				atom->StartSound(soundOfCreate);
			}
		}
		return false;
	}
	int numAtoms = 1;
	float radius = 0.0f;
	SoundAction soundOfCreate;
	bool setInitFrameFromIndex = false;
	FloatProvider* initScale = nullptr;
	FloatProvider* radiusScale = nullptr;
};

class CreateRuleFusedSphericalExplode: public CreateRule
{
public:
	void Read(const Properties& p) override
	{
		ReadCreateRule(*this, p);
		numAtoms = p.Int("NumAtoms", 100);
		fuseTime = p.Float("FuseTime", 1.0f);
		scaleYSpeed = p.Float("ScaleYSpeed", 1.0f);
		onlyHemisphere = p.Bool("OnlyHemisphere", false);
		minSpeed = p.Pointer<FloatProvider>("MinSpeed");
		maxSpeed = p.Pointer<FloatProvider>("MaxSpeed");
		soundExplode = p.Sound("SoundExplode");
		disableParent = p.Bool("DisableParent", true);
	}
	bool ModifyAtomCollection(Collection& collection) const override
	{
		if (creator == nullptr || minSpeed == nullptr || maxSpeed == nullptr)
		{
			return false;
		}
		if (collection.Age() < fuseTime)
		{
			return true;
		}
		const float speed = minSpeed->Value() + Random(maxSpeed->Value() - minSpeed->Value());
		for (int i = 0; i < numAtoms; ++i)
		{
			auto* atom = collection.NewAtom(nextGroups);
			creator->CreateParticle(*atom);
			glm::vec3 direction;
			do
			{
				direction = RandomR3();
			} while (glm::dot(direction, direction) == 0.0f);
			if (onlyHemisphere)
			{
				direction.y = std::abs(direction.y);
			}
			direction = glm::normalize(direction) * speed;
			direction.y *= scaleYSpeed;
			atom->velocity = direction;
			if (i == 0)
			{
				atom->StartSound(soundExplode);
			}
		}
		if (disableParent && collection.parentAtom != nullptr)
		{
			collection.parentAtom->inUse = false;
		}
		return false;
	}
	int numAtoms = 100;
	float fuseTime = 1.0f;
	float scaleYSpeed = 1.0f;
	bool onlyHemisphere = false;
	FloatProvider* minSpeed = nullptr;
	FloatProvider* maxSpeed = nullptr;
	SoundAction soundExplode;
	bool disableParent = true;
};

class AddSubCollectionsToAtom: public Modifier
{
public:
	struct Data: ModifierData
	{
		bool added = false;
	};
	void Read(const Properties& p) override
	{
		ReadModifier(*this, p);
		nextGroups = p.Ints("NextGroups");
	}
	void ModifyAtomCore(Atom& atom) const override
	{
		auto& data = atom.Data<Data>(this);
		if (!data.added)
		{
			data.added = true;
			atom.AddSubCollections(nextGroups);
		}
	}
	std::vector<int> nextGroups;
};

class CreateNewBaseAtom: public CreateRule
{
public:
	struct Data: ModifierData
	{
		bool created = false;
	};
	CreateNewBaseAtom() { flags &= ~6u; }
	void Read(const Properties& p) override
	{
		ReadCreateRule(*this, p);
		baseGroup = p.Int("BaseGroup", -1);
		soundCreation = p.Sound("SoundCreation");
	}
	void ModifyAtomCore(Atom& atom) const override
	{
		auto& data = atom.Data<Data>(this);
		if (data.created)
		{
			return;
		}
		auto* made = atom.collection->manager.CreateAtomInBaseGroup(baseGroup, nextGroups);
		if (made != nullptr)
		{
			if (creator != nullptr)
			{
				creator->CreateParticle(*made);
			}
			auto pos = atom.GlobalPos();
			made->GlobalToLocal(pos);
			made->localPos = pos;
			made->StartSound(soundCreation);
			data.created = true;
		}
	}
	int baseGroup = -1;
	SoundAction soundCreation;
};

// EmitterRule::ShouldEmit.
class EmitterRule: public CreateRule
{
public:
	struct Data: ModifierData
	{
		float nextEmitTime = 0.0f;
		int numEmitted = 0;
		bool first = true;
	};
	void ReadEmitter(const Properties& p)
	{
		ReadCreateRule(*this, p);
		emissionFreq = p.Float("EmissionFreq", 0.001f);
		maxAtoms = p.Int("MaxAtoms", -1);
		maxTotalAtomsToEmit = p.Int("MaxTotalAtomsToEmit", -1);
		initiallyVisible = p.Bool("InitiallyVisible", true);
		randomise = p.Bool("Randomise", true);
		allowMultipleEmits = p.Bool("AllowMultipleEmits", false);
		soundEmission = p.Sound("SoundEmission");
	}
	bool ShouldEmit(Collection& collection) const
	{
		auto& data = collection.Data<Data>(this);
		if (data.first)
		{
			data.first = false;
			data.nextEmitTime = collection.Age();
		}
		if (maxTotalAtomsToEmit != -1 && data.numEmitted >= maxTotalAtomsToEmit)
		{
			return false;
		}
		if (data.nextEmitTime < collection.Age() + k_TimeStep)
		{
			if (maxAtoms >= 0 && static_cast<int>(collection.atoms.size()) > maxAtoms)
			{
				return false;
			}
			float interval = 1.0f / emissionFreq;
			if (randomise)
			{
				interval *= Random(0.5f) + 0.5f;
			}
			data.nextEmitTime += interval;
			++data.numEmitted;
			return true;
		}
		return false;
	}
	float emissionFreq = 0.001f;
	int maxAtoms = -1;
	int maxTotalAtomsToEmit = -1;
	bool initiallyVisible = true;
	bool randomise = true;
	bool allowMultipleEmits = false;
	SoundAction soundEmission;
};

class EmitterRuleSimple: public EmitterRule
{
public:
	void Read(const Properties& p) override
	{
		ReadEmitter(p);
		speed = p.Float("Speed", 0.0f);
		orientWithParent = p.Bool("OrientWithParent", false);
	}
	bool ModifyAtomCollection(Collection& collection) const override
	{
		if (creator == nullptr)
		{
			return false;
		}
		if (ShouldEmit(collection))
		{
			auto* atom = collection.NewAtom(nextGroups);
			creator->CreateParticle(*atom);
			if (speed != 0.0f)
			{
				auto velocity = RandomR3();
				const float s = Random(speed);
				if (glm::dot(velocity, velocity) > 0.0f)
				{
					velocity = glm::normalize(velocity) * s;
				}
				atom->velocity = velocity;
			}
			if (orientWithParent && collection.parentAtom != nullptr)
			{
				atom->rotation = collection.parentAtom->rotation;
			}
			atom->inUse = initiallyVisible;
		}
		return true;
	}
	float speed = 0.0f;
	bool orientWithParent = false;
};

class EmitterRuleConical: public EmitterRule
{
public:
	void Read(const Properties& p) override
	{
		ReadEmitter(p);
		spread = p.Float("Spread", 0.0f);
		speed = p.Float("Speed", 0.0f);
		radius = p.Float("Radius", 0.0f);
	}
	bool ModifyAtomCollection(Collection& collection) const override
	{
		if (creator == nullptr)
		{
			return false;
		}
		bool emit = true;
		while (emit && ShouldEmit(collection))
		{
			emit = allowMultipleEmits;
			auto* atom = collection.NewAtom(nextGroups);
			creator->CreateParticle(*atom);
			const float s = Random(spread);
			const float angle = Random(k_TwoPi);
			const float v = Random(speed * 0.33f) + speed * 0.66f;
			const float horizontal = v * std::sin(s);
			if (radius != 0.0f)
			{
				const float offsetAngle = Random(k_TwoPi);
				atom->localPos += glm::vec3(std::cos(offsetAngle), 0.0f, std::sin(offsetAngle)) * Random(radius);
			}
			atom->velocity = glm::vec3(horizontal * std::cos(angle), v * std::cos(s), horizontal * std::sin(angle));
			atom->inUse = initiallyVisible;
			atom->StartSound(soundEmission);
		}
		return true;
	}
	float spread = 0.0f;
	float speed = 0.0f;
	float radius = 0.0f;
};

class DiskEmitter: public EmitterRule
{
public:
	void Read(const Properties& p) override
	{
		ReadEmitter(p);
		radius = p.Float("Radius", 0.0f);
		height = p.Float("Height", 0.0f);
	}
	bool ModifyAtomCollection(Collection& collection) const override
	{
		if (creator == nullptr)
		{
			return false;
		}
		if (ShouldEmit(collection))
		{
			auto* atom = collection.NewAtom(nextGroups);
			creator->CreateParticle(*atom);
			glm::vec3 offset;
			do
			{
				offset = glm::vec3(Random(2.0f) - 1.0f, 0.0f, Random(2.0f) - 1.0f);
			} while (offset.x * offset.x + offset.z * offset.z > 1.0f);
			const float r = Random(radius);
			if (glm::dot(offset, offset) > 0.0f)
			{
				offset = glm::normalize(offset) * r;
			}
			offset.y = height;
			atom->localPos += offset;
		}
		return true;
	}
	float radius = 0.0f;
	float height = 0.0f;
};

class SpreadingDiskEmitter: public EmitterRule
{
public:
	void Read(const Properties& p) override
	{
		ReadEmitter(p);
		startRadius = p.Float("StartRadius", 0.0f);
		stopRadius = p.Float("StopRadius", 1.0f);
		height = p.Float("Height", 0.0f);
	}
	bool ModifyAtomCollection(Collection& collection) const override
	{
		if (creator == nullptr)
		{
			return false;
		}
		bool emit = true;
		while (emit && ShouldEmit(collection))
		{
			emit = allowMultipleEmits;
			auto* atom = collection.NewAtom(nextGroups);
			creator->CreateParticle(*atom);
			const float r = Random(startRadius, stopRadius);
			const float angle = Random(k_TwoPi);
			auto offset = glm::vec3(std::cos(angle), 0.0f, std::sin(angle)) * r;
			offset.y = height;
			atom->localPos += offset;
		}
		return true;
	}
	float startRadius = 0.0f;
	float stopRadius = 1.0f;
	float height = 0.0f;
};

// UR_WillowWisp::ModifyAtomCollection: atoms trailed from its parent as it moves, MaxAtoms over DieAge, each given the
// parent's velocity (by Speed, at most MaxSpeed) and a random one, and gone at DieAge.
class UR_WillowWisp: public CreateRule
{
public:
	struct Data: ModifierData
	{
		bool first = true;
		glm::vec3 lastParentPos {0.0f};
		float numToEmit = 0.0f;
		float numEmitted = 0.0f;
	};
	void Read(const Properties& p) override
	{
		ReadCreateRule(*this, p);
		addCastVelToInitPos = p.Bool("AddCastVelToInitPos", false);
		speed = p.Float("Speed", 1.0f);
		maxSpeed = p.Float("MaxSpeed", 100.0f);
		randomSpeed = p.Float("RandomSpeed", 1.0f);
		randomiseInitOrientation = p.Bool("RandomiseInitOrientation", false);
		maxAtoms = p.Int("MaxAtoms", -1);
		dieAge = p.Float("DieAge", 1.0f);
		drawOnFirstUpdate = p.Bool("DrawOnFirstUpdate", true);
		soundEmission = p.Sound("SoundEmission");
		emitConditionOfParent = p.Pointer<Condition>("EmitConditionOfParent");
		adjustInitialScale = p.Pointer<FloatProvider>("AdjustInitialScale");
		adjustInitialRandomVel = p.Pointer<FloatProvider>("AdjustInitialRandomVel");
		emitDueToMovingMaxRate = p.Float("EmitDueToMovingMaxRate", 0.0f);
		emitDueToMovingDist = p.Float("EmitDueToMovingDist", 10.0f);
		emitDueToMoving = p.Bool("EmitDueToMoving", false);
		randomRadiusMin = p.Float("RandomRadiusMin", 0.0f);
		randomRadiusMax = p.Float("RandomRadiusMax", 0.0f);
		useParentScale = p.Bool("UseParentScale", false);
		deleteAtomsAtDieAge = p.Bool("DeleteAtomsAtDieAge", true);
	}
	bool ModifyAtomCollection(Collection& collection) const override
	{
		if (creator == nullptr)
		{
			return false;
		}
		auto& data = collection.Data<Data>(this);
		if (deleteAtomsAtDieAge)
		{
			for (auto it = collection.atoms.begin(); it != collection.atoms.end();)
			{
				auto next = std::next(it);
				if ((*it)->Age() > dieAge)
				{
					collection.DeleteAtom(it->get());
				}
				it = next;
			}
		}
		const auto parentPos = collection.ParentPos();
		auto velocity = collection.ParentVel() * speed;
		if (glm::dot(velocity, velocity) > maxSpeed * maxSpeed)
		{
			velocity = glm::normalize(velocity) * maxSpeed;
		}
		if (data.first)
		{
			data.lastParentPos = parentPos;
		}
		const float rate = static_cast<float>(maxAtoms) / dieAge;
		auto* parent = collection.parentAtom;
		if (emitConditionOfParent == nullptr || (parent != nullptr && emitConditionOfParent->TestOnAtom(*parent)))
		{
			float numToEmit = k_TimeStep * rate;
			const float oldNumToEmit = data.numToEmit;
			if (emitDueToMoving && emitDueToMovingDist != 0.0f)
			{
				float distance = glm::distance(parentPos, data.lastParentPos);
				if (adjustInitialScale != nullptr)
				{
					distance *= adjustInitialScale->Value();
				}
				float dueToMoving = distance / emitDueToMovingDist;
				if (emitDueToMovingMaxRate != 0.0f)
				{
					dueToMoving = std::min(dueToMoving / k_TimeStep, emitDueToMovingMaxRate) * k_TimeStep;
				}
				numToEmit = std::max(numToEmit, dueToMoving);
			}
			data.numToEmit += numToEmit;
			const float newNumToEmit = data.numToEmit;
			while (data.numEmitted < data.numToEmit - 1.0f)
			{
				data.numEmitted += 1.0f;
				auto* atom = collection.NewAtom(nextGroups);
				creator->CreateParticle(*atom);
				glm::vec3 pos = parentPos;
				if (newNumToEmit != oldNumToEmit)
				{
					const float t = (data.numEmitted - oldNumToEmit) / (newNumToEmit - oldNumToEmit);
					pos = data.lastParentPos + (parentPos - data.lastParentPos) * t;
					atom->SetAge(t * k_TimeStep);
				}
				if (randomRadiusMax > 0.0f)
				{
					const float angle = Random(k_TwoPi);
					pos += glm::vec3(std::cos(angle), 0.0f, std::sin(angle)) * Random(randomRadiusMin, randomRadiusMax);
				}
				auto randomVelocity = RandomR3() * Random(randomSpeed);
				if (adjustInitialRandomVel != nullptr)
				{
					randomVelocity *= adjustInitialRandomVel->Value();
				}
				atom->velocity = velocity + randomVelocity;
				if (addCastVelToInitPos)
				{
					pos.x = parentPos.x;
					pos.z = parentPos.z;
					pos += atom->velocity * k_TimeStep;
				}
				atom->localPos = pos;
				if (randomiseInitOrientation)
				{
					atom->RandomiseOrientation();
				}
				if (adjustInitialScale != nullptr)
				{
					atom->baseScale *= adjustInitialScale->Value();
				}
				if (useParentScale && parent != nullptr)
				{
					atom->baseScale *= parent->CombinedScale();
				}
				atom->flag0 = drawOnFirstUpdate;
				atom->StartSound(soundEmission);
			}
		}
		data.first = false;
		data.lastParentPos = parentPos;
		return true;
	}
	bool addCastVelToInitPos = false;
	float speed = 1.0f;
	float maxSpeed = 100.0f;
	float randomSpeed = 1.0f;
	bool randomiseInitOrientation = false;
	int maxAtoms = -1;
	float dieAge = 1.0f;
	bool drawOnFirstUpdate = true;
	SoundAction soundEmission;
	Condition* emitConditionOfParent = nullptr;
	FloatProvider* adjustInitialScale = nullptr;
	FloatProvider* adjustInitialRandomVel = nullptr;
	float emitDueToMovingMaxRate = 0.0f;
	float emitDueToMovingDist = 10.0f;
	bool emitDueToMoving = false;
	float randomRadiusMin = 0.0f;
	float randomRadiusMax = 0.0f;
	bool useParentScale = false;
	bool deleteAtomsAtDieAge = true;
};

// UR_HandSprinkle::ModifyAtomCollection: an atom at the hand (no higher than 58 over the land), the miracle pouring
// from it. (Its HandStateGrain::StartTimedPour - the hand turning over as it pours - is the spell's.)
class UR_HandSprinkle: public CreateRule
{
public:
	struct Data: ModifierData
	{
		bool first = true;
		float age = 0.0f;
	};
	UR_HandSprinkle() { flags ^= 4u; }
	void Read(const Properties& p) override
	{
		ReadCreateRule(*this, p);
		initSpeedYHumanPlayerCasting = p.Float("InitSpeedYHumanPlayerCasting", 0.0f);
		heightToRaise = p.Float("HeightToRaise", 10.0f);
		keyPoints = p.Floats("KeyPoints");
		if (keyPoints.size() < 4)
		{
			keyPoints = {0.0f, 0.0f, 0.2f, 1.0f, 0.8f, 1.0f, 1.0f, 0.0f};
		}
		angleToRaise = p.Float("AngleToRaise", k_HalfPi);
		totalTime = p.Float("TotalTime", 3.0f);
		fracToCloseDownOn = p.Float("FracToCloseDownOn", 0.7f);
		clampHand = p.Bool("ClampHand", false);
	}
	bool ModifyAtomCollection(Collection& collection) const override
	{
		constexpr float k_MaxHeight = 58.0f; // HandSprinkleMaxHeight
		auto& data = collection.Data<Data>(this);
		data.age += k_TimeStep;
		auto& manager = collection.manager;
		if (manager.IsMyInterfaceCasting() && heightToRaise != 0.0f)
		{
			// The pour: the hand raised along KeyPoints (time over TotalTime, then the fraction of HeightToRaise).
			const float t = totalTime > 0.0f ? data.age / totalTime : 1.0f;
			float raise = keyPoints.back();
			for (size_t i = 0; i + 3 < keyPoints.size(); i += 2)
			{
				if (t >= keyPoints[i] && t <= keyPoints[i + 2])
				{
					const float span = keyPoints[i + 2] - keyPoints[i];
					raise = span > 0.0f ? Lerp(keyPoints[i + 1], keyPoints[i + 3], (t - keyPoints[i]) / span) : keyPoints[i + 3];
					break;
				}
			}
			manager.handRaise = t >= 1.0f ? 0.0f : raise * heightToRaise;
			manager.pourCloseTime = totalTime * fracToCloseDownOn;
		}
		auto pos = manager.GesturePos();
		if (pos.y - Altitude(pos) > k_MaxHeight)
		{
			pos.y = Altitude(pos) + k_MaxHeight;
		}
		collection.GlobalToLocal(pos);
		if (data.first)
		{
			data.first = false;
			if (creator == nullptr)
			{
				return false;
			}
			auto* atom = collection.NewAtom(nextGroups);
			creator->CreateParticle(*atom);
			atom->localPos = pos;
		}
		const bool mine = manager.IsMyInterfaceCasting();
		if (mine)
		{
			collection.interpolateDrawPos = false;
		}
		for (auto& atom : collection.atoms)
		{
			glm::vec3 velocity = (pos - atom->localPos) * k_OneOverTimeStep;
			if (manager.IsHumanPlayerCasting())
			{
				velocity.y += initSpeedYHumanPlayerCasting;
			}
			velocity.x = 0.0f;
			velocity.z = 0.0f;
			atom->velocity = velocity;
			atom->localPos = pos;
			atom->followsHand = mine;
		}
		return true;
	}
	float initSpeedYHumanPlayerCasting = 0.0f;
	float heightToRaise = 10.0f;
	float angleToRaise = k_HalfPi;
	float totalTime = 3.0f;
	float fracToCloseDownOn = 0.7f;
	bool clampHand = false;
	std::vector<float> keyPoints;
};

// ER_MultiPickup::ModifyAtomCollection: while enabled, EmitRate a second rising from the land under the hand to it,
// each RaiseTime on the way (food or wood picked up by the handful).
class ER_MultiPickup: public CreateRule
{
public:
	struct CollectionState: ModifierData
	{
		float numToEmit = 0.0f;
		int numEmitted = 0;
	};
	struct AtomState: ModifierData
	{
		glm::vec3 startPos {0.0f};
	};
	void Read(const Properties& p) override
	{
		ReadCreateRule(*this, p);
		raiseTime = p.Float("RaiseTime", 1.0f);
		emitRate = p.Float("EmitRate", 8.0f);
		randomiseOrientations = p.Bool("RandomiseOrientations", true);
		defaultOrientation = p.Float("DefaultOrientation", 0.0f);
	}
	bool ModifyAtomCollection(Collection& collection) const override
	{
		if (creator == nullptr)
		{
			return false;
		}
		auto& manager = collection.manager;
		auto& data = collection.Data<CollectionState>(this);
		const auto gesture = manager.GesturePos();
		if (manager.IsInState(EnabledState))
		{
			data.numToEmit += k_TimeStep * emitRate;
			while (static_cast<float>(data.numEmitted) < data.numToEmit)
			{
				++data.numEmitted;
				auto* atom = collection.NewAtom(nextGroups);
				creator->CreateParticle(*atom);
				if (randomiseOrientations)
				{
					atom->RandomiseOrientation();
				}
				else
				{
					atom->SetAngleY(defaultOrientation);
				}
				auto& state = atom->Data<AtomState>(this);
				state.startPos = gesture;
				state.startPos.y = Altitude(state.startPos);
			}
		}
		for (auto it = collection.atoms.begin(); it != collection.atoms.end();)
		{
			auto next = std::next(it);
			auto& atom = **it;
			const float age = atom.Age();
			if (age > raiseTime)
			{
				collection.DeleteAtom(&atom);
			}
			else
			{
				auto& state = atom.Data<AtomState>(this);
				const auto pos = state.startPos + (gesture - state.startPos) * (age / raiseTime);
				atom.velocity = (pos - atom.localPos) * k_OneOverTimeStep;
				atom.localPos = pos;
			}
			it = next;
		}
		return true;
	}
	float raiseTime = 1.0f;
	float emitRate = 8.0f;
	bool randomiseOrientations = true;
	float defaultOrientation = 0.0f;
};

// UR_HealSpellChakra::ModifyAtomCollection: an atom on each of the system's targets (an event for each, so the spell
// heals it), its sub collection's alpha (and the target's glow) rising to MaxAlpha by AtomAgeMaxAlpha and back to
// nothing by AtomAgeZeroAlpha.
class UR_HealSpellChakra: public CreateRule
{
public:
	struct Data: ModifierData
	{
		uint32_t target = 0;
		bool finished = false;
		bool first = true;
		bool soundStarted = false;
		~Data() override
		{
			if (target != 0)
			{
				SetObjectSpecular(target, 0);
			}
		}
	};
	UR_HealSpellChakra() { flags ^= 4u; }
	void Read(const Properties& p) override
	{
		ReadCreateRule(*this, p);
		scalePropObjectSize = p.Bool("ScalePropObjectSize", false);
		takeCentrePos = p.Bool("TakeCentrePos", false);
		soundHeal = p.Sound("SoundHeal");
		soundSpacing = p.Float("SoundSpacing", 0.2f);
		maxAlpha = p.Float("MaxAlpha", 255.0f);
		specularR = p.Int("SpecularColorR", 0);
		specularG = p.Int("SpecularColorG", 0);
		specularB = p.Int("SpecularColorB", 0);
		atomAgeMaxAlpha = p.Float("AtomAgeMaxAlpha", 1.0f);
		atomAgeZeroAlpha = p.Float("AtomAgeZeroAlpha", 3.0f);
	}
	[[nodiscard]] glm::vec3 TargetPos(uint32_t target) const
	{
		glm::vec3 pos(0.0f);
		ObjectPos(target, pos);
		if (takeCentrePos)
		{
			pos.y += ObjectHeight(target) * 0.5f;
		}
		return pos;
	}
	bool ModifyAtomCollection(Collection& collection) const override
	{
		auto& manager = collection.manager;
		while (manager.TargetCount() > 0)
		{
			const auto target = manager.TakeTarget();
			glm::vec3 pos;
			if (target == 0 || !ObjectPos(target, pos) || Chakras().contains(target) || creator == nullptr)
			{
				continue;
			}
			pos = TargetPos(target);
			Event event;
			event.type = EventType::Heal;
			event.position = pos;
			event.target = target;
			manager.SpellEvent(event);
			auto* atom = collection.NewAtom(nextGroups);
			creator->CreateParticle(*atom);
			atom->Data<Data>(this).target = target;
			Chakras().insert(target);
			atom->localPos = pos;
		}
		int index = 0;
		for (auto it = collection.atoms.begin(); it != collection.atoms.end(); ++index)
		{
			auto next = std::next(it);
			auto& atom = **it;
			auto* data = dynamic_cast<Data*>(atom.FindData(this));
			if (data != nullptr)
			{
				if (index == 0 && !data->soundStarted && atom.Age() > soundSpacing * static_cast<float>(index))
				{
					data->soundStarted = true;
					atom.StartSound(soundHeal);
				}
				glm::vec3 pos;
				if (data->target != 0 && ObjectPos(data->target, pos) && !ObjectInHand(data->target))
				{
					atom.localPos = TargetPos(data->target);
					if (scalePropObjectSize)
					{
						atom.scale = ObjectRadius(data->target);
					}
				}
				else
				{
					data->finished = true;
				}
				if (!data->finished)
				{
					if (atom.subCollections.empty())
					{
						return false;
					}
					ModifySubCollection(*atom.subCollections.front(), *data);
					data->first = false;
				}
				if (data->finished)
				{
					Chakras().erase(data->target);
					collection.DeleteAtom(&atom);
				}
			}
			it = next;
		}
		return true;
	}
	void ModifySubCollection(Collection& sub, Data& data) const
	{
		if (sub.atoms.empty())
		{
			if (!data.first)
			{
				data.finished = true;
			}
			return;
		}
		const float age = sub.Age();
		float fraction = age < atomAgeMaxAlpha ? age / atomAgeMaxAlpha
		                                       : 1.0f - (age - atomAgeMaxAlpha) / (atomAgeZeroAlpha - atomAgeMaxAlpha);
		fraction = std::clamp(fraction, 0.0f, 1.0f);
		const auto alpha = static_cast<uint8_t>(fraction * maxAlpha);
		if (data.target != 0)
		{
			SetObjectSpecular(data.target, (static_cast<uint32_t>(static_cast<float>(specularR) * fraction) << 16) |
			                                   (static_cast<uint32_t>(static_cast<float>(specularG) * fraction) << 8) |
			                                   static_cast<uint32_t>(static_cast<float>(specularB) * fraction));
		}
		for (auto& atom : sub.atoms)
		{
			atom->SetAlpha(alpha);
		}
	}
	// ObjectsWithChakra: each object healed once at a time.
	static std::set<uint32_t>& Chakras()
	{
		static std::set<uint32_t> s_objects;
		return s_objects;
	}
	bool scalePropObjectSize = false;
	bool takeCentrePos = false;
	SoundAction soundHeal;
	float soundSpacing = 0.2f;
	float maxAlpha = 255.0f;
	int specularR = 0;
	int specularG = 0;
	int specularB = 0;
	float atomAgeMaxAlpha = 1.0f;
	float atomAgeZeroAlpha = 3.0f;
};

template <class T>
std::unique_ptr<Object> Make()
{
	return std::make_unique<T>();
}
} // namespace

std::unique_ptr<Object> MakeObject(const std::string& type)
{
	static const std::map<std::string, std::function<std::unique_ptr<Object>()>, std::less<>> k_Classes = {
	    // creators
	    {"ParticlePointCreator", Make<PointCreator>},
	    {"ParticleSpriteCreator", Make<SpriteCreator>},
	    {"ParticleMistCreator", Make<MistCreator>},
	    {"ParticleMeshCreator", Make<MeshCreator>},
	    // (Not drawn yet: its mesh wants its texture sliding over it, see-through - water's rain cone.)
	    {"ParticleMeshCreatorAnimTextured", Make<PointCreator>},
	    // float providers
	    {"ConstFloatProvider", Make<ConstFloatProvider>},
	    {"MagnitudeFloatProvider", Make<MagnitudeFloatProvider>},
	    {"StrengthFloatProvider", Make<StrengthFloatProvider>},
	    {"MagnitudeTimesStrengthFloatProvider", Make<MagnitudeTimesStrengthFloatProvider>},
	    {"RenderHandScaleFloatProvider", Make<RenderHandScaleFloatProvider>},
	    {"RenderHandScaleTimesStrengthFloatProvider", Make<RenderHandScaleTimesStrengthFloatProvider>},
	    // conditions
	    {"EventConditionTrueOnCloseDown", Make<EventConditionTrueOnCloseDown>},
	    {"EventConditionTrueWhenEnabled", Make<EventConditionTrueWhenEnabled>},
	    {"EventConditionNever", Make<EventConditionNever>},
	    {"EC_CollectionShouldBeEmitting", Make<EC_CollectionShouldBeEmitting>},
	    {"EventConditionCollectionDelay", Make<EventConditionCollectionDelay>},
	    {"EventConditionCollectionLimitedTime", Make<EventConditionCollectionLimitedTime>},
	    {"EventConditionRandom", Make<EventConditionRandom>},
	    {"EventConditionAtomDelay", Make<EventConditionAtomDelay>},
	    {"EventConditionAtomLimitedTime", Make<EventConditionAtomLimitedTime>},
	    {"EventConditionAtomInUse", Make<EventConditionAtomInUse>},
	    {"EventConditionAtomHasBeenDeflected", Make<EventConditionAtomHasBeenDeflected>},
	    {"EventConditionAtomBelowSpeed", Make<EventConditionAtomBelowSpeed>},
	    {"EventConditionAtomBelowHeight", Make<EventConditionAtomBelowHeight>},
	    {"EC_AtomAlphaAbove", Make<EC_AtomAlphaAbove>},
	    {"EC_DeflectionInAtomsHierarchy", Make<EC_DeflectionInAtomsHierarchy>},
	    // rules
	    {"EventAlways", Make<EventAlways>},
	    {"StartStopSoundOnCondition", Make<StartStopSoundOnCondition>},
	    {"AddSoundToAtom", Make<AddSoundToAtom>},
	    {"RemoveSoundFromAtom", Make<RemoveSoundFromAtom>},
	    {"LandscapeCollide", Make<LandscapeCollide>},
	    {"RemoveRuleOldAgeOnly", Make<RemoveRuleOldAgeOnly>},
	    {"RemoveRuleAfterCloseDown", Make<RemoveRuleAfterCloseDown>},
	    {"RemoveRuleAfterConditionTrue", Make<RemoveRuleAfterConditionTrue>},
	    {"RemoveRuleProb", Make<RemoveRuleProb>},
	    {"UpdateRuleGravity", Make<UpdateRuleGravity>},
	    {"UpdateRuleGravityWithFloor", Make<UpdateRuleGravityWithFloor>},
	    {"UR_UpdatePosnFromVelocity", Make<UR_UpdatePosnFromVelocity>},
	    {"UR_ChangeScale", Make<UR_ChangeScale>},
	    {"UR_ChangeScaleXYZ", Make<UR_ChangeScaleXYZ>},
	    {"AR_FadeAlpha", Make<AR_FadeAlpha>},
	    {"AR_FadeCollectionAlpha", Make<AR_FadeCollectionAlpha>},
	    {"AR_FadeOutOnceConditionTrue", Make<AR_FadeOutOnceConditionTrue>},
	    {"AppearanceRuleFadeOut", Make<AppearanceRuleFadeOut>},
	    {"AppearanceRuleTumble", Make<AppearanceRuleTumble>},
	    {"UpdateRuleRotatePrincipalAxis", Make<UpdateRuleRotatePrincipalAxis>},
	    {"UR_OrientSpriteWithRandomAngle", Make<UR_OrientSpriteWithRandomAngle>},
	    {"SetScale", Make<SetScale>},
	    {"SetAtomAlpha", Make<SetAtomAlpha>},
	    {"SetCollectionAlpha", Make<SetCollectionAlpha>},
	    {"SetAtomHasBeenDeflected", Make<SetAtomHasBeenDeflected>},
	    {"SetPSysCloseDown", Make<SetPSysCloseDown>},
	    {"FollowOrigin", Make<FollowOrigin>},
	    {"ForceConstantHeight", Make<ForceConstantHeight>},
	    {"ForceConstantAltitude", Make<ForceConstantAltitude>},
	    {"UR_FollowParent", Make<UR_FollowParent>},
	    {"UR_FollowLocalHand", Make<UR_FollowLocalHand>},
	    {"UR_FollowCastPosn", Make<UR_FollowCastPosn>},
	    {"UR_HealInHand", Make<UR_HealInHand>},
	    {"UR_SphereSurfaceTracer", Make<UR_SphereSurfaceTracer>},
	    {"UR_ScaleByCameraDist", Make<UR_ScaleByCameraDist>},
	    {"CreateRuleAnAtom", Make<CreateRuleAnAtom>},
	    {"CreateRuleSphere", Make<CreateRuleSphere>},
	    {"CreateRuleFusedSphericalExplode", Make<CreateRuleFusedSphericalExplode>},
	    {"AddSubCollectionsToAtom", Make<AddSubCollectionsToAtom>},
	    {"CreateNewBaseAtom", Make<CreateNewBaseAtom>},
	    {"EmitterRuleSimple", Make<EmitterRuleSimple>},
	    {"EmitterRuleConical", Make<EmitterRuleConical>},
	    {"DiskEmitter", Make<DiskEmitter>},
	    {"SpreadingDiskEmitter", Make<SpreadingDiskEmitter>},
	    {"UR_WillowWisp", Make<UR_WillowWisp>},
	    {"UR_HandSprinkle", Make<UR_HandSprinkle>},
	    {"ER_MultiPickup", Make<ER_MultiPickup>},
	    {"UR_HealSpellChakra", Make<UR_HealSpellChakra>},
	};
	const auto it = k_Classes.find(type);
	return it == k_Classes.end() ? nullptr : it->second();
}
} // namespace openblack::psys::core
