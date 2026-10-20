/******************************************************************************
 * Copyright (c) 2018-2026 openblack developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/openblack/openblack
 *
 * openblack is licensed under the GNU General Public License version 3.
 *******************************************************************************/

#pragma once

#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include <glm/mat4x4.hpp>
#include <spdlog/common.h>

#include "EngineConfig.h"
#include "Windowing/WindowingInterface.h" // For DisplayMode

union SDL_Event;

namespace openblack
{

enum class LoggingSubsystem : uint8_t
{
	game,
	input,
	graphics,
	scripting,
	audio,
	pathfinding,
	ai,

	_count
};

constexpr static std::array<std::string_view, static_cast<size_t>(LoggingSubsystem::_count)> k_LoggingSubsystemStrs {
    "game",        //
    "input",       //
    "graphics",    //
    "scripting",   //
    "audio",       //
    "pathfinding", //
    "ai",          //
};

struct Arguments
{
	std::string executablePath;
	int windowWidth;
	int windowHeight;
	bool vsync;
	openblack::windowing::DisplayMode displayMode;
	GraphicsBackend graphicsBackend;
	std::string gamePath;
	float guiScale;
	uint32_t numFramesToSimulate;
	std::string logFile;
	std::array<spdlog::level::level_enum, k_LoggingSubsystemStrs.size()> logLevels;
	std::string startLevel;
	std::optional<std::pair</* frame number */ uint32_t, /* output */ std::filesystem::path>> requestScreenshot;
};

class Game
{
public:
	static constexpr auto k_TurnDuration = std::chrono::milliseconds(100);
	static constexpr float k_TurnDurationMultiplierSlow = 2.0f;
	static constexpr float k_TurnDurationMultiplierNormal = 1.0f;
	static constexpr float k_TurnDurationMultiplierFast = 0.5f;

	explicit Game(Arguments&& args) noexcept;
	virtual ~Game() noexcept;

	bool ProcessEvents(const SDL_Event& event) noexcept;
	bool GameLogicLoop() noexcept;
	bool Update() noexcept;
	bool Initialize() noexcept;
	bool Run() noexcept;

	bool LoadMap(const std::filesystem::path& path) noexcept;
	void LoadLandscape(const std::filesystem::path& path);

	void SetTime(float time) noexcept;
	void SetGameSpeed(float multiplier) { _gameSpeedMultiplier = multiplier; }
	[[nodiscard]] float GetGameSpeed() const { return _gameSpeedMultiplier; }
	// How fast the whole game plays, for the player to hurry through or slow down (1 = normal; 2 = twice as fast).
	// Unlike the scripts' game speed above, it also speeds up the frame time: the camera, animations and the
	// scripts' clock.
	void ChangePlaySpeed(float factor);
	[[nodiscard]] float GetPlaySpeed() const { return _playSpeed; }
	// The speed as shown: against normal play, so normal reads x1.
	[[nodiscard]] float GetShownPlaySpeed() const { return _playSpeed / k_NormalPlaySpeed; }

	[[nodiscard]] uint32_t GetTurn() const { return _turnCount; }
	[[nodiscard]] bool IsPaused() const { return _paused; }
	[[nodiscard]] std::chrono::duration<float, std::milli> GetDeltaTime() const { return _turnDeltaTime; }
	[[nodiscard]] const glm::ivec2& GetMousePosition() const { return _mousePosition; }

	void RequestScreenshot(const std::filesystem::path& path) noexcept;

	static Game* Instance() { return sInstance; }

private:
	static Game* sInstance;

	/// path to Lionhead Studios Ltd/Black & White folder
	const std::filesystem::path _gamePath;

	std::filesystem::path _startMap;

	std::chrono::steady_clock::time_point _lastGameLoopTime;
	bool _turnRan {false};
	static constexpr int k_MaxTurnsPerFrame = 8;
	std::chrono::steady_clock::duration _turnDeltaTime;
	float _gameSpeedMultiplier {1.0f};
	// Normal play runs at 1.6 times the 100 ms game turn (Mike, 2026-10-19: x1 looked slow and choppy, x2 a little fast,
	// 1.6 about right); the speed steps double and halve from there.
	static constexpr float k_NormalPlaySpeed = 1.6f;
	float _playSpeed {k_NormalPlaySpeed};
	uint32_t _frameCount {0};
	uint32_t _turnCount {0};
	bool _paused {false}; // running from the start (Mike, 2026-10-18); + (P) pauses
	glm::ivec2 _mousePosition;
	bool _handGripping;
	std::optional<std::pair</* frame number */ uint32_t, /* output */ std::filesystem::path>> _requestScreenshot;
};
} // namespace openblack
