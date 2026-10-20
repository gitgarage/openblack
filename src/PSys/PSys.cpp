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
#include <limits>
#include <sstream>

#include <glm/geometric.hpp>
#include <spdlog/spdlog.h>

#include "Common/Zip.h"
#include "FileSystem/FileSystemInterface.h"
#include "Locator.h"
#include "Common/RandomNumberManager.h"

namespace openblack::psys
{
namespace
{
const std::vector<std::string>* Words(const FileObject& object, std::string_view property)
{
	const auto it = object.properties.find(property);
	return it == object.properties.end() || it->second.size() < 2 ? nullptr : &it->second;
}

float Random(float range)
{
	return Locator::rng::value().NextValue(0.0f, range);
}

// The classes run so far; any other is said once per file and left out.
const std::set<std::string, std::less<>> k_Known = {
    "ParticlePointCreator", "ParticleMistCreator",           "CreateRuleAnAtom",      "ParticleSpriteCreator",
    "FollowOrigin",         "SetScale",                      "MagnitudeFloatProvider",
    "ConstFloatProvider",   "RemoveRuleAfterCloseDown",      "AR_FadeOutOnceConditionTrue",
    "EventConditionTrueOnCloseDown"};

// LH3DMist (BW1W120 007f9560, its drawing at 007fa300): its frame counter starts at a random 0-15, runs at 0.255 a
// millisecond and wraps at 900; the frame shown is the counter over 20, of 16.
constexpr float k_MistCounterPerSecond = 255.0f;
constexpr float k_MistCounterWrap = 900.0f;
constexpr float k_MistRadius = 20.0f; // Data/Landscape/mist.l3d's dome
} // namespace

float FileObject::Float(std::string_view property, float fallback) const
{
	const auto* words = Words(*this, property);
	return words != nullptr ? std::stof((*words)[1]) : fallback;
}

int FileObject::Int(std::string_view property, int fallback) const
{
	const auto* words = Words(*this, property);
	return words != nullptr ? std::stoi((*words)[1]) : fallback;
}

bool FileObject::Bool(std::string_view property, bool fallback) const
{
	return Int(property, fallback ? 1 : 0) != 0;
}

std::string FileObject::Ref(std::string_view property) const
{
	const auto* words = Words(*this, property);
	return words == nullptr || (*words)[1] == "NULL_STRING" ? std::string() : (*words)[1];
}

std::vector<int> FileObject::Ints(std::string_view property) const
{
	// "ARRAY SIZE n v1 .. vn"
	std::vector<int> values;
	const auto it = properties.find(property);
	if (it != properties.end() && it->second.size() >= 3 && it->second[0] == "ARRAY")
	{
		for (size_t i = 3; i < it->second.size(); ++i)
		{
			values.push_back(std::stoi(it->second[i]));
		}
	}
	return values;
}

std::shared_ptr<const EffectFile> EffectFile::Load(const std::string& name)
{
	static std::map<std::string, std::shared_ptr<const EffectFile>> s_loaded;
	if (const auto it = s_loaded.find(name); it != s_loaded.end())
	{
		return it->second;
	}
	auto& slot = s_loaded[name];
	try
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
		auto file = std::make_shared<EffectFile>();
		file->name = name;
		std::istringstream lines(std::string(text.begin(), text.end()));
		std::string line;
		FileObject* current = nullptr;
		FileObject header; // the file's own properties, before its classes
		std::set<std::string> unknown;
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
				auto object = std::make_unique<FileObject>();
				words >> object->type >> object->name;
				if (!k_Known.contains(object->type))
				{
					unknown.insert(object->type);
				}
				current = object.get();
				file->byName[object->name] = current;
				file->objects.push_back(std::move(object));
			}
			else if (first == "ENDCLASS")
			{
				current = nullptr;
			}
			else if (first == "PROPERTY")
			{
				std::string property;
				words >> property;
				std::vector<std::string> values;
				for (std::string word; words >> word;)
				{
					values.push_back(word);
				}
				(current != nullptr ? current : &header)->properties[property] = std::move(values);
			}
		}
		file->initiallyCreated = header.Ints("InitiallyCreated");
		file->hierarchies = header.Ints("Hierarchies");
		for (const auto& type : unknown)
		{
			SPDLOG_LOGGER_INFO(spdlog::get("game"), "Effect {}: {} is not run yet", name, type);
		}
		SPDLOG_LOGGER_INFO(spdlog::get("game"), "Effect {}: {} objects", name, file->objects.size());
		slot = std::move(file);
	}
	catch (const std::exception& e)
	{
		SPDLOG_LOGGER_WARN(spdlog::get("game"), "Effect {}: {}", name, e.what());
	}
	return slot;
}

