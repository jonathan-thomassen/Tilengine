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
    TLN_SetLayerWindow(MAIN_LAYER, 0, 32, 256, 192, false);
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
static void step_simon_on_bridge(int db_frame) {
  if (db_frame > 0 && SimonGetScreenX() < DrawbridgeHingeX()) {
    SimonSetFeetY((int)DrawbridgeSurfaceY(SimonGetScreenX()));
    /* Push Simon rightward by a rate proportional to the bridge angle:
     * accumulate progress (0→1) each frame; push 1px per whole unit.
     * Result: no push when flat, 1 px/frame when fully vertical. */
    static float push_acc = 0.0f;
    push_acc += DrawbridgeGetProgress();
    int push = (int)push_acc;
    push_acc -= (float)push;
    if (push > 0)
      SimonPushRight(push);
  }
}

/* Raster callback: applies BLEND_MIX50 to MAIN_LAYER only on scanlines where
 * the water layer has a tile beneath them, so main-layer pixels above the moat
 * appear translucent over the water.  Water tiles start at tilemap x=512
 * (tile column 64, 8 px/tile).  vstart for the water layer is always 0, so the
 * tilemap y equals the screen scanline directly. */
static void raster_cb(int scanline) {
  TLN_TileInfo ti;
  /* Pick a tilemap-x guaranteed to be inside the moat (world x >= 512).
   * When scrolled past the moat start use the left screen edge (xpos);
   * otherwise anchor to x=512, the first tile column of the moat. */
  int sample_x = (xpos >= 512) ? xpos : 512;
  bool has_water = (sample_x < xpos + WIDTH) &&
                   TLN_GetLayerTile(WATER_LAYER, sample_x, scanline, &ti) &&
                   (ti.index != 0);
  TLN_SetLayerBlendMode(MAIN_LAYER, has_water ? BLEND_MIX50 : BLEND_NONE);
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

  /* Ensure Simon renders on top of all spawned torches and props,
   * then bring the chain in front of Simon and in front of priority tiles. */
  SimonBringToFront();
  if (pillar_prop_idx >= 0) {
    PropBringToFront(pillar_prop_idx);
    PropSetPriority(pillar_prop_idx, true);
  }
  if (chain_prop_idx >= 0) {
    PropBringToFront(chain_prop_idx);
    PropSetPriority(chain_prop_idx, true);
  }

  /* main loop */
  TLN_CreateWindow(CWF_NEAREST | CWF_S6 | CWF_NOVSYNC);
  TLN_SetRasterCallback(raster_cb);
  TLN_SetTargetFps(60);

  uint32_t fps_t0 = (uint32_t)SDL_GetTicks();
  int fps_frames = 0;
  char fps_title[32];

  while (TLN_ProcessWindow()) {
    SimonTasks();
    HudTasks();

    /* scroll */
    xpos = SimonGetPosition();

    /* drawbridge animation: triggered once xpos reaches 768, then runs to
     * completion regardless of player position.
     * (134 ticks × 9 game frames/tick ≈ 1197 frames at 60 fps = 20 s). */
    static int db_frame = 0;
    static bool db_triggered = false;
    static int prev_xpos = -1;
    if (!db_triggered && xpos >= 768) {
      db_triggered = true;
      SimonFreezeCamera();
    }
    if (db_triggered && db_frame < 134)
      step_drawbridge(&drawbridge_drawbridge, &db_frame);
    if (db_triggered)
      DrawbridgeSetProgress((float)db_frame / 134.0f);

    step_simon_on_bridge(db_frame);

    /* Camera is locked by SimonFreezeCamera(); xpos stays at its
     * frozen value for all layer positions. */

    /* Pin the chain's bottom-left corner to tile 27 (the link) in the
     * drawbridge layer. That tile rests at screen (116, 152) when flat
     * (tilemap px (848,160), layer offset (848,8), +116 px horizontal adjust).
     * The chain sprite is 128 px tall, so the top-left is 128 px above. */
    if (db_triggered && chain_prop_idx >= 0) {
      float lx;
      float ly;
      DrawbridgeRotatedPoint(80.0f, 168.0f, &lx, &ly);
      /* lx/ly is the bottom-left in screen coords; convert to world.
       * Apply a linear drift correction: the chain rides 8 px too high by
       * progress=1, so compensate by adding 8*progress to the y offset. */
      float drift_y = 8.0f * DrawbridgeGetProgress();
      PropSetWorldPos(chain_prop_idx, (int)(lx + 0.5f) + xpos,
                      (int)(ly + drift_y + 0.5f) - 128);
    }

    SandblockTasks(xpos);
    TorchTasks(xpos);
    PropTasks(xpos);
    DrawbridgeTasks();
    update_layer_positions(xpos, db_triggered, &prev_xpos);

    /* render to window */
    fps_frames++;
    uint32_t fps_now = (uint32_t)SDL_GetTicks();
    if (fps_now - fps_t0 >= 1000) {
      SDL_snprintf(fps_title, sizeof(fps_title), "sc4 — %d fps", fps_frames);
      TLN_SetWindowTitle(fps_title);
      fps_frames = 0;
      fps_t0 = fps_now;
    }
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
