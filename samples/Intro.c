#include "Tilengine.h"
#include "simplexml.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIDTH 256
#define HEIGHT 224

/* --------------------------------------------------------------------------
 * Palette-layer loader
 * Parses the "Palette" objectgroup in a .tmx file (one 8x8 object per tile,
 * each with a custom "palette" int property) and stamps tile->palette on
 * every tile in the tilemap so the engine selects the per-tile global palette.
 * -------------------------------------------------------------------------- */
static struct {
  bool in_palette_group; /* inside the "Palette" objectgroup */
  bool in_object;        /* inside an <object> tag */
  bool in_palette_prop;  /* <property name="palette" ...> seen for cur object */
  int cur_x;             /* x coords of the current object */
  int cur_y;             /* y coords of the current object */
  int cur_palette;       /* palette index accumulated for current object */
  int tile_size;         /* tile side in pixels */
  int cols;              /* tilemap width in tiles */
  int rows;              /* tilemap height in tiles */
  uint8_t *map;          /* output: palette index per (row*cols + col) */
} pal_state;

static void *pal_handler(SimpleXmlParser parser, SimpleXmlEvent evt,
                         const char *szName, const char *szAttribute,
                         const char *szValue) {
  (void)parser;
  switch (evt) {
  case ADD_SUBTAG:
    if (!strcasecmp(szName, "objectgroup")) {
      pal_state.in_palette_group = false;
    } else if (!strcasecmp(szName, "object")) {
      if (pal_state.in_palette_group) {
        pal_state.cur_x = pal_state.cur_y = pal_state.cur_palette = 0;
        pal_state.in_object = true;
        pal_state.in_palette_prop = false;
      }
    } else if (!strcasecmp(szName, "property")) {
      pal_state.in_palette_prop = false;
    }
    break;

  case ADD_ATTRIBUTE:
    if (!strcasecmp(szName, "objectgroup")) {
      if (!strcasecmp(szAttribute, "name"))
        pal_state.in_palette_group = !strcasecmp(szValue, "Palette");
    } else if (!strcasecmp(szName, "object") && pal_state.in_object) {
      if (!strcasecmp(szAttribute, "x"))
        pal_state.cur_x = atoi(szValue);
      else if (!strcasecmp(szAttribute, "y"))
        pal_state.cur_y = atoi(szValue);
    } else if (!strcasecmp(szName, "property") && pal_state.in_object) {
      if (!strcasecmp(szAttribute, "name"))
        pal_state.in_palette_prop = !strcasecmp(szValue, "palette");
      else if (!strcasecmp(szAttribute, "value") && pal_state.in_palette_prop)
        pal_state.cur_palette = atoi(szValue);
    }
    break;

  case FINISH_TAG:
    if (!strcasecmp(szName, "object") && pal_state.in_object) {
      int col = pal_state.cur_x / pal_state.tile_size;
      int row = pal_state.cur_y / pal_state.tile_size;
      if (row >= 0 && row < pal_state.rows && col >= 0 && col < pal_state.cols)
        pal_state.map[row * pal_state.cols + col] =
            (uint8_t)pal_state.cur_palette;
      pal_state.in_object = false;
    } else if (!strcasecmp(szName, "objectgroup")) {
      pal_state.in_palette_group = false;
    }
    break;

  default:
    break;
  }
  return &pal_handler;
}

/* Load a combined palette text file and split it into 8 sub-palettes.
 * The file contains one #RRGGBB hex color per line (blank / non-'#' lines
 * skipped).  Colors are read sequentially; sub-palette[i] gets 'stride'
 * consecutive colors starting at offset i*stride.
 * Each out[i] is a newly created TLN_Palette(stride); caller must free. */
static void load_and_split_palette(const char *path, int stride,
                                   TLN_Palette out[8]) {
  FILE *f = fopen(path, "r");
  if (!f)
    return;

  /* collect all colors from file – single allocation: [r0..rN | g0..gN |
   * b0..bN] */
  int total = stride * 8;
  uint8_t *buf = calloc((size_t)total * 3, 1);
  if (!buf) {
    fclose(f);
    return;
  }
  uint8_t *r = buf;
  uint8_t *g = buf + total;
  uint8_t *b = buf + total * 2;

  char line[32];
  int index = 0;
  while (index < total && fgets(line, sizeof(line), f)) {
    char const *p = line;
    while (*p == ' ' || *p == '\t')
      p++;
    if (*p == '#' && p[1] && p[2] && p[3] && p[4] && p[5] && p[6]) {
      unsigned int rgb = 0;
      if (sscanf(p + 1, "%6x", &rgb) == 1) {
        r[index] = (rgb >> 16) & 0xFF;
        g[index] = (rgb >> 8) & 0xFF;
        b[index] = rgb & 0xFF;
        index++;
      }
    }
  }
  fclose(f);

  /* build 8 individual palettes */
  for (int i = 0; i < 8; i++) {
    out[i] = TLN_CreatePalette(stride);
    if (!out[i])
      continue;
    for (int c = 0; c < stride; c++)
      TLN_SetPaletteColor(out[i], c, r[i * stride + c], g[i * stride + c],
                          b[i * stride + c]);
  }

  free(buf);
}

