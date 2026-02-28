#include "Simon.h"
#include "Tilengine.h"
#include <stdio.h>

#define WIDTH 256
#define HEIGHT 224

int xpos;

/* entry point */
int main(int argc, char *argv[]) {
  TLN_Tilemap drawbridge_bg;
  TLN_Tilemap drawbridge_fg0;
  TLN_Tilemap drawbridge_fg1;
  TLN_Tilemap drawbridge_fg2;
  TLN_Tilemap colission;

  /* setup engine */
  TLN_Init(WIDTH, HEIGHT, 5, 1, 0);
  TLN_SetBGColor(0x10, 0x00, 0x20);

  /* load resources*/
  TLN_SetLoadPath("assets/sc4");
  drawbridge_bg = TLN_LoadTilemap("drawbridge_bg.tmx", NULL);
  drawbridge_fg0 = TLN_LoadTilemap("drawbridge_fg0.tmx", NULL);
  drawbridge_fg1 = TLN_LoadTilemap("drawbridge_fg1.tmx", NULL);
  drawbridge_fg2 = TLN_LoadTilemap("drawbridge_fg2.tmx", "Tiles");
  colission = TLN_LoadTilemap("drawbridge_fg2.tmx", "Colission");
  TLN_SetLayerTilemap(4, colission);
  TLN_SetLayerTilemap(3, drawbridge_bg);
  TLN_SetLayerTilemap(1, drawbridge_fg0);
  TLN_SetLayerTilemap(2, drawbridge_fg1);
  TLN_SetLayerTilemap(0, drawbridge_fg2);

  SimonInit();

  TLN_SetLayerBlendMode(1, BLEND_MIX50);

  /* main loop */
  TLN_CreateWindow(CWF_NEAREST | CWF_S6 | CWF_NOVSYNC);
  TLN_SetTargetFps(60);

  int frame = 0;
  while (TLN_ProcessWindow()) {
    frame++;
    char title[48];
    snprintf(title, sizeof(title), "Frame: %d | FPS: %d", frame,
             TLN_GetAverageFps());
    TLN_SetWindowTitle(title);
    SimonTasks();

    /* scroll */
    xpos = SimonGetPosition();
    TLN_SetLayerPosition(0, xpos, 0);
    TLN_SetLayerPosition(1, xpos, 0);
    TLN_SetLayerPosition(2, xpos, 0);
    TLN_SetLayerPosition(3, xpos * 2 / 5, 0);
    TLN_SetLayerPosition(4, xpos, 0);

    /* render to window */
    TLN_DrawFrame(0);
  }

  SimonDeinit();
  TLN_DeleteTilemap(colission);
  TLN_DeleteTilemap(drawbridge_bg);
  TLN_DeleteTilemap(drawbridge_fg0);
  TLN_DeleteTilemap(drawbridge_fg1);
  TLN_DeleteTilemap(drawbridge_fg2);
  TLN_Deinit();
  return 0;
}
