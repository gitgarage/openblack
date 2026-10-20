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
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

// The original's particle systems (its PSys): each effect is a file in Data/Spells/ZSpellFiles (SF_<name>_txt.zzz,
// deflated text) naming its objects - particle creators, the rules that create, change and remove atoms (particles)
// in numbered groups, float providers and event conditions - with their properties. An effect runs here as the
// original runs it (bw1-decomp's PSysModifiers.cpp for the rules), and says what to draw.
namespace openblack::psys
{
// What an effect draws this frame.
struct Drawable
{
	enum class Kind
	{
		Mist,   // LH3DMist: a dome turned to the camera, showing a frame of the smoke sheet (rows 2-3)
		Sprite, // LH3DSprite: a quad turned to the camera, showing a frame of its sprite sheet
	};
	Kind kind;
	glm::vec3 position;
	float radius;     // a mist's dome: 20 units at scale 1 (Data/Landscape/mist.l3d); a sprite's half-width
	float ratio;      // a mist: seen from the side its height is radius / ratio. A sprite: its height over its width
	glm::vec4 colour; // 0-1, alpha included
	int frame;        // a mist's, of the 16 on the smoke sheet's rows 2 and 3
	// A sprite's: its sheet (Data/Textures/<texture>.raw, alpha <texture>a.raw), the frame's place on it (0-1), how
	// far it is moved across and up the screen from its position, and whether it adds light (else blends over).
	std::string texture;
	glm::vec2 uvMin {0.0f};
	glm::vec2 uvExtent {1.0f};
	glm::vec2 screenOffset {0.0f};
	bool additive = false;
};

// One object of an effect file: its class, name and properties (each kept as its words).
struct FileObject
{
	std::string type;
	std::string name;
	std::map<std::string, std::vector<std::string>, std::less<>> properties;

	[[nodiscard]] float Float(std::string_view property, float fallback = 0.0f) const;
	[[nodiscard]] int Int(std::string_view property, int fallback = 0) const;
	[[nodiscard]] bool Bool(std::string_view property, bool fallback = false) const;
	[[nodiscard]] std::string Ref(std::string_view property) const; // a PERSIS_PNTR's target, "" for none
	[[nodiscard]] std::vector<int> Ints(std::string_view property) const; // an ARRAY's values
};

// An effect file, read once.
struct EffectFile
{
	std::string name;
	std::vector<int> initiallyCreated; // per group: has a collection from the start
	std::vector<int> hierarchies;      // per group: its atoms carry the atoms of their next groups (position, scale)
	std::vector<std::unique_ptr<FileObject>> objects;
	std::map<std::string, const FileObject*, std::less<>> byName;

	// SF_<name>_txt.zzz from Data/Spells/ZSpellFiles; null if missing or unreadable.
	static std::shared_ptr<const EffectFile> Load(const std::string& name);
};

class Effect
{
public:
	explicit Effect(std::shared_ptr<const EffectFile> file);
	~Effect();

	void SetOrigin(const glm::vec3& origin) { _origin = origin; }
	void SetMagnitude(float magnitude) { _magnitude = magnitude; }
	// The original's PSYS_MANAGER_STATE_CLOSE_DOWN: its atoms fade and go by their own rules.
	void CloseDown();
	[[nodiscard]] bool Finished() const;
	void Update(float seconds);
	void Collect(std::vector<Drawable>& out) const;

private:
	struct Atom;
	struct Collection;
	void CreateCollection(int group, Atom* parent);
	Atom* CreateAtom(Collection& collection, const FileObject& rule);
	void RemoveAtom(Collection& collection, size_t index);
	[[nodiscard]] float ProviderValue(const std::string& name) const;
	[[nodiscard]] bool ConditionTrue(const std::string& name) const;
	[[nodiscard]] glm::vec3 GlobalPosition(const Atom& atom) const;
	[[nodiscard]] float GlobalScale(const Atom& atom) const;
	[[nodiscard]] const Atom* Carrier(const Atom& atom) const; // the parent it moves and scales with, if any

	std::shared_ptr<const EffectFile> _file;
	std::vector<std::unique_ptr<Collection>> _collections;
	glm::vec3 _origin {0.0f};
	float _magnitude = 1.0f;
	float _age = 0.0f;
	bool _closing = false;
	float _closeDownAge = 0.0f;
};
} // namespace openblack::psys