// An atom (the original's AtomCore): where it is relative to the atom whose collection it is in (the world for a
// root one), its scales, alpha and age, the creator that made it, and its fade's state.
struct Effect::Atom
{
	int group = 0;
	Atom* parent = nullptr;
	glm::vec3 localPos {0.0f};
	float baseScale = 1.0f;
	float scale = 1.0f;
	float alpha = 255.0f;
	float age = 0.0f;
	const FileObject* creator = nullptr;
	struct Fade
	{
		bool met = false;
		float startTime = 0.0f;
		float startScale = 1.0f;
		float startAlpha = 255.0f;
	};
	std::map<const FileObject*, Fade> fades;
	float mistCounter = 0.0f;
	float mistRatio = 3.0f;
	float spriteFrame = 0.0f;     // a sprite's frame, fractional
	float spriteDirection = 1.0f; // its frame rate's sign, flipped by RandomiseFrameDirection
};

// A collection (the original's AtomCollection): the atoms of one group, under the atom that made it.
struct Effect::Collection
{
	int group = 0;
	Atom* parent = nullptr;
	std::set<const FileObject*> onceDone;
	std::vector<std::unique_ptr<Atom>> atoms;
};

Effect::Effect(std::shared_ptr<const EffectFile> file)
    : _file(std::move(file))
{
	if (_file)
	{
		for (size_t group = 0; group < _file->initiallyCreated.size(); ++group)
		{
			if (_file->initiallyCreated[group] != 0)
			{
				CreateCollection(static_cast<int>(group), nullptr);
			}
		}
	}
}

Effect::~Effect() = default;

void Effect::CloseDown()
{
	if (!_closing)
	{
		_closing = true;
		_closeDownAge = _age;
	}
}

bool Effect::Finished() const
{
	if (!_file)
	{
		return true;
	}
	return _closing && std::all_of(_collections.begin(), _collections.end(), [](const auto& c) { return c->atoms.empty(); });
}

void Effect::CreateCollection(int group, Atom* parent)
{
	auto collection = std::make_unique<Collection>();
	collection->group = group;
	collection->parent = parent;
	_collections.push_back(std::move(collection));
}

Effect::Atom* Effect::CreateAtom(Collection& collection, const FileObject& rule)
{
	// CreateRuleAnAtom::ModifyAtomCollection: a new atom from its creator, its offset added, then a collection for
	// each of its next groups.
	const auto creator = _file->byName.find(rule.Ref("PCreator"));
	if (creator == _file->byName.end())
	{
		return nullptr;
	}
	auto atom = std::make_unique<Atom>();
	atom->group = collection.group;
	atom->parent = collection.parent;
	atom->creator = creator->second;
	atom->localPos = glm::vec3(rule.Float("OffsetX"), rule.Float("OffsetY"), rule.Float("OffsetZ"));
	atom->baseScale = creator->second->Float("InitialScale", 1.0f);
	if (const auto fp = rule.Ref("InitScaleFP"); !fp.empty())
	{
		atom->baseScale *= ProviderValue(fp);
	}
	atom->alpha = static_cast<float>(creator->second->Int("ColorA", 255));
	if (creator->second->type == "ParticleMistCreator")
	{
		// ParticleMistCreator::CreateLH3DMist: its ratio, or a random 5 to 7.5 without one.
		const float ratio = creator->second->Float("Ratio");
		atom->mistRatio = ratio != 0.0f ? ratio : 5.0f + Random(2.5f);
		atom->mistCounter = std::floor(Random(16.0f));
	}
	if (creator->second->type == "ParticleSpriteCreator")
	{
		// ParticleSpriteCreator::CreateParticle3DSprite: its first frame, or a random one, and maybe running backwards.
		const int frames = std::max(creator->second->Int("NumFrames", 1), 1);
		atom->spriteFrame = creator->second->Bool("RandomiseInitFrame") ? std::floor(Random(static_cast<float>(frames)))
		                                                                  : static_cast<float>(creator->second->Int("InitFrame"));
		if (creator->second->Bool("RandomiseFrameDirection") && Random(1.0f) < 0.5f)
		{
			atom->spriteDirection = -1.0f;
		}
	}
	auto* made = atom.get();
	collection.atoms.push_back(std::move(atom));
	for (const int group : rule.Ints("NextGroups"))
	{
		CreateCollection(group, made);
	}
	return made;
}

void Effect::RemoveAtom(Collection& collection, size_t index)
{
	// It goes with everything under it.
	Atom* atom = collection.atoms[index].get();
	for (auto& other : _collections)
	{
		if (other->parent == atom)
		{
			for (size_t i = other->atoms.size(); i-- > 0;)
			{
				RemoveAtom(*other, i);
			}
			other->parent = nullptr;
			other->group = -1; // gone with its atom
		}
	}
	collection.atoms.erase(collection.atoms.begin() + static_cast<std::ptrdiff_t>(index));
}

