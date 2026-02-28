/*
 * Tilengine - The 2D retro graphics engine with raster effects
 * Copyright (C) 2015-2019 Marc Palacios Domenech <mailto:megamarc@hotmail.com>
 * All rights reserved
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * */

#ifndef TLN_EXCLUDE_WINDOW
#define MAX_PLAYERS 4 /* number of unique players */
#define MAX_INPUTS 32 /* number of inputs per player */
#define INPUT_MASK (MAX_INPUTS - 1)

#ifdef WIN32
#include <Windows.h>
#endif
#include "Engine.h"
#include "Tilengine.h"
#include "crt.h"
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *backbuffer;
static SDL_Thread *thread;
static SDL_Mutex *lock;
static SDL_Condition *cond;
static CRTHandler crt;
static SDL_FRect dstrect;

static bool init;
static bool done;
static int wnd_width;
static int wnd_height;
static int instances = 0;
static uint8_t *rt_pixels;
static int rt_pitch;
static char *window_title;

static int last_key;
static TLN_SDLCallback sdl_callback = NULL;

/* player input */
typedef struct {
  bool enabled;
  uint8_t joystick_id;
  SDL_Joystick *joy;
  SDL_Keycode keycodes[MAX_INPUTS];
  uint8_t joybuttons[MAX_INPUTS];
  uint32_t inputs;
} PlayerInput;

static PlayerInput player_inputs[MAX_PLAYERS];

struct {
  CRTType type;
  bool blur;
  bool enable;
} static crt_params = {CRT_SLOT, true, false};

#define MAX_PATH 260

/* Window manager */
typedef struct {
  int width;
  int height;
  int flags;
  volatile int retval;
  uint32_t t0;        /* frame start time for non-vsync pacing */
  uint32_t min_delay; /* actual granularity of SDL_Delay() */
  uint32_t fps_t0;    /* for actual FPS calc */
  uint32_t fps_frames;
  uint32_t fps_average;
} WndParams;

static WndParams wnd_params;

typedef union {
  uint8_t value;
  struct {
    bool fullscreen : 1;
    bool vsync : 1;
    uint8_t factor : 4;
    bool nearest : 1;
    bool novsync : 1;
  };
} WindowFlags;

/* local prototypes */
static bool create_window(void);
static void delete_window(void);
static void resize_window(int new_factor);
static void calculate_window_dimensions(const SDL_DisplayMode *mode,
                                        WindowFlags *flags, int *rflags);
static void initialize_default_input(void);
static void initialize_joystick(void);
static void calibrate_timing(WindowFlags flags);

#ifndef _MSC_VER
extern char *strdup(const char *s);
#endif

static void SetupBackBuffer(void) {
  /* create framebuffer texture */
  if (backbuffer != NULL)
    SDL_DestroyTexture(backbuffer);
  backbuffer = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                 SDL_TEXTUREACCESS_STREAMING, wnd_params.width,
                                 wnd_params.height);
  SDL_SetTextureScaleMode(backbuffer, crt_params.enable
                                          ? SDL_SCALEMODE_LINEAR
                                          : SDL_SCALEMODE_NEAREST);
}

/* calculate window dimensions based on fullscreen/windowed mode */
static void calculate_window_dimensions(const SDL_DisplayMode *mode,
                                        WindowFlags *flags, int *rflags) {
  if (!flags->fullscreen) {
    *rflags = 0;
    if (flags->factor == 0) {
      flags->factor = 1;
    }

    wnd_width = wnd_params.width * flags->factor;
    wnd_height = wnd_params.height * flags->factor;

    dstrect.x = 0.0f;
    dstrect.y = 0.0f;
    dstrect.w = (float)wnd_width;
    dstrect.h = (float)wnd_height;
    wnd_params.flags = flags->value;
  } else {
    *rflags = SDL_WINDOW_FULLSCREEN;
    wnd_width = mode->w;
    wnd_height = wnd_width * wnd_params.height / wnd_params.width;
    if (wnd_height > mode->h) {
      wnd_height = mode->h;
      wnd_width = wnd_height * wnd_params.width / wnd_params.height;
    }

    dstrect.x = (float)((mode->w - wnd_width) >> 1);
    dstrect.y = (float)((mode->h - wnd_height) >> 1);
    dstrect.w = (float)wnd_width;
    dstrect.h = (float)wnd_height;
  }
}

