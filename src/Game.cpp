/******************************************************************************
 * Copyright (c) 2018-2026 openblack developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/openblack/openblack
 *
 * openblack is licensed under the GNU General Public License version 3.
 *******************************************************************************/

#include "Game.h"

#include <bgfx/bgfx.h>

#include "HandPoses.h"
#include "HelpSpirits.h"
#include "LoadingScreen.h"
#include "Subtitles.h"
#include "PSys/PSys.h"

#include <cstdlib>
#include <fstream>
#include <string>

#include <LHVM.h>
#include <SDL.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/intersect.hpp>
#include <glm/gtx/transform.hpp>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "3D/CreatureBody.h"
#include "3D/L3DMesh.h"
#include "3D/LandIslandInterface.h"
#include "3D/OceanInterface.h"
#include "3D/SkyInterface.h"
#include "3D/TempleInteriorInterface.h"
#include "Audio/AudioManagerInterface.h"
#include "CHLApi.h"
#include "Camera/Camera.h"
#include "Common/EventManager.h"
#include "Common/StringUtils.h"
#include "Debug/DebugGuiInterface.h"
#include "ECS/Archetypes/PlayerArchetype.h"
#include "ECS/Components/CameraBookmark.h"
#include "ECS/Components/Hand.h"
#include "ECS/Components/Mesh.h"
#include "ECS/Map.h"
#include "ECS/Registry.h"
#include "ECS/Systems/CameraBookmarkSystemInterface.h"
#include "ECS/Systems/DynamicsSystemInterface.h"
#include "ECS/Systems/HandSystemInterface.h"
#include "ECS/Systems/LivingActionSystemInterface.h"
#include "ECS/Systems/PathfindingSystemInterface.h"
#include "ECS/Systems/PlayerSystemInterface.h"
#include "ECS/Systems/RenderingSystemInterface.h"
#include "EngineConfig.h"
#include "FileSystem/FileSystemInterface.h"
#include "Graphics/FrameBuffer.h"
#include "Graphics/RendererInterface.h"
#include "Input/GameActionMapInterface.h"
#include "LHScriptX/Script.h"
#include "Locator.h"
#include "Parsers/InfoFile.h"
#include "Profiler.h"
#include "Resources/Loaders.h"
#include "Resources/ResourcesInterface.h"
#include "Serializer/FotFile.h"
#include "Video/MoviePlayer.h"

#ifdef __ANDROID__
#include <spdlog/sinks/android_sink.h>
#endif

#if defined(__SWITCH__)
void switchInputFrame();
bool switchUpdateWantsRestart(); // switch-port/switch_update.cpp: a new build is installed
#endif

using namespace openblack;
using namespace openblack::lhscriptx;
using namespace std::chrono_literals;

const std::string k_WindowTitle = "openblack";

Game* Game::sInstance = nullptr;

Game::Game(Arguments&& args) noexcept
    : _gamePath(args.gamePath)
    , _startMap(args.startLevel)
    , _requestScreenshot(args.requestScreenshot)
{
	Locator::camera::emplace(glm::zero<glm::vec3>());
	std::function<std::shared_ptr<spdlog::logger>(const std::string&)> createLogger;
#ifdef __ANDROID__
	if (!args.logFile.empty() && args.logFile == "logcat")
	{
		createLogger = [](const std::string& name) { return spdlog::android_logger_mt(name, "spdlog-android"); };
	}
	else
#endif // __ANDROID__
	{
		if (!args.logFile.empty() && args.logFile != "stdout")
		{
			createLogger = [&args](const std::string& name) { return spdlog::basic_logger_mt(name, args.logFile); };
		}
		else
		{
			createLogger = [](const std::string& name) { return spdlog::stdout_color_mt(name); };
		}
	}
	// TODO (#749) use std::views::enumerate
	for (size_t i = 0; const auto& subsystem : k_LoggingSubsystemStrs)
	{
		auto logger = createLogger(subsystem.data());
		logger->set_level(args.logLevels.at(i));
		++i;
	}
	sInstance = this;

	auto& config = Locator::config::emplace();
	config.numFramesToSimulate = args.numFramesToSimulate;
	config.numFramesToSimulate = args.numFramesToSimulate;
	config.numFramesToSimulate = args.numFramesToSimulate;
	config.resolution = {args.windowWidth, args.windowHeight};
	config.displayMode = args.displayMode;
	config.graphicsBackend = args.graphicsBackend;
	config.vsync = args.vsync;
	config.guiScale = args.guiScale;
	// The land's bump maps stay on on the Switch: they are its texture detail (the large one over each block, the small
	// one's fine grain near the camera), and without them the ground looked low-res and blurry beside the PC's
	// (Mike, 2026-10-23). Two texture reads a pixel.
}

Game::~Game() noexcept
{
	ShutDownServices();
	SDL_Quit(); // todo: move to GameWindow
	spdlog::shutdown();
}

// Mouse buttons as their events say. SDL_GetMouseState misses buttons sent as events (the Switch's ZR, ZL
// and touch arrive that way), so GAME_THING_CLICKED and the hand read these.
namespace
{
bool g_leftButtonDown = false;
bool g_rightButtonDown = false;
uint32_t g_scrollUnderHand = 0; // a scroll the pointer is on: the Action Button clicks it rather than picking up
} // namespace

bool Game::ProcessEvents(const SDL_Event& event) noexcept
{
	static bool leftMouseButton = false;
	static bool middleMouseButton = false;

	if ((event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) && event.button.button == SDL_BUTTON_LEFT)
	{
		g_leftButtonDown = event.type == SDL_MOUSEBUTTONDOWN;
	}
	if ((event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) && event.button.button == SDL_BUTTON_RIGHT)
	{
		g_rightButtonDown = event.type == SDL_MOUSEBUTTONDOWN;
	}
	// Test save points: keypad 1/2 save points 1/2, keypad 3/4 load them (D-pad on the Switch).
	if (event.type == SDL_KEYDOWN && event.key.repeat == 0)
	{
		switch (event.key.keysym.scancode)
		{
		case SDL_SCANCODE_KP_1:
			chlapi::SaveTestPoint(1);
			break;
		case SDL_SCANCODE_KP_2:
			chlapi::SaveTestPoint(2);
			break;
		case SDL_SCANCODE_KP_3:
			chlapi::LoadTestPoint(1);
			break;
		case SDL_SCANCODE_KP_4:
			chlapi::LoadTestPoint(2);
			break;
		default:
			break;
		}
	}

	if ((event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) && event.button.button == SDL_BUTTON_LEFT)
	{
		leftMouseButton = !leftMouseButton;
	}
	if ((event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) && event.button.button == SDL_BUTTON_MIDDLE)
	{
		middleMouseButton = !middleMouseButton;
	}

	_handGripping = middleMouseButton || leftMouseButton;

	auto& window = Locator::windowing::value();
	auto& camera = Locator::camera::value();

	// Each way out says why (2026-10-18: a clean exit while holding a tree, and nothing told what asked for it).
	switch (event.type)
	{
	case SDL_QUIT:
		SPDLOG_LOGGER_INFO(spdlog::get("game"), "Quitting: SDL_QUIT (the system, or a self-update restart)");
		return false;
	case SDL_WINDOWEVENT:
		if (event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == window.GetID())
		{
			SPDLOG_LOGGER_INFO(spdlog::get("game"), "Quitting: the window was closed");
			return false;
		}
		else if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
		{
			const auto resolution = glm::u16vec2(event.window.data1, event.window.data2);
			Locator::rendererInterface::value().Reset(resolution);
			Locator::rendererInterface::value().ConfigureView(graphics::RenderPass::Main, resolution, 0x274659ff);

			auto aspect = window.GetAspectRatio();
			const auto& config = Locator::config::value();
			camera.SetProjectionMatrixPerspective(config.cameraXFov, aspect, config.cameraNearClip, config.cameraFarClip);
		}
		break;
	case SDL_KEYDOWN:
		switch (event.key.keysym.sym)
		{
		case SDLK_ESCAPE:
			SPDLOG_LOGGER_INFO(spdlog::get("game"), "Quitting: Escape pressed");
			return false;
		case SDLK_f:
			window.SetDisplayMode(windowing::DisplayMode::Fullscreen);
			break;
		case SDLK_p:
			_paused = !_paused;
			break;
		case SDLK_F1:
			Locator::rendererInterface::value().SetDebug(!Locator::rendererInterface::value().GetDebug());
			break;
		case SDLK_1:
		case SDLK_2:
		case SDLK_3:
		case SDLK_4:
		case SDLK_5:
		case SDLK_6:
		case SDLK_7:
		case SDLK_8:
			if ((event.key.keysym.mod & KMOD_CTRL) != 0)
			{
				const auto index = static_cast<uint8_t>(event.key.keysym.sym - SDLK_1);
				const auto positions = Locator::handSystem::value().GetPlayerHandPositions();
				if (positions[static_cast<size_t>(ecs::systems::HandSystemInterface::Side::Left)] ||
				    positions[static_cast<size_t>(ecs::systems::HandSystemInterface::Side::Right)])
				{
					const auto handPosition =
					    positions[static_cast<size_t>(ecs::systems::HandSystemInterface::Side::Left)].value_or(
					        positions[static_cast<size_t>(ecs::systems::HandSystemInterface::Side::Right)].value_or(
					            glm::zero<glm::vec3>()));
					Locator::cameraBookmarkSystem::value().SetBookmark(index, handPosition, camera.GetOrigin());
				}
			}
			else
			{
				const auto& entitiesRegistry = Locator::entitiesRegistry::value();
				const size_t index = event.key.keysym.sym - SDLK_1;
				const auto& bookmarkEntities = Locator::cameraBookmarkSystem::value().GetBookmarks();
				const auto entity = bookmarkEntities.at(index);
				const auto [transform, bookmark] =
				    entitiesRegistry.TryGet<ecs::components::Transform, ecs::components::CameraBookmark>(entity);
				if (transform != nullptr && bookmark != nullptr)
				{
					camera.GetModel().SetFlight(bookmark->savedOrigin, transform->position);
				}
			}
			break;
		}
		break;
	case SDL_MOUSEMOTION:
	{
		SDL_GetMouseState(&_mousePosition.x, &_mousePosition.y);
		break;
	}
	case SDL_MOUSEBUTTONUP:
		switch (event.button.button)
		{
		case SDL_BUTTON_MIDDLE:
		{
			const glm::ivec2 screenSize = window.GetSize();
			SDL_SetRelativeMouseMode((event.type == SDL_MOUSEBUTTONDOWN) ? SDL_TRUE : SDL_FALSE);
			SDL_WarpMouseInWindow(static_cast<SDL_Window*>(window.GetHandle()), screenSize.x / 2, screenSize.y / 2);
		}
		break;
		}
		break;
	}

	return true;
}

void Game::ChangePlaySpeed(float factor)
{
	constexpr float k_Slowest = k_NormalPlaySpeed / 8.0f;
	constexpr float k_Fastest = k_NormalPlaySpeed * 8.0f;
	_playSpeed = std::clamp(_playSpeed * factor, k_Slowest, k_Fastest);
	SPDLOG_LOGGER_INFO(spdlog::get("game"), "Play speed x{} (clock x{})", _playSpeed / k_NormalPlaySpeed, _playSpeed);
}

