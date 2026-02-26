#include "PaletteLayer.h"
#include "Simon.h"
#include "Tilengine.h"

#define WIDTH 256
#define HEIGHT 224

int xpos;
int ypos;

/* entry point */
int main(int argc, char *argv[]) {
  TLN_Tilemap tilemap1;
  TLN_Tilemap tilemap2;
  TLN_Tilemap tilemap3;
  TLN_Palette palettes[8] = {0};

  /* setup engine */
  TLN_Init(WIDTH, HEIGHT, 3, 1, 0);
  TLN_SetBGColor(0x02 * 8, 0, 0x04 * 8);

  /* load resources*/
  TLN_SetLoadPath("assets/sc4");
  tilemap1 = TLN_LoadTilemap("drawbridge_intro1.tmx", NULL);
  tilemap2 = TLN_LoadTilemap("drawbridge_intro2.tmx", NULL);
  tilemap3 = TLN_LoadTilemap("drawbridge_intro3.tmx", NULL);
  TLN_SetLayerTilemap(0, tilemap1);
  TLN_SetLayerTilemap(1, tilemap2);
  TLN_SetLayerTilemap(2, tilemap3);

  /* Split intro_palettes.txt into 8 sub-palettes (stride=16 each) and
   * register each in its matching global palette slot. */
  load_and_split_palette("assets/sc4/drawbridge_intro_palettes.txt", 16,
                         palettes);
  for (int c = 0; c < 8; c++)
    TLN_SetGlobalPalette(c, palettes[c]);

  /* Stamp the per-tile palette indices from the TMX "Palette" objectgroup
   * directly into each Tile's .palette bitfield. */
  apply_palette_layer(tilemap1, "assets/sc4/drawbridge_intro1.tmx");
  apply_palette_layer(tilemap2, "assets/sc4/drawbridge_intro2.tmx");
  apply_palette_layer(tilemap3, "assets/sc4/drawbridge_intro3.tmx");

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
    TLN_SetLayerPosition(0, xpos, 0);
    TLN_SetLayerPosition(1, xpos / 2, 0);

    /* render to window */
    TLN_DrawFrame(0);
  }

  SimonDeinit();
  TLN_DeleteTilemap(tilemap1);
  TLN_DeleteTilemap(tilemap2);
  TLN_DeleteTilemap(tilemap3);
  for (int c = 0; c < 8; c++)
    if (palettes[c])
      TLN_DeletePalette(palettes[c]);
  TLN_Deinit();
  return 0;
}