/* initialize default input mappings for PLAYER1 */
static void initialize_default_input(void) {
  TLN_EnableInput(PLAYER1, true);
  TLN_DefineInputKey(PLAYER1, INPUT_UP, SDLK_UP);
  TLN_DefineInputKey(PLAYER1, INPUT_DOWN, SDLK_DOWN);
  TLN_DefineInputKey(PLAYER1, INPUT_LEFT, SDLK_LEFT);
  TLN_DefineInputKey(PLAYER1, INPUT_RIGHT, SDLK_RIGHT);
  TLN_DefineInputKey(PLAYER1, INPUT_BUTTON1, SDLK_Z);
  TLN_DefineInputKey(PLAYER1, INPUT_BUTTON2, SDLK_X);
  TLN_DefineInputKey(PLAYER1, INPUT_BUTTON3, SDLK_C);
  TLN_DefineInputKey(PLAYER1, INPUT_BUTTON4, SDLK_V);
  TLN_DefineInputKey(PLAYER1, INPUT_START, SDLK_RETURN);
  TLN_DefineInputKey(PLAYER1, INPUT_QUIT, SDLK_ESCAPE);
  TLN_DefineInputKey(PLAYER1, INPUT_CRT, SDLK_BACKSPACE);
}

/* initialize joystick for PLAYER1 */
static void initialize_joystick(void) {
  int num_joysticks = 0;
  SDL_JoystickID *joysticks = SDL_GetJoysticks(&num_joysticks);
  if (joysticks != NULL && num_joysticks > 0) {
    SDL_SetJoystickEventsEnabled(true);
    TLN_AssignInputJoystick(PLAYER1, 0);
    TLN_DefineInputButton(PLAYER1, INPUT_BUTTON1, 1);
    TLN_DefineInputButton(PLAYER1, INPUT_BUTTON2, 0);
    TLN_DefineInputButton(PLAYER1, INPUT_BUTTON3, 2);
    TLN_DefineInputButton(PLAYER1, INPUT_BUTTON4, 3);
    TLN_DefineInputButton(PLAYER1, INPUT_START, 5);
    SDL_free(joysticks);
  }
}

/* calibrate timing for novsync mode */
static void calibrate_timing(WindowFlags flags) {
  if (!flags.novsync)
    return;

#if defined WIN32
  timeBeginPeriod(1);
#endif
  int c;
  uint32_t t0;
  SDL_Delay(1);
  t0 = (uint32_t)SDL_GetTicks();
  for (c = 0; c < 8; c += 1) {
    SDL_Delay(1);
  }
  wnd_params.min_delay = (uint32_t)((SDL_GetTicks() - t0) / c);

  /* capture actual monitor fps */
  SDL_Renderer *temp_renderer = SDL_CreateRenderer(window, NULL);
  if (temp_renderer != NULL) {
    SDL_SetRenderVSync(temp_renderer, 1);
    int target_fps = 0;
    SDL_RenderPresent(temp_renderer);
    t0 = (uint32_t)SDL_GetTicks();
    for (c = 0; c < 20; c += 1)
      SDL_RenderPresent(temp_renderer);
    target_fps = (c * 1000) / ((uint32_t)SDL_GetTicks() - t0);
    SDL_DestroyRenderer(temp_renderer);

    /* try "snapping" for common rates */
    uint8_t rates[] = {24, 30, 60, 75, 144, 200, 240};
    for (c = 0; c < sizeof(rates); c += 1) {
      if (abs(target_fps - (int)rates[c]) < 4) {
        target_fps = rates[c];
        break;
      }
    }
    engine->timing.target_fps = target_fps;
  }

#if defined WIN32
  timeEndPeriod(1);
#endif
}