bool Game::GameLogicLoop() noexcept
{
	using namespace ecs::components;
	using namespace ecs::systems;

	if (_paused || video::MoviePlaying())
	{
		return false;
	}

	const auto currentTime = std::chrono::steady_clock::now();
	const auto delta = currentTime - _lastGameLoopTime;
	const auto turnDuration = std::chrono::duration_cast<std::chrono::steady_clock::duration>(k_TurnDuration * _gameSpeedMultiplier / _playSpeed);
	// NOLINTNEXTLINE(modernize-use-nullptr): clang-tidy bug
	if (delta < turnDuration)
	{
		return false;
	}

	// Build Map Grid Acceleration Structure
	Locator::entitiesMap::value().Rebuild();

	auto& profiler = Locator::profiler::value();

	{
		auto pathfinding = profiler.BeginScoped(Profiler::Stage::PathfindingUpdate);
		Locator::pathfindingSystem::value().Update();
	}
	{
		auto actions = profiler.BeginScoped(Profiler::Stage::LivingActionUpdate);
		Locator::livingActionSystem::value().Update();
	}

	auto& lhvm = Locator::vm::value();
	lhvm.LookIn(lhvm::ScriptType::All);
	chlapi::EndScriptTurn();
	if (std::getenv("OPENBLACK_TRACE_VM") != nullptr && _turnCount % 50 == 0) // temporary diagnostic
	{
		SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "VM turn {}: {} task(s), camera origin ({:.0f}, {:.0f}, {:.0f}){}",
		                   _turnCount, lhvm.GetTasks().size(), Locator::camera::value().GetOrigin().x,
		                   Locator::camera::value().GetOrigin().y, Locator::camera::value().GetOrigin().z,
		                   Locator::camera::value().IsScriptControlled() ? " scripted" : "");
		for (const auto& [id, task] : lhvm.GetTasks())
		{
			const auto& code = lhvm.GetInstructions();
			std::string at;
			for (uint32_t k = task.instructionAddress >= 2 ? task.instructionAddress - 2 : 0;
			     k < std::min<uint32_t>(task.instructionAddress + 2, static_cast<uint32_t>(code.size())); ++k)
			{
				at += fmt::format("{}{} {} | ", k == task.instructionAddress ? ">" : "",
				                  lhvm::k_OpcodeNames.at(static_cast<size_t>(code[k].code)), code[k].data.intVal);
			}
			SPDLOG_LOGGER_INFO(spdlog::get("scripting"), "  task {} '{}' ip {} waiting {} sleeping {} at: {}", id, task.name,
			                   task.instructionAddress, task.waitingTaskId, task.sleeping, at);
		}
	}

	// The turn's own slot, not the frame's time: the remainder carries to the next turn, so a sped-up game
	// gets all its turns (dropping it held x2 to about x1.45, and capped any speed at one turn a frame).
	_lastGameLoopTime += turnDuration;
	if (currentTime - _lastGameLoopTime > turnDuration * k_MaxTurnsPerFrame)
	{
		_lastGameLoopTime = currentTime; // too far behind (a load, a stall): start again from now
	}
	_turnRan = true;
	_turnDeltaTime = delta;
	++_turnCount;

	return false;
}

