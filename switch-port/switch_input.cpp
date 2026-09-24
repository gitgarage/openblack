// Switch controls for openblack, as the SDL events a desktop would produce - openblack's own input
// code (GameActionMap) is unchanged. Called once per frame from Game.cpp, on the main thread, just
// before openblack drains SDL's event queue.
//
//   Touchscreen tap/drag  -> left mouse (point, grab and move the land) - SDL's own touch-to-mouse
//   ZR (hold)             -> left mouse button at the cursor
//   ZL                    -> right mouse button (action)
//   Left stick            -> arrow keys (move the camera)
//   Right stick           -> Z / U (rotate left/right), A / Q (tilt up/down)
//   R / L                 -> mouse wheel (zoom in/out), repeating while held
//   X / Y                 -> Space (zoom to temple) / C (zoom to creature)
#include <SDL.h>
#include <switch.h>

namespace
{
constexpr s32 k_StickThreshold = JOYSTICK_MAX / 3; // a third of the travel before a key "presses"
constexpr u64 k_WheelRepeatNs = 90'000'000ULL;

PadState g_pad;
bool g_initialized = false;
bool g_keyDown[SDL_NUM_SCANCODES] = {};
bool g_zrDown = false, g_zlDown = false;
u64 g_nextWheel = 0;

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
	e.key.windowID = SDL_GetWindowID(SDL_GetMouseFocus());
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
	e.button.windowID = SDL_GetWindowID(SDL_GetMouseFocus());
	SDL_PushEvent(&e);
}

void wheel(int y)
{
	SDL_Event e {};
	e.type = SDL_MOUSEWHEEL;
	e.wheel.y = y;
	e.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
	e.wheel.windowID = SDL_GetWindowID(SDL_GetMouseFocus());
	SDL_PushEvent(&e);
}
} // namespace

void switchInputFrame()
{
	if (!g_initialized)
	{
		padConfigureInput(1, HidNpadStyleSet_NpadStandard);
		padInitializeDefault(&g_pad);
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

	setMouseButton(g_zrDown, SDL_BUTTON_LEFT, (held & HidNpadButton_ZR) != 0);
	setMouseButton(g_zlDown, SDL_BUTTON_RIGHT, (held & HidNpadButton_ZL) != 0);

	const bool zoomIn = (held & HidNpadButton_R) != 0;
	const bool zoomOut = (held & HidNpadButton_L) != 0;
	const u64 now = armTicksToNs(armGetSystemTick());
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