/* create window delegate */
static bool create_window(void) {
  const SDL_DisplayMode *mode;
  int rflags;
  WindowFlags flags;
  flags.value = (uint8_t)wnd_params.flags;

  /*  gets desktop size and calculate window dimensions */
  mode = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
  calculate_window_dimensions(mode, &flags, &rflags);

  /* create window */
  if (window_title == NULL)
    window_title = strdup("Tilengine window");
  window = SDL_CreateWindow(window_title, wnd_width, wnd_height, rflags);
  if (!window) {
    delete_window();
    return false;
  }

  /* one time init, avoid being forgotten in Alt+TAB */
  if (init == false) {
    initialize_default_input();
    initialize_joystick();
    calibrate_timing(flags);
    init = true;
  }

  /* create render context */
  renderer = SDL_CreateRenderer(window, NULL);
  if (!renderer) {
    delete_window();
    return false;
  }
  if (!(wnd_params.flags & CWF_NOVSYNC))
    SDL_SetRenderVSync(renderer, 1);

  /* setup backbuffer & crt effect */
  SetupBackBuffer();
  crt = CRTCreate(renderer, backbuffer, crt_params.type, wnd_width, wnd_height,
                  crt_params.blur);

  if (wnd_params.flags & CWF_FULLSCREEN)
    SDL_HideCursor();

  done = false;
  return true;
}

/* resize the existing window to a new integer scale factor */
static void resize_window(int new_factor) {
  WindowFlags flags;
  flags.value = (uint8_t)wnd_params.flags;
  flags.factor = (uint8_t)new_factor;
  wnd_params.flags = flags.value;

  wnd_width = wnd_params.width * new_factor;
  wnd_height = wnd_params.height * new_factor;
  dstrect.x = 0.0f;
  dstrect.y = 0.0f;
  dstrect.w = (float)wnd_width;
  dstrect.h = (float)wnd_height;

  SDL_SetWindowSize(window, wnd_width, wnd_height);

  CRTDelete(crt);
  crt = CRTCreate(renderer, backbuffer, crt_params.type, wnd_width, wnd_height,
                  crt_params.blur);
}

/* destroy window delegate */
static void delete_window(void) {
  /* close all player joysticks */
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (player_inputs[i].joy != NULL) {
      SDL_CloseJoystick(player_inputs[i].joy);
      player_inputs[i].joy = NULL;
    }
  }

  CRTDelete(crt);
  crt = NULL;

  if (backbuffer) {
    SDL_DestroyTexture(backbuffer);
    backbuffer = NULL;
  }

  if (renderer) {
    SDL_DestroyRenderer(renderer);
    renderer = NULL;
  }

  if (window) {
    SDL_DestroyWindow(window);
    window = NULL;
  }
}

/*!
 * \brief
 * Sets window title
 *
 * \param title
 * Text with the title to set
 *
 */
void TLN_SetWindowTitle(const char *title) {
  if (window != NULL)
    SDL_SetWindowTitle(window, title);
  if (window_title != NULL) {
    free(window_title);
    window_title = NULL;
  }
  if (title != NULL)
    window_title = strdup(title);
}

static int WindowThread(void * /* data */) {
  bool ok;

  ok = create_window();
  if (ok == true)
    wnd_params.retval = 1;
  else {
    wnd_params.retval = 2;
    return 0;
  }

  /* main loop */
  while (TLN_IsWindowActive()) {
    SDL_LockMutex(lock);
    TLN_DrawFrame(0);
    SDL_SignalCondition(cond);
    SDL_UnlockMutex(lock);
    TLN_ProcessWindow();
  }
  return 0;
}

/*!
 * \brief
 * Creates a window for rendering
 *
 * \param flags
 * Mask of the possible creation flags:
 * CWF_FULLSCREEN, CWF_VSYNC, CWF_S1 - CWF_S5 (scaling factor, none = auto max)
 *
 * \returns
 * True if window was created or false if error
 *
 * Creates a host window with basic user input for tilengine. If fullscreen, it
 * uses the desktop resolution and stretches the output resolution with aspect
 * correction, letterboxing or pillarboxing as needed. If windowed, it creates a
 * centered window that is the maximum possible integer multiply of the
 * resolution configured at TLN_Init()
 *
 * \remarks
 * Using this feature is optional, Tilengine is designed to output its rendering
 * to a user-provided surface so it can be used as a backend renderer of an
 * already existing framework. But it is provided for convenience, so it isn't
 * needed to provide external components to run the examples or do engine tests.
 *
 * \see
 * TLN_DeleteWindow(), TLN_ProcessWindow(), TLN_GetInput(), TLN_DrawFrame()
 */