/* Read a file into a malloc'd buffer; caller must free(). */
static uint8_t *read_file(const char *path, long *out_size) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  rewind(f);
  uint8_t *buf = malloc((size_t)sz);
  if (buf)
    fread(buf, 1, (size_t)sz, f);
  fclose(f);
  *out_size = sz;
  return buf;
}

/* Parse the "Palette" objectgroup from tmxpath and apply palette indices
 * directly to the tilemap tiles so TLN_SetGlobalPalette drives per-tile color.
 */
static void apply_palette_layer(TLN_Tilemap tilemap, const char *tmxpath) {
  int cols = TLN_GetTilemapCols(tilemap);
  int rows = TLN_GetTilemapRows(tilemap);
  uint8_t *map = calloc((size_t)(cols * rows), 1);
  if (!map)
    return;

  memset(&pal_state, 0, sizeof(pal_state));
  pal_state.tile_size = 8;
  pal_state.cols = cols;
  pal_state.rows = rows;
  pal_state.map = map;

  long size;
  uint8_t *data = read_file(tmxpath, &size);
  if (data) {
    SimpleXmlParser parser = simpleXmlCreateParser((char *)data, size);
    if (parser) {
      simpleXmlParse(parser, pal_handler);
      simpleXmlDestroyParser(parser);
    }
    free(data);
  }

  /* Stamp palette index onto each tile using the direct-pointer API */
  for (int row = 0; row < rows; row++) {
    for (int col = 0; col < cols; col++) {
      TLN_Tile tile = TLN_GetTilemapTiles(tilemap, row, col);
      if (tile)
        tile->palette = map[row * cols + col];
    }
  }
  free(map);
}

/* entry point */
int main(int argc, char *argv[]) {
  TLN_Tilemap tilemap;
  TLN_Tilemap tilemap2;
  TLN_Tilemap tilemap3;
  TLN_Palette palettes[8] = {0};

  /* setup engine */
  TLN_Init(WIDTH, HEIGHT, 3, 0, 0);
  TLN_SetBGColor(0x02 * 8, 0, 0x04 * 8);

  /* load resources*/
  TLN_SetLoadPath("assets/sc4");
  tilemap = TLN_LoadTilemap("intro_ram.tmx", NULL);
  tilemap2 = TLN_LoadTilemap("intro_ram2.tmx", NULL);
  tilemap3 = TLN_LoadTilemap("intro_ram3.tmx", NULL);
  TLN_SetLayerTilemap(1, tilemap);
  TLN_SetLayerTilemap(0, tilemap2);
  TLN_SetLayerTilemap(2, tilemap3);

  /* Split intro_palettes.txt into 8 sub-palettes (stride=16 each) and
   * register each in its matching global palette slot. */
  load_and_split_palette("assets/sc4/intro_palettes.txt", 16, palettes);
  for (int c = 0; c < 8; c++)
    TLN_SetGlobalPalette(c, palettes[c]);

  /* Stamp the per-tile palette indices from the TMX "Palette" objectgroup
   * directly into each Tile's .palette bitfield. */
  apply_palette_layer(tilemap, "assets/sc4/intro_ram.tmx");
  apply_palette_layer(tilemap2, "assets/sc4/intro_ram2.tmx");
  apply_palette_layer(tilemap3, "assets/sc4/intro_ram3.tmx");

  /* main loop */
  TLN_CreateWindow(CWF_NEAREST | CWF_S6);
  while (TLN_ProcessWindow()) {
    /* render to window */
    TLN_DrawFrame(0);
  }

  TLN_DeleteTilemap(tilemap);
  TLN_DeleteTilemap(tilemap2);
  TLN_DeleteTilemap(tilemap3);
  for (int c = 0; c < 8; c++)
    if (palettes[c])
      TLN_DeletePalette(palettes[c]);
  TLN_Deinit();
  return 0;
}
