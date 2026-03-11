#include <SDL3/SDL_events.h>
#include <SDL3/SDL_timer.h>
#include <stdio.h>
#include <string.h>

#include "Drawbridge.h"
#include "Hud.h"
#include "Prop.h"
#include "Sandblock.h"
#include "Simon.h"
#include "Tilengine.h"
#include "Torch.h"

#define WIDTH 256
#define HEIGHT 224
#define HUD_LAYER 0
#define MAIN_LAYER 1
#define WATER_LAYER 2
#define BACKGROUND_LAYER 3

int xpos;

static int chain_prop_idx = -1;
static int pillar_prop_idx = -1;

/* Spawns a single Tiled object into the appropriate game system. */
static void spawn_object(TLN_ObjectInfo const *info) {
  if (!strcasecmp(info->name, "simon")) {
    /* Tiled tile-object y is bottom of sprite; convert to top-left */
    SimonSetPosition(info->x, info->y - info->height);
  } else if (!strcasecmp(info->name, "sandblock")) {
    SandblockSpawn(info->x, info->y - info->height);
  } else if (!strcasecmp(info->name, "torch")) {
    if (TorchSpawn(info->x, info->y - info->height) < 0)
      printf("[objects] could not spawn torch at (%d,%d)\n", info->x, info->y);
  } else if (!strcasecmp(info->name, "moon")) {
    /* Screen-fixed; renders behind all tilemap layers */
    if (PropSpawnBackground(info->name, info->x, info->y) < 0)
      printf("[objects] could not spawn background prop 'moon' at (%d,%d)\n",
             info->x, info->y);
  } else if (!strcasecmp(info->name, "chain")) {
    chain_prop_idx = PropSpawn(info->name, info->x, info->y);
    if (chain_prop_idx < 0)
      printf("[objects] could not spawn prop 'chain' at (%d,%d)\n", info->x,
             info->y);
  } else if (!strcasecmp(info->name, "pillar")) {
    pillar_prop_idx = PropSpawn(info->name, info->x, info->y);
    if (pillar_prop_idx < 0)
      printf("[objects] could not spawn prop 'pillar' at (%d,%d)\n", info->x,
             info->y);
  } else if (PropSpawn(info->name, info->x, info->y) < 0) {
    printf("[objects] could not spawn prop '%s' at (%d,%d)\n", info->name,
           info->x, info->y);
  }
}

/* Advances the drawbridge animation by one game step.
 * Loads the tilemap on the first call (skipping the tick so the first
 * rendered frame is always at progress=0), then ticks every subsequent call.
 * p_tilemap must point to a TLN_Tilemap variable initialised to NULL. */
static void step_drawbridge(TLN_Tilemap *p_tilemap, int *p_frame) {
  if (*p_tilemap == NULL) {
    *p_tilemap = TLN_LoadTilemap("drawbridge_drawbridge.tmx", NULL);
    TLN_SetLayerTilemap(MAIN_LAYER, *p_tilemap);
    TLN_SetLayerWindow(MAIN_LAYER, 0, 32, 256, 224, false);
    return; /* skip tick on load frame */
  }
  if (DrawbridgeTick())
    (*p_frame)++;
}

/* Pins Simon's feet to the drawbridge surface while the bridge is in motion
 * and pushes him rightward at a rate proportional to the bridge angle.
 * Moving left raises Simon (distance from hinge grows); moving right lowers
 * him. SimonSetFeetY also zeroes sy so gravity doesn't fight the override.
 * Once Simon passes the hinge he is on solid castle ground — stop tracking. */
static void step_simon_on_bridge(bool db_active) {
  if (SimonGetScreenX() + 16 < DrawbridgeHingeX()) {
    int surface_y = (int)DrawbridgeSurfaceY(SimonGetScreenX() + 16);
    if (!db_active) {
      /* Flat bridge (before animation): prevent falling through the deck
       * without touching sy or state — check_floor has no tiles here. */
      if (SimonGetFeetY() >= surface_y)
        SimonPinFeetY(surface_y);
      return;
    }
    if (DrawbridgeGetProgress() < 134) {
      /* Animated bridge: full landing snap — zeroes sy so gravity doesn't
       * fight the forced position as the surface rises under Simon. */
      if (SimonGetFeetY() >= surface_y)
        SimonSetFeetY(surface_y);
      /* Push Simon rightward by a rate proportional to the bridge angle:
       * accumulate progress (0→1) each frame; push 1px per whole unit.
       * Result: no push when flat, 1 px/frame when fully vertical. */
      static float push_acc = 0.0f;
      push_acc += (float)DrawbridgeGetProgress() / 134.0f;
      int push = (int)push_acc;
      push_acc -= (float)push;
      if (push > 0)
        SimonPushRight(push);
    }
  }
}