bool TLN_CreateWindow(int flags) {
  bool ok;

  /* allow single instance */
  if (instances) {
    instances++;
    return true;
  }

  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK))
    return false;

  /* fill parameters for window creation */
  wnd_params.width = TLN_GetWidth();
  wnd_params.height = TLN_GetHeight();
  wnd_params.flags = flags;

  crt_params.enable = (wnd_params.flags & CWF_NEAREST) == 0;
  ok = create_window();
  if (ok)
    instances++;
  return ok;
}

/*!
 * \brief
 * Creates a multithreaded window for rendering
 *
 * \param overlay
 * Deprecated parameter in 2.10, kept for compatibility. Set to NULL
 *
 * \param flags
 * Mask of the possible creation flags:
 * CWF_FULLSCREEN, CWF_VSYNC, CWF_S1 - CWF_S5 (scaling factor, none = auto max)
 *
 * \returns
 * True if window was created or false if error
 *
 * Creates a host window with basic user input for tilengine. If fullscreen, it
 * uses the desktop resolution and stretches the output resolution with aspect
 * correction, letterboxing or pillarboxing as needed. If windowed, it creates a
 * centered window that is the maximum possible integer multiply of the
 * resolution configured at TLN_Init()
 *
 * \remarks
 * Unlike TLN_CreateWindow, This window runs in its own thread
 *
 * \see
 * TLN_DeleteWindow(), TLN_IsWindowActive(), TLN_GetInput(), TLN_UpdateFrame()
 */
bool TLN_CreateWindowThread(int flags) {
  /* allow single instance */
  if (instances) {
    instances++;
    return true;
  }

  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK))
    return false;

  /* fill parameters for window creation */
  wnd_params.retval = 0;
  wnd_params.width = TLN_GetWidth();
  wnd_params.height = TLN_GetHeight();
  wnd_params.flags = flags;

  crt_params.enable = (wnd_params.flags & CWF_NEAREST) == 0;
  lock = SDL_CreateMutex();
  cond = SDL_CreateCondition();

  /* init thread & wait window creation result */
  thread = SDL_CreateThread(WindowThread, "WindowThread", &wnd_params);
  while (wnd_params.retval == 0)
    SDL_Delay(10);

  if (wnd_params.retval == 1)
    return true;
  else
    return false;
}

/*!
 * \brief
 * Deletes the window previoulsy created with TLN_CreateWindow() or
 * TLN_CreateWindowThread()
 *
 * \see
 * TLN_CreateWindow()
 */
void TLN_DeleteWindow(void) {
  /* single instance, delete when reach 0 */
  if (!instances)
    return;
  instances--;
  if (instances)
    return;

  delete_window();
  SDL_Quit();
  printf(" ");
}

/* marks input as pressed */
static void SetInput(TLN_Player player, TLN_Input input) {
  player_inputs[player].inputs |= (1 << input);
  last_key = input;
}

/* marks input as unpressed */
static void ClrInput(TLN_Player player, TLN_Input input) {
  player_inputs[player].inputs &= ~(1 << input);
}

/* process keyboard input */
static void ProcessKeycodeInput(TLN_Player player, SDL_Keycode keycode,
                                uint8_t state) {
  PlayerInput const *player_input = &player_inputs[player];
  TLN_Input input = INPUT_NONE;

  /* search input */
  for (int c = INPUT_UP; c < MAX_INPUTS; c++) {
    if (input != INPUT_NONE)
      break;
    if (player_input->keycodes[c] == keycode)
      input = (TLN_Input)c;
  }

  /* update */
  if (input != INPUT_NONE) {
    if (state)
      SetInput(player, input);
    else
      ClrInput(player, input);
  }
}

/* process joystick button input */
static void ProcessJoybuttonInput(TLN_Player player, uint8_t button,
                                  uint8_t state) {
  PlayerInput const *player_input = &player_inputs[player];
  TLN_Input input = INPUT_NONE;

  /* search input */
  for (int c = INPUT_BUTTON1; c < MAX_INPUTS; c++) {
    if (input != INPUT_NONE)
      break;
    if (player_input->joybuttons[c] == button)
      input = (TLN_Input)c;
  }

  /* update */
  if (input != INPUT_NONE) {
    if (state)
      SetInput(player, input);
    else
      ClrInput(player, input);
  }
}