bool Game::Update() noexcept
{
	auto& profiler = Locator::profiler::value();

	profiler.Frame();

	auto& camera = Locator::camera::value();
	auto& config = Locator::config::value();

	auto previous = profiler.GetEntries().at(profiler.GetEntryIndex(-1)).frameStart;
	auto current = profiler.GetEntries().at(profiler.GetEntryIndex(0)).frameStart;
	// Prevent spike at first frame
	if (previous.time_since_epoch().count() == 0)
	{
		current = previous;
	}
	auto deltaTime = std::chrono::duration_cast<std::chrono::microseconds>((current - previous) * _playSpeed);

	Locator::debugGui::value().SetScale(config.guiScale);

	// Physics
	{
		auto physics = profiler.BeginScoped(Profiler::Stage::PhysicsUpdate);
		if (_frameCount > 0)
		{
			auto& dynamicsSystem = Locator::dynamicsSystem::value();
			dynamicsSystem.Update(deltaTime);
			dynamicsSystem.UpdatePhysicsTransforms();
		}
	}

	// Input events
	{
		auto sdlInput = profiler.BeginScoped(Profiler::Stage::SdlInput);
		if (!Locator::debugGui::value().StealsFocus())
		{
			Locator::gameActionSystem::value().Frame();
		}
#if defined(__SWITCH__)
		switchInputFrame(); // switch-port/switch_input.cpp: Joy-Con as SDL key/mouse events
		if (switchUpdateWantsRestart())
		{
			return false; // ends as if closed; the loader starts the new build
		}
#endif
		SDL_Event e;
		while (SDL_PollEvent(&e) != 0)
		{
			// During a movie input only skips it: a press of anything ends it, and the game never sees the press.
			if (video::MoviePlaying() && e.type != SDL_QUIT)
			{
				if (e.type == SDL_KEYDOWN || e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_FINGERDOWN ||
				    e.type == SDL_CONTROLLERBUTTONDOWN || e.type == SDL_JOYBUTTONDOWN)
				{
					SPDLOG_LOGGER_INFO(spdlog::get("game"), "Movie skipped");
					video::StopMovie();
				}
				continue;
			}
			Locator::events::value().Create<SDL_Event>(e);
		}
		video::UpdateMovie();
		camera.HandleActions(deltaTime);
	}

	if (!config.running)
	{
		return false;
	}

	// ImGui events + prepare
	{
		auto guiLoop = profiler.BeginScoped(Profiler::Stage::GuiLoop);
		if (Locator::debugGui::value().Loop())
		{
			return false; // Quit event
		}
	}

	camera.Update(deltaTime);
	{
		// Diagnosis (Mike, 2026-10-16: after every cutscene the view comes back a bit too low, under the ground): where
		// the camera is as a script hands it back, and for 3 s after, with the ground under it.
		static bool s_wasScripted = false;
		static int s_samplesLeft = 0;
		static auto s_nextSample = std::chrono::steady_clock::time_point {};
		const auto now = std::chrono::steady_clock::now();
		const bool scripted = camera.IsScriptControlled();
		auto report = [&camera](const char* when) {
			const auto origin = camera.GetOrigin();
			const auto focus = camera.GetFocus();
			const auto target = camera.GetOrigin(Camera::Interpolation::Target);
			const float ground = Locator::terrainSystem::value().GetHeightAt(glm::vec2(origin.x, origin.z));
			SPDLOG_LOGGER_INFO(spdlog::get("game"),
			                   "Camera {}: origin ({:.1f}, {:.1f}, {:.1f}) ground {:.1f} ({:+.1f} above), focus ({:.1f}, {:.1f}, {:.1f}), heading to ({:.1f}, {:.1f}, {:.1f})",
			                   when, origin.x, origin.y, origin.z, ground, origin.y - ground, focus.x, focus.y, focus.z, target.x,
			                   target.y, target.z);
		};
		if (s_wasScripted && !scripted)
		{
			report("handed back by the script");
			s_samplesLeft = 6;
			s_nextSample = now + std::chrono::milliseconds(500);
		}
		else if (s_samplesLeft > 0 && now >= s_nextSample)
		{
			report("after the hand-back");
			--s_samplesLeft;
			s_nextSample = now + std::chrono::milliseconds(500);
		}
		s_wasScripted = scripted;
	}
	{
		// A script's camera (cutscenes, the creature close-up) comes within a unit of the person it films, and the
		// usual near plane cut away the half of them facing it. Only then, as the 24-bit depth buffer's far precision
		// pays for a closer plane.
		const auto& config = Locator::config::value();
		const float nearClip = camera.IsScriptControlled() ? std::min(config.cameraNearClip, 0.1f) : config.cameraNearClip;
		const float aspect = Locator::windowing::has_value() ? Locator::windowing::value().GetAspectRatio() : 1.0f;
		camera.SetProjectionMatrixPerspective(camera.GetLens(config.cameraXFov), aspect, nearClip, config.cameraFarClip);
	}
	{
		// Close in on your creature's fight and the view goes side on, as the original: your creature on the right, the
		// other on the left, facing each other, following them as they move. B (or the fight's end) leaves it.
		static bool s_sideView = false;
		static bool s_fresh = true; // the side view's smoothing starts over each time it is taken
		const auto fight = chlapi::PlayersFight();
		const auto ring = chlapi::FightRing();
		auto& registry = Locator::entitiesRegistry::value();
		if (s_sideView && (!fight || chlapi::TakeFightViewExit()))
		{
			s_sideView = false;
			camera.SetScriptControl(false);
		}
		else if (!s_sideView && fight && ring && !camera.IsScriptControlled() &&
		         glm::distance(camera.GetOrigin(), ring->first) < ring->second * 3.0f)
		{
			s_sideView = true;
			s_fresh = true;
			camera.SetScriptControl(true);
			chlapi::TakeFightViewExit(); // a B pressed before doesn't count
		}
		if (s_sideView && fight)
		{
			const auto mine = registry.Get<ecs::components::Transform>(static_cast<entt::entity>(fight->first)).position;
			const auto theirs = registry.Get<ecs::components::Transform>(static_cast<entt::entity>(fight->second)).position;
			// Followed smoothly (over about a third of a second): set straight from the fighters' positions, which
			// jump with every lunge, step and recoil, the whole view shook like an earthquake.
			static glm::vec3 s_middle;
			static glm::vec3 s_across;
			const auto middle = (mine + theirs) * 0.5f;
			const auto across = glm::vec3(mine.x - theirs.x, 0.0f, mine.z - theirs.z);
			const float follow = s_fresh ? 1.0f : 1.0f - std::exp(-3.0f * std::chrono::duration<float>(deltaTime).count());
			s_middle = glm::mix(s_middle, middle, follow);
			s_across = glm::mix(s_across, across, follow);
			s_fresh = false;
			const float apart = std::max(glm::length(s_across), 4.0f);
			// Looking along the side: with "right" = across, the camera sits on the side where up x across points.
			const auto side = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), s_across / apart));
			camera.SetOrigin(s_middle - side * apart * 1.6f + glm::vec3(0.0f, apart * 0.35f, 0.0f));
			camera.SetFocus(s_middle + glm::vec3(0.0f, apart * 0.25f, 0.0f));
		}
	}
	chlapi::UpdateTemple(deltaTime); // inside the Temple the view only turns, in place
	Locator::cameraBookmarkSystem::value().Update(deltaTime);
	if (!_paused && !video::MoviePlaying())
	{
		chlapi::UpdateScriptFrame(deltaTime);
	}

	// Update Game Logic in Registry
	{
		auto gameLogic = profiler.BeginScoped(Profiler::Stage::GameLogic);
		// Every turn that is due, up to a few a frame: at x8 a turn is due every 12.5 ms, faster than frames.
		for (int turn = 0; turn < k_MaxTurnsPerFrame; ++turn)
		{
			_turnRan = false;
			if (GameLogicLoop())
			{
				return false; // Quit event
			}
			if (!_turnRan)
			{
				break;
			}
		}
	}

	// Update Uniforms
	static glm::vec2 s_handScreen(0.5f); // where the hand is on the screen, 0-1 (for its lean, below)
	{
		auto profilerScopedUpdateUniforms = profiler.BeginScoped(Profiler::Stage::UpdateUniforms);

		// Update Hand and intersection point
		ecs::components::Transform intersectionTransform {};
		{
			const auto screenSize =
			    Locator::windowing::has_value() ? Locator::windowing::value().GetSize() : glm::zero<glm::ivec2>();
			const auto scale = glm::vec3(50.0f, 50.0f, 50.0f);
			if (screenSize.x > 0 && screenSize.y > 0)
			{
				glm::vec3 rayOrigin;
				glm::vec3 rayDirection;
				// A tutorial hand demo moves the hand in place of the mouse.
				const auto demoHand = chlapi::UpdateHandDemo();
				subtitles::SetDemoHand(demoHand);
				const glm::vec2 handScreen =
				    demoHand ? *demoHand : static_cast<glm::vec2>(_mousePosition) / static_cast<glm::vec2>(screenSize);
				s_handScreen = handScreen;
				camera.DeprojectScreenToWorld(handScreen, rayOrigin, rayDirection);
				auto& dynamicsSystem = Locator::dynamicsSystem::value();

				uint32_t objectUnderHand = 0;
				// A scroll (highlight) pointed at: the Action Button or a tap on it is a click for the scripts
				// (GAME_THING_CLICKED) - the original's "Click the Action Button to access them".
				{
					const bool rayValid = !glm::any(glm::isnan(rayOrigin) || glm::isnan(rayDirection));
					const auto scroll = rayValid ? chlapi::HighlightOnRay(rayOrigin, rayDirection) : 0u;
					// A creature or a person is clicked the same way (neither has a physics body for the ray below).
					auto clickable = scroll != 0 ? scroll : (rayValid ? chlapi::CreatureOnRay(rayOrigin, rayDirection) : 0u);
					if (clickable == 0 && rayValid)
					{
						clickable = chlapi::VillagerOnRay(rayOrigin, rayDirection);
					}
					static bool s_scrollRightWas = false;
					static bool s_scrollLeftWas = false;
					if (clickable != 0 && ((g_rightButtonDown && !s_scrollRightWas) || (g_leftButtonDown && !s_scrollLeftWas)))
					{
						chlapi::NotifyGameThingClicked(clickable);
					}
					s_scrollRightWas = g_rightButtonDown;
					s_scrollLeftWas = g_leftButtonDown;
					g_scrollUnderHand = scroll;
				}
				if (!glm::any(glm::isnan(rayOrigin) || glm::isnan(rayDirection)))
				{
					if (auto hit = dynamicsSystem.RayCastClosestHit(rayOrigin, rayDirection, 1e10f))
					{
						intersectionTransform = hit->first;
						if (hit->second.type == RigidBodyType::Entity && hit->second.id != 0)
						{
							objectUnderHand = static_cast<uint32_t>(hit->second.id);
						}
						// A left-button press on a game object is a click for the scripts
						// (GAME_THING_CLICKED). ZR and touchscreen taps arrive as the left button.
						static bool s_leftWasDown = false;
						if (g_leftButtonDown && !s_leftWasDown && objectUnderHand != 0)
						{
							chlapi::NotifyGameThingClicked(objectUnderHand);
						}
						s_leftWasDown = g_leftButtonDown;
					}
					else // For the water
					{
						float intersectDistance = 0.0f;
						const auto planeOrigin = glm::vec3(0.0f, 0.0f, 0.0f);
						const auto planeNormal = glm::vec3(0.0f, 1.0f, 0.0f);
						if (glm::intersectRayPlane(rayOrigin, rayDirection, planeOrigin, planeNormal, intersectDistance))
						{
							intersectionTransform.position = rayOrigin + rayDirection * intersectDistance;
							intersectionTransform.rotation = glm::mat3(1.0f);
						}
					}
				}
				intersectionTransform.scale = scale;

				// The right button (ZL) is the original's Action Button, as its hand demos record it (givewood,
				// throwrock): a press over an object picks it up and the hand keeps it after the release; a second
				// press lets go on its release - dropped where the hand is still, thrown if it is moving. Rocks,
				// people and logs have no physics body, so the ray lands on the ground under them: take the
				// nearest of those within reach of that point.
				constexpr float k_HandReach = 3.0f;
				bool rightDown = g_rightButtonDown;
				if (const auto test = chlapi::TestPickupStep())
				{
					intersectionTransform.position = test->first;
					objectUnderHand = 0;
					rightDown = test->second;
				}
				if (objectUnderHand == 0)
				{
					objectUnderHand = chlapi::FindObjectNear(intersectionTransform.position, k_HandReach);
				}
				chlapi::SetObjectUnderHand(objectUnderHand);
				chlapi::SetHandGround(intersectionTransform.position);
				chlapi::SetHandRay(rayOrigin, rayDirection);
				{
					// A tap or ZR on a miracle sphere takes it; the next one casts it there.
					static bool s_miracleLeftWas = false;
					if (g_leftButtonDown && !s_miracleLeftWas && !chlapi::FightClick(rayOrigin, rayDirection, intersectionTransform.position) &&
					    !chlapi::MiracleClick(intersectionTransform.position))
					{
						chlapi::ObjectClick(objectUnderHand);
					}
					s_miracleLeftWas = g_leftButtonDown;
				}
				chlapi::SetWoodUnderHand(chlapi::FindWoodNear(intersectionTransform.position, k_HandReach));
				static bool s_rightWasDown = false;
				static bool s_letGoOnRelease = false;
				static std::chrono::steady_clock::time_point s_pressedAt {};
				if (rightDown && !s_rightWasDown)
				{
					s_pressedAt = std::chrono::steady_clock::now();
				}
				// What was picked up by this press (or since it began - a grab the same frame) isn't let go of on its
				// release: tap ZL to pick up, tap (or press, swipe, release) again to throw (Mike, 2026-10-17).
				if (s_letGoOnRelease && chlapi::HandPickedUpAt() >= s_pressedAt - std::chrono::milliseconds(50))
				{
					s_letGoOnRelease = false;
				}
				if (rightDown && !s_rightWasDown && g_scrollUnderHand == 0)
				{
					// On your creature (with empty hands) the press starts rewarding or punishing it: the camera
					// faces it, and the hand's movement while the button stays held strokes or slaps it.
					// Holding grain or wood over a village store, the press puts it into the store. Over a wood
					// pile the press collects wood for as long as it is held (adding to wood already in the hand);
					// otherwise a press while holding lets go on its release.
					if (!chlapi::HandHolding() && chlapi::BeginCreatureInteraction(rayOrigin, rayDirection))
					{
					}
					else if (!chlapi::HandHolding() && chlapi::LeashCommand(intersectionTransform.position, objectUnderHand))
					{
					}
					else if (chlapi::HandHolding() && chlapi::HandGiveToCreature(rayOrigin, rayDirection))
					{
					}
					else if (chlapi::HandDepositIntoStore(intersectionTransform.position))
					{
					}
					else if (const auto pile = chlapi::HandWoodUnder(); pile != 0)
					{
						chlapi::HandCollectBegin(pile);
					}
					else if (chlapi::HandHolding())
					{
						s_letGoOnRelease = true;
					}
					else if (const auto target = chlapi::PickUpTarget(objectUnderHand, intersectionTransform.position); target != 0)
					{
						chlapi::HandPickUp(target);
					}
				}
				if (chlapi::InCreatureInteraction())
				{
					if (rightDown)
					{
						chlapi::UpdateCreatureInteraction(handScreen, rayOrigin, rayDirection, deltaTime);
					}
					else
					{
						chlapi::EndCreatureInteraction();
					}
				}
				if (rightDown && chlapi::HandCollecting())
				{
					chlapi::HandCollect(deltaTime);
				}
				if (!rightDown)
				{
					chlapi::HandCollectEnd();
				}
				if (chlapi::HandHolding())
				{
					chlapi::HandCarry(intersectionTransform.position);
				}
				if (!rightDown && s_rightWasDown && s_letGoOnRelease)
				{
					s_letGoOnRelease = false;
					chlapi::HandDrop(intersectionTransform.position);
				}
				s_rightWasDown = rightDown;
			}

			if (!_handGripping)
			{
				const glm::vec3 handOffset(0, 1.5f, 0);
				const glm::mat4 modelRotationCorrection = glm::eulerAngleX(glm::radians(90.0f));

				const auto handEntity =
				    Locator::handSystem::value()
				        .GetPlayerHands()[static_cast<size_t>(ecs::systems::HandSystemInterface::Side::Left)];
				// The hand's entity gone (2026-10-18: two crashes reading it): say so, once, rather than read freed memory.
				if (!Locator::entitiesRegistry::value().Valid(handEntity) ||
				    !Locator::entitiesRegistry::value().AllOf<ecs::components::Transform>(handEntity))
				{
					static bool s_said = false;
					if (!s_said)
					{
						s_said = true;
						SPDLOG_LOGGER_ERROR(spdlog::get("game"), "The hand's entity {} is no longer valid", static_cast<uint32_t>(handEntity));
					}
					return true;
				}
				auto& handTransform = Locator::entitiesRegistry::value().Get<ecs::components::Transform>(handEntity);
				// TODO(#480): move using velocity rather than snapping hand to intersectionTransform
				handTransform.position = intersectionTransform.position;
				handTransform.rotation = glm::eulerAngleY(camera.GetRotation().y) * modelRotationCorrection;
				handTransform.rotation = intersectionTransform.rotation * handTransform.rotation;
				handTransform.position += intersectionTransform.rotation * handOffset;
				Locator::entitiesRegistry::value().SetDirty();
			}

			// No hand in a cutscene: drawn at no size while a script films (its own size kept for after).
			{
				const auto handEntity =
				    Locator::handSystem::value().GetPlayerHands()[static_cast<size_t>(ecs::systems::HandSystemInterface::Side::Left)];
				auto& handTransform = Locator::entitiesRegistry::value().Get<ecs::components::Transform>(handEntity);
				static glm::vec3 s_handScale(0.0f);
				if (handTransform.scale != glm::vec3(0.0f))
				{
					s_handScale = handTransform.scale;
				}
				// (Not with a test pose held up to look at: OPENBLACK_TEST_HAND_POSE, below.)
				static const bool s_testPose = std::getenv("OPENBLACK_TEST_HAND_POSE") != nullptr;
				handTransform.scale = chlapi::InCutscene() && !s_testPose ? glm::vec3(0.0f) : s_handScale;
			}
			// The hand's pose, from the original's hand pose file: closed round what it holds, reaching over
			// what it could take. OPENBLACK_TEST_HAND_POSE=<name>:<frame> holds one pose, to look at.
			{
				auto& registry = Locator::entitiesRegistry::value();
				const auto handEntity =
				    Locator::handSystem::value()
				        .GetPlayerHands()[static_cast<size_t>(ecs::systems::HandSystemInterface::Side::Left)];
				static const char* s_testPose = std::getenv("OPENBLACK_TEST_HAND_POSE");
				std::string poseName;
				float poseFrame = 0.0f;
				if (s_testPose != nullptr)
				{
					const std::string_view test(s_testPose);
					const auto colon = test.find(':');
					poseName = std::string(test.substr(0, colon));
					poseFrame = colon == std::string_view::npos ? 0.0f : std::stof(std::string(test.substr(colon + 1)));
					// ...in front of the camera, where it can be seen without a mouse in the window.
					auto& handTransform = registry.Get<ecs::components::Transform>(handEntity);
					// OPENBLACK_TEST_HAND_TURN=<x>,<y>,<z> degrees turns it some other way than lying flat.
					static const char* s_turnText = std::getenv("OPENBLACK_TEST_HAND_TURN");
					glm::vec3 turn(90.0f, 0.0f, 0.0f);
					if (s_turnText != nullptr)
					{
						std::sscanf(s_turnText, "%f,%f,%f", &turn.x, &turn.y, &turn.z);
					}
					handTransform.position = camera.GetOrigin() + camera.GetForward() * 7.0f;
					handTransform.rotation = glm::eulerAngleY(camera.GetRotation().y) *
					                         glm::eulerAngleXYZ(glm::radians(turn.x), glm::radians(turn.y), glm::radians(turn.z));
				}
				else if (chlapi::HandHolding())
				{
					poseName = chlapi::k_HandHoldPose; // held still (below)
				}
				else if (chlapi::HandObjectUnder() != 0)
				{
					poseName = "can_pickup";
				}
				else
				{
					poseName = "wiggle"; // the original's hand at rest: its fingers wiggling
				}
				// Played over its own length (the original's CHand runs its animation's clock, CycleTime), from its
				// start each time the pose changes.
				static std::string s_playing;
				static auto s_since = std::chrono::steady_clock::now();
				if (s_testPose == nullptr)
				{
					const auto now = std::chrono::steady_clock::now();
					if (poseName != s_playing)
					{
						s_playing = poseName;
						s_since = now;
					}
					poseFrame = poseName.empty() ? 0.0f
					                             : handposes::FrameAt(poseName, std::chrono::duration<float>(now - s_since).count());
					if (chlapi::HandHolding())
					{
						poseFrame = chlapi::HandHoldOpenness() * static_cast<float>(handposes::FrameCount(poseName)) / 2.0f;
					}
				}
				const auto& handMesh = *Locator::resources::value().GetMeshes().Handle(ecs::components::Hand::k_MeshId);
				// It leans as it moves, as the original's (CHand::PrepareForDrawing, HandStateNormal::Update): a point
				// trails the hand across the screen on a spring - pulled toward it at 20 a second squared, its speed
				// kept by 0.03 each second, never more than 80 pixels behind - and how far behind it is plays the
				// pose's "_lr" and "_fb" layers. The original's pixels; this screen is taken as 1920 by 1080 of them (the laptop's, where it was measured).
				static const glm::vec2 k_OriginalScreen(1920.0f, 1080.0f);
				static glm::vec2 s_trail(-1.0f);
				static glm::vec2 s_trailSpeed(0.0f);
				static auto s_lastLean = std::chrono::steady_clock::now();
				const auto leanNow = std::chrono::steady_clock::now();
				const float leanSeconds = std::min(std::chrono::duration<float>(leanNow - s_lastLean).count(), 0.1f);
				s_lastLean = leanNow;
				const glm::vec2 pointer = s_handScreen * k_OriginalScreen;
				if (s_trail.x < 0.0f)
				{
					s_trail = pointer;
				}
				s_trail += s_trailSpeed * leanSeconds;
				s_trail = glm::clamp(s_trail, pointer - glm::vec2(80.0f), pointer + glm::vec2(80.0f));
				s_trailSpeed = (s_trailSpeed + (pointer - s_trail) * 20.0f * leanSeconds) * std::pow(0.03f, leanSeconds);
				const glm::vec2 lean(s_trail.x - pointer.x, pointer.y - s_trail.y);
				auto pose = poseName.empty()     ? std::vector<glm::mat4> {}
				            : s_testPose != nullptr ? handposes::LocalBones(handMesh, poseName, poseFrame)
				                                    : handposes::LeaningBones(handMesh, poseName, poseFrame, lean);
				// Held things are held beside the hand, between thumb and forefinger (hold_side), and hang under the
				// pointer (HandCarry): the hand moves aside so that grip is over them, as the original puts the hand
				// where its grip meets what it holds (ObtainRequiredHandPosition). The grip: midway between the
				// thumb's tip (bone 21) and the forefinger's (19).
				constexpr size_t k_ThumbTip = 21;
				constexpr size_t k_ForefingerTip = 19;
				if (chlapi::HandHolding() && s_testPose == nullptr && pose.size() > k_ThumbTip)
				{
					const auto& parents = handMesh.GetBoneParents();
					std::vector<glm::mat4> model(pose.size());
					for (size_t i = 0; i < pose.size(); ++i)
					{
						model[i] = parents[i] != std::numeric_limits<uint32_t>::max() ? model[parents[i]] * pose[i] : pose[i];
					}
					const glm::vec3 grip = 0.5f * (glm::vec3(model[k_ThumbTip][3]) + glm::vec3(model[k_ForefingerTip][3]));
					auto& handTransform = registry.Get<ecs::components::Transform>(handEntity);
					const glm::vec3 gripOffset = handTransform.rotation * (handTransform.scale * grip);
					handTransform.position.x = intersectionTransform.position.x - gripOffset.x;
					handTransform.position.z = intersectionTransform.position.z - gripOffset.z;
				}
				if (pose.empty())
				{
					registry.Remove<ecs::components::Animated>(handEntity);
				}
				else
				{
					registry.AssignOrReplace<ecs::components::Animated>(handEntity, 0u, 0.0f, std::move(pose));
				}
			}
		}

		// The advisors, in front of the camera as it is now.
		{
			static auto s_lastAdvisors = std::chrono::steady_clock::now();
			const auto now = std::chrono::steady_clock::now();
			// Test hook: OPENBLACK_TEST_ADVISORS=<seconds> brings both in that long after starting, where Land 1's
			// opening first puts them (FLY_SPIRIT 0.2/0.8, 0.75), the Demon pointing after two more seconds.
			static const char* s_testAdvisors = std::getenv("OPENBLACK_TEST_ADVISORS");
			static const auto s_started = now;
			static int s_testStep = 0;
			if (s_testAdvisors != nullptr)
			{
				const float since = std::chrono::duration<float>(now - s_started).count();
				const float at = static_cast<float>(std::atof(s_testAdvisors));
				if (s_testStep == 0 && since > at)
				{
					helpspirits::Eject(helpspirits::Spirit::Good);
					helpspirits::Eject(helpspirits::Spirit::Evil);
					helpspirits::FlyTo(helpspirits::Spirit::Good, 0.8f, 0.75f);
					helpspirits::FlyTo(helpspirits::Spirit::Evil, 0.2f, 0.75f);
					s_testStep = 1;
				}
				if (s_testStep == 1 && since > at + 4.0f)
				{
					helpspirits::PointAt(helpspirits::Spirit::Evil, Locator::camera::value().GetFocus());
					s_testStep = 2;
				}
			}
			helpspirits::Update(std::chrono::duration<float>(now - s_lastAdvisors).count());
			// Test hook: OPENBLACK_HEIGHT_PROBE=<file of "x y z" lines> logs the land's height at each, and a cell to each side.
			static bool s_probed = false;
			if (const char* probe = std::getenv("OPENBLACK_HEIGHT_PROBE"); probe != nullptr && !s_probed)
			{
				s_probed = true;
				std::ifstream in(probe);
				float x = 0.0f;
				float y = 0.0f;
				float z = 0.0f;
				auto& land = Locator::terrainSystem::value();
				while (in >> x >> y >> z)
				{
					SPDLOG_LOGGER_INFO(spdlog::get("game"), "Probe ({:.1f}, {:.2f}, {:.1f}): here {:.2f}, x-10 {:.2f}, x+10 {:.2f}, z-10 {:.2f}, z+10 {:.2f}, -10-10 {:.2f}, +10+10 {:.2f}", x, y, z,
					                   land.GetHeightAt({x, z}), land.GetHeightAt({x - 10, z}), land.GetHeightAt({x + 10, z}),
					                   land.GetHeightAt({x, z - 10}), land.GetHeightAt({x, z + 10}), land.GetHeightAt({x - 10, z - 10}),
					                   land.GetHeightAt({x + 10, z + 10}));
				}
			}
			s_lastAdvisors = now;
		}

		// Update Entities
		{
			auto updateEntities = profiler.BeginScoped(Profiler::Stage::UpdateEntities);
			if (config.drawEntities)
			{
				Locator::rendereringSystem::value().PrepareDraw(config.drawBoundingBoxes, config.drawFootpaths,
				                                                config.drawStreams);
			}
		}
	} // Update Uniforms

	// Update Audio
	{
		auto updateAudio = profiler.BeginScoped(Profiler::Stage::UpdateAudio);
		Locator::audio::value().Update();
	} // Update Audio

	return config.numFramesToSimulate == 0 || _frameCount < config.numFramesToSimulate;
}

