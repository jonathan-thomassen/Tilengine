#include <SDL3/SDL_events.h>
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
#define ROCKS_LAYER 1
#define MAIN_LAYER 2
#define WATER_LAYER 3
#define BACKGROUND_LAYER 4

int xpos;

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

/* entry point */
int main(void) {
  TLN_Tilemap collision;
  TLN_Tilemap drawbridge_bg;
  TLN_Tilemap drawbridge_water;
  TLN_Tilemap drawbridge_main;
  TLN_Tilemap drawbridge_rocks;
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
  drawbridge_rocks = TLN_LoadTilemap("drawbridge_rocks.tmx", NULL);
  hud = TLN_LoadTilemap("hud.tmx", NULL);
  TLN_SetLayerTilemap(COLLISION_LAYER, collision);
  TLN_SetLayerTilemap(BACKGROUND_LAYER, drawbridge_bg);
  TLN_SetLayerTilemap(WATER_LAYER, drawbridge_water);
  TLN_SetLayerTilemap(MAIN_LAYER, drawbridge_main);
  TLN_SetLayerTilemap(ROCKS_LAYER, drawbridge_rocks);

  DrawbridgeInit(MAIN_LAYER, 221, 183);

  TLN_SetLayerTilemap(HUD_LAYER, hud);

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

  /* Ensure Simon renders on top of all spawned torches and props. */
  SimonBringToFront();

  TLN_SetLayerBlendMode(ROCKS_LAYER, BLEND_MIX50);

  /* main loop */
  TLN_CreateWindow(CWF_NEAREST | CWF_S6 | CWF_NOVSYNC);
  TLN_SetTargetFps(60);

  while (TLN_ProcessWindow()) {
    char title[48];
    snprintf(title, sizeof(title), "FPS: %d", TLN_GetAverageFps());
    TLN_SetWindowTitle(title);
    SimonTasks();
    HudTasks();

    /* scroll */
    xpos = SimonGetPosition();

    /* drawbridge animation: triggered once xpos reaches 768, then runs to
     * completion regardless of player position.
     * (134 ticks × 9 game frames/tick ≈ 1197 frames at 60 fps = 20 s). */
    static int db_frame = 0;
    static bool db_triggered = false;
    if (!db_triggered && xpos >= 768) {
      db_triggered = true;
      SimonFreezeCamera();
    }
    if (db_triggered && db_frame < 134)
      step_drawbridge(&drawbridge_drawbridge, &db_frame);
    DrawbridgeSetProgress((float)db_frame / 134.0f);

    /* Push Simon rightward as the bridge rises; accumulate fractional pixels
     * so the force builds smoothly from the first frame. */
    if (db_frame > 0) {
      static float push_acc = 0.0f;
      float p = (float)db_frame / 134.0f;
      push_acc += p * 2.5f;
      int push = (int)push_acc;
      push_acc -= (float)push;
      SimonPushRight(push);
      SimonSetFeetY((int)DrawbridgeSurfaceY(SimonGetScreenX()) - 4);
    }

    /* Camera is locked by SimonFreezeCamera(); xpos stays at its
     * frozen value for all layer positions. */

    SandblockTasks(xpos);
    TorchTasks(xpos);
    PropTasks(xpos);
    DrawbridgeTasks();
    TLN_SetLayerPosition(ROCKS_LAYER, xpos, 0);
    TLN_SetLayerPosition(MAIN_LAYER, xpos + (db_triggered ? 80 : 0),
                         (db_triggered ? 8 : 0));
    TLN_SetLayerPosition(WATER_LAYER, xpos, 0);
    TLN_SetLayerPosition(BACKGROUND_LAYER, xpos * 2 / 5, 0);
    TLN_SetLayerPosition(COLLISION_LAYER, xpos, 0);

    /* render to window */
    TLN_DrawFrame(0);
  }

  PropDeinit();
  TorchDeinit();
  SandblockDeinit();
  SimonDeinit();
  TLN_DeleteTilemap(collision);
  TLN_DeleteTilemap(drawbridge_bg);
  TLN_DeleteTilemap(drawbridge_rocks);
  TLN_DeleteTilemap(drawbridge_water);
  TLN_DeleteTilemap(drawbridge_main);
  TLN_DeleteTilemap(drawbridge_drawbridge);
  TLN_DeleteTilemap(hud);
  TLN_Deinit();
  return 0;
}
