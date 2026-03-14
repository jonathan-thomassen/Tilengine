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
#define NUM_LAYERS 7

#define TARGET_FPS 60

#define HINGE_X 221
#define HINGE_Y 183
#define DB_TRIGGER_X 768     /* world-x where drawbridge animation starts */
#define DB_LAYER_X_OFFSET 80 /* main-layer x shift once drawbridge triggers */
#define DB_LAYER_Y_OFFSET 8  /* main-layer y shift once drawbridge triggers */
#define DB_WINDOW_TOP 32     /* top clip row for the drawbridge tilemap */
#define DB_WALL_MARGIN 32    /* pixels before the hinge that act as a wall */
#define RAILS_STEP 3         /* on-rails camera scroll speed */
#define RAILS_TRIGGER_X 640  /* world-x where camera locks and auto-scrolls */
#define SIMON_X_OFFSET 24    /* offset from Simon's left edge to his foot */

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
      fprintf(stderr, "[objects] could not spawn torch at (%d,%d)\n", info->x,
              info->y);
  } else if (!strcasecmp(info->name, "moon")) {
    /* Screen-fixed; renders behind all tilemap layers */
    if (PropSpawnBackground(info->name, info->x, info->y) < 0)
      fprintf(stderr,
              "[objects] could not spawn background prop 'moon' at (%d,%d)\n",
              info->x, info->y);
  } else if (!strcasecmp(info->name, "chain")) {
    chain_prop_idx = PropSpawn(info->name, info->x, info->y);
    if (chain_prop_idx < 0)
      fprintf(stderr, "[objects] could not spawn prop 'chain' at (%d,%d)\n",
              info->x, info->y);
  } else if (!strcasecmp(info->name, "pillar")) {
    pillar_prop_idx = PropSpawn(info->name, info->x, info->y);
    if (pillar_prop_idx < 0)
      fprintf(stderr, "[objects] could not spawn prop 'pillar' at (%d,%d)\n",
              info->x, info->y);
  } else if (PropSpawn(info->name, info->x, info->y) < 0) {
    fprintf(stderr, "[objects] could not spawn prop '%s' at (%d,%d)\n",
            info->name, info->x, info->y);
  }
}

/* Pushes Simon rightward proportional to the bridge angle (Bresenham).
 * Only active while the bridge is animating (db_active) and Simon is left
 * of the hinge.  The bridge floor itself is set before SimonTasks() via
 * SimonSetBridgeFloor() so physics handles the surface — no snap needed. */
static void step_simon_on_bridge(bool db_active) {
  if (!db_active || DrawbridgeGetProgress() >= DB_STEPS - 1)
    return;
  if (SimonGetScreenX() + SIMON_X_OFFSET >= DrawbridgeHingeX())
    return;
  int p = DrawbridgeGetProgress();
  int push = p / 30 < 3 ? p / 30 : 3;
  if (push > 0)
    SimonPushRight(push);
}