namespace
{
// While the game loads, a frame now and then saying so (it opened on a black screen for half a minute): a line of
// bgfx's debug text and a bar, drawn at each stage of loading - about a dozen frames, a few milliseconds each.
void ShowLoading(std::string_view what, float fraction)
{
	if (Locator::config::value().graphicsBackend == GraphicsBackend::Noop || !Locator::windowing::has_value())
	{
		return;
	}
	SPDLOG_LOGGER_DEBUG(spdlog::get("game"), "Loading: {} ({:.0f}%)", what, fraction * 100.0f);
	const auto size = Locator::windowing::value().GetSize();
	bgfx::setViewRect(0, 0, 0, static_cast<uint16_t>(size.x), static_cast<uint16_t>(size.y));
	bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x000000ff, 1.0f, 0);
	// The original's loading screen (LoadingScreen.cpp): its tip and its bar sweeping, drawn each time loading looks up.
	loading::Draw();
}
} // namespace

bool Game::Initialize() noexcept
{
	auto& config = Locator::config::value();

	if (config.graphicsBackend != GraphicsBackend::Noop)
	{
		uint32_t extraFlags = 0;
		if (config.graphicsBackend == GraphicsBackend::Metal)
		{
			extraFlags |= SDL_WINDOW_METAL;
		}
		openblack::InitializeWindow(k_WindowTitle, config.resolution.x, config.resolution.y, config.displayMode, extraFlags);
	}

	using filesystem::Path;
	if (!InitializeEngine(config.graphicsBackend, config.vsync))
	{
		SPDLOG_LOGGER_CRITICAL(spdlog::get("game"), "Failed to initialize engine services.");
		return false;
	}
	auto& fileSystem = Locator::filesystem::value();
	auto& events = Locator::events::value();

	events.AddHandler(std::function([this, &config](const SDL_Event& event) {
		// If gui captures this input, do not propagate
		if (!Locator::debugGui::value().ProcessEvents(event))
		{
			config.running = this->ProcessEvents(event);
			Locator::gameActionSystem::value().ProcessEvent(event);
		}
	}));

	if (!fileSystem.IsPathValid(_gamePath))
	{
		// no key, don't guess, let the user know to set the command param
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Game Path missing",
		                         "Game path was not supplied, use the -g "
		                         "command parameter to set it.",
		                         nullptr);
		SPDLOG_LOGGER_ERROR(spdlog::get("game"), "Failed to find the GameDir.");
		return false;
	}

	fileSystem.SetGamePath(_gamePath);

	SPDLOG_LOGGER_DEBUG(spdlog::get("game"), "The GamePath is \"{}\".", fileSystem.GetGamePath().generic_string());

	if (std::filesystem::path(_startMap).is_absolute())
	{
		if (std::find(_startMap.begin(), _startMap.end(), "Scripts") != _startMap.end())
		{
			auto p = _startMap;
			while (p.filename() != "Scripts" && p != p.parent_path())
			{
				p = p.parent_path();
			}
			fileSystem.AddAdditionalPath(p.parent_path());
		}
		else
		{
			fileSystem.AddAdditionalPath(_startMap.parent_path());
		}
	}
	else
	{
		_startMap = fileSystem.GetPath<Path::Scripts>() / _startMap;
	}

	ShowLoading("Starting up", 0.02f);
	if (!InitializeGame())
	{
		SPDLOG_LOGGER_CRITICAL(spdlog::get("game"), "Failed to initialize game services.");
		return false;
	}
	ShowLoading("The Temple", 0.08f);

	auto& resources = Locator::resources::value();
	auto& meshManager = resources.GetMeshes();
	auto& textureManager = resources.GetTextures();
	auto& animationManager = resources.GetAnimations();
	auto& levelManager = resources.GetLevels();
	auto& soundManager = resources.GetSounds();
	auto& glowManager = resources.GetGlows();

	fileSystem.Iterate(
	    fileSystem.GetPath<Path::Citadel>() / "OutsideMeshes", false, [&meshManager](const std::filesystem::path& f) {
		    if (f.extension() == ".zzz")
		    {
			    SPDLOG_LOGGER_DEBUG(spdlog::get("game"), "Loading temple mesh: {}", f.stem().string());
			    try
			    {
				    meshManager.Load(fmt::format("temple/{}", f.stem().string()), resources::L3DLoader::FromDiskTag {}, f);
			    }
			    catch (std::runtime_error& err)
			    {
				    SPDLOG_LOGGER_ERROR(spdlog::get("game"), "{}", err.what());
			    }
		    }
	    });

	fileSystem.Iterate( //
	    fileSystem.GetPath<filesystem::Path::Citadel>() / "engine", false,
	    [&meshManager, &glowManager](const std::filesystem::path& f) {
		    ShowLoading("The Temple", 0.08f);
		    if (f.extension() == ".zzz")
		    {
			    if (f.stem().string().ends_with("lo_l3d"))
			    {
				    SPDLOG_LOGGER_WARN(
				        spdlog::get("game"),
				        "Skipping lo duplicate lo meshes. See https://github.com/openblack/openblack/issues/727");
				    return;
			    }
			    SPDLOG_LOGGER_DEBUG(spdlog::get("game"), "Loading interior temple mesh: {}", f.stem().string());
			    try
			    {
				    meshManager.Load(fmt::format("temple/interior/{}", f.stem().string()), resources::L3DLoader::FromDiskTag {},
				                     f);
			    }
			    catch (std::runtime_error& err)
			    {
				    SPDLOG_LOGGER_ERROR(spdlog::get("game"), "{}", err.what());
			    }
		    }
		    else if (f.extension() == ".glw")
		    {
			    SPDLOG_LOGGER_DEBUG(spdlog::get("game"), "Loading interior temple glows: {}", f.stem().string());
			    try
			    {
				    glowManager.Load(fmt::format("temple/interior/glow/{}", f.stem().string()),
				                     resources::LightLoader::FromDiskTag {}, f);
			    }
			    catch (std::runtime_error& err)
			    {
				    SPDLOG_LOGGER_ERROR(spdlog::get("game"), "{}", err.what());
			    }
		    }
	    });

	ShowLoading("Models", 0.12f);
	pack::PackFile pack;

	auto packResult = pack.ReadFile(*fileSystem.GetData(fileSystem.GetPath<Path::Data>() / "AllMeshes.g3d"));
	if (packResult != pack::PackResult::Success)
	{
		SPDLOG_LOGGER_CRITICAL(spdlog::get("game"), "Unable to load AllMeshes.g3d: {}", pack::ResultToStr(packResult));
		return false;
	}

	const auto& meshes = pack.GetMeshes();
	// TODO (#749) use std::views::enumerate
	for (size_t i = 0; const auto& mesh : meshes)
	{
		const auto meshId = static_cast<MeshId>(i);
		meshManager.Load(meshId, resources::L3DLoader::FromBufferTag {}, k_MeshNames.at(i), mesh);
		++i;
		if (i % 128 == 0)
		{
			ShowLoading("Models", 0.12f + 0.28f * static_cast<float>(i) / static_cast<float>(meshes.size()));
		}
	}

	ShowLoading("Textures", 0.40f);
	const auto& textures = pack.GetTextures();
	for (auto const& [name, g3dTexture] : textures)
	{
		ShowLoading("Textures", 0.40f); // the loading screen's sweep kept going (it draws at most every 150 ms)
		textureManager.Load(g3dTexture.header.id, resources::Texture2DLoader::FromPackTag {}, name, g3dTexture);
	}

	ShowLoading("Animations", 0.48f);
	pack::PackFile animationPack;
	packResult = animationPack.ReadFile(*fileSystem.GetData(fileSystem.GetPath<Path::Data>() / "AllAnims.anm"));
	if (packResult != pack::PackResult::Success)
	{
		SPDLOG_LOGGER_CRITICAL(spdlog::get("game"), "Unable to load AllAnims.anm: {}", pack::ResultToStr(packResult));
		return false;
	}

	const auto& animations = animationPack.GetAnimations();
	// TODO (#749) use std::views::enumerate
	for (size_t i = 0; i < animations.size(); i++)
	{
		ShowLoading("Animations", 0.48f);
		animationManager.Load(i, resources::L3DAnimLoader::FromBufferTag {}, animations[i]);
	}

	fileSystem.Iterate(fileSystem.GetPath<Path::CreatureMesh>(), false, [&meshManager](const std::filesystem::path& f) {
		ShowLoading("Creatures", 0.5f);
		const auto& fileName = f.stem().string();
		SPDLOG_LOGGER_DEBUG(spdlog::get("game"), "Loading creature mesh: {}", fileName);
		try
		{
			if (string_utils::BeginsWith(fileName, "Hand"))
			{
				return;
			}

			const auto meshId = creature::GetIdFromMeshName(fileName);
			meshManager.Load(meshId, resources::L3DLoader::FromDiskTag {}, f);
		}
		catch (std::runtime_error& err)
		{
			SPDLOG_LOGGER_ERROR(spdlog::get("game"), "{}", err.what());
		}
	});

	// Load loose one-off assets
	{
		using AFromDiskTag = resources::L3DAnimLoader::FromDiskTag;
		animationManager.Load("coffre", AFromDiskTag {}, fileSystem.GetPath<Path::Misc>() / "coffre.anm");
		// The opening's hand (runblack.exe 1.20's JCMisc.cpp Intro): picking the boy out of the sea, putting him down.
		animationManager.Load("hand_intro", AFromDiskTag {}, fileSystem.GetPath<Path::Misc>() / "hand_intro.anm");
		animationManager.Load("hand_intro2", AFromDiskTag {}, fileSystem.GetPath<Path::Misc>() / "hand_intro2.anm");

		using LFromDiskTag = resources::L3DLoader::FromDiskTag;
		meshManager.Load("hand", LFromDiskTag {}, fileSystem.GetPath<Path::CreatureMesh>() / "Hand_Boned_Base2.l3d");
		handposes::Load();
		helpspirits::Load();
		meshManager.Load("coffre", LFromDiskTag {}, fileSystem.GetPath<Path::Misc>() / "coffre.l3d");
		meshManager.Load("hand_intro", LFromDiskTag {}, fileSystem.GetPath<Path::Misc>() / "hand_intro.l3d");
		meshManager.Load("cone", LFromDiskTag {}, fileSystem.GetPath<Path::Data>() / "cone.l3d");
		meshManager.Load("marker", LFromDiskTag {}, fileSystem.GetPath<Path::Data>() / "marker.l3d");
		meshManager.Load("river", LFromDiskTag {}, fileSystem.GetPath<Path::Data>() / "river.l3d");
		meshManager.Load("river2", LFromDiskTag {}, fileSystem.GetPath<Path::Data>() / "river2.l3d");
		meshManager.Load("metre_sphere", LFromDiskTag {}, fileSystem.GetPath<Path::Data>() / "metre_sphere.l3d");
		// The spells' own models (Data/Spells/Meshes): the portal's Vortex, the miracle sphere's Seed... as spell/<name>.
		fileSystem.Iterate(fileSystem.GetPath<Path::Data>() / "Spells" / "Meshes", false, [&meshManager](const std::filesystem::path& f) {
			ShowLoading("Miracles", 0.55f);
			if (string_utils::LowerCase(f.extension().string()) != ".l3d")
			{
				return;
			}
			try
			{
				meshManager.Load(fmt::format("spell/{}", string_utils::LowerCase(f.stem().string())), LFromDiskTag {}, f);
				SPDLOG_LOGGER_INFO(spdlog::get("game"), "Spell mesh spell/{} loaded", string_utils::LowerCase(f.stem().string()));
			}
			catch (std::exception& e)
			{
				SPDLOG_LOGGER_WARN(spdlog::get("game"), "Spell mesh {} not loaded: {}", f.string(), e.what());
			}
		});
	}

	// TODO(raffclar): #400: Parse level files within the resource loader
	// TODO(raffclar): #405: Determine campaign levels from the challenge script file
	// Load the campaign levels
	fileSystem.Iterate(fileSystem.GetPath<Path::Scripts>(), false, [&levelManager](const std::filesystem::path& f) {
		const auto& name = f.stem().string();
		if (f.extension() != ".txt" || name.rfind("InfoScript", 0) != std::string::npos)
		{
			return;
		}
		SPDLOG_LOGGER_DEBUG(spdlog::get("game"), "Loading campaign level: {}", f.stem().string());
		try
		{
			if (Level::IsLevelFile(f))
			{
				levelManager.Load(fmt::format("campaign/{}", name), resources::LevelLoader::FromDiskTag {}, f,
				                  Level::LandType::Campaign);
			}
		}
		catch (std::runtime_error& err)
		{
			SPDLOG_LOGGER_ERROR(spdlog::get("game"), "{}", err.what());
		}
	});
	// Load Playgrounds
	// Attempt to load additional levels as playgrounds
	fileSystem.Iterate(fileSystem.GetPath<Path::Playgrounds>(), false, [&levelManager](const std::filesystem::path& f) {
		if (f.extension() != ".txt")
		{
			return;
		}
		const auto& name = f.stem().string();
		if (levelManager.Contains(fmt::format("playgrounds/{}", name)))
		{
			// Already added
			return;
		}

		SPDLOG_LOGGER_DEBUG(spdlog::get("game"), "Loading custom level: {}", f.stem().string());
		try
		{
			if (Level::IsLevelFile(f))
			{
				levelManager.Load(fmt::format("playgrounds/{}", name), resources::LevelLoader::FromDiskTag {}, f,
				                  Level::LandType::Skirmish);
			}
		}
		catch (std::runtime_error& err)
		{
			SPDLOG_LOGGER_ERROR(spdlog::get("game"), "{}", err.what());
		}
	});

	// Load all sound packs in the Audio directory
	ShowLoading("Sounds", 0.60f);
	auto& audioManager = Locator::audio::value();
	fileSystem.Iterate(
	    fileSystem.GetPath<Path::Audio>(), true, [&audioManager, &soundManager, &fileSystem](const std::filesystem::path& f) {
		    ShowLoading("Sounds", 0.60f);
		    if (f.extension() != ".sad")
		    {
			    return;
		    }

		    pack::PackFile soundPack;
		    SPDLOG_LOGGER_DEBUG(spdlog::get("audio"), "Opening sound pack {}", f.filename().string());
		    const auto result = soundPack.ReadFile(*fileSystem.GetData(f));
		    if (result != pack::PackResult::Success)
		    {
			    SPDLOG_LOGGER_ERROR(spdlog::get("game"), "Unable to load sound pack {}: {}", f.filename().string(),
			                        pack::ResultToStr(result));
			    return;
		    }
		    const auto& audioHeaders = soundPack.GetAudioSampleHeaders();
		    const auto& audioData = soundPack.GetAudioSamplesData();
		    auto soundName = std::filesystem::path(audioHeaders[0].name.data());

		    if (audioHeaders.empty())
		    {
			    SPDLOG_LOGGER_WARN(spdlog::get("audio"), "Empty sound pack found for {}. Skipping", f.filename().string());
			    return;
		    }

		    auto groupName = f.filename().string();

		    // A hacky way of detecting if the sound is music as all music sounds end with "mpg"
		    if (soundName.extension() == ".mpg")
		    {
			    auto buffers = std::queue<std::vector<uint8_t>>();
			    auto packName = f.string();
			    audioManager.AddMusicEntry(packName);
		    }
		    else
		    {
			    audioManager.CreateSoundGroup(groupName);
			    for (size_t i = 0; i < audioHeaders.size(); i++)
			    {
				    soundName = std::filesystem::path(audioHeaders[i].name.data());
				    if (audioData[i].empty())
				    {
					    SPDLOG_LOGGER_WARN(spdlog::get("audio"), "Empty sound buffer found for {}. Skipping",
					                       soundName.string());
					    continue; // just this sample: `return` dropped the rest of the pack (InGame.sad starts with an empty one)
				    }

				    const auto stringId = fmt::format("{}/{}", groupName, audioHeaders[i].id);
				    const entt::id_type id = entt::hashed_string(stringId.c_str());
				    const std::vector<std::vector<uint8_t>> buffer = {audioData[i]};
				    SPDLOG_LOGGER_DEBUG(spdlog::get("audio"), "Loading sound {}: {}", stringId, audioHeaders[i].name.data());
				    soundManager.Load(id, resources::SoundLoader::FromBufferTag {}, audioHeaders[i], buffer);
				    audioManager.AddToSoundGroup(groupName, id);
			    }
		    }
	    });

	{
		InfoFile infoFile;
		auto result = infoFile.LoadFromFile(Locator::filesystem::value().GetPath<filesystem::Path::Scripts>() / "info.dat");
		if (!result)
		{
			SPDLOG_LOGGER_ERROR(spdlog::get("game"), "Failed to load game info data.");
			return false;
		}
		Locator::infoConstants::reset(result.release());
		// OPENBLACK_TEST_MUSIC=<sound pack>: play it as music at once, as START_MUSIC does - to try a pack that crashed
		// the Switch (Land 1's MissionariesVerse1.sad) on the laptop.
		if (const char* music = std::getenv("OPENBLACK_TEST_MUSIC"); music != nullptr && Locator::audio::has_value())
		{
			SPDLOG_LOGGER_INFO(spdlog::get("audio"), "Test: playing {} as music", music);
			Locator::audio::value().PlayMusic(music, audio::PlayType::Once);
		}
		// OPENBLACK_DUMP_BELIEF=1: the original's belief and impressiveness numbers from info.dat, then quit - for
		// building town conversion on the game's own values.
		// OPENBLACK_DUMP_CREATURE=1: info.dat's creature entries (ages, and the 416 bytes not yet named) as floats, then quit.
		// OPENBLACK_DUMP_VILLAGER=1: info.dat's villager-life numbers (state life drain, home/sleep thresholds, the town's
		// bed time and the sleep desire's trigger), then quit - for the original's villager life (bw1-decomp).
		// OPENBLACK_DUMP_POSES=<file in Data/CTR>: each creature animation's CBN header (duration ms, movement).
		if (const char* posesFile = std::getenv("OPENBLACK_DUMP_POSES"); posesFile != nullptr)
		{
			morphposes::Poses poses;
			morphposes::Load(Locator::filesystem::value().GetPath<filesystem::Path::Data>() / "CTR" / posesFile, "Creature", poses);
			for (const auto& [name, pose] : poses)
			{
				const auto& h = pose.header;
				SPDLOG_LOGGER_INFO(spdlog::get("game"), "POSE {} ms={} f4={} f8={:.3f} fc={:.3f} move=({:.3f},{:.3f},{:.3f}) frames={}/{}",
				                   name, h.unknown0x0, h.unknown0x4, h.unknown0x8, h.unknown0xc, h.unknown0x10, h.unknown0x14,
				                   h.unknown0x18, h.frameCount, pose.keyframes.size());
			}
			// ...and with OPENBLACK_DUMP_POSES_SPECIES=<CreatureType>, its base mesh's bones in the model at rest and in
			// stand and walk, posed the old way (LocalBones) and the original's (CreatureBones): BONE lines to draw.
			if (const char* speciesText = std::getenv("OPENBLACK_DUMP_POSES_SPECIES"); speciesText != nullptr)
			{
				const auto meshId = creature::GetIdFromType(static_cast<CreatureType>(std::atoi(speciesText)),
				                                            creature::CreatureBody::Appearance::Base);
				const auto& meshes = Locator::resources::value().GetMeshes();
				if (meshes.Contains(meshId))
				{
					const auto& mesh = *meshes.Handle(meshId);
					const auto& parents = mesh.GetBoneParents();
					const auto stand = poses.find("stand");
					auto dump = [&](const char* method, const std::string& name, int frame, std::vector<glm::mat4> local) {
						for (size_t i = 0; i < local.size(); ++i)
						{
							if (parents[i] != std::numeric_limits<uint32_t>::max())
							{
								local[i] = local[parents[i]] * local[i];
							}
							SPDLOG_LOGGER_INFO(spdlog::get("game"), "BONE {} {} {} {} {} {:.3f} {:.3f} {:.3f}", method, name, frame, i,
							                   static_cast<int32_t>(parents[i]), local[i][3].x, local[i][3].y, local[i][3].z);
						}
					};
					dump("rest", "rest", 0, [&] {
						auto local = mesh.GetBoneMatrices();
						for (size_t i = local.size(); i-- > 0;)
						{
							if (parents[i] != std::numeric_limits<uint32_t>::max())
							{
								local[i] = glm::inverse(mesh.GetBoneMatrices()[parents[i]]) * local[i];
							}
						}
						return local;
					}());
					if (const auto walk = poses.find("walk"); walk != poses.end() && stand != poses.end())
					{
						for (const int percent : {25, 50, 75})
						{
							const float w = static_cast<float>(percent) / 100.0f;
							dump("blend", fmt::format("w{}", percent), 3,
							     morphposes::BlendedCreatureBones(mesh, {{&stand->second, 0.0f, 1.0f - w}, {&walk->second, 3.0f, w}},
							                                      &stand->second));
						}
					}
					for (const char* name : {"stand", "walk", "run"})
					{
						const auto pose = poses.find(name);
						if (pose == poses.end())
						{
							continue;
						}
						for (int frame = 0; frame < static_cast<int>(pose->second.keyframes.size()); ++frame)
						{
							dump("old", name, frame, morphposes::LocalBones(mesh, pose->second, static_cast<float>(frame), true));
							dump("new", name, frame,
							     morphposes::CreatureBones(mesh, pose->second, static_cast<float>(frame),
							                               stand != poses.end() ? &stand->second : nullptr));
						}
					}
				}
			}
		}
		// OPENBLACK_DUMP_HAND_POSES=1: the hand's bones in every pose of Data/CTR/hh.HBN, at its first and last frames,
		// posed the old way (LocalBones) and the original's (CreatureBones): BONE lines to draw.
		if (std::getenv("OPENBLACK_DUMP_HAND_POSES") != nullptr)
		{
			morphposes::Poses poses;
			morphposes::Load(Locator::filesystem::value().GetPath<filesystem::Path::Data>() / "CTR" / "hh.HBN", "Hand", poses);
			const auto& mesh = *Locator::resources::value().GetMeshes().Handle(ecs::components::Hand::k_MeshId);
			const auto& parents = mesh.GetBoneParents();
			auto dump = [&](const char* method, const std::string& name, int frame, std::vector<glm::mat4> local) {
				for (size_t i = 0; i < local.size(); ++i)
				{
					if (parents[i] != std::numeric_limits<uint32_t>::max())
					{
						local[i] = local[parents[i]] * local[i];
					}
					SPDLOG_LOGGER_INFO(spdlog::get("game"), "BONE {} {} {} {} {} {:.3f} {:.3f} {:.3f}", method, name, frame, i,
					                   static_cast<int32_t>(parents[i]), local[i][3].x, local[i][3].y, local[i][3].z);
				}
				std::string bones;
				for (size_t i = 0; i < local.size(); ++i)
				{
					bones += fmt::format("{}{} {} {} {} {} {} {} {} {} {} {} {}", i == 0 ? "" : " | ", local[i][0].x, local[i][0].y,
					                     local[i][0].z, local[i][1].x, local[i][1].y, local[i][1].z, local[i][2].x, local[i][2].y,
					                     local[i][2].z, local[i][3].x, local[i][3].y, local[i][3].z);
				}
				SPDLOG_LOGGER_INFO(spdlog::get("game"), "MAT {}:{}:{} bones={}", method, name, frame, bones);
			};
			// ...every pose between its frames, a tenth of a frame apart (OPENBLACK_DUMP_HAND_POSES=between).
			if (std::string_view(std::getenv("OPENBLACK_DUMP_HAND_POSES")) == "between")
			{
				for (const auto& [name, pose] : poses)
				{
					for (int tenth = 0; tenth <= static_cast<int>(pose.keyframes.size() - 1) * 10; ++tenth)
					{
						dump("between", fmt::format("{}:{:.1f}", name, static_cast<float>(tenth) / 10.0f), tenth,
						     handposes::LocalBones(mesh, name, static_cast<float>(tenth) / 10.0f));
					}
				}
			}
			// ...and every pose with lean layers, over a grid of leans (OPENBLACK_DUMP_HAND_POSES=lean).
			if (std::string_view(std::getenv("OPENBLACK_DUMP_HAND_POSES")) == "lean")
			{
				for (const auto& [name, pose] : poses)
				{
					if (!poses.contains(name + "_lr") && !poses.contains(name + "_fb"))
					{
						continue;
					}
					for (int frame = 0; frame < static_cast<int>(pose.keyframes.size()); ++frame)
					{
						for (int lx = -80; lx <= 80; lx += 20)
						{
							for (int ly = -80; ly <= 80; ly += 20)
							{
								dump("lean", fmt::format("{}:{}:{}", name, lx, ly), frame,
								     handposes::LeaningBones(mesh, name, static_cast<float>(frame), glm::vec2(lx, ly)));
							}
						}
					}
				}
			}
			for (int variant = 0; variant < 4; ++variant)
			{
				morphposes::Poses variantPoses;
				morphposes::Load(Locator::filesystem::value().GetPath<filesystem::Path::Data>() / "CTR" / "hh.HBN", "Hand",
				                 variantPoses, variant);
				for (const auto& [name, pose] : variantPoses)
				{
					for (int frame = 0; frame < static_cast<int>(pose.keyframes.size()); ++frame)
					{
						dump(fmt::format("var{}", variant).c_str(), name, frame,
						     morphposes::CreatureBones(mesh, pose, static_cast<float>(frame), nullptr));
					}
				}
			}
			for (const auto& [name, pose] : poses)
			{
				const auto& h = pose.header;
				SPDLOG_LOGGER_INFO(spdlog::get("game"), "HANDPOSE {} ms={} cyclic={} frames={}", name, h.unknown0x0, h.unknown0x4,
				                   pose.keyframes.size());
				for (int frame = 0; frame < static_cast<int>(pose.keyframes.size()); ++frame)
				{
					dump("old", name, frame, morphposes::LocalBones(mesh, pose, static_cast<float>(frame), false));
					dump("new", name, frame, morphposes::CreatureBones(mesh, pose, static_cast<float>(frame), nullptr));
				}
			}
		}

		// OPENBLACK_DUMP_SPELLSEEDS=1: each spell seed's model (info +0x140) and particle effect type (+0x174).
		if (std::getenv("OPENBLACK_DUMP_SPELLSEEDS") != nullptr)
		{
			const auto& info = Locator::infoConstants::value();
			for (size_t i = 0; i < info.spellSeed.size(); ++i)
			{
				const auto& seed = info.spellSeed[i];
				SPDLOG_LOGGER_INFO(spdlog::get("game"), "SPELLSEED {} mesh {} particle {} (f0x134 {:.2f} f0x150 {:.2f} f0x154 {:.2f})", i,
				                   seed.field0x130, seed.field0x164, seed.field0x134, seed.field0x150, seed.field0x154);
			}
		}
		if (std::getenv("OPENBLACK_DUMP_VILLAGER") != nullptr)
		{
			const auto& info = Locator::infoConstants::value();
			for (size_t i = 0; i < info.villagerStateTable.size(); i++)
			{
				const auto& state = info.villagerStateTable.at(i);
				if (state.name[0] == '\0')
				{
					continue;
				}
				SPDLOG_LOGGER_INFO(spdlog::get("game"), "VSTATE {} {} drain {:.5f} checks {} hurtHome {} worship {} final {} speedId {} anim {}", i,
				                   state.name.data(), state.field0xf8, state.field0xe4, state.field0xe8, state.field0xd8, state.isFinalState,
				                   state.field0x24, state.field0x0);
			}
			for (size_t i = 0; i < info.villager.size(); i++)
			{
				const auto& v = info.villager.at(i);
				SPDLOG_LOGGER_INFO(spdlog::get("game"),
				                   "VINFO {} tribe {} number {} life {:.3f} goHome {:.3f} sleepUntil {:.3f} restTime {} restBy {:.4f} hungry {:.3f} "
				                   "starving {:.3f} ownDesire {:.3f} waitAtHome {}",
				                   i, static_cast<int>(v.tribeType), static_cast<int>(v.villagerNumber), v.life, v.damageThresholdToGoHome,
				                   v.damageThresholdToSleepUntil, v.restAtHomeTime, v.restAtHomeRestoresLifeBy, v.hungryForFood, v.starvingForFood,
				                   v.ownDesireThreshold, v.waitAtHome);
				const auto& sp = v.speedGroup;
				SPDLOG_LOGGER_INFO(spdlog::get("game"),
				                   "VSPEED {} speeds {} {} {} {} {} {} (m/s x100/65536) townNeeds base {:.3f} div {:.3f} fullWood {:.3f} fullFood {:.3f} "
				                   "walksWounded {:.3f} crawls {:.3f} foodPowerup {:.3f} maxWood {} maxFood {}",
				                   i, static_cast<uint32_t>(sp.speedDefault), static_cast<uint32_t>(sp.speedFleeing), static_cast<uint32_t>(sp.speed2),
				                   static_cast<uint32_t>(sp.speed3), static_cast<uint32_t>(sp.speed4), static_cast<uint32_t>(sp.speed5),
				                   v.baseForTownNeedsSpeedMod, v.divisorForTownNeedsSpeedMod, v.speedModWhenFullLoadOfWood, v.speedModWhenFullLoadOfFood,
				                   v.lifeWhenWalksWounded, v.lifeWhenCrawlsWounded, v.foodPowerupIncrease, v.maxWoodCarried, v.maxFoodCarried);
			}
			SPDLOG_LOGGER_INFO(spdlog::get("game"), "TOWN bedTimeMod {:.3f} relaxationMod {:.3f} maxForTimeWillWorkUntil {:.3f} foodWanted {:.3f} beliefSpeed {:.3f} {:.3f} {:.3f} {:.3f} {:.3f} {:.3f} mp {:.3f}",
			                   info.town.bedTimeMod, info.town.relaxationMod, info.town.maxForTimeWillWorkUntil, info.town.foodWantedMultiplier,
			                   info.town.beliefSpeedScaleStory[0], info.town.beliefSpeedScaleStory[1], info.town.beliefSpeedScaleStory[2],
			                   info.town.beliefSpeedScaleStory[3], info.town.beliefSpeedScaleStory[4], info.town.beliefSpeedScaleStory[5],
			                   info.town.beliefSpeedScaleMultiPlayer);
			for (size_t i = 0; i < info.townDesire.size(); i++)
			{
				const auto& d = info.townDesire.at(i);
				SPDLOG_LOGGER_INFO(spdlog::get("game"), "TDESIRE {} trigger {:.3f} emergency {:.3f} tribe {:.2f} {:.2f} {:.2f} {:.2f} {:.2f} {:.2f} {:.2f} {:.2f} {:.2f}", i,
				                   d.desireTriggersVillagerAction, d.desireTriggersVillagerEmergencyAction, d.tribeMultiplier[0], d.tribeMultiplier[1],
				                   d.tribeMultiplier[2], d.tribeMultiplier[3], d.tribeMultiplier[4], d.tribeMultiplier[5], d.tribeMultiplier[6],
				                   d.tribeMultiplier[7], d.tribeMultiplier[8]);
			}
			for (size_t i = 0; i < info.speedThreshold.size(); i++)
			{
				SPDLOG_LOGGER_INFO(spdlog::get("game"), "VTHRESH {} walk max {} run max {}", i, static_cast<uint32_t>(info.speedThreshold[i].speedMaxWalk),
				                   static_cast<uint32_t>(info.speedThreshold[i].speedMaxRun));
			}
			auto& animations = Locator::resources::value().GetAnimations();
			for (uint32_t a = 200; a < 450; a++)
			{
				const auto id = resources::HashIdentifier(a);
				if (!animations.Contains(id))
				{
					continue;
				}
				const auto h = animations.Handle(id);
				SPDLOG_LOGGER_INFO(spdlog::get("game"), "VANIM {} {} duration {} distance {:.3f} frames {} w20 {:#x} w50 {:#x}", a, h->GetName().c_str(),
				                   h->GetDuration(), h->GetDistancePerPlay(), h->GetFrames().size(), h->GetHeaderWord20(), h->GetHeaderWord50());
			}
			std::exit(0);
		}
		if (std::getenv("OPENBLACK_DUMP_CREATURE") != nullptr)
		{
			const auto& info = Locator::infoConstants::value();
			{
				auto& animations = Locator::resources::value().GetAnimations();
				SPDLOG_LOGGER_INFO(spdlog::get("game"), "ANIM dump: 427 there {}", animations.Contains(resources::HashIdentifier(427u)));
				for (uint32_t a : {427u, 428u, 426u, 212u, 213u, 214u, 215u, 216u, 340u, 348u})
				{
					const auto id = resources::HashIdentifier(a);
					if (animations.Contains(id))
					{
						const auto h = animations.Handle(id);
						const auto f = h->GetHeaderFloats();
						const auto& frames = h->GetFrames();
						const auto root0 = frames.empty() || frames.front().bones.empty() ? glm::mat4(1.0f) : frames.front().bones[0];
						const auto root1 = frames.empty() || frames.back().bones.empty() ? glm::mat4(1.0f) : frames.back().bones[0];
						SPDLOG_LOGGER_INFO(spdlog::get("game"), "ANIM {} {} frames {} header {:.3f} {:.3f} {:.3f} {:.3f} {:.3f}", a, h->GetName(), frames.size(), f[0], f[1], f[2], f[3], f[4]);
						SPDLOG_LOGGER_INFO(spdlog::get("game"), "ANIM {} root first {:.2f} {:.2f} {:.2f} last {:.2f} {:.2f} {:.2f} fwd {:.2f} {:.2f} {:.2f}", a, root0[3].x, root0[3].y, root0[3].z, root1[3].x, root1[3].y, root1[3].z, root0[2].x, root0[2].y, root0[2].z);
					}
				}
			}
			{
				const auto& meshes = Locator::resources::value().GetMeshes();
				for (auto m : {MeshId::ObjectLogsInHand, MeshId::ObjectWoodInHand, MeshId::ObjectBranchHardWoodInHand})
				{
					const auto id = resources::HashIdentifier(m);
					if (meshes.Contains(id))
					{
						const auto b = meshes.Handle(id)->GetBoundingBox();
						SPDLOG_LOGGER_INFO(spdlog::get("game"), "WOODMESH {} min ({:.2f},{:.2f},{:.2f}) max ({:.2f},{:.2f},{:.2f})", static_cast<int>(m),
						                   b.minima.x, b.minima.y, b.minima.z, b.maxima.x, b.maxima.y, b.maxima.z);
					}
				}
				const auto villager = resources::HashIdentifier(MeshId::PersonNorseSailor);
				if (meshes.Contains(villager))
				{
					const auto b = meshes.Handle(villager)->GetBoundingBox();
					SPDLOG_LOGGER_INFO(spdlog::get("game"), "WOODMESH villager min ({:.2f},{:.2f},{:.2f}) max ({:.2f},{:.2f},{:.2f})", b.minima.x,
					                   b.minima.y, b.minima.z, b.maxima.x, b.maxima.y, b.maxima.z);
				}
			}
			for (size_t i = 0; i < info.spellSeed.size(); ++i)
			{
				const auto& seed = info.spellSeed.at(i);
				const auto* words = reinterpret_cast<const uint32_t*>(&seed.field0xf0);
				std::string fields;
				for (size_t k = 0; k < 15; ++k)
				{
					fields += fmt::format(" {}", words[k]);
				}
				SPDLOG_LOGGER_INFO(spdlog::get("game"), "SEED {} '{}'{}", i,
				                   std::string(seed.debugString.data(), strnlen(seed.debugString.data(), seed.debugString.size())), fields);
			}
			auto str = [](const auto& a) { return std::string(a.data(), strnlen(a.data(), a.size())); };
			for (size_t i = 0; i < info.creatureAction.size(); ++i)
			{
				SPDLOG_LOGGER_INFO(spdlog::get("game"), "CACTION {} {}", i, str(info.creatureAction.at(i).name));
			}
			for (size_t i = 0; i < info.desireSourceTable.size(); ++i)
			{
				SPDLOG_LOGGER_INFO(spdlog::get("game"), "CSOURCE {} {} | {}", i, str(info.desireSourceTable.at(i).field0x14),
				                   str(info.desireSourceTable.at(i).field0x54));
			}
			for (size_t i = 0; i < info.creatureMimic.size(); ++i)
			{
				SPDLOG_LOGGER_INFO(spdlog::get("game"), "CMIMIC {} {}", i, str(info.creatureMimic.at(i).name));
			}
			for (size_t i = 0; i < info.villager.size(); ++i)
			{
				const auto& v = info.villager.at(i);
				SPDLOG_LOGGER_INFO(spdlog::get("game"), "VILLAGER {} speedDefault {} m/s fleeing {}", i,
				                   GetSpeedStateSpeed(v.speedGroup.speedDefault), GetSpeedStateSpeed(v.speedGroup.speedFleeing));
			}
			for (size_t i = 0; i < info.creature.size(); ++i)
			{
				const auto& c = info.creature.at(i);
				std::string floats;
				for (size_t k = 0; k + 4 <= c.field0x1e4.size(); k += 4)
				{
					float f;
					std::memcpy(&f, c.field0x1e4.data() + k, sizeof(f));
					floats += fmt::format(" {}:{:.4g}", k, f);
				}
				SPDLOG_LOGGER_INFO(spdlog::get("game"), "CREATURE {} type {} life {} strength {} ages {}/{}/{}/{} unnamed{}", i,
				                   static_cast<int>(c.creatureType), c.life, c.strength, c.startAge, c.grownUpAge, c.oldAge,
				                   c.retirementAge, floats);
			}
			return false;
		}
		if (std::getenv("OPENBLACK_DUMP_BELIEF") != nullptr)
		{
			const auto& info = Locator::infoConstants::value();
			const auto& b = info.belief;
			SPDLOG_LOGGER_INFO(spdlog::get("game"),
			                   "BELIEF townDesireBeliefThresholds {} claimedTownBeliefMultiplier {} lostATownBeliefInPlayerMultiplier {} "
			                   "minimumThreshold {} defaultBoredomOfMe {} beliefLeftWhenHelpSpritesWarn {}",
			                   b.townDesireBeliefThresholds, b.claimedTownBeliefMultiplier, b.lostATownBeliefInPlayerMultiplier,
			                   b.minimumThreshold, b.defaultBoredomOfMe, b.beliefLeftWhenHelpSpritesWarn);
			const auto& t = info.town;
			SPDLOG_LOGGER_INFO(spdlog::get("game"),
			                   "TOWN influence {} beliefNeededForWonder {} baseBeliefDesireValue {} beliefInNeutralPlayer {} "
			                   "startingBeliefForCreatedTown {} multiplierForAddingResourceToTown {} populationForUnmodifiedBelief {} "
			                   "oneOffBeliefForCreature {} howImpressedWithFlyingThing {} fightWonImpressiveValue {}",
			                   t.influence, t.beliefNeededForWonder, t.baseBeliefDesireValue, t.beliefInNeutralPlayer,
			                   t.startingBeliefForCreatedTown, t.multiplierForAddingResourceToTown, t.populationForUnmodifiedBelief,
			                   t.oneOffBeliefForCreature, t.howImpressedWithFlyingThing, t.fightWonImpressiveValue);
			for (size_t i = 0; i < t.beliefSpeedScaleStory.size(); ++i)
			{
				SPDLOG_LOGGER_INFO(spdlog::get("game"), "TOWN beliefSpeedScaleStory[{}] {}", i, t.beliefSpeedScaleStory.at(i));
			}
			for (size_t i = 0; i < info.magicEffect.size(); ++i)
			{
				const auto& m = info.magicEffect.at(i);
				SPDLOG_LOGGER_INFO(spdlog::get("game"), "MAGIC {} '{}' impressiveValue {} cpImpressiveBalance {} costToCreate {}", i,
				                   std::string(m.debugString.data(), strnlen(m.debugString.data(), m.debugString.size())),
				                   m.impressiveValue, m.cpImpressiveBalance, m.costToCreate);
			}
			return false;
		}
	}

	ShowLoading("Land textures", 0.80f);
	fileSystem.Iterate(fileSystem.GetPath<Path::Textures>(), false, [&textureManager](const std::filesystem::path& f) {
		if (string_utils::LowerCase(f.extension().string()) == ".raw")
		{
			SPDLOG_LOGGER_DEBUG(spdlog::get("game"), "Loading raw texture: {}", f.stem().string());
			try
			{
				textureManager.Load(fmt::format("raw/{}", f.stem().string()), resources::Texture2DLoader::FromDiskTag {}, f);
			}
			catch (std::runtime_error& err)
			{
				SPDLOG_LOGGER_ERROR(spdlog::get("game"), "{}", err.what());
			}
		}
	});

	return true;
}