/* Updates all layer scroll positions whenever xpos changes. */
static void update_layer_positions(int scroll_x, bool db_triggered,
                                   int *p_prev_xpos) {
  if (scroll_x == *p_prev_xpos)
    return;
  int main_x_off = db_triggered ? 80 : 0;
  int main_y_off = db_triggered ? 8 : 0;
  TLN_SetLayerPosition(MAIN_LAYER, scroll_x + main_x_off, main_y_off);
  TLN_SetLayerPosition(WATER_LAYER, scroll_x, 0);
  TLN_SetLayerPosition(BACKGROUND_LAYER, scroll_x * 2 / 5, 0);
  TLN_SetLayerPosition(COLLISION_LAYER, scroll_x, 0);
  *p_prev_xpos = scroll_x;
}

/* Loads the object layer from drawbridge_main.tmx and spawns all entities. */
static void load_objects(void) {
  TLN_ObjectList objects = TLN_LoadObjectList("drawbridge_main.tmx", "Objects");
  if (objects != NULL) {
    TLN_ObjectInfo info;
    bool ok = TLN_GetListObject(objects, &info);
    while (ok) {
      spawn_object(&info);
      ok = TLN_GetListObject(objects, NULL);
    }
    TLN_DeleteObjectList(objects);
  } else {
    printf(
        "[objects] warning: could not load object layer '%s' from "
        "drawbridge_main.tmx\n",
        "Objects");
  }
}

/* Ensures Simon renders above props, then brings chain/pillar to the front. */
static void setup_entity_priorities(void) {
  SimonBringToFront();
  if (pillar_prop_idx >= 0) {
    PropBringToFront(pillar_prop_idx);
    PropSetPriority(pillar_prop_idx, true);
    PropEnableBlendMask(pillar_prop_idx, true);
  }
  if (chain_prop_idx >= 0) {
    PropBringToFront(chain_prop_idx);
    PropSetPriority(chain_prop_idx, true);
  }
}

/* Pins the chain sprite to the rotating drawbridge surface.
 * Screen x and y are looked up directly from baked tables.
 */
static void tick_chain_prop(bool db_triggered) {
  if (db_triggered && chain_prop_idx >= 0) {
    int cx;
    int cy;
    DrawbridgeChainPos(&cx, &cy);
    PropSetWorldPos(chain_prop_idx, cx + xpos, cy);
  }
}

/* Handles the Esc pause toggle; returns true while the game is paused. */
static bool process_pause(bool *p_paused, bool *p_esc_prev) {
  const bool *keys = SDL_GetKeyboardState(NULL);
  bool esc_now = keys[SDL_SCANCODE_ESCAPE];
  if (esc_now && !*p_esc_prev)
    *p_paused = !*p_paused;
  *p_esc_prev = esc_now;
  return *p_paused;
}

/* Increments the frame counter and updates the window title once per second. */
static void update_fps_title(uint32_t *p_t0, int *p_frames) {
  (*p_frames)++;
  uint32_t now = (uint32_t)SDL_GetTicks();
  if (now - *p_t0 >= 1000) {
    char title[32];
    SDL_snprintf(title, sizeof(title), "sc4 \xe2\x80\x94 %d fps", *p_frames);
    TLN_SetWindowTitle(title);
    *p_frames = 0;
    *p_t0 = now;
  }
}

