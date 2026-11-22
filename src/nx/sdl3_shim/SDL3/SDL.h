#pragma once
#ifndef __cplusplus
#include <stdbool.h>
#endif
#include <sys/stat.h>
#include <SDL2/SDL.h>
#include "SDL_scancode.h"

#ifdef __cplusplus
extern "C" {
#endif

/* SDL3 -> SDL2 compatibility shim for Nintendo Switch (portlibs SDL2). */

typedef SDL_GameController SDL_Gamepad;
typedef SDL_GameControllerButton SDL_GamepadButton;
typedef SDL_GameControllerAxis SDL_GamepadAxis;
typedef SDL_ControllerAxisEvent SDL_GamepadAxisEvent;
typedef SDL_JoystickGUID SDL_GUID;
typedef SDL_JoystickID SDL_DisplayID;
typedef Uint32 SDL_WindowID;
typedef Uint32 SDL_PropertiesID;

#define SDL_INIT_GAMEPAD SDL_INIT_GAMECONTROLLER
#define SDL_KMOD_ALT KMOD_ALT
#define SDL_KMOD_CTRL KMOD_CTRL
#define SDL_KMOD_GUI KMOD_GUI
#define SDL_KMOD_SHIFT KMOD_SHIFT
#define SDLK_Q SDLK_q
#define SDLK_E SDLK_e
#define SDLK_W SDLK_w
#define SDLK_S SDLK_s

#define SDL_EVENT_QUIT SDL_QUIT
#define SDL_EVENT_KEY_DOWN SDL_KEYDOWN
#define SDL_EVENT_KEY_UP SDL_KEYUP
#define SDL_EVENT_TEXT_INPUT SDL_TEXTINPUT
#define SDL_EVENT_MOUSE_MOTION SDL_MOUSEMOTION
#define SDL_EVENT_MOUSE_BUTTON_DOWN SDL_MOUSEBUTTONDOWN
#define SDL_EVENT_MOUSE_BUTTON_UP SDL_MOUSEBUTTONUP
#define SDL_EVENT_MOUSE_WHEEL SDL_MOUSEWHEEL
#define SDL_EVENT_FINGER_DOWN SDL_FINGERDOWN
#define SDL_EVENT_FINGER_UP SDL_FINGERUP
#define SDL_EVENT_FINGER_MOTION SDL_FINGERMOTION
#define SDL_EVENT_FINGER_CANCELED SDL_FINGERUP
#define SDL_PEN_TOUCHID ((Sint64)-2)
#define SDL_EVENT_GAMEPAD_ADDED SDL_CONTROLLERDEVICEADDED
#define SDL_EVENT_GAMEPAD_REMOVED SDL_CONTROLLERDEVICEREMOVED
#define SDL_EVENT_GAMEPAD_BUTTON_DOWN SDL_CONTROLLERBUTTONDOWN
#define SDL_EVENT_GAMEPAD_BUTTON_UP SDL_CONTROLLERBUTTONUP
#define SDL_EVENT_GAMEPAD_AXIS_MOTION SDL_CONTROLLERAXISMOTION
#define SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN SDL_CONTROLLERTOUCHPADDOWN

#define SDL_EVENT_WINDOW_CLOSE_REQUESTED SDL_WINDOWEVENT_CLOSE
#define SDL_EVENT_WINDOW_RESIZED SDL_WINDOWEVENT_RESIZED
#define SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED SDL_WINDOWEVENT_SIZE_CHANGED
#define SDL_EVENT_WINDOW_MINIMIZED SDL_WINDOWEVENT_MINIMIZED
#define SDL_EVENT_WINDOW_RESTORED SDL_WINDOWEVENT_RESTORED
#define SDL_EVENT_WINDOW_SHOWN SDL_WINDOWEVENT_SHOWN
#define SDL_EVENT_WINDOW_HIDDEN SDL_WINDOWEVENT_HIDDEN
#define SDL_EVENT_WINDOW_FOCUS_LOST SDL_WINDOWEVENT_FOCUS_LOST

#define SDL_GAMEPAD_BUTTON_INVALID SDL_CONTROLLER_BUTTON_INVALID
#define SDL_GAMEPAD_BUTTON_SOUTH SDL_CONTROLLER_BUTTON_A
#define SDL_GAMEPAD_BUTTON_EAST SDL_CONTROLLER_BUTTON_B
#define SDL_GAMEPAD_BUTTON_WEST SDL_CONTROLLER_BUTTON_X
#define SDL_GAMEPAD_BUTTON_NORTH SDL_CONTROLLER_BUTTON_Y
#define SDL_GAMEPAD_BUTTON_BACK SDL_CONTROLLER_BUTTON_BACK
#define SDL_GAMEPAD_BUTTON_START SDL_CONTROLLER_BUTTON_START
#define SDL_GAMEPAD_BUTTON_LEFT_STICK SDL_CONTROLLER_BUTTON_LEFTSTICK
#define SDL_GAMEPAD_BUTTON_RIGHT_STICK SDL_CONTROLLER_BUTTON_RIGHTSTICK
#define SDL_GAMEPAD_BUTTON_LEFT_SHOULDER SDL_CONTROLLER_BUTTON_LEFTSHOULDER
#define SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER SDL_CONTROLLER_BUTTON_RIGHTSHOULDER
#define SDL_GAMEPAD_BUTTON_DPAD_UP SDL_CONTROLLER_BUTTON_DPAD_UP
#define SDL_GAMEPAD_BUTTON_DPAD_DOWN SDL_CONTROLLER_BUTTON_DPAD_DOWN
#define SDL_GAMEPAD_BUTTON_DPAD_LEFT SDL_CONTROLLER_BUTTON_DPAD_LEFT
#define SDL_GAMEPAD_BUTTON_DPAD_RIGHT SDL_CONTROLLER_BUTTON_DPAD_RIGHT
#define SDL_GAMEPAD_BUTTON_TOUCHPAD SDL_CONTROLLER_BUTTON_TOUCHPAD

#define SDL_GAMEPAD_AXIS_LEFTX SDL_CONTROLLER_AXIS_LEFTX
#define SDL_GAMEPAD_AXIS_LEFTY SDL_CONTROLLER_AXIS_LEFTY
#define SDL_GAMEPAD_AXIS_RIGHTX SDL_CONTROLLER_AXIS_RIGHTX
#define SDL_GAMEPAD_AXIS_RIGHTY SDL_CONTROLLER_AXIS_RIGHTY
#define SDL_GAMEPAD_AXIS_LEFT_TRIGGER SDL_CONTROLLER_AXIS_TRIGGERLEFT
#define SDL_GAMEPAD_AXIS_RIGHT_TRIGGER SDL_CONTROLLER_AXIS_TRIGGERRIGHT

#define SDL_AUDIO_S16 AUDIO_S16LSB
#define SDL_AUDIO_F32 AUDIO_F32LSB
#define SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK 0

#ifndef SDL_HINT_JOYSTICK_HIDAPI
#define SDL_HINT_JOYSTICK_HIDAPI "SDL_JOYSTICK_HIDAPI"
#endif
#ifndef SDL_HINT_VIDEO_DRIVER
#define SDL_HINT_VIDEO_DRIVER SDL_HINT_VIDEODRIVER
#endif

static inline SDL_DisplayID *SDL_GetDisplays(int *count) {
    int n = SDL_GetNumVideoDisplays();
    if (n < 0) n = 0;
    SDL_DisplayID *ids = (SDL_DisplayID *)SDL_malloc((size_t)(n + 1) * sizeof(SDL_DisplayID));
    if (!ids) {
        if (count) *count = 0;
        return NULL;
    }
    for (int i = 0; i < n; ++i) ids[i] = (SDL_DisplayID)i;
    ids[n] = 0;
    if (count) *count = n;
    return ids;
}

static inline Uint64 SDL_GetTicksNS(void) {
    return (Uint64)SDL_GetTicks64() * 1000000ULL;
}

/* SDL3 returns true on success; SDL2 returns 0. Every caller tests it as a boolean. */
static inline SDL_bool dcp_SDL_InitSubSystem(Uint32 flags) {
    return (SDL_bool)(SDL_InitSubSystem(flags) == 0);
}
#define SDL_InitSubSystem dcp_SDL_InitSubSystem

static inline SDL_bool dcp_SDL_GL_MakeCurrent(SDL_Window *window, SDL_GLContext context) {
    return (SDL_bool)(SDL_GL_MakeCurrent(window, context) == 0);
}
#define SDL_GL_MakeCurrent dcp_SDL_GL_MakeCurrent

static inline SDL_bool dcp_SDL_GL_SetSwapInterval(int interval) {
    return (SDL_bool)(SDL_GL_SetSwapInterval(interval) == 0);
}
#define SDL_GL_SetSwapInterval dcp_SDL_GL_SetSwapInterval

static inline SDL_bool dcp_SDL_GetWindowBordersSize(SDL_Window *window, int *top, int *left, int *bottom,
                                                   int *right) {
    return (SDL_bool)(SDL_GetWindowBordersSize(window, top, left, bottom, right) == 0);
}
#define SDL_GetWindowBordersSize dcp_SDL_GetWindowBordersSize

static inline int SDL_AddGamepadMappingsFromFile(const char *file) {
    return SDL_GameControllerAddMappingsFromFile(file);
}

static inline SDL_Gamepad *SDL_OpenGamepad(SDL_JoystickID id) {
    return SDL_GameControllerOpen((int)id);
}

static inline void SDL_CloseGamepad(SDL_Gamepad *gp) {
    SDL_GameControllerClose(gp);
}

static inline SDL_bool SDL_IsGamepad(SDL_JoystickID id) {
    return SDL_IsGameController((int)id);
}

static inline SDL_bool SDL_GamepadConnected(SDL_Gamepad *gp) {
    return SDL_GameControllerGetAttached(gp);
}

static inline const char *SDL_GetGamepadName(SDL_Gamepad *gp) {
    return SDL_GameControllerName(gp);
}

static inline Uint16 SDL_GetGamepadVendor(SDL_Gamepad *gp) {
    return SDL_GameControllerGetVendor(gp);
}

static inline Uint16 SDL_GetGamepadProduct(SDL_Gamepad *gp) {
    return SDL_GameControllerGetProduct(gp);
}

static inline Uint8 SDL_GetGamepadButton(SDL_Gamepad *gp, SDL_GamepadButton btn) {
    return SDL_GameControllerGetButton(gp, btn);
}

static inline Sint16 SDL_GetGamepadAxis(SDL_Gamepad *gp, SDL_GamepadAxis axis) {
    return SDL_GameControllerGetAxis(gp, axis);
}

static inline SDL_GamepadButton SDL_GetGamepadButtonFromString(const char *str) {
    return SDL_GameControllerGetButtonFromString(str);
}

static inline int SDL_RumbleGamepad(SDL_Gamepad *gp, Uint16 low, Uint16 high, Uint32 ms) {
    return SDL_GameControllerRumble(gp, low, high, ms);
}

static inline int SDL_GetNumGamepadTouchpads(SDL_Gamepad *gp) {
    return SDL_GameControllerGetNumTouchpads(gp);
}

static inline int SDL_GetNumGamepadTouchpadFingers(SDL_Gamepad *gp, int touchpad) {
    return SDL_GameControllerGetNumTouchpadFingers(gp, touchpad);
}

static inline int SDL_GetGamepadTouchpadFinger(SDL_Gamepad *gp, int touchpad, int finger, bool *state, float *x, float *y, float *pressure) {
    Uint8 s = 0;
    int r = SDL_GameControllerGetTouchpadFinger(gp, touchpad, finger, &s, x, y, pressure);
    if (state) *state = s != 0;
    return r == 0;
}

static inline int SDL_HideCursor(void) { return SDL_ShowCursor(SDL_DISABLE); }
static inline int SDL_SetWindowRelativeMouseMode(SDL_Window *w, SDL_bool enabled) {
    (void)w;
    return SDL_SetRelativeMouseMode(enabled);
}
static inline void SDL_SyncWindow(SDL_Window *w) { (void)w; }
static inline float SDL_GetWindowDisplayScale(SDL_Window *w) { (void)w; return 1.0f; }
static inline float SDL_GetWindowPixelDensity(SDL_Window *w) { (void)w; return 1.0f; }
static inline float SDL_GetDisplayContentScale(SDL_DisplayID id) { (void)id; return 1.0f; }
static inline SDL_DisplayID SDL_GetPrimaryDisplay(void) { return 0; }
static inline SDL_DisplayID SDL_GetDisplayForWindow(SDL_Window *w) { return SDL_GetWindowDisplayIndex(w); }
static inline void SDL_GUIDToString_Shim(void) {}
#ifdef __SWITCH__
static inline int SDL_GetAudioStreamQueued(SDL_AudioStream *stream) {
    return (int)SDL_GetQueuedAudioSize((SDL_AudioDeviceID)(uintptr_t)stream);
}
static inline int SDL_PutAudioStreamData(SDL_AudioStream *stream, const void *buf, int len) {
    return SDL_QueueAudio((SDL_AudioDeviceID)(uintptr_t)stream, buf, (Uint32)len);
}

typedef struct { int type; Uint64 size; } SDL_PathInfo;
static inline bool SDL_GetPathInfo(const char *path, SDL_PathInfo *info) {
    struct stat st;
    if (!path || stat(path, &st) != 0) return false;
    if (info) { info->type = S_ISDIR(st.st_mode) ? 2 : 1; info->size = (Uint64)st.st_size; }
    return true;
}

static inline SDL_GUID SDL_GetGamepadGUIDForID(SDL_JoystickID id) {
    return SDL_JoystickGetDeviceGUID((int)id);
}
static inline const char *SDL_GetGamepadPathForID(SDL_JoystickID id) {
    return SDL_JoystickPathForIndex((int)id);
}
static inline int SDL_GetGamepadPlayerIndexForID(SDL_JoystickID id) {
    return SDL_JoystickGetDevicePlayerIndex((int)id);
}
static inline SDL_JoystickID *SDL_GetJoysticks(int *count) {
    int n = SDL_NumJoysticks();
    if (n < 0) n = 0;
    SDL_JoystickID *ids = (SDL_JoystickID *)SDL_malloc((size_t)(n + 1) * sizeof(SDL_JoystickID));
    if (!ids) { if (count) *count = 0; return NULL; }
    for (int i = 0; i < n; ++i) ids[i] = (SDL_JoystickID)i;
    ids[n] = 0;
    if (count) *count = n;
    return ids;
}
static inline SDL_JoystickID *SDL_GetGamepads(int *count) {
    int n = SDL_NumJoysticks();
    if (n < 0) n = 0;
    SDL_JoystickID *ids = (SDL_JoystickID *)SDL_malloc((size_t)(n + 1) * sizeof(SDL_JoystickID));
    if (!ids) { if (count) *count = 0; return NULL; }
    int out = 0;
    for (int i = 0; i < n; ++i) {
        if (SDL_IsGameController(i)) ids[out++] = (SDL_JoystickID)i;
    }
    ids[out] = 0;
    if (count) *count = out;
    return ids;
}
static inline SDL_GUID SDL_GetJoystickGUIDForID(SDL_JoystickID id) {
    return SDL_JoystickGetDeviceGUID((int)id);
}
static inline const char *SDL_GetJoystickNameForID(SDL_JoystickID id) {
    return SDL_JoystickNameForIndex((int)id);
}
static inline Uint16 SDL_GetJoystickVendorForID(SDL_JoystickID id) {
    return SDL_JoystickGetDeviceVendor((int)id);
}
static inline Uint16 SDL_GetJoystickProductForID(SDL_JoystickID id) {
    return SDL_JoystickGetDeviceProduct((int)id);
}
static inline const bool *dcp_SDL_GetKeyboardState(int *numkeys) {
    return (const bool *)SDL_GetKeyboardState(numkeys);
}
#ifndef __cplusplus
#define SDL_GetKeyboardState dcp_SDL_GetKeyboardState
#endif
#endif
static inline int SDL_SetWindowFullscreenMode(SDL_Window *w, const SDL_DisplayMode *mode) { return SDL_SetWindowDisplayMode(w, mode); }

static inline SDL_bool dcp_SDL_GL_SwapWindow(SDL_Window *w) {
    SDL_GL_SwapWindow(w);
    return SDL_TRUE;
}
#define SDL_GL_SwapWindow dcp_SDL_GL_SwapWindow

#ifdef __cplusplus
}
#endif