bool Game::Run() noexcept
{
	auto& config = Locator::config::value();

	if (!LoadMap(_startMap))
	{
		return false;
	}

	Locator::dynamicsSystem::value().RegisterRigidBodies();

	auto& fileSystem = Locator::filesystem::value();

	if (std::getenv("OPENBLACK_START_UNPAUSED") != nullptr) // temporary diagnostic: headless campaign runs
	{
		_paused = false;
	}

	auto challengePath = fileSystem.GetPath<filesystem::Path::Quests>() / "challenge.chl";
	if (fileSystem.Exists(challengePath))
	{
		auto& chlapi = Locator::chlapi::value();
		auto& lhvm = Locator::vm::value();
		// The error callback was nullptr, so every VM error - an unknown script, a stack fault inside
		// the campaign - vanished silently and the campaign simply never ran (2026-09-23).
		lhvm.Initialise(&chlapi.GetFunctionsTable(), nullptr, nullptr, nullptr,
		                [](lhvm::ErrorCode code, const std::string v0, uint32_t v1) {
			                SPDLOG_LOGGER_ERROR(spdlog::get("scripting"), "LHVM error: {} ({} / {})",
			                                    lhvm::k_ErrorMsg.at(static_cast<size_t>(code)), v0, v1);
		                },
		                nullptr, nullptr);
		try
		{
			lhvm.LoadBinary(fileSystem.ReadAll(challengePath));
			lhvm.StartScript("LandControlAll", lhvm::ScriptType::All);
		}
		catch (const std::runtime_error& err)
		{
			SPDLOG_LOGGER_ERROR(spdlog::get("game"), "Failed to read challenge file at {}: {}",
			                    (fileSystem.GetGamePath() / challengePath).generic_string(), err.what());
		}
	}
	else
	{
		SPDLOG_LOGGER_ERROR(spdlog::get("game"), "Challenge file not found at {}",
		                    (fileSystem.GetGamePath() / challengePath).generic_string());
		return false;
	}

	// Initialize the Acceleration Structure
	Locator::entitiesMap::value().Rebuild();

	if (Locator::windowing::has_value())
	{
		const auto size = static_cast<glm::u16vec2>(Locator::windowing::value().GetSize());
		Locator::rendererInterface::value().ConfigureView(graphics::RenderPass::Main, size, 0x274659ff);
	}

	{
		uint16_t width;
		uint16_t height;
		Locator::oceanSystem::value().GetReflectionFramebuffer().GetSize(width, height);
		Locator::rendererInterface::value().ConfigureView(graphics::RenderPass::Reflection, {width, height}, 0x274659ff);
	}

	if (config.drawIsland)
	{
		uint16_t width;
		uint16_t height;
		Locator::terrainSystem::value().GetFootprintFramebuffer().GetSize(width, height);
		Locator::rendererInterface::value().ConfigureView(graphics::RenderPass::Footprint, {width, height}, 0x00000000);
	}

	Game::SetTime(config.timeOfDay);

	_frameCount = 0;
	auto lastTime = std::chrono::high_resolution_clock::now();
	auto& profiler = Locator::profiler::value();
	loading::Finish(); // the game's first frame is next
	while (Update())
	{
		auto duration = std::chrono::high_resolution_clock::now() - lastTime;
		auto milliseconds = std::chrono::duration_cast<std::chrono::duration<uint32_t, std::milli>>(duration);
		{
			auto section = profiler.BeginScoped(Profiler::Stage::SceneDraw);

			const graphics::RendererInterface::DrawSceneDesc drawDesc {
			    .camera = &Locator::camera::value(),
			    .frameBuffer = nullptr,
			    .entities = Locator::entitiesRegistry::value(),
			    .time = milliseconds.count(), // TODO(#481): get actual time
			    .timeOfDay = config.timeOfDay,
			    .bumpMapStrength = config.bumpMapStrength,
			    .smallBumpMapStrength = config.smallBumpMapStrength,
			    .viewId = graphics::RenderPass::Main,
			    .drawSky = config.drawSky,
			    .drawWater = config.drawWater,
			    .drawIsland = config.drawIsland,
			    .drawEntities = config.drawEntities,
			    .drawSprites = config.drawSprites,
			    .drawBoundingBoxes = config.drawBoundingBoxes,
			    .cullBack = false,
			    .wireframe = config.wireframe,
			};
			Locator::rendererInterface::value().DrawScene(drawDesc);
		}

		// Periodic screenshots (OPENBLACK_SHOT_EVERY seconds into OPENBLACK_SHOT_DIR, emptied at start; the
		// newest k_MaxShots are kept), so runs on a console can be looked at afterwards.
		{
			static const char* shotDir = std::getenv("OPENBLACK_SHOT_DIR");
			static const float shotEvery = std::getenv("OPENBLACK_SHOT_EVERY") != nullptr
			                                   ? static_cast<float>(std::atof(std::getenv("OPENBLACK_SHOT_EVERY")))
			                                   : 0.0f;
			static uint32_t shotNumber = 0;
			static auto nextShot = std::chrono::steady_clock::now();
			// (OPENBLACK_SHOT_KEEP keeps more, to look over a whole cutscene.)
			static const uint32_t k_MaxShots = std::getenv("OPENBLACK_SHOT_KEEP") != nullptr ? static_cast<uint32_t>(std::atoi(std::getenv("OPENBLACK_SHOT_KEEP"))) : 60;
			if (shotDir != nullptr && shotEvery > 0.0f)
			{
				if (shotNumber == 0 && nextShot <= std::chrono::steady_clock::now())
				{
					std::error_code ec;
					std::filesystem::create_directories(shotDir, ec);
					for (const auto& entry : std::filesystem::directory_iterator(shotDir, ec))
					{
						std::filesystem::remove(entry.path(), ec);
					}
				}
				if (std::chrono::steady_clock::now() >= nextShot && !_requestScreenshot.has_value())
				{
					++shotNumber;
					std::error_code ec;
					if (shotNumber > k_MaxShots)
					{
						std::filesystem::remove(std::filesystem::path(shotDir) / fmt::format("shot_{:04}.png", shotNumber - k_MaxShots), ec);
					}
					RequestScreenshot(std::filesystem::path(shotDir) / fmt::format("shot_{:04}.png", shotNumber));
					nextShot = std::chrono::steady_clock::now() +
					           std::chrono::milliseconds(static_cast<int>(shotEvery * 1000.0f));
				}
			}
		}

		{
			auto section = profiler.BeginScoped(Profiler::Stage::GuiDraw);
			const bool screenshotThisFrame = _requestScreenshot.has_value() && _requestScreenshot->first == _frameCount;
			// Skip drawing Debug UI for screenshots (OPENBLACK_SHOT_WITH_GUI keeps it: the subtitles are in it)
			static const bool s_shotWithGui = std::getenv("OPENBLACK_SHOT_WITH_GUI") != nullptr;
			if (screenshotThisFrame)
			{
				SPDLOG_LOGGER_INFO(spdlog::get("game"), "Requesting a screenshot at frame {}...", _frameCount);
				Locator::rendererInterface::value().RequestScreenshot(_requestScreenshot->second);
			}
			if (!screenshotThisFrame || s_shotWithGui)
			{
				Locator::debugGui::value().Draw();
			}
		}

		{
			auto section = profiler.BeginScoped(Profiler::Stage::RendererFrame);
			Locator::rendererInterface::value().Frame();
		}

		// Clear the stale screenshot request
		if (_requestScreenshot.has_value())
		{
			if (_requestScreenshot->first <= _frameCount)
			{
				_requestScreenshot = std::nullopt;
			}
		}

		_frameCount++;
	}

	return true;
}

