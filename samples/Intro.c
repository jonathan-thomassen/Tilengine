#include "Pillar.h"
#include "Sandblock.h"
#include "Simon.h"
#include "Tilengine.h"
#include <stdio.h>
#include <string.h>

#define WIDTH 256
#define HEIGHT 224
#define OBJECT_LAYER "Objects"
#define TILE_LAYER "Tiles"
#define COLISSION_LAYER "Colission"

int xpos;

/* entry point */
int main(int argc, char *argv[]) {
  TLN_Tilemap drawbridge_bg;
  TLN_Tilemap drawbridge_rocks;
  TLN_Tilemap drawbridge_water;
  TLN_Tilemap drawbridge_main;
  TLN_Tilemap colission;

  /* setup engine */
  TLN_Init(WIDTH, HEIGHT, 5, 1 + MAX_SANDBLOCKS + MAX_PILLARS, 0);
  TLN_SetBGColor(0x10, 0x00, 0x20);

  /* load resources*/
  TLN_SetLoadPath("assets/sc4");
  drawbridge_bg = TLN_LoadTilemap("drawbridge_bg.tmx", NULL);
  drawbridge_rocks = TLN_LoadTilemap("drawbridge_rocks.tmx", NULL);
  drawbridge_water = TLN_LoadTilemap("drawbridge_water.tmx", NULL);
  drawbridge_main = TLN_LoadTilemap("drawbridge_main.tmx", TILE_LAYER);
  colission = TLN_LoadTilemap("drawbridge_main.tmx", COLISSION_LAYER);
  TLN_SetLayerTilemap(4, colission);
  TLN_SetLayerTilemap(3, drawbridge_bg);
  TLN_SetLayerTilemap(2, drawbridge_water);
  TLN_SetLayerTilemap(1, drawbridge_main);
  TLN_SetLayerTilemap(0, drawbridge_rocks);

  SimonInit();
  SandblockInit();
  PillarInit();

  /* place entities from the object layer */
  TLN_ObjectList objects =
      TLN_LoadObjectList("drawbridge_main.tmx", OBJECT_LAYER);
  if (objects != NULL) {
    TLN_ObjectInfo info;
    bool ok = TLN_GetListObject(objects, &info);
    while (ok) {
      if (!strcasecmp(info.name, "Simon")) {
        /* Tiled tile-object y is bottom of sprite; convert to top-left */
        SimonSetPosition(info.x, info.y - info.height);
      } else if (!strcasecmp(info.name, "Sandblock")) {
        SandblockSpawn(info.x, info.y - info.height);
      } else if (!strcasecmp(info.name, "Pillar")) {
        PillarSpawn(info.x, info.y - info.height);
      } else {
        printf("[objects] unknown object '%s' at (%d,%d)\n", info.name, info.x,
               info.y);
      }
      ok = TLN_GetListObject(objects, NULL);
    }
    TLN_DeleteObjectList(objects);
  } else {
    printf("[objects] warning: could not load object layer '%s' from "
           "drawbridge_main.tmx\n",
           OBJECT_LAYER);
  }

  TLN_SetLayerBlendMode(0, BLEND_MIX50);

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
    PillarTasks(xpos);
    TLN_SetLayerPosition(0, xpos, 0);
    TLN_SetLayerPosition(1, xpos, 0);
    TLN_SetLayerPosition(2, xpos, 0);
    TLN_SetLayerPosition(3, xpos * 2 / 5, 0);
    TLN_SetLayerPosition(4, xpos, 0);

    /* render to window */
    TLN_DrawFrame(0);
  }

  PillarDeinit();
  SandblockDeinit();
  SimonDeinit();
  TLN_DeleteTilemap(colission);
  TLN_DeleteTilemap(drawbridge_bg);
  TLN_DeleteTilemap(drawbridge_rocks);
  TLN_DeleteTilemap(drawbridge_water);
  TLN_DeleteTilemap(drawbridge_main);
  TLN_Deinit();
  return 0;
}
