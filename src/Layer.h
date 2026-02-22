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

/* capa */
typedef struct Layer {
  TLN_LayerType type;     /* layer type */
  TLN_Tilemap tilemap;    /* pointer to tilemap */
  TLN_Palette palette;    /* pointer to current color alette */
  TLN_Bitmap bitmap;      /* pointer to bitmap (bitmap layer mode) */
  TLN_ObjectList objects; /* pointer to object list (objects layer mode) */
  int width;              /* layer width in pixels */
  int height;             /* layer height in pixels */
  bool ok;
  bool affine;
  ScanDrawPtr draw;
  ScanBlitPtr blitters[2];
  Matrix3 transform;
  int *column; /* column offset (optional) */
  fix_t xfactor;
  fix_t dx;
  fix_t dy;
  uint8_t *blend;          /* pointer to blend table */
  TLN_PixelMap *pixel_map; /* pointer to pixel mapping table */
  draw_t mode;
  bool priority; /* whole layer in front of regular sprites */

  /* world mode related data */
  struct {
    int offsetx;
    int offsety;
    float xfactor;
    float yfactor;
  } world;
  bool dirty; /* requires update before draw */

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