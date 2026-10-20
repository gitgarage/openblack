// Switch controls for openblack, as the SDL events a desktop would produce - openblack's own input
// code (GameActionMap) is unchanged. Called once per frame from Game.cpp, on the main thread, just
// before openblack drains SDL's event queue.
//
//   Touchscreen           -> the hand follows the finger; what the touch does is decided by how it starts:
//                              hold still ~150 ms, then drag  -> left button held (grab and move the land) -
//                                                               or, over a wood pile, the right button held:
//                                                               collect wood until the finger lifts (ZL then
//                                                               puts it down where the hand is), or at a
//                                                               finished Temple's door: go in, or on a
//                                                               scroll: click it, or with your creature's leash
//                                                               on, on a tree or house: tie it there (untie: again)
//                              move straight away              -> no button (hover: drawing a gesture)
//                              quick tap without moving        -> left click
//   ZR (hold)             -> left mouse button at the cursor (also turns any touch into a grab at once)
//   ZL                    -> right mouse button (action); pressed on the finished Temple's door, go in
//   Left stick            -> arrow keys (move the camera)
//   Right stick           -> Z / U (rotate left/right), A / Q (tilt up/down)
//   R / L                 -> zoom in / out, continuously while held (straight into the camera's held zoom:
//                            not the wheel, whose steps each waited on a camera flight, nor Ctrl + Up / Down,
//                            which took over the left stick's keys)
//   X                     -> bring your creature to where the hand is
//   Y                     -> switch between the time of day's own light and broad daylight (to see at night)
//   +                     -> P (pause/unpause; the game starts running)
//   D-pad up / left       -> save test point 1 / 2 (keypad 1 / 2)
//   D-pad down / right    -> load test point 1 / 2 (keypad 3 / 4)
//   B, -                  -> leave the Temple (- is the original's Esc), or the side-on fight view
//   A                     -> cycle your creature's leash: none, regular, reward, punishment
//   Right stick press     -> play twice as fast (again for faster still, up to x8)
//   Left stick press      -> play half as fast (down to x1/8)
#include <cstdlib>

#include <SDL.h>
#include <switch.h>

#include "CHLApi.h"
#include "Game.h"
#include "Camera/DefaultWorldCameraModel.h"

