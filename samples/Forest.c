/******************************************************************************
*
* Tilengine sample
* 2021 Marc Palacios
* http://www.tilengine.org

*
******************************************************************************/

#include "Tilengine.h"
#include <stdio.h>

#define HRES 424
#define VRES 240

/* layers, must match "map.tmx" layer structure! */
enum {
  LAYER_PROPS,        /* object layer */
  LAYER_FOREGROUND,   /* main foreground layer (tiles) */
  LAYER_MIDDLEGROUND, /* middle (bitmap) */
  LAYER_BACKGROUND,   /* back ( bitmap) */
  NUM_LAYERS
};

int main(int argc, char *argv[]) {
  TLN_Spriteset atlas;
  TLN_Sequence idle;
  int xworld = 0;
  int xplayer;
  int yplayer;
  int oldx = -1;
  int width;
  char const *respack = NULL;
  char const *passkey = NULL;

  /* get arguments */
  if (argc > 1)
    respack = argv[1];
  if (argc > 2)
    passkey = argv[2];

  TLN_Init(HRES, VRES, NUM_LAYERS, 8, 0);
  TLN_SetLogLevel(TLN_LOG_ERRORS);
  if (respack != NULL) {
    bool ok = TLN_OpenResourcePack(respack, passkey);
    if (!ok) {
      printf("Cannot open resource pack!\n");
      TLN_Deinit();
      return 0;
    }
    TLN_SetLoadPath("forest");
  } else
    TLN_SetLoadPath("assets/forest");
  TLN_LoadWorld("map.tmx", 0);
  width = TLN_GetLayerWidth(LAYER_FOREGROUND);
  atlas = TLN_LoadSpriteset("atlas.png");
  idle = TLN_CreateSpriteSequence(NULL, atlas, "player-idle/player-idle-", 6);
  TLN_CreateSpriteSequence(NULL, atlas, "player-skip/player-skip-", 6);
  xplayer = 48;
  yplayer = 144;
  TLN_ConfigSprite(0, atlas, 0);
  TLN_SetSpriteAnimation(0, idle, 0);
  TLN_SetSpriteWorldPosition(0, xplayer, yplayer);
  TLN_CreateWindow(NULL, CWF_NEAREST);
  while (TLN_ProcessWindow()) {
    TLN_DrawFrame(0);

    /* move 3 pixels right/left main layer */
    if (TLN_GetInput(INPUT_LEFT) && xworld > 0)
      xworld -= 3;
    else if (TLN_GetInput(INPUT_RIGHT) && xworld < width - HRES)
      xworld += 3;

    /* update on change */
    if (xworld != oldx) {
      TLN_SetWorldPosition(xworld, 0);
      oldx = xworld;
    }
  }

  /* release resources */
  TLN_ReleaseWorld();
  TLN_DeleteWindow();
  TLN_Deinit();
  return 0;
}