/* entry point */
int main(void) {
  TLN_Tilemap collision;
  TLN_Tilemap drawbridge_bg;
  TLN_Tilemap drawbridge_water;
  TLN_Tilemap drawbridge_main;
  TLN_Tilemap hud;
  TLN_Tilemap drawbridge_drawbridge = NULL;

  /* setup engine */
  TLN_Init(WIDTH, HEIGHT, 7, 1 + MAX_SANDBLOCKS + MAX_TORCHES + MAX_PROPS + 1,
           0);
  TLN_SetBGColor(0x10, 0x00, 0x20);

  /* load resources*/
  TLN_SetLoadPath("assets");
  collision = TLN_LoadTilemap("drawbridge_main.tmx", "Collision");
  drawbridge_bg = TLN_LoadTilemap("drawbridge_bg.tmx", NULL);
  drawbridge_water = TLN_LoadTilemap("drawbridge_water.tmx", NULL);
  drawbridge_main = TLN_LoadTilemap("drawbridge_main.tmx", "Tiles");
  hud = TLN_LoadTilemap("hud.tmx", NULL);
  TLN_SetLayerTilemap(COLLISION_LAYER, collision);
  TLN_SetLayerTilemap(BACKGROUND_LAYER, drawbridge_bg);
  TLN_SetLayerTilemap(WATER_LAYER, drawbridge_water);
  TLN_SetLayerTilemap(MAIN_LAYER, drawbridge_main);

  DrawbridgeInit(MAIN_LAYER, 221, 183);

  TLN_SetLayerTilemap(HUD_LAYER, hud);
  TLN_SetLayerPriority(HUD_LAYER, true);

  SimonInit();
  SandblockInit();
  TorchInit();
  PropInit();
  HudInit(hud);

  /* place entities from the object layer */
  load_objects();

  /* Ensure Simon renders on top of all spawned torches and props,
   * then bring the chain in front of Simon and in front of priority tiles. */
  setup_entity_priorities();

  TLN_SetLayerBlendMask(MAIN_LAYER, WATER_LAYER);
  TLN_SetLayerBlendMode(MAIN_LAYER, BLEND_MIX50);

  /* main loop */
  TLN_CreateWindow(CWF_NEAREST | CWF_S6 | CWF_NOVSYNC);
  TLN_DefineInputKey(PLAYER1, INPUT_QUIT, SDLK_F4);
  TLN_SetTargetFps(60);

  uint32_t fps_t0 = (uint32_t)SDL_GetTicks();
  int fps_frames = 0;
  bool paused = false;
  bool esc_prev = false;

  while (TLN_ProcessWindow()) {
    if (process_pause(&paused, &esc_prev)) {
      SDL_Delay(16); /* throttle loop; last frame stays visible */
      continue;
    }

    SimonTasks();
    HudTasks();

    /* scroll */
    xpos = SimonGetPosition();

    /* Rails: once xpos reaches 633 the camera locks and auto-scrolls right
     * to the end of the tilemap over ~5 seconds (300 frames at 60 fps). */
    static bool rails_triggered = false;
    static int rails_pos = 0;
    static int rails_step = 0;
    static int rails_max = 0;
    static int db_frame = 0;
    static bool db_triggered = false;
    static int prev_xpos = -1;
    if (!rails_triggered && xpos >= 640) {
      rails_triggered = true;
      rails_pos = xpos;
      rails_max = TLN_GetLayerWidth(MAIN_LAYER) - WIDTH;
      rails_step = (rails_max - rails_pos) / 60;
      SimonFreezeCamera();
      SimonSetScreenX(SimonGetScreenX() + 16);
    }
    int camera_delta = 0;
    if (rails_triggered) {
      rails_pos += rails_step;
      if (rails_pos > rails_max)
        rails_pos = rails_max;
      xpos = rails_pos;
      /* prev_xpos holds last frame's camera position (set by
       * update_layer_positions). The difference is how far the camera moved
       * this frame; we'll subtract it from Simon's screen x to keep him
       * stationary in world space unless the player moves him. */
      camera_delta = xpos - prev_xpos;
    }

    /* drawbridge animation: triggered once xpos reaches 768, then runs to
     * completion regardless of player position.
     * (134 ticks × 9 game frames/tick ≈ 1197 frames at 60 fps = 20 s). */
    if (!db_triggered && xpos >= 768) {
      db_triggered = true;
      /* The drawbridge layer is shifted up by main_y_off=8 when db_triggered.
       * Update the hinge Y to a screen-space coordinate so DrawbridgeSurfaceY
       * returns the correct screen Y for SimonSetFeetY (otherwise Simon sinks
       * 8px into the bridge surface). */
      DrawbridgeSetHinge(DrawbridgeHingeX(), 183 - 8);
      SimonFreezeCamera();
    }
    if (db_triggered && db_frame < 134)
      step_drawbridge(&drawbridge_drawbridge, &db_frame);
    if (db_triggered)
      DrawbridgeSetProgress(db_frame);

    if (rails_triggered)
      step_simon_on_bridge(db_triggered);
    /* Hard clamp: once the bridge is fully raised it acts as a wall;
     * allow Simon up to 32px closer than the hinge. */
    if (db_frame >= 134 && SimonGetScreenX() < DrawbridgeHingeX() - 32)
      SimonSetScreenX(DrawbridgeHingeX() - 32);

    /* Compensate Simon's screen x for camera movement so his world position
     * stays fixed during the rails scroll. Player input applied by
     * SimonTasks() is preserved because it changes screen x before this
     * correction. Stop compensating once the drawbridge takes over. */
    if (rails_triggered && !db_triggered)
      SimonSetScreenX(SimonGetScreenX() - camera_delta);

    /* Camera is locked by SimonFreezeCamera(); xpos stays at its
     * frozen value for all layer positions. */

    tick_chain_prop(db_triggered);

    SandblockTasks(xpos);
    TorchTasks(xpos);
    PropTasks(xpos);
    DrawbridgeTasks();
    update_layer_positions(xpos, db_triggered, &prev_xpos);

    /* render to window */
    update_fps_title(&fps_t0, &fps_frames);
    TLN_DrawFrame(0);
  }

  PropDeinit();
  TorchDeinit();
  SandblockDeinit();
  SimonDeinit();
  TLN_DeleteTilemap(collision);
  TLN_DeleteTilemap(drawbridge_bg);
  TLN_DeleteTilemap(drawbridge_water);
  TLN_DeleteTilemap(drawbridge_main);
  TLN_DeleteTilemap(drawbridge_drawbridge);
  TLN_DeleteTilemap(hud);
  TLN_Deinit();
  return 0;
}
