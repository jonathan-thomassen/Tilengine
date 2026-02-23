/*
 * Tilengine - The 2D retro graphics engine with raster effects
 * Copyright (C) 2015-2019 Marc Palacios Domenech <mailto:megamarc@hotmail.com>
 * All rights reserved
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * */

#ifndef _ENGINE_H
#define _ENGINE_H

#define NUM_PALETTES 8
#define INTERNAL_FPS 60

#include "Animation.h"
#include "Bitmap.h"
#include "Blitters.h"
#include "Layer.h"
#include "List.h"
#include "Sprite.h"
#include "Tilengine.h"

/* background sub-struct */
typedef struct {
  uint32_t color;        /* background color */
  TLN_Bitmap bitmap;     /* background bitmap */
  TLN_Palette palette;   /* background bitmap palette */
  ScanBlitPtr blit_fast; /* blitter for background bitmap */
  uint8_t *blend_table;  /* current blending table */
} EngineBackground;

/* scanline/frame callback sub-struct */
typedef struct {
  void (*raster)(int); /* per-scanline raster callback */
  void (*frame)(int);  /* per-frame callback */
} EngineCallbacks;

/* frame/line timing counters sub-struct */
typedef struct {
  int frame;      /* current frame number */
  int line;       /* current scanline */
  int target_fps; /* target frames per second */
} EngineTiming;

/* sprite mask scanline range sub-struct */
typedef struct {
  int top;    /* top scanline for sprite masking */
  int bottom; /* bottom scanline for sprite masking */
} EngineSpriteMask;

/* world-space scroll position sub-struct */
typedef struct {
  int x;      /* world x coordinate */
  int y;      /* world y coordinate */
  bool dirty; /* world position updated since last draw */
} EngineWorld;

/* animation collection sub-struct */
typedef struct {
  int num;          /* number of animations */
  Animation *items; /* pointer to animation buffer */
  List list;        /* linked list of active animations */
} EngineAnimations;

typedef struct Engine {
  uint32_t header;    /* object signature to identify as engine context */
  uint32_t *priority; /* buffer receiving tiles with priority */
  uint16_t
      *collision; /* buffer with sprite coverage IDs for per-pixel collision */
  uint32_t *linebuffer; /* buffer for intermediate scanline output */
  int numsprites;       /* number of sprites */
  Sprite *sprites;      /* pointer to sprite buffer */
  int numlayers;        /* number of layers */
  Layer *layers;        /* pointer to layer buffer */
  EngineAnimations anim;
  bool dopriority; /* there is some data in "priority" buffer that need blitting
                    */
  TLN_Error error; /* last error code */
  TLN_LogLevel log_level; /* logging level */
  EngineBackground bg;
  TLN_Palette palettes[NUM_PALETTES]; /* optional global palettes */
  EngineCallbacks callbacks;
  EngineTiming timing;
  List list_sprites; /* linked list of active sprites */
  EngineSpriteMask sprite_mask;
  EngineWorld world;

  struct {
    int width;
    int height;
    int pitch;
    uint8_t *data;
  } framebuffer;
} Engine;

extern Engine *engine;

extern void tln_trace(TLN_LogLevel log_level, const char *message);

#define GetFramebufferLine(line)                                               \
  (uint32_t *)(engine->framebuffer.data + (line * engine->framebuffer.pitch))

#endif