namespace
{
constexpr s32 k_StickThreshold = JOYSTICK_MAX / 3; // a third of the travel before a key "presses"

PadState g_pad;
bool g_initialized = false;
bool g_keyDown[SDL_NUM_SCANCODES] = {};
bool g_leftDown = false, g_rightDown = false;

// Touch state (see the header comment). Times are armGetSystemTick nanoseconds, positions window pixels.
constexpr u64 k_TouchHoldNs = 150'000'000ULL;
constexpr int k_TouchSlopPx = 12;
enum class Touch
{
	None,
	Undecided, // down, still deciding between grab and hover
	Grab,
	Collect, // a long press over wood
	Hover,
};
Touch g_touch = Touch::None;
u64 g_touchStart = 0;
int g_touchStartX = 0, g_touchStartY = 0;
bool g_tapPending = false; // a tap's button-down was sent; send the up next frame

SDL_Window* gameWindow()
{
	return SDL_GL_GetCurrentWindow();
}

void setKey(SDL_Scancode code, bool down)
{
	if (g_keyDown[code] == down)
	{
		return;
	}
	g_keyDown[code] = down;
	SDL_Event e {};
	e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
	e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
	e.key.keysym.scancode = code;
	e.key.keysym.sym = SDL_GetKeyFromScancode(code);
	e.key.windowID = SDL_GetWindowID(gameWindow());
	SDL_PushEvent(&e);
}

void setMouseButton(bool& state, Uint8 button, bool down)
{
	if (state == down)
	{
		return;
	}
	state = down;
	int x = 0, y = 0;
	SDL_GetMouseState(&x, &y);
	SDL_Event e {};
	e.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
	e.button.button = button;
	e.button.state = down ? SDL_PRESSED : SDL_RELEASED;
	e.button.clicks = 1;
	e.button.x = x;
	e.button.y = y;
	e.button.windowID = SDL_GetWindowID(gameWindow());
	SDL_PushEvent(&e);
}

// Returns whether the touch wants the left button held this frame (collecting is g_touch == Touch::Collect).
bool touchFrame(u64 now, bool forceGrab)
{
	HidTouchScreenState touch {};
	const bool down = hidGetTouchScreenStates(&touch, 1) != 0 && touch.count > 0;
	if (!down)
	{
		const bool wasTap = g_touch == Touch::Undecided;
		g_touch = Touch::None;
		if (wasTap)
		{
			g_tapPending = true; // lifted before deciding: a click
			return true;
		}
		return false;
	}

	SDL_Window* window = gameWindow();
	int w = 1280, h = 720;
	if (window != nullptr)
	{
		SDL_GetWindowSize(window, &w, &h);
	}
	const int x = static_cast<int>(touch.touches[0].x) * w / 1280;
	const int y = static_cast<int>(touch.touches[0].y) * h / 720;
	SDL_WarpMouseInWindow(window, x, y); // the hand follows the finger in every state

	if (g_touch == Touch::None)
	{
		openblack::chlapi::HandJumped(); // the hand lands where the finger does, from wherever it was
		g_touch = Touch::Undecided;
		g_touchStart = now;
		g_touchStartX = x;
		g_touchStartY = y;
	}
	if (g_touch == Touch::Undecided)
	{
		const bool moved = std::abs(x - g_touchStartX) > k_TouchSlopPx || std::abs(y - g_touchStartY) > k_TouchSlopPx;
		if (forceGrab || (!moved && now - g_touchStart >= k_TouchHoldNs))
		{
			// At the Temple's door (as of last frame's hand position, which is where the finger is): go in.
			// Over wood: collect it.
			const glm::vec2 screen(static_cast<float>(x) / static_cast<float>(w), static_cast<float>(y) / static_cast<float>(h));
			if (!forceGrab && !openblack::chlapi::InTemple() && openblack::chlapi::ClickScrollAt(screen))
			{
				g_touch = Touch::Hover; // a long press on a scroll clicks it (starts its challenge)
			}
			else if (!forceGrab && openblack::chlapi::InTemple())
			{
				// Inside the Temple a long press walks to a room or leaves by the map: no grab.
				openblack::chlapi::TempleLongPress(screen);
				g_touch = Touch::Hover;
			}
			else if (!forceGrab && openblack::chlapi::HandAtTempleDoor())
			{
				openblack::chlapi::EnterTemple();
				g_touch = Touch::Hover;
			}
			else if (!forceGrab && openblack::chlapi::TakeSphereUnderHand())
			{
				g_touch = Touch::Hover; // a long press on a miracle sphere picks it up
			}
			else if (!forceGrab && openblack::chlapi::LeashTieAtPointer())
			{
				g_touch = Touch::Hover; // a long press on a tree or house with the leash on ties it there (or unties it)
			}
			else if (!forceGrab && openblack::chlapi::LetGoAtPointer())
			{
				g_touch = Touch::Hover; // something held: a long press lets go of it there (into the village store if on it)
			}
			else
			{
				g_touch = !forceGrab && openblack::chlapi::HandWoodUnder() != 0 ? Touch::Collect : Touch::Grab;
			}
		}
		else if (moved)
		{
			g_touch = Touch::Hover;
		}
	}
	return g_touch == Touch::Grab;
}

} // namespace