bool Game::LoadMap(const std::filesystem::path& path) noexcept
{
	auto& fileSystem = Locator::filesystem::value();

	if (!fileSystem.Exists(path))
	{
		SPDLOG_LOGGER_ERROR(spdlog::get("game"), "Could not find script {}", path.generic_string());
		return false;
	}

	const auto data = fileSystem.ReadAll(path);
	const auto source = std::string(reinterpret_cast<const char*>(data.data()), data.size());

	// Reset everything. Deletes all entities and their components
	Locator::entitiesRegistry::value().Reset();
	// TODO(#661): split entities that are permanent from map entities and move hand and camera to init
	// We need a hand for the player
	Locator::handSystem::value().Initialize();

	// create our camera
	auto& config = Locator::config::value();
	const auto aspect = Locator::windowing::has_value() ? Locator::windowing::value().GetAspectRatio() : 1.0f;
	Locator::camera::value().SetProjectionMatrixPerspective(config.cameraXFov, aspect, config.cameraNearClip,
	                                                        config.cameraFarClip);

	ShowLoading("The land", 0.88f);
	Script script;
	script.Load(source);
	ShowLoading("Footpaths", 0.97f);

	// Each released map comes with an optional .fot file which contains the footpath information for the map
	const auto stem = string_utils::LowerCase(path.stem().generic_string());
	const auto fotPath = fileSystem.GetPath<filesystem::Path::Landscape>() / fmt::format("{}.fot", stem);

	if (fileSystem.Exists(fotPath))
	{
		FotFile fotFile(*this);
		fotFile.Load(fotPath);
	}
	else
	{
		SPDLOG_LOGGER_WARN(spdlog::get("game"), "The map at {} does not come with a footpath file. Expected {}",
		                   path.generic_string(), fotPath.generic_string());
	}

	_lastGameLoopTime = std::chrono::steady_clock::now();
	_turnDeltaTime = 0ns;
	SetGameSpeed(Game::k_TurnDurationMultiplierNormal);
	_turnCount = 0;
	_paused = false; // a map starts running (Mike, 2026-10-18: it had started paused)

	return true;
}

void Game::LoadLandscape(const std::filesystem::path& path)
{
	auto& fileSystem = Locator::filesystem::value();

	auto fixedName = fileSystem.FindPath(filesystem::FileSystemInterface::FixPath(path));

	if (!fileSystem.Exists(fixedName))
	{
		throw std::runtime_error("Could not find landscape " + path.generic_string());
	}
	InitializeLevel(fixedName);

	// There is always a player active
	Locator::playerSystem::value().AddPlayer(ecs::archetypes::PlayerArchetype::Create(PlayerNames::PLAYER_ONE));

	// There is always at least one player active.
	ecs::archetypes::PlayerArchetype::Create(PlayerNames::PLAYER_ONE);

	Locator::cameraBookmarkSystem::value().Initialize();
	Locator::dynamicsSystem::value().RegisterIslandRigidBodies(Locator::terrainSystem::value());
	Locator::playerSystem::value().RegisterPlayers();
}

void Game::SetTime(float time) noexcept
{
	Locator::skySystem::value().SetTime(time);
}

void Game::RequestScreenshot(const std::filesystem::path& path) noexcept
{
	_requestScreenshot = std::make_pair(_frameCount, path);
}