/* process joystic axis input */
static void ProcessJoyaxisInput(TLN_Player player, uint8_t axis, int value) {
  if (axis == 0) {
    ClrInput(player, INPUT_LEFT);
    ClrInput(player, INPUT_RIGHT);
    if (value > 1000)
      SetInput(player, INPUT_RIGHT);
    else if (value < -1000)
      SetInput(player, INPUT_LEFT);
  } else if (axis == 1) {
    ClrInput(player, INPUT_UP);
    ClrInput(player, INPUT_DOWN);
    if (value > 1000)
      SetInput(player, INPUT_DOWN);
    else if (value < -1000)
      SetInput(player, INPUT_UP);
  }
}

/* process special keyboard inputs */
static void process_special_keys(SDL_Keycode key, uint16_t mod) {
  if (key == player_inputs[PLAYER1].keycodes[INPUT_QUIT]) {
    done = true;
  } else if (key == player_inputs[PLAYER1].keycodes[INPUT_CRT]) {
    crt_params.enable = !crt_params.enable;
    SetupBackBuffer();
    CRTSetRenderTarget(crt, backbuffer);
  } else if (key == SDLK_RETURN && mod & SDL_KMOD_ALT) {
    delete_window();
    wnd_params.flags ^= CWF_FULLSCREEN;
    create_window();
  } else if ((key == SDLK_PLUS || key == SDLK_KP_PLUS || key == SDLK_EQUALS) &&
             (mod & SDL_KMOD_CTRL)) {
    WindowFlags flags;
    flags.value = (uint8_t)wnd_params.flags;
    if (!flags.fullscreen) {
      const SDL_DisplayMode *mode =
          SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
      int new_factor = flags.factor + 1;
      if (mode && wnd_params.width * new_factor <= mode->w &&
          wnd_params.height * new_factor <= mode->h)
        resize_window(new_factor);
    }
  } else if ((key == SDLK_MINUS || key == SDLK_KP_MINUS) &&
             (mod & SDL_KMOD_CTRL)) {
    WindowFlags flags;
    flags.value = (uint8_t)wnd_params.flags;
    if (!flags.fullscreen && flags.factor > 1)
      resize_window(flags.factor - 1);
  }
}

/* apply a new integer scale factor, handling fullscreen→windowed transition */
static void set_window_scale(int factor) {
  WindowFlags flags;
  flags.value = (uint8_t)wnd_params.flags;
  if (factor == flags.factor)
    return;
  if (flags.fullscreen) {
    flags.factor = (uint8_t)factor;
    flags.fullscreen = false;
    wnd_params.flags = flags.value;
    delete_window();
    create_window();
  } else {
    resize_window(factor);
  }
}

/* process window scale override (Alt+1 through Alt+5) */
static void process_window_scale(SDL_Keycode key, uint16_t mod) {
  if (!(mod & SDL_KMOD_ALT))
    return;

  for (int c = 1; c <= 5; c += 1) {
    if (key == ('0' + c)) {
      set_window_scale(c);
      break;
    }
  }
}

/* process keyboard input for all enabled players */
static void process_all_players_keyinput(SDL_Keycode key, uint8_t down) {
  for (int c = PLAYER1; c < MAX_PLAYERS; c++) {
    if (player_inputs[c].enabled)
      ProcessKeycodeInput((TLN_Player)c, key, down);
  }
}

/* process joystick button for all enabled players */
static void process_all_players_joybutton(SDL_JoystickID which, uint8_t button,
                                          uint8_t down) {
  for (int c = PLAYER1; c < MAX_PLAYERS; c++) {
    if (player_inputs[c].enabled && player_inputs[c].joystick_id == which)
      ProcessJoybuttonInput((TLN_Player)c, button, down);
  }
}

/* process joystick axis for all enabled players */
static void process_all_players_joyaxis(SDL_JoystickID which, uint8_t axis,
                                        int value) {
  for (int c = PLAYER1; c < MAX_PLAYERS; c++) {
    if (player_inputs[c].enabled && player_inputs[c].joystick_id == which)
      ProcessJoyaxisInput((TLN_Player)c, axis, value);
  }
}

/*!
 * \brief
 * Does basic window housekeeping in signgle-threaded window
 *
 * \returns
 * True if window is active or false if the user has requested to end the
 * application (by pressing Esc key or clicking the close button)
 *
 * If a window has been created with TLN_CreateWindow, this function must be
 * called periodically (call it inside the main loop so it gets called
 * regularly). If the window was created with TLN_CreateWindowThread, do not use
 * it
 *
 * \see
 * TLN_CreateWindow()
 */
