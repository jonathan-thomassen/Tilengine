#include "Prop.h"
#include "Sandblock.h"
#include "Simon.h"
#include "Tilengine.h"
#include <stdio.h>
#include <string.h>

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
  if (!strcasecmp(info->name, "Simon")) {
    /* Tiled tile-object y is bottom of sprite; convert to top-left */
    SimonSetPosition(info->x, info->y - info->height);
  } else if (!strcasecmp(info->name, "Sandblock")) {
    SandblockSpawn(info->x, info->y - info->height);
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

/* entry point */
int main(int argc, char *argv[]) {
  TLN_Tilemap colission;
  TLN_Tilemap drawbridge_bg;
  TLN_Tilemap drawbridge_water;
  TLN_Tilemap drawbridge_main;
  TLN_Tilemap drawbridge_rocks;
  TLN_Tilemap hud;

  /* setup engine */
  TLN_Init(WIDTH, HEIGHT, 6, 1 + MAX_SANDBLOCKS + MAX_PROPS, 0);
  TLN_SetBGColor(0x10, 0x00, 0x20);

  /* load resources*/
  TLN_SetLoadPath("assets/sc4");
  colission = TLN_LoadTilemap("drawbridge_main.tmx", "Colission");
  drawbridge_bg = TLN_LoadTilemap("drawbridge_bg.tmx", NULL);
  drawbridge_water = TLN_LoadTilemap("drawbridge_water.tmx", NULL);
  drawbridge_main = TLN_LoadTilemap("drawbridge_main.tmx", "Tiles");
  drawbridge_rocks = TLN_LoadTilemap("drawbridge_rocks.tmx", NULL);
  hud = TLN_LoadTilemap("hud.tmx", NULL);
  TLN_SetLayerTilemap(COLISSION_LAYER, colission);
  TLN_SetLayerTilemap(BACKGROUND_LAYER, drawbridge_bg);
  TLN_SetLayerTilemap(WATER_LAYER, drawbridge_water);
  TLN_SetLayerTilemap(MAIN_LAYER, drawbridge_main);
  TLN_SetLayerTilemap(ROCKS_LAYER, drawbridge_rocks);
  TLN_SetLayerTilemap(HUD_LAYER, hud);

  SimonInit();
  SandblockInit();
  PropInit();

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
    printf("[objects] warning: could not load object layer '%s' from "
           "drawbridge_main.tmx\n",
           "Objects");
  }

  TLN_SetLayerBlendMode(ROCKS_LAYER, BLEND_MIX50);

  /* main loop */
  TLN_CreateWindow(CWF_NEAREST | CWF_S6 | CWF_NOVSYNC);
  TLN_SetTargetFps(60);

  while (TLN_ProcessWindow()) {
    char title[48];
    snprintf(title, sizeof(title), "FPS: %d", TLN_GetAverageFps());
    TLN_SetWindowTitle(title);
    SimonTasks();

    /* scroll */
    xpos = SimonGetPosition();
    SandblockTasks(xpos);
    PropTasks(xpos);
    TLN_SetLayerPosition(ROCKS_LAYER, xpos, 0);
    TLN_SetLayerPosition(MAIN_LAYER, xpos, 0);
    TLN_SetLayerPosition(WATER_LAYER, xpos, 0);
    TLN_SetLayerPosition(BACKGROUND_LAYER, xpos * 2 / 5, 0);
    TLN_SetLayerPosition(COLISSION_LAYER, xpos, 0);

    /* render to window */
    TLN_DrawFrame(0);
  }

  PropDeinit();
  SandblockDeinit();
  SimonDeinit();
  TLN_DeleteTilemap(colission);
  TLN_DeleteTilemap(drawbridge_bg);
  TLN_DeleteTilemap(drawbridge_rocks);
  TLN_DeleteTilemap(drawbridge_water);
  TLN_DeleteTilemap(drawbridge_main);
  TLN_DeleteTilemap(hud);
  TLN_Deinit();
  return 0;
}
