// Switch controls for openblack, as the SDL events a desktop would produce - openblack's own input
// code (GameActionMap) is unchanged. Called once per frame from Game.cpp, on the main thread, just
// before openblack drains SDL's event queue.
//
//   Touchscreen           -> the hand follows the finger; what the touch does is decided by how it starts:
//                              hold still ~150 ms, then drag  -> left button held (grab and move the land)
//                              move straight away              -> no button (hover: drawing a gesture)
//                              quick tap without moving        -> left click
//   ZR (hold)             -> left mouse button at the cursor (also turns any touch into a grab at once)
//   ZL                    -> right mouse button (action)
//   Left stick            -> arrow keys (move the camera)
//   Right stick           -> Z / U (rotate left/right), A / Q (tilt up/down)
//   R / L                 -> mouse wheel (zoom in/out), repeating while held
//   X / Y                 -> Space (zoom to temple) / C (zoom to creature)
//   +                     -> P (pause/unpause - openblack starts paused, so nothing simulates until
//                            this is pressed)
#include <cstdlib>

#include <SDL.h>
#include <switch.h>

namespace
{
constexpr s32 k_StickThreshold = JOYSTICK_MAX / 3; // a third of the travel before a key "presses"
constexpr u64 k_WheelRepeatNs = 90'000'000ULL;

PadState g_pad;
bool g_initialized = false;
bool g_keyDown[SDL_NUM_SCANCODES] = {};
bool g_leftDown = false, g_zlDown = false;
u64 g_nextWheel = 0;

// Touch state (see the header comment). Times are armGetSystemTick nanoseconds, positions window pixels.
constexpr u64 k_TouchHoldNs = 150'000'000ULL;
constexpr int k_TouchSlopPx = 12;
enum class Touch
{
	None,
	Undecided, // down, still deciding between grab and hover
	Grab,
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

// Returns whether the touch wants the left button held this frame.
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
			g_touch = Touch::Grab;
		}
		else if (moved)
		{
			g_touch = Touch::Hover;
		}
	}
	return g_touch == Touch::Grab;
}

void wheel(int y)
{
	SDL_Event e {};
	e.type = SDL_MOUSEWHEEL;
	e.wheel.y = y;
	e.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
	e.wheel.windowID = SDL_GetWindowID(gameWindow());
	SDL_PushEvent(&e);
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

	// Camera movement: left stick as the arrow keys (stick up = forwards).
	setKey(SDL_SCANCODE_UP, left.y > k_StickThreshold);
	setKey(SDL_SCANCODE_DOWN, left.y < -k_StickThreshold);
	setKey(SDL_SCANCODE_LEFT, left.x < -k_StickThreshold);
	setKey(SDL_SCANCODE_RIGHT, left.x > k_StickThreshold);

	// Rotation and tilt: right stick.
	setKey(SDL_SCANCODE_Z, right.x < -k_StickThreshold);
	setKey(SDL_SCANCODE_U, right.x > k_StickThreshold);
	setKey(SDL_SCANCODE_A, right.y > k_StickThreshold);
	setKey(SDL_SCANCODE_Q, right.y < -k_StickThreshold);

	setKey(SDL_SCANCODE_SPACE, (held & HidNpadButton_X) != 0);
	setKey(SDL_SCANCODE_C, (held & HidNpadButton_Y) != 0);
	setKey(SDL_SCANCODE_P, (held & HidNpadButton_Plus) != 0);

	const u64 now = armTicksToNs(armGetSystemTick());
	const bool zr = (held & HidNpadButton_ZR) != 0;
	if (g_tapPending)
	{
		g_tapPending = false;
		setMouseButton(g_leftDown, SDL_BUTTON_LEFT, false); // finish last frame's tap
	}
	const bool touchLeft = touchFrame(now, zr);
	setMouseButton(g_leftDown, SDL_BUTTON_LEFT, zr || touchLeft);
	setMouseButton(g_zlDown, SDL_BUTTON_RIGHT, (held & HidNpadButton_ZL) != 0);

	const bool zoomIn = (held & HidNpadButton_R) != 0;
	const bool zoomOut = (held & HidNpadButton_L) != 0;
	if ((zoomIn || zoomOut) && now >= g_nextWheel)
	{
		wheel(zoomIn ? 1 : -1);
		g_nextWheel = now + k_WheelRepeatNs;
	}
	else if (!zoomIn && !zoomOut)
	{
		g_nextWheel = 0;
	}
}