void switchInputFrame()
{
	if (!g_initialized)
	{
		padConfigureInput(1, HidNpadStyleSet_NpadStandard);
		padInitializeDefault(&g_pad);
		hidInitializeTouchScreen();
		SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0"); // touch is handled here, not as SDL's left button
		g_initialized = true;
	}
	padUpdate(&g_pad);
	const u64 held = padGetButtons(&g_pad);
	const HidAnalogStickState left = padGetStickPos(&g_pad, 0);
	const HidAnalogStickState right = padGetStickPos(&g_pad, 1);

	// Camera movement: left stick as the arrow keys (stick up = forwards). R / L zoom straight through the
	// camera's held zoom, leaving the stick's keys alone.
	const bool zoomIn = (held & HidNpadButton_R) != 0;
	const bool zoomOut = (held & HidNpadButton_L) != 0;
	openblack::g_heldZoomInput = zoomIn == zoomOut ? 0.0f : (zoomIn ? -1.0f : 1.0f);
	setKey(SDL_SCANCODE_UP, left.y > k_StickThreshold);
	setKey(SDL_SCANCODE_DOWN, left.y < -k_StickThreshold);
	setKey(SDL_SCANCODE_LEFT, left.x < -k_StickThreshold);
	setKey(SDL_SCANCODE_RIGHT, left.x > k_StickThreshold);

	// Rotation and tilt: right stick.
	setKey(SDL_SCANCODE_Z, right.x < -k_StickThreshold);
	setKey(SDL_SCANCODE_U, right.x > k_StickThreshold);
	setKey(SDL_SCANCODE_A, right.y > k_StickThreshold);
	setKey(SDL_SCANCODE_Q, right.y < -k_StickThreshold);

	// X: your creature to where the hand is (Mike, 2026-10-17: it was slow to come to its fight). Was Space.
	static bool s_xWasDown = false;
	const bool xDown = (held & HidNpadButton_X) != 0;
	if (xDown && !s_xWasDown)
	{
		openblack::chlapi::TeleportCreatureToHand();
	}
	s_xWasDown = xDown;
	setKey(SDL_SCANCODE_P, (held & HidNpadButton_Plus) != 0);
	setKey(SDL_SCANCODE_KP_1, (held & HidNpadButton_Up) != 0);
	setKey(SDL_SCANCODE_KP_2, (held & HidNpadButton_Left) != 0);
	setKey(SDL_SCANCODE_KP_3, (held & HidNpadButton_Down) != 0);
	setKey(SDL_SCANCODE_KP_4, (held & HidNpadButton_Right) != 0);
	// - is the original's Esc: out of the Temple (as a long press on its floor), out of the side-on fight view. Not
	// SDL's Escape, which quits openblack.
	static bool s_minusWasDown = false;
	const bool minusDown = (held & HidNpadButton_Minus) != 0;
	if (minusDown && !s_minusWasDown)
	{
		openblack::chlapi::ExitTemple();
	}
	s_minusWasDown = minusDown;
	static bool s_bWasDown = false;
	const bool bDown = (held & HidNpadButton_B) != 0;
	if (bDown && !s_bWasDown)
	{
		openblack::chlapi::ExitTemple();
	}
	s_bWasDown = bDown;
	// A: your creature's leash - none, regular, reward, punishment - one step each press.
	static bool s_aWasDown = false;
	const bool aDown = (held & HidNpadButton_A) != 0;
	if (aDown && !s_aWasDown)
	{
		openblack::chlapi::CycleLeash();
	}
	s_aWasDown = aDown;
	// Y: broad daylight, or back to the time of day's own light - one switch each press.
	static bool s_yWasDown = false;
	const bool yDown = (held & HidNpadButton_Y) != 0;
	if (yDown && !s_yWasDown)
	{
		openblack::chlapi::ToggleDaylight();
	}
	s_yWasDown = yDown;
	// Play speed, for hurrying through cutscenes: each press of a stick doubles (right) or halves (left) it.
	static u64 s_sticksWere = 0;
	const u64 sticks = held & (HidNpadButton_StickR | HidNpadButton_StickL);
	if (auto* game = openblack::Game::Instance(); game != nullptr)
	{
		if ((sticks & ~s_sticksWere & HidNpadButton_StickR) != 0)
		{
			game->ChangePlaySpeed(2.0f);
		}
		if ((sticks & ~s_sticksWere & HidNpadButton_StickL) != 0)
		{
			game->ChangePlaySpeed(0.5f);
		}
	}
	s_sticksWere = sticks;

	const u64 now = armTicksToNs(armGetSystemTick());
	// Holding something, either trigger is the action button (the original's throw: press it while holding, sweep
	// with the finger, let go of it to let go of what is held - its throwrock.hnd demo; Mike, 2026-10-17: ZL or ZR).
	const bool holding = openblack::chlapi::HandHolding();
	const bool zr = (held & HidNpadButton_ZR) != 0 && !holding;
	if (g_tapPending)
	{
		g_tapPending = false;
		setMouseButton(g_leftDown, SDL_BUTTON_LEFT, false); // finish last frame's tap
	}
	const bool touchLeft = touchFrame(now, zr);
	setMouseButton(g_leftDown, SDL_BUTTON_LEFT, zr || touchLeft);
	// ZL pressed with the hand on (or right by) the finished Temple's door goes in, as a long press there does (Mike,
	// 2026-10-19); held, it is the action button as ever - not while it went in.
	static bool s_zlWas = false;
	static bool s_zlEntered = false;
	const bool zl = (held & HidNpadButton_ZL) != 0;
	if (zl && !s_zlWas && !holding && !openblack::chlapi::InTemple() && openblack::chlapi::HandAtTempleDoor())
	{
		openblack::chlapi::EnterTemple();
		s_zlEntered = true;
	}
	if (!zl)
	{
		s_zlEntered = false;
	}
	s_zlWas = zl;
	setMouseButton(g_rightDown, SDL_BUTTON_RIGHT,
	               (zl && !s_zlEntered) || (holding && (held & HidNpadButton_ZR) != 0) || g_touch == Touch::Collect);


}
