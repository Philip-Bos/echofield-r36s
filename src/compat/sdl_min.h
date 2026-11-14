/*
 * sdl_min.h - the subset of the SDL 2.0 public API used by ECHOFIELD.
 *
 * Declarations and constants follow the stable SDL 2.0 ABI (identical on x86_64
 * and aarch64). Only functions available since SDL 2.0.5 are used so the binary
 * works with whatever SDL2 build ArkOS / PortMaster provides. SDL is zlib
 * licensed; these declarations are transcribed from the public headers.
 */
#ifndef EF_SDL_MIN_H
#define EF_SDL_MIN_H
#include <stdint.h>

typedef uint8_t Uint8; typedef int8_t Sint8;
typedef uint16_t Uint16; typedef int16_t Sint16;
typedef uint32_t Uint32; typedef int32_t Sint32;
typedef uint64_t Uint64; typedef int64_t Sint64;
typedef enum { SDL_FALSE = 0, SDL_TRUE = 1 } SDL_bool;

#define SDL_INIT_TIMER 0x00000001u
#define SDL_INIT_AUDIO 0x00000010u
#define SDL_INIT_VIDEO 0x00000020u
#define SDL_INIT_JOYSTICK 0x00000200u
#define SDL_INIT_GAMECONTROLLER 0x00002000u
#define SDL_INIT_EVENTS 0x00004000u

typedef struct SDL_version { Uint8 major, minor, patch; } SDL_version;
typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Texture SDL_Texture;
typedef struct SDL_RWops SDL_RWops;
typedef struct SDL_Rect { int x, y, w, h; } SDL_Rect;
typedef struct SDL_Point { int x, y; } SDL_Point;
typedef struct SDL_PixelFormat SDL_PixelFormat;
typedef struct SDL_Surface {
    Uint32 flags; SDL_PixelFormat *format; int w, h; int pitch; void *pixels;
    void *userdata; int locked; void *lock_data; SDL_Rect clip_rect; void *map; int refcount;
} SDL_Surface;
typedef struct SDL_mutex SDL_mutex;
typedef struct SDL_cond SDL_cond;
typedef struct SDL_Thread SDL_Thread;
typedef int (*SDL_ThreadFunction)(void *data);
typedef struct _SDL_GameController SDL_GameController;
typedef struct _SDL_Joystick SDL_Joystick;
typedef Sint32 SDL_JoystickID;
typedef Uint32 SDL_AudioDeviceID;
typedef Uint16 SDL_AudioFormat;
typedef void (*SDL_AudioCallback)(void *userdata, Uint8 *stream, int len);
typedef struct SDL_AudioSpec {
    int freq; SDL_AudioFormat format; Uint8 channels; Uint8 silence;
    Uint16 samples; Uint16 padding; Uint32 size;
    SDL_AudioCallback callback; void *userdata;
} SDL_AudioSpec;
#define AUDIO_S16LSB 0x8010
#define AUDIO_F32LSB 0x8120
#define SDL_AUDIO_ALLOW_FREQUENCY_CHANGE 0x00000001
#define SDL_AUDIO_ALLOW_FORMAT_CHANGE 0x00000002
#define SDL_AUDIO_ALLOW_CHANNELS_CHANGE 0x00000004

#define SDL_WINDOWPOS_CENTERED 0x2FFF0000u
#define SDL_WINDOW_FULLSCREEN 0x00000001u
#define SDL_WINDOW_SHOWN 0x00000004u
#define SDL_WINDOW_FULLSCREEN_DESKTOP (SDL_WINDOW_FULLSCREEN | 0x00001000u)
#define SDL_RENDERER_SOFTWARE 0x1u
#define SDL_RENDERER_ACCELERATED 0x2u
#define SDL_RENDERER_PRESENTVSYNC 0x4u
#define SDL_RENDERER_TARGETTEXTURE 0x8u
#define SDL_PIXELFORMAT_ARGB8888 0x16362004u
#define SDL_TEXTUREACCESS_STATIC 0
#define SDL_TEXTUREACCESS_STREAMING 1
#define SDL_TEXTUREACCESS_TARGET 2
typedef enum { SDL_BLENDMODE_NONE = 0, SDL_BLENDMODE_BLEND = 1, SDL_BLENDMODE_ADD = 2, SDL_BLENDMODE_MOD = 4 } SDL_BlendMode;