float Effect::ProviderValue(const std::string& name) const
{
	const auto it = _file->byName.find(name);
	if (it == _file->byName.end())
	{
		return 1.0f;
	}
	const auto& provider = *it->second;
	if (provider.type == "MagnitudeFloatProvider") // MagnitudeFloatProvider::UpdateParams
	{
		const float value = _magnitude * provider.Float("ScaleBy", 1.0f);
		return std::clamp(value, provider.Float("Minimum", -std::numeric_limits<float>::max()),
		                  provider.Float("Maximum", std::numeric_limits<float>::max()));
	}
	if (provider.type == "ConstFloatProvider")
	{
		return provider.Float("ConstValue", 1.0f);
	}
	return 1.0f;
}

bool Effect::ConditionTrue(const std::string& name) const
{
	const auto it = _file->byName.find(name);
	if (it == _file->byName.end())
	{
		return true; // no condition
	}
	if (it->second->type == "EventConditionTrueOnCloseDown")
	{
		return _closing != it->second->Bool("InvertResponse");
	}
	return false;
}

const Effect::Atom* Effect::Carrier(const Atom& atom) const
{
	// Only a group marked in the file's Hierarchies carries the atoms it makes (SF_WaterOnHolder's group 0 does;
	// SF_FireBallOnHolder's do not, so its flames are not scaled by its root sprite).
	if (atom.parent == nullptr)
	{
		return nullptr;
	}
	const auto group = static_cast<size_t>(atom.parent->group);
	return group < _file->hierarchies.size() && _file->hierarchies[group] != 0 ? atom.parent : nullptr;
}

glm::vec3 Effect::GlobalPosition(const Atom& atom) const
{
	const auto* carrier = Carrier(atom);
	return carrier != nullptr ? GlobalPosition(*carrier) + atom.localPos * GlobalScale(*carrier) : atom.localPos;
}

float Effect::GlobalScale(const Atom& atom) const
{
	const auto* carrier = Carrier(atom);
	return (carrier != nullptr ? GlobalScale(*carrier) : 1.0f) * atom.baseScale * atom.scale;
}

void Effect::Update(float seconds)
{
	if (!_file)
	{
		return;
	}
	_age += seconds;
	for (size_t c = 0; c < _collections.size(); ++c)
	{
		auto& collection = *_collections[c];
		if (collection.group < 0)
		{
			continue;
		}
		for (const auto& object : _file->objects)
		{
			if (object->Int("Group", -1) != collection.group)
			{
				continue;
			}
			// Create rules: CreateRuleAnAtom is a once-only rule, once in each collection of its group.
			if (object->type == "CreateRuleAnAtom" && !collection.onceDone.contains(object.get()))
			{
				if (ConditionTrue(object->Ref("Condition")))
				{
					collection.onceDone.insert(object.get());
					CreateAtom(*_collections[c], *object); // may grow _collections: by index, not reference
				}
			}
		}
		auto& atoms = _collections[c]->atoms;
		for (size_t i = 0; i < atoms.size(); ++i)
		{
			auto& atom = *atoms[i];
			for (const auto& object : _file->objects)
			{
				if (object->Int("Group", -1) != _collections[c]->group)
				{
					continue;
				}
				if (object->type == "FollowOrigin") // FollowOrigin::ModifyAtomCore
				{
					const auto* carrier = Carrier(atom);
					atom.localPos = carrier == nullptr
					                    ? _origin
					                    : (_origin - GlobalPosition(*carrier)) / std::max(GlobalScale(*carrier), 1e-6f);
				}
				else if (object->type == "SetScale") // SetScale::ModifyAtomCore
				{
					if (const auto scale = object->Ref("Scale"); !scale.empty())
					{
						atom.scale = ProviderValue(scale);
					}
				}
				else if (object->type == "AR_FadeOutOnceConditionTrue") // AR_FadeOutOnceConditionTrue::ModifyAtomCore
				{
					auto& fade = atom.fades[object.get()];
					if (!fade.met && ConditionTrue(object->Ref("ConditionStartFadeOut")))
					{
						fade = {true, atom.age, atom.scale, atom.alpha};
					}
					if (fade.met)
					{
						const float t = std::clamp((atom.age - fade.startTime) / std::max(object->Float("TimeToFadeOut", 1.0f), 1e-6f), 0.0f, 1.0f);
						if (object->Bool("FadeAlpha"))
						{
							atom.alpha = fade.startAlpha * (1.0f - t);
						}
						if (object->Bool("ShrinkScale"))
						{
							atom.scale = fade.startScale * (1.0f - t);
						}
					}
				}
			}
			atom.age += seconds;
			if (atom.creator != nullptr && atom.creator->type == "ParticleSpriteCreator" && atom.creator->Bool("PlayAnim", true))
			{
				// Its frames at FrameRate a second (backwards when negative), round again if LoopAnim, else held at the end.
				const auto frames = static_cast<float>(std::max(atom.creator->Int("NumFrames", 1), 1));
				atom.spriteFrame += seconds * atom.creator->Float("FrameRate") * atom.spriteDirection;
				atom.spriteFrame = atom.creator->Bool("LoopAnim", true)
				                       ? atom.spriteFrame - frames * std::floor(atom.spriteFrame / frames)
				                       : std::clamp(atom.spriteFrame, 0.0f, frames - 1.0f);
			}
			atom.mistCounter = std::fmod(atom.mistCounter + seconds * k_MistCounterPerSecond, k_MistCounterWrap);
		}
		// Remove rules.
		for (const auto& object : _file->objects)
		{
			if (object->Int("Group", -1) != _collections[c]->group || object->type != "RemoveRuleAfterCloseDown")
			{
				continue;
			}
			if (_closing && _age - _closeDownAge > object->Float("Delay")) // RemoveRuleAfterCloseDown::ShouldRemove
			{
				for (size_t i = _collections[c]->atoms.size(); i-- > 0;)
				{
					RemoveAtom(*_collections[c], i);
				}
			}
		}
	}
}