/* Updates all layer scroll positions whenever xpos changes. */
static void update_layer_positions(int scroll_x, bool db_triggered,
                                   int *p_prev_xpos) {
  if (scroll_x == *p_prev_xpos)
    return;
  int main_x_off = db_triggered ? DB_LAYER_X_OFFSET : 0;
  int main_y_off = db_triggered ? DB_LAYER_Y_OFFSET : 0;
  TLN_SetLayerPosition(MAIN_LAYER, scroll_x + main_x_off, main_y_off);
  TLN_SetLayerPosition(WATER_LAYER, scroll_x, 0);
  TLN_SetLayerPosition(BACKGROUND_LAYER, scroll_x * 2 / 5, 0);
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
    fprintf(stderr,
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
 * Screen x and y are looked up directly from baked tables. */
static void tick_chain_prop(bool db_triggered, int xpos) {
  if (db_triggered && chain_prop_idx >= 0) {
    ChainPos cp = DrawbridgeChainPos();
    PropSetWorldPos(chain_prop_idx, cp.x + xpos, cp.y);
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
static void update_fps_title(Uint64 *p_t0, int *p_frames) {
  (*p_frames)++;
  Uint64 now = SDL_GetTicks();
  if (now - *p_t0 >= 1000) {
    char title[32];
    SDL_snprintf(title, sizeof(title), "sc4 - %d fps", *p_frames);
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
  TLN_Tilemap drawbridge_bridge = NULL;

  /* setup engine */
  TLN_Init(WIDTH, HEIGHT, NUM_LAYERS,
           1 + MAX_SANDBLOCKS + MAX_TORCHES + MAX_PROPS + 1, 0);
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

  DrawbridgeInit(MAIN_LAYER, HINGE_X, HINGE_Y);

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
  TLN_SetTargetFps(TARGET_FPS);

  Uint64 fps_t0 = SDL_GetTicks();
  int fps_frames = 0;
  bool paused = false;
  bool esc_prev = false;
  bool rails_triggered = false;
  int rails_pos = 0;
  int rails_max = 0;
  int xpos = 0;
  bool db_triggered = false;
  int prev_xpos = -1;

  while (TLN_ProcessWindow()) {
    if (process_pause(&paused, &esc_prev)) {
      SDL_Delay(1000 /
                TARGET_FPS); /* throttle loop; last frame stays visible */
      continue;
    }

    /* Set bridge floor BEFORE SimonTasks so the bridge surface acts as a
     * true physics floor inside apply_collisions, replacing the tile check
     * that would otherwise fight the bridge geometry.
     * Guard on db_triggered (not rails_triggered): the bridge only animates
     * after xpos >= DB_TRIGGER_X; before that, normal tile floor collision must
     * apply for the castle approach geometry to work correctly. */
    SimonClearBridgeFloor();
    if (db_triggered && SimonGetScreenX() + SIMON_X_OFFSET < DrawbridgeHingeX())
      SimonSetBridgeFloor(
          DrawbridgeSurfaceY(SimonGetScreenX() + SIMON_X_OFFSET));
    SimonTasks();
    HudTasks();

    /* scroll */
    xpos = SimonGetPosition();

    /* Rails: once xpos reaches RAILS_TRIGGER_X the camera locks and
     * auto-scrolls right at 3 pixels per frame. */
    if (!rails_triggered && xpos >= RAILS_TRIGGER_X) {
      rails_triggered = true;
      rails_pos = xpos;
      rails_max = TLN_GetLayerWidth(MAIN_LAYER) - WIDTH;
      SimonFreezeCamera();
      SimonSetScreenX(SimonGetScreenX() + 8);
    }
    int camera_delta = 0;
    if (rails_triggered) {
      rails_pos += RAILS_STEP;
      if (rails_pos > rails_max)
        rails_pos = rails_max;
      xpos = rails_pos;
      /* prev_xpos holds last frame's camera position (set by
       * update_layer_positions). The difference is how far the camera moved
       * this frame; we'll subtract it from Simon's screen x to keep him
       * stationary in world space unless the player moves him. */
      camera_delta = xpos - prev_xpos;
    }

    if (db_triggered && DrawbridgeGetProgress() < DB_STEPS - 1 &&
        DrawbridgeTick()) {
      DrawbridgeAdvance();
    }

    /* drawbridge animation: triggered once xpos reaches DB_TRIGGER_X, then runs
     * to completion regardless of player position. (DB_STEPS - 1 ticks ×
     * DB_TICK_RATE game frames/tick ≈ 1197 frames at TARGET_FPS fps = 20 s). */
    if (!db_triggered && xpos >= DB_TRIGGER_X) {
      db_triggered = true;

      drawbridge_bridge = TLN_LoadTilemap("drawbridge_drawbridge.tmx", NULL);
      TLN_SetLayerTilemap(MAIN_LAYER, drawbridge_bridge);
      TLN_SetLayerWindow(MAIN_LAYER, 0, DB_WINDOW_TOP, WIDTH, HEIGHT, false);

      /* The drawbridge layer is shifted up by main_y_off=DB_LAYER_Y_OFFSET when
       * db_triggered. Update the hinge Y to a screen-space coordinate so
       * DrawbridgeSurfaceY returns the correct screen Y for SimonSetFeetY
       * (otherwise Simon sinks DB_LAYER_Y_OFFSET px into the bridge surface).
       */
      DrawbridgeSetHinge(DrawbridgeHingeX(), HINGE_Y - DB_LAYER_Y_OFFSET);
    }

    /* Hard clamp: once the bridge is fully raised it acts as a wall;
     * allow Simon up to DB_WALL_MARGIN px closer than the hinge. */
    if (DrawbridgeGetProgress() >= DB_STEPS - 1 &&
        SimonGetScreenX() < DrawbridgeHingeX() - DB_WALL_MARGIN)
      SimonSetScreenX(DrawbridgeHingeX() - DB_WALL_MARGIN);

    /* Compensate Simon's screen x for camera movement so his world position
     * stays fixed during the rails scroll. Player input applied by
     * SimonTasks() is preserved because it changes screen x before this
     * correction. Stop compensating once the drawbridge takes over. */
    if (rails_triggered && !db_triggered)
      SimonSetScreenX(SimonGetScreenX() - camera_delta);

    /* Keep Simon's internal world-x in sync with the camera so that tile
     * collision queries (check_floor, check_ceiling, check_wall_*) sample
     * the correct tilemap column.  Without this, xworld stays frozen at the
     * rails-trigger point while xpos advances, causing check_floor to miss
     * the floor tiles and making Simon fall through the ground. */
    if (rails_triggered)
      SimonSetWorldX(xpos);

    /* Camera is locked by SimonFreezeCamera(); xpos stays at its
     * frozen value for all layer positions. */
    step_simon_on_bridge(db_triggered);

    tick_chain_prop(db_triggered, xpos);

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
  if (drawbridge_bridge)
    TLN_DeleteTilemap(drawbridge_bridge);
  TLN_DeleteTilemap(hud);
  TLN_Deinit();
  return 0;
}