bool TLN_ProcessWindow(void) {
  SDL_Event evt;
  SDL_KeyboardEvent const *keybevt;
  SDL_JoyButtonEvent const *joybuttonevt;
  SDL_JoyAxisEvent const *joyaxisevt;

  if (done)
    return false;

  /* dispatch message queue */
  while (SDL_PollEvent(&evt)) {
    switch (evt.type) {
    case SDL_EVENT_QUIT:
      done = true;
      break;

    case SDL_EVENT_KEY_DOWN:
      keybevt = (SDL_KeyboardEvent *)&evt;
      if (keybevt->repeat == 0) {
        process_special_keys(keybevt->key, keybevt->mod);
        process_window_scale(keybevt->key, keybevt->mod);
        process_all_players_keyinput(keybevt->key, keybevt->down);
      }
      break;

    case SDL_EVENT_KEY_UP:
      keybevt = (SDL_KeyboardEvent *)&evt;
      process_all_players_keyinput(keybevt->key, keybevt->down);
      break;

    case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
    case SDL_EVENT_JOYSTICK_BUTTON_UP:
      joybuttonevt = (SDL_JoyButtonEvent *)&evt;
      process_all_players_joybutton(joybuttonevt->which, joybuttonevt->button,
                                    joybuttonevt->down);
      break;

    case SDL_EVENT_JOYSTICK_AXIS_MOTION:
      joyaxisevt = (SDL_JoyAxisEvent *)&evt;
      process_all_players_joyaxis(joyaxisevt->which, joyaxisevt->axis,
                                  joyaxisevt->value);
      break;

    default:
      break;
    }

    /* procesa eventos de usuario */
    if (sdl_callback != NULL)
      sdl_callback(&evt);
  }

  /* delete */
  if (done)
    TLN_DeleteWindow();

  return TLN_IsWindowActive();
}

/*!
 * \brief
 * Checks window state
 *
 * \returns
 * True if window is active or false if the user has requested to end the
 * application (by pressing Esc key or clicking the close button)
 *
 * \see
 * TLN_CreateWindow(), TLN_CreateWindowThread()
 */
bool TLN_IsWindowActive(void) { return !done; }

/*!
 * \brief
 * Thread synchronization for multithreaded window. Waits until the current
 * frame has ended rendering
 *
 * \see
 * TLN_CreateWindowThread()
 */
void TLN_WaitRedraw(void) {
  if (lock) {
    SDL_LockMutex(lock);
    SDL_WaitCondition(cond, lock);
    SDL_UnlockMutex(lock);
  }
}

/*!
 * \brief
 * Enables or disables optional horizontal blur in CRT effect
 *
 * \param mode
 * Enables or disables RF emulation on CRT effect
 */
void TLN_EnableRFBlur(bool mode) { CRTSetBlur(crt, mode); }

/*!
 * \brief
 * Enables CRT simulation post-processing effect to give true retro appeareance
 *
 * \param type One possible value of \ref TLN_CRT enumeration
 * \param blur Optional RF (horizontal) blur, increases CPU usage
 */

void TLN_ConfigCRTEffect(TLN_CRT type, bool blur) {
  if (crt != NULL)
    CRTDelete(crt);

  crt_params.type = (CRTType)type;
  crt_params.blur = blur;
  crt_params.enable = true;
  SetupBackBuffer();
  crt = CRTCreate(renderer, backbuffer, crt_params.type, wnd_width, wnd_height,
                  crt_params.blur);
}

/*!
 * \brief
 * Disables the CRT post-processing effect
 *
 * \see
 * TLN_ConfigCRTEffect
 */
void TLN_DisableCRTEffect(void) {
  crt_params.enable = false;
  SetupBackBuffer();
}

