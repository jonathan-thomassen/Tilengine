/*
 * Tilengine - The 2D retro graphics engine with raster effects
 * Copyright (C) 2015-2019 Marc Palacios Domenech <mailto:megamarc@hotmail.com>
 * All rights reserved
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * */

#include "Tables.h"

#include <stdlib.h>

#include "Tilengine.h"

#define BLEND_SIZE (1 << 16)

static uint8_t *blend_tables[MAX_BLEND];
static int instances = 0;

bool CreateBlendTables(void) {
  /* increase reference count */
  instances += 1;
  if (instances > 1)
    return true;

  /* get memory */
  for (int c = BLEND_MIX25; c < MAX_BLEND; c++) {
    blend_tables[c] = (uint8_t *)malloc(BLEND_SIZE);
    if (blend_tables[c] == NULL)
      return false;
  }

  /* build tables */
  for (int a = 0; a < 256; a++) {
    for (int b = 0; b < 256; b++) {
      const int offset = (a << 8) + b;
      blend_tables[BLEND_MIX25][offset] = (uint8_t)((a + b + b) / 3);
      blend_tables[BLEND_MIX50][offset] = (uint8_t)((a + b) >> 1);
      blend_tables[BLEND_MIX75][offset] = (uint8_t)((a + a + b) / 3);
      blend_tables[BLEND_ADD][offset] = (a + b) > 255 ? 255 : (uint8_t)(a + b);
      blend_tables[BLEND_SUB][offset] = (a - b) < 0 ? 0 : (uint8_t)(a - b);
      blend_tables[BLEND_MOD][offset] = (uint8_t)(a * b) / 255;
      blend_tables[BLEND_CUSTOM][offset] = (uint8_t)a;
    }
  }
  return true;
}

void DeleteBlendTables(void) {
  /* decrease reference count */
  if (instances > 0)
    instances -= 1;
  if (instances != 0)
    return;

  for (int c = BLEND_MIX25; c < MAX_BLEND; c++) {
    if (blend_tables[c] != NULL)
      free(blend_tables[c]);
  }
}

/* returns blend table according to selected blend mode */
uint8_t *SelectBlendTable(TLN_Blend mode) {
  return blend_tables[mode];
}