void Effect::Collect(std::vector<Drawable>& out) const
{
	for (const auto& collection : _collections)
	{
		for (const auto& atom : collection->atoms)
		{
			if (atom->creator == nullptr)
			{
				continue;
			}
			const auto& creator = *atom->creator;
			if (creator.type == "ParticleSpriteCreator")
			{
				// Particle3DSprite::DrawAt and LH3DSprite::Draw: half as wide as its scale, as high times StretchVertically,
				// moved by its origin (in its own widths) and, CentreAtBase, up by half its height; its colour's alpha
				// times ScaleAlpha.
				const float width = GlobalScale(*atom);
				const float stretch = creator.Float("StretchVertically", 1.0f);
				const int perRow = std::max(creator.Int("NumSpritesPerRow", 8), 1);
				const int index = creator.Int("FileOffset") + static_cast<int>(atom->spriteFrame);
				const float cell = 1.0f / static_cast<float>(perRow);
				std::string texture = creator.Ref("TextureFileName");
				std::replace(texture.begin(), texture.end(), '\\', '/');
				if (const auto slash = texture.find_last_of('/'); slash != std::string::npos)
				{
					texture = texture.substr(slash + 1);
				}
				if (const auto dot = texture.find_last_of('.'); dot != std::string::npos)
				{
					texture = texture.substr(0, dot);
				}
				Drawable sprite {};
				sprite.kind = Drawable::Kind::Sprite;
				sprite.position = GlobalPosition(*atom);
				sprite.radius = width;
				sprite.ratio = stretch;
				sprite.colour = glm::vec4(static_cast<float>(creator.Int("ColorR", 255)) / 255.0f,
				                          static_cast<float>(creator.Int("ColorG", 255)) / 255.0f,
				                          static_cast<float>(creator.Int("ColorB", 255)) / 255.0f,
				                          std::clamp(atom->alpha / 255.0f, 0.0f, 1.0f) *
				                              static_cast<float>(creator.Int("ScaleAlpha", 255)) / 255.0f);
				sprite.texture = texture;
				sprite.uvMin = glm::vec2(static_cast<float>(index % perRow), static_cast<float>(index / perRow)) * cell;
				sprite.uvExtent = glm::vec2(cell);
				sprite.screenOffset = glm::vec2(-creator.Float("SpriteOriginX") * width,
				                                -creator.Float("SpriteOriginY") * width * stretch +
				                                    (creator.Bool("CentreAtBase") ? 0.5f * width * stretch : 0.0f));
				sprite.additive = creator.Bool("UseAdditiveAlpha");
				out.push_back(std::move(sprite));
				continue;
			}
			if (creator.type != "ParticleMistCreator")
			{
				continue;
			}
			const glm::vec4 colour(static_cast<float>(creator.Int("ColorR", 255)) / 255.0f,
			                       static_cast<float>(creator.Int("ColorG", 255)) / 255.0f,
			                       static_cast<float>(creator.Int("ColorB", 255)) / 255.0f, std::clamp(atom->alpha / 255.0f, 0.0f, 1.0f));
			out.push_back({Drawable::Kind::Mist, GlobalPosition(*atom), k_MistRadius * GlobalScale(*atom), atom->mistRatio, colour,
			               static_cast<int>(atom->mistCounter / 20.0f) & 15});
		}
	}
}
} // namespace openblack::psys