/*!
 * \brief
 * Returns the state of a given input
 *
 * \param input
 * Input to check state. It can be one of the following values:
 *	 * INPUT_UP
 *	 * INPUT_DOWN
 *	 * INPUT_LEFT
 *	 * INPUT_RIGHT
 *	 * INPUT_BUTTON1 - INPUT_BUTTON6,
 *	 * INPUT_START
 *	 * Optionally combine with INPUT_P1 to INPUT_P4 to request input for
 * specific player
 *
 * \returns
 * True if that input is pressed or false if not
 *
 * If a window has been created with TLN_CreateWindow, it provides basic user
 * input. It simulates a classic arcade setup, with 4 directional buttons
 * (INPUT_UP to INPUT_RIGHT), 6 action buttons (INPUT_BUTTON1 to INPUT_BUTTON6)
 * and a start button (INPUT_START). By default directional buttons are mapped
 * to keyboard cursors and joystick 1 D-PAD, and the first four action buttons
 * are the keys Z,X,C,V and joystick buttons 1 to 4.
 *
 * \see
 * TLN_CreateWindow(), TLN_DefineInputKey(), TLN_DefineInputButton()
 */
bool TLN_GetInput(TLN_Input input) {
  const TLN_Player player = (TLN_Player)(input >> 5);
  const uint32_t mask =
      (player_inputs[player].inputs & (1 << (input & INPUT_MASK)));
  if (mask)
    return true;
  return false;
}

/*!
 * \brief
 * Enables or disables input for specified player
 *
 * \param player
 * Player number to enable (PLAYER1 - PLAYER4)
 *
 * \param enable
 * Set true to enable, false to disable
 */
void TLN_EnableInput(TLN_Player player, bool enable) {
  player_inputs[player].enabled = enable;
}

/*!
 * \brief
 * Assigns a joystick index to the specified player
 *
 * \param player
 * Player number to configure (PLAYER1 - PLAYER4)
 *
 * \param index
 * Joystick index to assign, 0-based index. -1 = disable
 */
void TLN_AssignInputJoystick(TLN_Player player, int index) {
  PlayerInput *player_input = &player_inputs[player];
  if (player_input->joy != NULL) {
    SDL_CloseJoystick(player_input->joy);
    player_input->joy = NULL;
  }
  if (index >= 0) {
    int num_joysticks = 0;
    SDL_JoystickID *joysticks = SDL_GetJoysticks(&num_joysticks);
    if (joysticks != NULL && index < num_joysticks) {
      player_input->joy = SDL_OpenJoystick(joysticks[index]);
      player_input->joystick_id = (uint8_t)SDL_GetJoystickID(player_input->joy);
      SDL_free(joysticks);
    } else if (joysticks != NULL) {
      SDL_free(joysticks);
    }
  }
}

/*!
 * \brief
 * Assigns a keyboard input to a player
 *
 * \param player
 * Player number to configure (PLAYER1 - PLAYER4)
 *
 * \param input
 * Input to associate to the given key
 *
 * \param keycode
 * ASCII key value or scancode as defined in SDL.h
 */
void TLN_DefineInputKey(TLN_Player player, TLN_Input input, uint32_t keycode) {
  player_inputs[player].keycodes[input & INPUT_MASK] = keycode;
}

/*!
 * \brief
 * Assigns a button joystick input to a player
 *
 * \param player
 * Player number to configure (PLAYER1 - PLAYER4)
 *
 * \param input
 * Input to associate to the given button
 *
 * \param joybutton
 * Button index
 */
void TLN_DefineInputButton(TLN_Player player, TLN_Input input,
                           uint8_t joybutton) {
  player_inputs[player].joybuttons[input & INPUT_MASK] = joybutton;
}

/*!
 * \brief
 * Returns the last pressed input button
 *
 * \see
 * TLN_GetInput()
 */
int TLN_GetLastInput(void) {
  int retval = last_key;
  last_key = INPUT_NONE;
  return retval;
}

static void BeginWindowFrame(void) {
  wnd_params.t0 = (uint32_t)SDL_GetTicks();
  SDL_LockTexture(backbuffer, NULL, (void **)&rt_pixels, &rt_pitch);
  TLN_SetRenderTarget(rt_pixels, rt_pitch);
  if (wnd_params.fps_t0 == 0)
    wnd_params.fps_t0 = (uint32_t)SDL_GetTicks();
}

