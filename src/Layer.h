/*
 * Tilengine - The 2D retro graphics engine with raster effects
 * Copyright (C) 2015-2019 Marc Palacios Domenech <mailto:megamarc@hotmail.com>
 * All rights reserved
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * */

#ifndef _LAYER_H
#define _LAYER_H

#include "Blitters.h"
#include "Draw.h"
#include "Math2D.h"
#include "Tilengine.h"

typedef struct {
  int x1; /* clip region */
  int y1;
  int x2;
  int y2;
  bool invert;    /* false=clip outside, true=clip inside */
  uint8_t *blend; /* optional solid color blend function */
  uint32_t color; /* color for optional blend function */
} LayerWindow;

/* render pipeline sub-struct */
typedef struct {
  ScanDrawPtr draw;
  ScanBlitPtr blitters[2];
  draw_t mode;
  uint8_t *blend; /* pointer to blend table */
} LayerRender;

/* scaling/transform factors sub-struct */
typedef struct {
  fix_t xfactor;
  fix_t dx;
  fix_t dy;
} LayerScale;

/* boolean state flags sub-struct */
typedef struct {
  bool ok;
  bool affine;
  bool priority; /* whole layer in front of regular sprites */
  bool dirty;    /* requires update before draw */
} LayerFlags;

typedef struct Layer {
  TLN_LayerType type;     /* layer type */
  TLN_Tilemap tilemap;    /* pointer to tilemap */
  TLN_Palette palette;    /* pointer to current color alette */
  TLN_Bitmap bitmap;      /* pointer to bitmap (bitmap layer mode) */
  TLN_ObjectList objects; /* pointer to object list (objects layer mode) */
  int width;              /* layer width in pixels */
  int height;             /* layer height in pixels */
  LayerRender render;
  Matrix3 transform;
  int *column; /* column offset (optional) */
  LayerScale scale;
  TLN_PixelMap *pixel_map; /* pointer to pixel mapping table */
  LayerFlags flags;

  /* world mode related data */
  struct {
    int offsetx;
    int offsety;
    float xfactor;
    float yfactor;
  } world;

  /* */
  int hstart; /* horizontal start offset */
  int vstart; /* vertical start offset*/

  /* clip */
  LayerWindow window;

  /* mosaic */
  struct {
    int w; /* virtual pixel size */
    int h;
    uint32_t *buffer; /* line buffer */
  } mosaic;
} Layer;

Layer *GetLayer(int index);

#endif