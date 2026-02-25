#include "PaletteLayer.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------------------------
 * Palette-layer loader
 * Reads the "Palette" tile layer from a .tmx file (encoding="csv").
 * Each CSV cell value is a raw palette index that is stamped directly onto
 * the corresponding tilemap tile so the engine selects the per-tile palette.
 * -------------------------------------------------------------------------- */

/* Stamp palette indices from a CSV string into every tile in the tilemap. */
static void apply_palette_from_csv(TLN_Tilemap tilemap, const char *csv,
                                   int cols, int rows) {
  const char *p = csv;
  for (int row = 0; row < rows; row++) {
    for (int col = 0; col < cols; col++) {
      while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
      int val = (int)strtol(p, (char **)&p, 10);
      while (*p == ',')
        p++;
      TLN_Tile tile = TLN_GetTilemapTiles(tilemap, row, col);
      if (tile)
        tile->palette = (uint8_t)(val & 0x07);
    }
  }
}

void load_and_split_palette(const char *path, int stride, TLN_Palette out[8]) {
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

void apply_palette_layer(TLN_Tilemap tilemap, const char *tmxpath) {
  int cols = TLN_GetTilemapCols(tilemap);
  int rows = TLN_GetTilemapRows(tilemap);

  long size;
  uint8_t *data = read_file(tmxpath, &size);
  if (!data)
    return;

  /* Locate the <layer element whose name attribute equals "Palette". */
  const char *csv_start = NULL;
  const char *p = (const char *)data;
  while ((p = strstr(p, "<layer")) != NULL) {
    const char *tag_end = strstr(p, ">");
    const char *name_attr = strstr(p, "name=");
    if (name_attr && tag_end && name_attr < tag_end) {
      const char *val = name_attr + 5;
      char quote = *val++;
      if ((quote == '"' || quote == '\'') && strncmp(val, "Palette", 7) == 0 &&
          *(val + 7) == quote) {
        /* Found the Palette layer – locate the CSV content after <data ...> */
        const char *data_tag = strstr(tag_end, "<data");
        if (data_tag) {
          const char *data_content = strchr(data_tag, '>');
          if (data_content)
            csv_start = data_content + 1;
        }
        break;
      }
    }
    p++;
  }

  if (csv_start)
    apply_palette_from_csv(tilemap, csv_start, cols, rows);

  free(data);
}
