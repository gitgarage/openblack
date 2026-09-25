/******************************************************************************
 * Copyright (c) 2018-2026 openblack developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/openblack/openblack
 *
 * openblack is licensed under the GNU General Public License version 3.
 *******************************************************************************/

#pragma once

#include <chrono>
#include <optional>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <vector>

#include <LHVMTypes.h>

namespace openblack::chlapi
{

using openblack::lhvm::DataType;
using openblack::lhvm::VMValue;

class CHLApi
{
public:
	CHLApi();

	[[nodiscard]] const std::vector<lhvm::NativeFunction>& GetFunctionsTable();

private:
	void InitFunctionsTable0();
	void InitFunctionsTable1();
	void InitFunctionsTable2();
	void InitFunctionsTable3();
	void InitFunctionsTable4();

	std::vector<lhvm::NativeFunction> _functionsTable;
};

// The player clicked a game object (the entity id under the cursor at a left-button press).
// GAME_THING_CLICKED reports it once to the script that asks about that object.
void NotifyGameThingClicked(uint32_t entity);

// Advances a playing hand demo (PLAY_HAND_DEMO) by real time; call once per frame. While one plays it
// drives the camera and returns where the hand is on screen (0..1), which replaces the mouse.
std::optional<glm::vec2> UpdateHandDemo();

// Every frame the game isn't paused: moves livings walking a track (WALK_PATH) at their script speed,
// and flock members after their leader, smoothly rather than a step per game turn.
void UpdateScriptFrame(std::chrono::microseconds dt);

// The player's hand holding a game object (right button, ZL on the Switch): picked up when pressed over an
// object, carried with the hand, put down on the ground under the hand when released.
bool HandPickUp(uint32_t entity);
void HandCarry(const glm::vec3& handPosition);
void HandDrop(const glm::vec3& handPosition);
// What the hand is over this frame (0 for none), for GET_OBJECT_HAND_IS_OVER.
void SetObjectUnderHand(uint32_t entity);
// The nearest object the hand could take within `radius` of a point on the ground (0 for none): most
// objects (rocks, people, logs) have no physics body for the hand's ray to hit.
uint32_t FindObjectNear(const glm::vec3& point, float radius);
// Test hook (OPENBLACK_TEST_PICKUP): where to put the hand and whether to hold the right button this
// frame, to pick up, swing and throw the object nearest the camera's focus.
std::optional<std::pair<glm::vec3, bool>> TestPickupStep();

} // namespace openblack::chlapi
