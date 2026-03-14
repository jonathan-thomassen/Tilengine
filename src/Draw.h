/*
 * Tilengine - The 2D retro graphics engine with raster effects
 * Copyright (C) 2015-2019 Marc Palacios Domenech <mailto:megamarc@hotmail.com>
 * All rights reserved
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * */

#ifndef DRAW_H
#define DRAW_H

#include <stdbool.h>
#include <stdint.h>

/* render modes */
typedef enum { MODE_NORMAL, MODE_SCALING, MODE_TRANSFORM, MODE_PIXEL_MAP, MAX_DRAW_MODE } draw_t;

typedef bool (*ScanDrawPtr)(int, uint32_t *, int, int, int);
typedef struct Layer Layer;

ScanDrawPtr GetLayerDraw(Layer const *layer);
ScanDrawPtr GetSpriteDraw(draw_t mode);

extern bool DrawScanline(void);

/* Per-frame profiling accumulators for the blend-mask render path.
 * Counted in SDL_GetPerformanceCounter ticks; reset to 0 each frame by
 * the caller (see prof_draw_reset / prof_draw_read in Intro.c). */
extern uint64_t g_prof_linebuf_ticks;      /* MAIN_LAYER → linebuffer          */
extern uint64_t g_prof_fillmask_ticks;     /* fill_blend_mask_scanline          */
extern uint64_t g_prof_blit_ticks;         /* Blit32_32_Masked composite        */
extern uint64_t g_prof_layers_ticks;       /* entire draw_regular_layers pass   */
extern uint64_t g_prof_sprites_ticks;      /* entire draw_regular_sprites pass  */
extern uint64_t g_prof_per_layer_ticks[8]; /* per-layer breakdown            */

#endif