static void EndWindowFrame(void) {
  WindowFlags flags;
  flags.value = (uint8_t)wnd_params.flags;

  if (flags.fullscreen) {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
  }

  if (crt_params.enable && crt != NULL && flags.factor > 1)
    CRTDraw(crt, rt_pixels, rt_pitch, &dstrect);

  else {
    SDL_UnlockTexture(backbuffer);
    SDL_SetTextureBlendMode(backbuffer, SDL_BLENDMODE_NONE);
    SDL_RenderTexture(renderer, backbuffer, NULL, &dstrect);
  }

  /* no vsync: timed sync */
  if (flags.novsync) {
#if defined WIN32
    timeBeginPeriod(1);
#endif
    Engine const *context = TLN_GetContext();
    const int fps = context->timing.target_fps;
    /* sub-millisecond accumulator: tracks the fractional ms remainder so the
     * average frame interval converges to exactly 1000/fps ms even though
     * individual delays are whole milliseconds. */
    static uint32_t remainder = 0;
    static int last_fps = 0;
    if (fps != last_fps) {
      remainder = 0;
      last_fps = fps;
    }
    remainder += 1000;
    uint32_t delay_ms = remainder / (uint32_t)fps;
    remainder %= (uint32_t)fps;
    uint32_t due_time = wnd_params.t0 + delay_ms;
    uint32_t now = (uint32_t)SDL_GetTicks();
    while (now < due_time) {
      if (due_time - now > wnd_params.min_delay)
        SDL_Delay(wnd_params.min_delay);
      now = (uint32_t)SDL_GetTicks();
    }
#if defined WIN32
    timeEndPeriod(1);
#endif
  }

  SDL_RenderPresent(renderer);

  /* update averaged fps */
  const uint32_t now = (uint32_t)SDL_GetTicks();
  const uint32_t elapsed = now - wnd_params.fps_t0;
  wnd_params.fps_frames += 1;
  if (elapsed >= 500) {
    wnd_params.fps_average = (wnd_params.fps_frames * 1000) / elapsed;
    wnd_params.fps_frames = 0;
    wnd_params.fps_t0 = now;
  }
}

/*!
 * \brief
 * Draws a frame to the window
 *
 * \param frame Optional frame number. Set to 0 to autoincrement from previous
 * value
 *
 * Draws a frame to the window
 *
 * \remarks
 * If a window has been created with TLN_CreateWindow(), it renders the frame to
 * it. This function is a wrapper to TLN_UpdateFrame which also automatically
 * sets the render target for the window, so when calling this function it is
 * not needed to call TLN_UpdateFrame() too.
 *
 * \see
 * TLN_CreateWindow(), TLN_UpdateFrame()
 */
void TLN_DrawFrame(int frame) {
  BeginWindowFrame();
  TLN_UpdateFrame(frame);
  EndWindowFrame();
}

/*!
 * \brief
 * Returns the number of milliseconds since application start
 */
uint32_t TLN_GetTicks(void) { return (uint32_t)SDL_GetTicks(); }

/*!
 * \brief
 * Suspends execition for a fixed time
 * \param time Number of milliseconds to wait
 */
void TLN_Delay(uint32_t time) { SDL_Delay(time); }

/*!
 * \brief
 * Returns horizontal dimension of window after scaling
 */
int TLN_GetWindowWidth(void) { return wnd_width; }

/*!
 * \brief
 * Returns vertical dimension of window after scaling
 */
int TLN_GetWindowHeight(void) { return wnd_height; }

/*!
 * \brief
 * Registers a user-defined callback to capture internal SDL3 events
 * \param callback pointer to user funcion with signature void (SDL_Event*)
 */
void TLN_SetSDLCallback(TLN_SDLCallback callback) { sdl_callback = callback; }

/*!
 * \brief Returns averaged fps being rendered on the built-in window, updated
 * each 500 ms
 */
uint32_t TLN_GetAverageFps(void) { return wnd_params.fps_average; }

/*!
 * \brief Returns current window scaling factor.
 * \remarks This value can be set during call to TLN_CreateWindow() (flags
 * CWF_S1 to CWF_S5), calling TLN_SetWindowScaleFactor(), or pressing ALT-1 to
 * ALT-5 at runtime
 */
int TLN_GetWindowScaleFactor(void) {
  WindowFlags flags;
  flags.value = (uint8_t)wnd_params.flags;
  return flags.factor;
}

/*!
 * \brief Sets current window scaling factor
 */
void TLN_SetWindowScaleFactor(int factor) {
  WindowFlags flags;
  flags.value = (uint8_t)wnd_params.flags;
  flags.factor = (uint8_t)factor;
  wnd_params.flags = flags.value;
}

#endif
