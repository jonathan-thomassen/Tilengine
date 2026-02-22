/*
 * Tilengine - The 2D retro graphics engine with raster effects
 * Copyright (C) 2015-2019 Marc Palacios Domenech <mailto:megamarc@hotmail.com>
 * All rights reserved
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * */

#ifndef _SPRITE_H
#define _SPRITE_H

#include "Animation.h"
#include "Blitters.h"
#include "Draw.h"
#include "List.h"
#include "Spriteset.h"
#include "Tilengine.h"

/* rectangulo */
typedef struct {
  int x1;
  int y1;
  int x2;
  int y2;
} rect_t;

extern void MakeRect(rect_t *rect, int x, int y, int w, int h);

/* sprite internal helper structures */
typedef struct {
  int x;
  int y;
} SpritePos;
typedef struct {
  int x;
  int y;
} SpriteIncrement;
typedef struct {
  int x;
  int y;
} SpriteWorldPos;
typedef struct {
  float x;
  float y;
} SpriteScale;
typedef struct {
  float x;
  float y;
} SpritePivot;
typedef struct {
  uint8_t *pixels;
  int pitch;
} SpritePixelData;
typedef struct {
  ScanDrawPtr draw;
  ScanBlitPtr blitter;
} SpriteDrawFuncs;

/* sprite status flags (stored in flags field) */
#define SPRITE_FLAG_OK (1 << 24)
#define SPRITE_FLAG_DO_COLLISION (1 << 25)
#define SPRITE_FLAG_COLLISION (1 << 26)
#define SPRITE_FLAG_WORLD_SPACE (1 << 27)
#define SPRITE_FLAG_DIRTY (1 << 28)

/* sprite flag accessor macros */
#define GetSpriteFlag(sprite, flag) (((sprite)->flags & (flag)) != 0)
#define SetSpriteFlag(sprite, flag, value)                                     \
  do {                                                                         \
    if (value)                                                                 \
      (sprite)->flags |= (flag);                                               \
    else                                                                       \
      (sprite)->flags &= ~(flag);                                              \
  } while (0)

/* sprite */
typedef struct Sprite {
  TLN_Spriteset spriteset;
  TLN_Palette palette;
  SpriteEntry *info;
  SpritePixelData pixel_data;
  int num;
  int index;     /* spriteset picture index */
  SpritePos pos; /* screen space location (TLN_SetSpritePosition) */
  SpriteIncrement inc;
  SpriteWorldPos
      world_pos; /* world space location (TLN_SetSpriteWorldPosition) */
  SpriteScale scale;
  SpritePivot
      pivot; /* normalized pivot position inside sprite (default = 0,0) */
  rect_t srcrect;
  rect_t dstrect;
  draw_t mode;
  uint8_t *blend;
  uint32_t flags;
  SpriteDrawFuncs funcs;
  TLN_Bitmap rotation_bitmap;
  ListNode list_node;
  Animation animation;
} Sprite;

extern void UpdateSprite(Sprite *sprite);

#endif