/* events */
#define SDL_QUIT 0x100
#define SDL_WINDOWEVENT 0x200
#define SDL_KEYDOWN 0x300
#define SDL_KEYUP 0x301
#define SDL_JOYDEVICEADDED 0x605
#define SDL_JOYDEVICEREMOVED 0x606
#define SDL_CONTROLLERAXISMOTION 0x650
#define SDL_CONTROLLERBUTTONDOWN 0x651
#define SDL_CONTROLLERBUTTONUP 0x652
#define SDL_CONTROLLERDEVICEADDED 0x653
#define SDL_CONTROLLERDEVICEREMOVED 0x654
#define SDL_AUDIODEVICEADDED 0x1100
#define SDL_AUDIODEVICEREMOVED 0x1101
typedef struct SDL_Keysym { Sint32 scancode; Sint32 sym; Uint16 mod; Uint32 unused; } SDL_Keysym;
typedef struct SDL_KeyboardEvent { Uint32 type, timestamp, windowID; Uint8 state, repeat, padding2, padding3; SDL_Keysym keysym; } SDL_KeyboardEvent;
typedef struct SDL_ControllerAxisEvent { Uint32 type, timestamp; SDL_JoystickID which; Uint8 axis, padding1, padding2, padding3; Sint16 value; Uint16 padding4; } SDL_ControllerAxisEvent;
typedef struct SDL_ControllerButtonEvent { Uint32 type, timestamp; SDL_JoystickID which; Uint8 button, state, padding1, padding2; } SDL_ControllerButtonEvent;
typedef struct SDL_ControllerDeviceEvent { Uint32 type, timestamp; Sint32 which; } SDL_ControllerDeviceEvent;
typedef union SDL_Event {
    Uint32 type;
    SDL_KeyboardEvent key;
    SDL_ControllerAxisEvent caxis;
    SDL_ControllerButtonEvent cbutton;
    SDL_ControllerDeviceEvent cdevice;
    Uint8 padding[56];
} SDL_Event;
_Static_assert(sizeof(SDL_Event) == 56, "SDL_Event size");
_Static_assert(sizeof(SDL_AudioSpec) == 32, "SDL_AudioSpec size");

#define SDLK_SC(x) ((x) | (1 << 30))
#define SDLK_RETURN '\r'
#define SDLK_ESCAPE 27
#define SDLK_BACKSPACE 8
#define SDLK_TAB 9
#define SDLK_SPACE ' '
#define SDLK_RIGHT SDLK_SC(79)
#define SDLK_LEFT SDLK_SC(80)
#define SDLK_DOWN SDLK_SC(81)
#define SDLK_UP SDLK_SC(82)
#define SDLK_LSHIFT SDLK_SC(225)
#define SDLK_RSHIFT SDLK_SC(229)
#define SDLK_F1 SDLK_SC(58)
#define SDLK_F12 SDLK_SC(69)

typedef enum {
    SDL_CONTROLLER_BUTTON_INVALID = -1, SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B,
    SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y, SDL_CONTROLLER_BUTTON_BACK,
    SDL_CONTROLLER_BUTTON_GUIDE, SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_LEFTSTICK,
    SDL_CONTROLLER_BUTTON_RIGHTSTICK, SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
    SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, SDL_CONTROLLER_BUTTON_DPAD_UP,
    SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_CONTROLLER_BUTTON_DPAD_LEFT,
    SDL_CONTROLLER_BUTTON_DPAD_RIGHT
} SDL_GameControllerButton;
typedef enum {
    SDL_CONTROLLER_AXIS_LEFTX, SDL_CONTROLLER_AXIS_LEFTY, SDL_CONTROLLER_AXIS_RIGHTX,
    SDL_CONTROLLER_AXIS_RIGHTY, SDL_CONTROLLER_AXIS_TRIGGERLEFT, SDL_CONTROLLER_AXIS_TRIGGERRIGHT
} SDL_GameControllerAxis;

#define SDL_HINT_RENDER_SCALE_QUALITY "SDL_RENDER_SCALE_QUALITY"

int SDL_Init(Uint32 flags);
int SDL_InitSubSystem(Uint32 flags);
void SDL_Quit(void);
const char *SDL_GetError(void);
void SDL_GetVersion(SDL_version *ver);
SDL_bool SDL_SetHint(const char *name, const char *value);
const char *SDL_GetPlatform(void);
int SDL_GetCPUCount(void);
int SDL_GetSystemRAM(void);
const char *SDL_GetCurrentVideoDriver(void);
const char *SDL_GetCurrentAudioDriver(void);

SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags);
void SDL_DestroyWindow(SDL_Window *w);
void SDL_GetWindowSize(SDL_Window *w, int *ww, int *hh);
int SDL_ShowCursor(int toggle);
SDL_Renderer *SDL_CreateRenderer(SDL_Window *w, int index, Uint32 flags);
void SDL_DestroyRenderer(SDL_Renderer *r);
int SDL_RenderSetLogicalSize(SDL_Renderer *r, int w, int h);
int SDL_GetRendererOutputSize(SDL_Renderer *r, int *w, int *h);
int SDL_SetRenderDrawColor(SDL_Renderer *r, Uint8 cr, Uint8 cg, Uint8 cb, Uint8 ca);
int SDL_SetRenderDrawBlendMode(SDL_Renderer *r, SDL_BlendMode m);
int SDL_RenderClear(SDL_Renderer *r);
int SDL_RenderFillRect(SDL_Renderer *r, const SDL_Rect *rect);
int SDL_RenderFillRects(SDL_Renderer *r, const SDL_Rect *rects, int n);
int SDL_RenderDrawRect(SDL_Renderer *r, const SDL_Rect *rect);
int SDL_RenderDrawLine(SDL_Renderer *r, int x1, int y1, int x2, int y2);
int SDL_RenderDrawLines(SDL_Renderer *r, const SDL_Point *pts, int n);
int SDL_RenderDrawPoints(SDL_Renderer *r, const SDL_Point *pts, int n);
int SDL_RenderCopy(SDL_Renderer *r, SDL_Texture *t, const SDL_Rect *src, const SDL_Rect *dst);
void SDL_RenderPresent(SDL_Renderer *r);
int SDL_SetRenderTarget(SDL_Renderer *r, SDL_Texture *t);
SDL_bool SDL_RenderTargetSupported(SDL_Renderer *r);
int SDL_RenderReadPixels(SDL_Renderer *r, const SDL_Rect *rect, Uint32 format, void *pixels, int pitch);
SDL_Texture *SDL_CreateTexture(SDL_Renderer *r, Uint32 format, int access, int w, int h);
void SDL_DestroyTexture(SDL_Texture *t);
int SDL_UpdateTexture(SDL_Texture *t, const SDL_Rect *rect, const void *pixels, int pitch);
int SDL_SetTextureBlendMode(SDL_Texture *t, SDL_BlendMode m);
int SDL_SetTextureColorMod(SDL_Texture *t, Uint8 r, Uint8 g, Uint8 b);
int SDL_SetTextureAlphaMod(SDL_Texture *t, Uint8 a);
SDL_Surface *SDL_CreateRGBSurfaceWithFormatFrom(void *pixels, int w, int h, int depth, int pitch, Uint32 format);
void SDL_FreeSurface(SDL_Surface *s);
SDL_RWops *SDL_RWFromFile(const char *file, const char *mode);
int SDL_SaveBMP_RW(SDL_Surface *s, SDL_RWops *dst, int freedst);

int SDL_PollEvent(SDL_Event *e);
int SDL_NumJoysticks(void);
SDL_bool SDL_IsGameController(int idx);
SDL_GameController *SDL_GameControllerOpen(int idx);
void SDL_GameControllerClose(SDL_GameController *c);
const char *SDL_GameControllerName(SDL_GameController *c);
SDL_Joystick *SDL_GameControllerGetJoystick(SDL_GameController *c);
SDL_JoystickID SDL_JoystickInstanceID(SDL_Joystick *j);
Sint16 SDL_GameControllerGetAxis(SDL_GameController *c, SDL_GameControllerAxis a);
int SDL_GameControllerAddMappingsFromRW(SDL_RWops *rw, int freerw);
int SDL_GameControllerEventState(int state);

int SDL_GetNumAudioDevices(int iscapture);
const char *SDL_GetAudioDeviceName(int index, int iscapture);
SDL_AudioDeviceID SDL_OpenAudioDevice(const char *device, int iscapture, const SDL_AudioSpec *desired, SDL_AudioSpec *obtained, int allowed_changes);
void SDL_PauseAudioDevice(SDL_AudioDeviceID dev, int pause_on);
void SDL_LockAudioDevice(SDL_AudioDeviceID dev);
void SDL_UnlockAudioDevice(SDL_AudioDeviceID dev);
void SDL_CloseAudioDevice(SDL_AudioDeviceID dev);
int SDL_GetAudioDeviceStatus(SDL_AudioDeviceID dev);

Uint32 SDL_GetTicks(void);
Uint64 SDL_GetPerformanceCounter(void);
Uint64 SDL_GetPerformanceFrequency(void);
void SDL_Delay(Uint32 ms);
SDL_Thread *SDL_CreateThread(SDL_ThreadFunction fn, const char *name, void *data);
void SDL_WaitThread(SDL_Thread *t, int *status);
SDL_mutex *SDL_CreateMutex(void);
int SDL_LockMutex(SDL_mutex *m);
int SDL_UnlockMutex(SDL_mutex *m);
void SDL_DestroyMutex(SDL_mutex *m);
SDL_cond *SDL_CreateCond(void);
void SDL_DestroyCond(SDL_cond *c);
int SDL_CondSignal(SDL_cond *c);
int SDL_CondWaitTimeout(SDL_cond *c, SDL_mutex *m, Uint32 ms);
void *SDL_LoadObject(const char *sofile);
void *SDL_LoadFunction(void *handle, const char *name);
void SDL_UnloadObject(void *handle);

#endif
