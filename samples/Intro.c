#include "PaletteLayer.h"
#include "Simon.h"
#include "Tilengine.h"

#define WIDTH 256
#define HEIGHT 224

int xpos;
int ypos;

/* entry point */
int main(int argc, char *argv[]) {
  TLN_Tilemap drawbridge_bg;
  TLN_Tilemap drawbridge_mg;
  TLN_Tilemap hud;
  TLN_Palette palettes[8] = {0};

  /* setup engine */
  TLN_Init(WIDTH, HEIGHT, 3, 1, 0);
  TLN_SetBGColor(0x02 * 8, 0, 0x04 * 8);

  /* load resources*/
  TLN_SetLoadPath("assets/sc4");
  drawbridge_bg = TLN_LoadTilemap("drawbridge_bg.tmx", NULL);
  drawbridge_mg = TLN_LoadTilemap("drawbridge_mg.tmx", NULL);
  hud = TLN_LoadTilemap("hud.tmx", NULL);
  TLN_SetLayerTilemap(2, drawbridge_bg);
  TLN_SetLayerTilemap(1, drawbridge_mg);
  TLN_SetLayerTilemap(0, hud);

  /* Split intro_palettes.txt into 8 sub-palettes (stride=16 each) and
   * register each in its matching global palette slot. */
  load_and_split_palette("assets/sc4/drawbridge_palettes.txt", 16, palettes);
  for (int c = 0; c < 8; c++)
    TLN_SetGlobalPalette(c, palettes[c]);

  /* Stamp the per-tile palette indices from the TMX "Palette" objectgroup
   * directly into each Tile's .palette bitfield. */
  apply_palette_layer(drawbridge_bg, "assets/sc4/drawbridge_bg.tmx");
  apply_palette_layer(drawbridge_mg, "assets/sc4/drawbridge_mg.tmx");
  apply_palette_layer(hud, "assets/sc4/hud.tmx");

  SimonInit();

  /* main loop */
  TLN_CreateWindow(CWF_NEAREST | CWF_S6);
  TLN_SetTargetFps(60);
  while (TLN_ProcessWindow()) {
    ypos++;
    SimonTasks();

    /* input */
    xpos = SimonGetPosition();

    /* scroll */
    TLN_SetLayerPosition(1, xpos, 0);
    TLN_SetLayerPosition(2, xpos / 2, 0);

    /* render to window */
    TLN_DrawFrame(0);
  }

  SimonDeinit();
  TLN_DeleteTilemap(drawbridge_bg);
  TLN_DeleteTilemap(drawbridge_mg);
  TLN_DeleteTilemap(hud);
  for (int c = 0; c < 8; c++)
    if (palettes[c])
      TLN_DeletePalette(palettes[c]);
  TLN_Deinit();
  return 0;
}
