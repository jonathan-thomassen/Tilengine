/*
 * Tilengine - The 2D retro graphics engine with raster effects
 * Copyright (C) 2015-2019 Marc Palacios Domenech <mailto:megamarc@hotmail.com>
 * All rights reserved
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * */

#include "Draw.h"
#include "Engine.h"
#include "ObjectList.h"
#include "Sprite.h"
#include "Tilemap.h"
#include "Tilengine.h"
#include "Tileset.h"
#include <stdlib.h>
#include <string.h>

/* private prototypes */
static void DrawSpriteCollision(int nsprite, uint8_t const *srcpixel,
                                uint16_t *dstpixel, int width, int dx);
static void DrawSpriteCollisionScaling(int nsprite, uint8_t const *srcpixel,
                                       uint16_t *dstpixel, int width, int dx,
                                       int srcx);

static bool check_sprite_coverage(Sprite const *sprite, int nscan) {
  /* check sprite coverage */
  if (nscan < sprite->dstrect.y1 || nscan >= sprite->dstrect.y2)
    return false;
  if (sprite->dstrect.x2 < 0 || sprite->srcrect.x2 < 0)
    return false;
  if ((sprite->flags & FLAG_MASKED) && nscan >= engine->sprite_mask.top &&
      nscan <= engine->sprite_mask.bottom)
    return false;
  return true;
}

/* selects target scan buffer and sets build_mosaic flag */
static uint32_t *select_scan_buffer(Layer const *layer, int line,
                                    bool *build_mosaic) {
  *build_mosaic = false;
  if (layer->mosaic.h != 0) {
    if (line % layer->mosaic.h == 0) {
      *build_mosaic = true;
      return engine->linebuffer;
    }
    return NULL;
  }
  if (layer->render.mode >= MODE_TRANSFORM)
    return engine->linebuffer;
  return GetFramebufferLine(line);
}

/* draws the regular (non-mosaic) region respecting window invert and inside */
static bool draw_window_region(int nlayer, uint32_t *scan, int line,
                               LayerWindow const *window, bool inside,
                               int framewidth) {
  Layer const *layer = &engine->layers[nlayer];
  bool priority = false;
  if (!window->invert) {
    if (inside)
      priority |=
          layer->render.draw(nlayer, scan, line, window->x1, window->x2);
  } else {
    if (inside) {
      priority |= layer->render.draw(nlayer, scan, line, 0, layer->window.x1);
      priority |=
          layer->render.draw(nlayer, scan, line, layer->window.x2, framewidth);
    } else
      priority |= layer->render.draw(nlayer, scan, line, 0, framewidth);
  }
  return priority;
}

/* blits the mosaic linebuffer to the framebuffer respecting window settings */
static void blit_mosaic_window(uint32_t *mosaic, uint32_t *scan,
                               LayerWindow const *window, bool inside,
                               int framewidth, int windowwidth,
                               uint8_t const *blend) {
  if (!window->invert) {
    if (inside)
      Blit32_32(mosaic + window->x1, scan + window->x1, windowwidth, blend);
  } else {
    if (inside) {
      Blit32_32(mosaic, scan, windowwidth, blend);
      Blit32_32(mosaic + window->x2, scan + window->x2, framewidth - window->x2,
                blend);
    } else
      Blit32_32(mosaic, scan, framewidth, blend);
  }
}

/* fills the clipped (outside-window) region with the window color */
static void blit_clipped_window(uint32_t *scan, LayerWindow const *window,
                                bool inside, int framewidth, int windowwidth) {
  if (window->color == 0)
    return;
  if (!window->invert) {
    if (inside) {
      BlitColor(scan, window->color, window->x1, window->blend);
      BlitColor(scan + window->x2, window->color, framewidth - window->x2,
                window->blend);
    } else
      BlitColor(scan, window->color, framewidth, window->blend);
  } else if (inside)
    BlitColor(scan + window->x1, window->color, windowwidth, window->blend);
}

/* draw background scanline taking into account mosaic and windowing effects */
static bool draw_background_scanline(int nlayer, int line) {
  Layer *layer = &engine->layers[nlayer];
  LayerWindow const *window = &layer->window;
  uint32_t *mosaic = layer->mosaic.buffer;
  const bool inside = line >= window->y1 && line <= window->y2;
  const int framewidth = engine->framebuffer.width;
  const int windowwidth = layer->window.x2 - layer->window.x1;
  bool priority = false;
  bool build_mosaic = false;

  uint32_t *scan = select_scan_buffer(layer, line, &build_mosaic);
  if (scan == engine->linebuffer && scan != NULL)
    memset(scan, 0, framewidth * sizeof(uint32_t));

  if (scan != NULL)
    priority |=
        draw_window_region(nlayer, scan, line, window, inside, framewidth);

  scan = GetFramebufferLine(line);

  /* build mosaic to linebuffer */
  if (build_mosaic) {
    if (mosaic != NULL)
      memset(mosaic, 0, framewidth * sizeof(uint32_t));
    BlitMosaic(engine->linebuffer, mosaic, framewidth, layer->mosaic.w, NULL);
  }

  if (layer->mosaic.h != 0)
    blit_mosaic_window(mosaic, scan, window, inside, framewidth, windowwidth,
                       layer->render.blend);
  else if (layer->render.mode >= MODE_TRANSFORM)
    Blit32_32(engine->linebuffer, scan, framewidth, layer->render.blend);

  blit_clipped_window(scan, window, inside, framewidth, windowwidth);

  return priority;
}

/* Draws the next scanline of the frame started with TLN_BeginFrame() or
 * TLN_BeginWindowFrame() */
/* fills the background with bitmap or solid color */
static void fill_background(uint32_t *scan, int size, int line) {
  if (engine->bg.bitmap && engine->bg.palette) {
    if (size > engine->bg.bitmap->width)
      size = engine->bg.bitmap->width;
    if (line < engine->bg.bitmap->height)
      engine->bg.blit_fast(TLN_GetBitmapPtr(engine->bg.bitmap, 0, line),
                           engine->bg.palette, scan, size, 1, 0, NULL);
  } else if (engine->bg.color) {
    BlitColor(scan, engine->bg.color, size, NULL);
  }
}

/* updates layer scroll position when world or layer is dirty */
static void update_layer_if_dirty(int c) {
  Layer *layer = &engine->layers[c];
  if (!engine->world.dirty && !layer->flags.dirty)
    return;
  const int lx = (int)((float)engine->world.x * layer->world.xfactor) -
                 layer->world.offsetx;
  const int ly = (int)((float)engine->world.y * layer->world.yfactor) -
                 layer->world.offsety;
  TLN_SetLayerPosition(c, lx, ly);
  layer->flags.dirty = false;
}

/* draws all non-priority background layers; returns true if any have priority
 * tiles */
static bool draw_regular_layers(int line) {
  bool priority = false;
  if (engine->numlayers == 0)
    return priority;
  if (engine->priority != NULL)
    memset(engine->priority, 0, engine->framebuffer.width * sizeof(uint32_t));
  for (int c = engine->numlayers - 1; c >= 0; c--) {
    update_layer_if_dirty(c);
    Layer const *layer = &engine->layers[c];
    if (layer->flags.ok && !layer->flags.priority)
      priority |= draw_background_scanline(c, line);
  }
  return priority;
}

/* updates sprite world-space position when dirty */
static void update_sprite_if_dirty(Sprite *sprite) {
  if (!GetSpriteFlag(sprite, SPRITE_FLAG_WORLD_SPACE))
    return;
  if (!GetSpriteFlag(sprite, SPRITE_FLAG_DIRTY) && !engine->world.dirty)
    return;
  sprite->pos.x = sprite->world_pos.x - engine->world.x;
  sprite->pos.y = sprite->world_pos.y - engine->world.y;
  UpdateSprite(sprite);
  SetSpriteFlag(sprite, SPRITE_FLAG_DIRTY, false);
}

/* draws all background sprites (FLAG_BACKGROUND) — rendered below every layer
 */
static void draw_background_sprites(uint32_t *scan, int line) {
  if (engine->numsprites == 0)
    return;
  List const *list = &engine->list_sprites;
  int index = list->first;
  while (index != -1) {
    Sprite *sprite = &engine->sprites[index];
    update_sprite_if_dirty(sprite);
    if (check_sprite_coverage(sprite, line) &&
        (sprite->flags & FLAG_BACKGROUND))
      sprite->funcs.draw(index, scan, line, 0, 0);
    index = sprite->list_node.next;
  }
}

/* draws all non-priority sprites; returns true if any priority sprites exist */
static bool draw_regular_sprites(uint32_t *scan, int line) {
  bool sprite_priority = false;
  if (engine->numsprites == 0)
    return sprite_priority;
  if (engine->collision != NULL)
    memset(engine->collision, -1, engine->framebuffer.width * sizeof(uint16_t));
  List const *list = &engine->list_sprites;
  int index = list->first;
  while (index != -1) {
    Sprite *sprite = &engine->sprites[index];
    update_sprite_if_dirty(sprite);
    bool has_coverage = check_sprite_coverage(sprite, line);
    bool has_background = (sprite->flags & FLAG_BACKGROUND) != 0;
    bool has_priority = (sprite->flags & FLAG_PRIORITY) != 0;
    if (has_background) {
      /* already drawn before layers — skip */
    } else if (has_coverage && !has_priority)
      sprite->funcs.draw(index, scan, line, 0, 0);
    else if (has_coverage && has_priority)
      sprite_priority = true;
    index = sprite->list_node.next;
  }
  return sprite_priority;
}

/* draws all priority background layers */
static void draw_priority_layers(int line) {
  for (int c = engine->numlayers - 1; c >= 0; c--) {
    Layer const *layer = &engine->layers[c];
    if (layer->flags.ok && layer->flags.priority)
      draw_background_scanline(c, line);
  }
}

/* overlays the priority tile buffer onto the framebuffer scanline */
static void overlay_priority_pixels(uint32_t *scan) {
  uint32_t const *src = engine->priority;
  uint32_t *dst = scan;
  for (int c = 0; c < engine->framebuffer.width; c++) {
    if (*src)
      *dst = *src;
    src++;
    dst++;
  }
}

/* draws all priority sprites */
static void draw_priority_sprites(uint32_t *scan, int line) {
  List const *list = &engine->list_sprites;
  int index = list->first;
  while (index != -1) {
    Sprite const *sprite = &engine->sprites[index];
    if (check_sprite_coverage(sprite, line) && (sprite->flags & FLAG_PRIORITY))
      sprite->funcs.draw(index, scan, line, 0, 0);
    index = sprite->list_node.next;
  }
}

/* Draws the next scanline of the frame started with TLN_BeginFrame() or
 * TLN_BeginWindowFrame() */
bool DrawScanline(void) {
  int line = engine->timing.line;
  uint32_t *scan = GetFramebufferLine(line);

  if (engine->callbacks.raster)
    engine->callbacks.raster(line);

  fill_background(scan, engine->framebuffer.width, line);
  draw_background_sprites(scan, line); /* behind all layers */

  bool background_priority = draw_regular_layers(line);
  bool sprite_priority = draw_regular_sprites(scan, line);

  if (engine->numlayers > 0)
    draw_priority_layers(line);

  if (background_priority)
    overlay_priority_pixels(scan);

  if (sprite_priority)
    draw_priority_sprites(scan, line);

  engine->world.dirty = false;
  engine->timing.line++;
  return engine->timing.line < engine->framebuffer.height;
}

typedef struct {
  int width;
  int height;
  int srcx;
  int srcy;
  int dx;
  int stride;
} Tilescan;

/* process flip flags */
static inline void process_flip(uint16_t flags, Tilescan *scan) {
  /* H/V flip */
  if (flags & FLAG_FLIPX) {
    scan->dx = -scan->dx;
    scan->srcx = scan->width - 1;
  }
  if (flags & FLAG_FLIPY)
    scan->srcy = scan->height - scan->srcy - 1;
}

/* process flip & rotation flags */
static inline void process_flip_rotation(uint16_t flags, Tilescan *scan) {
  if (flags & FLAG_ROTATE) {
    int tmp = scan->srcx;
    scan->srcx = scan->srcy;
    scan->srcy = tmp;
    scan->dx *= scan->stride;

    /* H/V flip */
    if (flags & FLAG_FLIPX) {
      scan->dx = -scan->dx;
      scan->srcy = scan->height - scan->srcy - 1;
    }
    if (flags & FLAG_FLIPY)
      scan->srcx = scan->width - scan->srcx - 1;
  } else {
    /* H/V flip */
    if (flags & FLAG_FLIPX) {
      scan->dx = -scan->dx;
      scan->srcx = scan->width - scan->srcx - 1;
    }
    if (flags & FLAG_FLIPY)
      scan->srcy = scan->height - scan->srcy - 1;
  }
}

/* draw scanline of tiled background */
static bool DrawTiledScanline(int nlayer, uint32_t *dstpixel, int nscan,
                              int tx1, int tx2) {
  const Layer *layer = (const Layer *)&engine->layers[nlayer];
  bool priority = false;
  Tilescan scan = {0};

  /* target lines */
  int x = tx1;
  const struct Tilemap *tilemap = layer->tilemap;
  const struct Tileset *tileset = tilemap->tilesets[0];
  int xpos = (layer->hstart + x) % layer->width;
  int xtile = xpos >> tileset->hshift;

  scan.width = scan.height = scan.stride = tileset->width;
  scan.srcx = xpos & GetTilesetHMask(tileset);

  /* fill whole scanline */
  int column = x % tileset->width;
  while (x < tx2) {
    /* column offset: update ypos */
    int ypos;
    if (layer->column) {
      ypos = (layer->vstart + nscan + layer->column[column]) % layer->height;
      if (ypos < 0)
        ypos = layer->height + ypos;
    } else
      ypos = (layer->vstart + nscan) % layer->height;

    int ytile = ypos >> tileset->vshift;
    scan.srcy = ypos & GetTilesetVMask(tileset);

    const union Tile *tile = &tilemap->tiles[ytile * tilemap->cols + xtile];

    /* get effective tile width */
    int tilewidth = tileset->width - scan.srcx;
    int x1 = x + tilewidth;
    if (x1 > tx2)
      x1 = tx2;
    int width = x1 - x;

    /* paint if not empty tile */
    if (tile->index != 0) {
      const struct Tileset *tileset2 = tilemap->tilesets[tile->tileset];
      const uint16_t tile_index = tileset2->tiles[tile->index] - 1;

      /* selects suitable palette */
      TLN_Palette palette = tileset2->palette;
      if (layer->palette != NULL)
        palette = layer->palette;
      else if (engine->palettes[tile->palette] != NULL)
        palette = engine->palettes[tile->palette];

      /* process rotate & flip flags */
      scan.dx = 1;
      if ((tile->flags & (FLAG_FLIPX + FLAG_FLIPY + FLAG_ROTATE)) != 0)
        process_flip_rotation(tile->flags, &scan);

      /* paint tile scanline */
      const uint8_t *srcpixel =
          &GetTilesetPixel(tileset2, tile_index, scan.srcx, scan.srcy);
      uint32_t *dst = dstpixel;
      if (tile->flags & FLAG_PRIORITY) {
        dst = engine->priority;
        priority = true;
      }

      layer->render.blitters[1](srcpixel, palette, dst + x, width, scan.dx, 0,
                                layer->render.blend);
    }

    /* next tile */
    x += width;
    xtile = (xtile + 1) % tilemap->cols;
    scan.srcx = 0;
    column += 1;
  }
  return priority;
}

/* draw scanline of tiled background with scaling */
static bool DrawTiledScanlineScaling(int nlayer, uint32_t *dstpixel, int nscan,
                                     int tx1, int tx2) {
  const Layer *layer = (const Layer *)&engine->layers[nlayer];
  bool priority = false;
  Tilescan scan = {0};

  /* target lines */
  int x = tx1;
  const struct Tilemap *tilemap = layer->tilemap;
  const struct Tileset *tileset = tilemap->tilesets[0];
  int xpos = (layer->hstart + fix2int(x * layer->scale.dx)) % layer->width;
  int xtile = xpos >> tileset->hshift;

  scan.width = scan.height = scan.stride = tileset->width;
  scan.srcx = xpos & GetTilesetHMask(tileset);

  /* fill whole scanline */
  fix_t fix_x = int2fix(x);
  int column = x % tileset->width;
  while (x < tx2) {
    /* column offset: update ypos */
    int ypos = nscan;
    if (layer->column)
      ypos += layer->column[column];

    ypos = layer->vstart + fix2int(ypos * layer->scale.dy);
    if (ypos < 0)
      ypos = layer->height + ypos;
    else
      ypos = ypos % layer->height;

    int ytile = ypos >> tileset->vshift;
    scan.srcy = ypos & GetTilesetVMask(tileset);

    /* get effective tile width */
    int tilewidth = tileset->width - scan.srcx;
    fix_t dx = int2fix(tilewidth);
    fix_t fix_tilewidth = tilewidth * layer->scale.xfactor;
    fix_x += fix_tilewidth;
    int x1 = fix2int(fix_x);
    int tilescalewidth = x1 - x;
    if (tilescalewidth)
      dx /= tilescalewidth;
    else
      dx = 0;

    /* right clip */
    if (x1 > tx2)
      x1 = tx2;
    int width = x1 - x;

    /* paint if tile is not empty */
    const union Tile *tile = &tilemap->tiles[ytile * tilemap->cols + xtile];
    if (tile->index != 0) {
      const struct Tileset *tileset2 = tilemap->tilesets[tile->tileset];
      const uint16_t tile_index = tileset2->tiles[tile->index] - 1;

      /* selects suitable palette */
      TLN_Palette palette = tileset2->palette;
      if (layer->palette != NULL)
        palette = layer->palette;
      else if (engine->palettes[tile->palette] != NULL)
        palette = engine->palettes[tile->palette];

      /* process flip flags */
      scan.dx = dx;
      if ((tile->flags & (FLAG_FLIPX + FLAG_FLIPY)) != 0)
        process_flip(tile->flags, &scan);

      /* paint tile scanline */
      const uint8_t *srcpixel =
          &GetTilesetPixel(tileset2, tile_index, scan.srcx, scan.srcy);
      uint32_t *dst = dstpixel;
      if (tile->flags & FLAG_PRIORITY) {
        dst = engine->priority;
        priority = true;
      }

      int line = GetTilesetLine(tileset2, tile_index, scan.srcy);
      bool color_key = *(tileset2->color_key + line);
      layer->render.blitters[color_key](srcpixel, palette, dst + x, width,
                                        scan.dx, 0, layer->render.blend);
    }

    /* next tile */
    x = x1;
    xtile = (xtile + 1) % tilemap->cols;
    scan.srcx = 0;
    column += 1;
  }
  return priority;
}

/* draw scanline of tiled background with affine transform */
static bool DrawTiledScanlineAffine(int nlayer, uint32_t *dstpixel, int nscan,
                                    int tx1, int tx2) {
  const Layer *layer = (const Layer *)&engine->layers[nlayer];
  Tilescan scan = {0};

  const struct Tilemap *tilemap = layer->tilemap;
  const struct Tileset *tileset = tilemap->tilesets[0];
  int xpos = layer->hstart;
  int ypos = layer->vstart + nscan;

  Point2D p1;
  Point2D p2;
  Point2DSet(&p1, (math2d_t)xpos + (math2d_t)tx1, (math2d_t)ypos);
  Point2DSet(&p2, (math2d_t)xpos + (math2d_t)tx2, (math2d_t)ypos);
  Point2DMultiply(&p1, &layer->transform);
  Point2DMultiply(&p2, &layer->transform);

  int x1 = float2fix(p1.x);
  int y1 = float2fix(p1.y);
  int x2 = float2fix(p2.x);
  int y2 = float2fix(p2.y);

  const int twidth = tx2 - tx1;
  const int dx = (x2 - x1) / twidth;
  const int dy = (y2 - y1) / twidth;

  scan.width = scan.height = scan.stride = tileset->width;
  dstpixel += tx1;

  while (tx1 < tx2) {
    xpos = abs(fix2int(x1) + layer->width) % layer->width;
    ypos = abs(fix2int(y1) + layer->height) % layer->height;

    int xtile = xpos >> tileset->hshift;
    int ytile = ypos >> tileset->vshift;

    scan.srcx = xpos & GetTilesetHMask(tileset);
    scan.srcy = ypos & GetTilesetVMask(tileset);
    const union Tile *tile = &tilemap->tiles[ytile * tilemap->cols + xtile];

    /* paint if not empty tile */
    if (tile->index != 0) {
      const struct Tileset *tileset2 = tilemap->tilesets[tile->tileset];
      const uint16_t tile_index = tileset2->tiles[tile->index] - 1;

      /* process flip & rotation flags */
      if ((tile->flags & (FLAG_FLIPX + FLAG_FLIPY + FLAG_ROTATE)) != 0)
        process_flip_rotation(tile->flags, &scan);

      /* paint RGB pixel value */
      const struct Palette *palette =
          layer->palette != NULL ? layer->palette : tileset2->palette;
      *dstpixel = palette->data[GetTilesetPixel(tileset2, tile_index, scan.srcx,
                                                scan.srcy)];
    }

    /* next pixel */
    tx1 += 1;
    x1 += dx;
    y1 += dy;
    dstpixel += 1;
  }
  return false;
}

/* draw scanline of tiled background with per-pixel mapping */
static bool DrawTiledScanlinePixelMapping(int nlayer, uint32_t *dstpixel,
                                          int nscan, int tx1, int tx2) {
  const Layer *layer = (const Layer *)&engine->layers[nlayer];
  Tilescan scan = {0};

  /* target lines */
  int x = tx1;
  dstpixel += x;

  const struct Tilemap *tilemap = layer->tilemap;
  const struct Tileset *tileset = tilemap->tilesets[0];
  const int hstart = layer->hstart + layer->width;
  const int vstart = layer->vstart + layer->height;
  const TLN_PixelMap *pixel_map =
      &layer->pixel_map[nscan * engine->framebuffer.width + x];

  scan.width = scan.height = scan.stride = tileset->width;

  while (x < tx2) {
    int xpos = abs(hstart + pixel_map->dx) % layer->width;
    int ypos = abs(vstart + pixel_map->dy) % layer->height;

    int xtile = xpos >> tileset->hshift;
    int ytile = ypos >> tileset->vshift;

    scan.srcx = xpos & GetTilesetHMask(tileset);
    scan.srcy = ypos & GetTilesetVMask(tileset);
    const union Tile *tile = &tilemap->tiles[ytile * tilemap->cols + xtile];

    /* paint if not empty tile */
    if (tile->index != 0) {
      const struct Tileset *tileset2 = tilemap->tilesets[tile->tileset];
      const uint16_t tile_index = tileset2->tiles[tile->index] - 1;

      /* process flip & rotation flags */
      if ((tile->flags & (FLAG_FLIPX + FLAG_FLIPY + FLAG_ROTATE)) != 0)
        process_flip_rotation(tile->flags, &scan);

      /* paint RGB pixel value */
      const struct Palette *palette =
          layer->palette != NULL ? layer->palette : tileset2->palette;
      *dstpixel = palette->data[GetTilesetPixel(tileset2, tile_index, scan.srcx,
                                                scan.srcy)];
    }

    /* next pixel */
    x += 1;
    dstpixel += 1;
    pixel_map += 1;
  }
  return false;
}

/* draw sprite scanline */
static bool DrawSpriteScanline(int nsprite, uint32_t *dstscan, int nscan,
                               int /*tx1*/, int /*tx2*/) {
  Sprite *sprite = &engine->sprites[nsprite];

  Tilescan scan = {0};
  scan.srcx = sprite->srcrect.x1;
  scan.srcy = sprite->srcrect.y1 + (nscan - sprite->dstrect.y1);
  scan.width = sprite->info->w;
  scan.height = sprite->info->h;
  scan.stride = sprite->pixel_data.pitch;

  /* disable rotation for non-squared sprites */
  uint16_t flags = (uint16_t)sprite->flags;
  if ((flags & FLAG_ROTATE) && sprite->info->w != sprite->info->h)
    flags &= ~FLAG_ROTATE;

  const int w = sprite->dstrect.x2 - sprite->dstrect.x1;

  /* process rotate & flip flags */
  scan.dx = 1;
  if ((flags & (FLAG_FLIPX + FLAG_FLIPY + FLAG_ROTATE)) != 0)
    process_flip_rotation(flags, &scan);

  /* blit scanline */
  uint8_t const *srcpixel = sprite->pixel_data.pixels +
                            (scan.srcy * sprite->pixel_data.pitch) + scan.srcx;
  uint32_t *dstpixel = dstscan + sprite->dstrect.x1;
  sprite->funcs.blitter(srcpixel, sprite->palette, dstpixel, w, scan.dx, 0,
                        sprite->blend);

  if (GetSpriteFlag(sprite, SPRITE_FLAG_DO_COLLISION)) {
    uint16_t *collision_pixel = engine->collision + sprite->dstrect.x1;
    DrawSpriteCollision(nsprite, srcpixel, collision_pixel, w, scan.dx);
  }
  return true;
}

/* draw sprite scanline with scaling */
static bool DrawScalingSpriteScanline(int nsprite, uint32_t *dstscan, int nscan,
                                      int /*tx1*/, int /*tx2*/) {
  Sprite *sprite = &engine->sprites[nsprite];

  int srcx = sprite->srcrect.x1;
  int srcy = sprite->srcrect.y1 + (nscan - sprite->dstrect.y1) * sprite->inc.y;
  int dstw = sprite->dstrect.x2 - sprite->dstrect.x1;

  /* H/V flip */
  int dx;
  if (sprite->flags & FLAG_FLIPX) {
    srcx = int2fix(sprite->info->w) - srcx;
    dx = -sprite->inc.x;
  } else {
    dx = sprite->inc.x;
  }
  if (sprite->flags & FLAG_FLIPY)
    srcy = int2fix(sprite->info->h) - srcy;

  /* blit scanline */
  uint8_t const *srcpixel =
      sprite->pixel_data.pixels + (fix2int(srcy) * sprite->pixel_data.pitch);
  uint32_t *dstpixel = dstscan + sprite->dstrect.x1;
  sprite->funcs.blitter(srcpixel, sprite->palette, dstpixel, dstw, dx, srcx,
                        sprite->blend);

  if (GetSpriteFlag(sprite, SPRITE_FLAG_DO_COLLISION)) {
    uint16_t *collision_pixel = engine->collision + sprite->dstrect.x1;
    DrawSpriteCollisionScaling(nsprite, srcpixel, collision_pixel, dstw, dx,
                               srcx);
  }
  return true;
}

/* updates per-pixel sprite collision buffer */
static void DrawSpriteCollision(int nsprite, uint8_t const *srcpixel,
                                uint16_t *dstpixel, int width, int dx) {
  while (width) {
    if (*srcpixel) {
      if (*dstpixel != 0xFFFF) {
        SetSpriteFlag(&engine->sprites[nsprite], SPRITE_FLAG_COLLISION, true);
        SetSpriteFlag(&engine->sprites[*dstpixel], SPRITE_FLAG_COLLISION, true);
      }
      *dstpixel = (uint16_t)nsprite;
    }
    srcpixel += dx;
    dstpixel += 1;
    width -= 1;
  }
}

/* updates per-pixel sprite collision buffer for scaled sprite */
static void DrawSpriteCollisionScaling(int nsprite, uint8_t const *srcpixel,
                                       uint16_t *dstpixel, int width, int dx,
                                       int srcx) {
  while (width) {
    uint32_t src = *(srcpixel + srcx / (1 << FIXED_BITS));
    if (src) {
      if (*dstpixel != 0xFFFF) {
        SetSpriteFlag(&engine->sprites[nsprite], SPRITE_FLAG_COLLISION, true);
        SetSpriteFlag(&engine->sprites[*dstpixel], SPRITE_FLAG_COLLISION, true);
      }
      *dstpixel = (uint16_t)nsprite;
    }

    /* next pixel */
    srcx += dx;
    dstpixel += 1;
    width -= 1;
  }
}

/* draws regular bitmap scanline for bitmap-based layer */
static bool DrawBitmapScanline(int nlayer, uint32_t *dstpixel, int nscan,
                               int tx1, int tx2) {
  const Layer *layer = (const Layer *)&engine->layers[nlayer];

  /* target lines */
  int x = tx1;
  dstpixel += x;
  int ypos = (layer->vstart + nscan) % layer->height;
  int xpos = (layer->hstart + x) % layer->width;

  /* draws bitmap scanline */
  TLN_Bitmap bitmap = layer->bitmap;
  TLN_Palette palette =
      layer->palette != NULL ? layer->palette : bitmap->palette;
  while (x < tx2) {
    /* get effective width */
    int width = layer->width - xpos;
    int x1 = x + width;
    if (x1 > tx2)
      x1 = tx2;
    width = x1 - x;

    uint8_t const *srcpixel = get_bitmap_ptr(bitmap, xpos, ypos);
    layer->render.blitters[1](srcpixel, palette, dstpixel, width, 1, 0,
                              layer->render.blend);
    x += width;
    dstpixel += width;
    xpos = 0;
  }
  return false;
}

/* draws regular bitmap scanline for bitmap-based layer with scaling */
static bool DrawBitmapScanlineScaling(int nlayer, uint32_t *dstpixel, int nscan,
                                      int tx1, int tx2) {
  const Layer *layer = (const Layer *)&engine->layers[nlayer];

  /* target line */
  int x = tx1;
  dstpixel += x;
  int xpos = (layer->hstart + fix2int(x * layer->scale.dx)) % layer->width;

  /* fill whole scanline */
  const struct Bitmap *bitmap = layer->bitmap;
  TLN_Palette palette =
      layer->palette != NULL ? layer->palette : bitmap->palette;
  fix_t fix_x = int2fix(x);
  while (x < tx2) {
    int ypos = layer->vstart + fix2int(nscan * layer->scale.dy);
    if (ypos < 0)
      ypos = layer->height + ypos;
    else
      ypos = ypos % layer->height;

    /* get effective width */
    int width = layer->width - xpos;
    fix_t dx = int2fix(width);
    fix_t fix_tilewidth = width * layer->scale.xfactor;
    fix_x += fix_tilewidth;
    int x1 = fix2int(fix_x);
    int tilescalewidth = x1 - x;
    if (tilescalewidth)
      dx /= tilescalewidth;
    else
      dx = 0;

    /* right clipping */
    if (x1 > tx2)
      x1 = tx2;
    width = x1 - x;

    /* draw bitmap scanline */
    uint8_t const *srcpixel = (uint8_t *)get_bitmap_ptr(bitmap, xpos, ypos);
    layer->render.blitters[1](srcpixel, palette, dstpixel, width, dx, 0,
                              layer->render.blend);

    /* next */
    dstpixel += width;
    x = x1;
    xpos = 0;
  }
  return false;
}

/* draws regular bitmap scanline for bitmap-based layer with affine transform */
static bool DrawBitmapScanlineAffine(int nlayer, uint32_t *dstpixel, int nscan,
                                     int tx1, int tx2) {
  const Layer *layer = (const Layer *)&engine->layers[nlayer];
  bool priority = false;

  int xpos = layer->hstart;
  int ypos = layer->vstart + nscan;

  Point2D p1;
  Point2D p2;
  Point2DSet(&p1, (math2d_t)xpos + (math2d_t)tx1, (math2d_t)ypos);
  Point2DSet(&p2, (math2d_t)xpos + (math2d_t)tx2, (math2d_t)ypos);
  Point2DMultiply(&p1, &layer->transform);
  Point2DMultiply(&p2, &layer->transform);

  int x1 = float2fix(p1.x);
  int y1 = float2fix(p1.y);
  int x2 = float2fix(p2.x);
  int y2 = float2fix(p2.y);

  const int twidth = tx2 - tx1;
  const int dx = (x2 - x1) / twidth;
  const int dy = (y2 - y1) / twidth;

  const struct Bitmap *bitmap = layer->bitmap;
  const struct Palette *palette =
      layer->palette != NULL ? layer->palette : bitmap->palette;
  while (tx1 < tx2) {
    xpos = abs(fix2int(x1) + layer->width) % layer->width;
    ypos = abs(fix2int(y1) + layer->height) % layer->height;
    *dstpixel = palette->data[*get_bitmap_ptr(bitmap, xpos, ypos)];

    /* next pixel */
    tx1 += 1;
    x1 += dx;
    y1 += dy;
    dstpixel += 1;
  }
  return priority;
}

/* draws regular bitmap scanline for bitmap-based layer with per-pixel mapping
 */
static bool DrawBitmapScanlinePixelMapping(int nlayer, uint32_t *dstpixel,
                                           int nscan, int tx1, int tx2) {
  const Layer *layer = (const Layer *)&engine->layers[nlayer];
  bool priority = false;

  /* target lines */
  int x = tx1;
  dstpixel += x;

  const int hstart = layer->hstart + layer->width;
  const int vstart = layer->vstart + layer->height;
  const struct Bitmap *bitmap = layer->bitmap;
  const TLN_PixelMap *pixel_map =
      &layer->pixel_map[nscan * engine->framebuffer.width + x];
  while (x < tx2) {
    int xpos = abs(hstart + pixel_map->dx) % layer->width;
    int ypos = abs(vstart + pixel_map->dy) % layer->height;
    *dstpixel = layer->palette->data[*get_bitmap_ptr(bitmap, xpos, ypos)];

    /* next pixel */
    x += 1;
    dstpixel += 1;
    pixel_map += 1;
  }
  return priority;
}

/* draws regular object layer scanline */
static bool DrawObjectScanline(int nlayer, uint32_t *dstpixel, int nscan,
                               int tx1, int tx2) {
  const Layer *layer = (const Layer *)&engine->layers[nlayer];
  struct _Object *object = layer->objects->list;
  struct _Object tmpobject = {0};

  int x1 = layer->hstart + tx1;
  int x2 = layer->hstart + tx2;
  int y = layer->vstart + nscan;
  uint32_t *dstscan = dstpixel;
  bool priority = false;

  while (object != NULL) {
    /* swap width & height for rotated objects */
    memcpy(&tmpobject, object, sizeof(struct _Object));
    if (tmpobject.flags & FLAG_ROTATE) {
      tmpobject.width = object->height;
      tmpobject.height = object->width;
    }

    if (IsObjectInLine(&tmpobject, x1, x2, y) && tmpobject.visible &&
        tmpobject.bitmap != NULL) {
      Tilescan scan = {0};
      scan.srcx = 0;
      scan.srcy = y - tmpobject.y;

      int dstx1 = tmpobject.x - x1;
      int dstx2 = dstx1 + tmpobject.width;
      if (dstx1 < tx1) {
        int w = tx1 - dstx1;
        scan.srcx = w;
        dstx1 = 0;
      }
      if (dstx2 > tx2)
        dstx2 = tx2;
      int w = dstx2 - dstx1;

      TLN_Bitmap bitmap = tmpobject.bitmap;
      scan.width = bitmap->width;
      scan.height = bitmap->height;
      scan.stride = bitmap->pitch;

      /* process rotate & flip flags */
      scan.dx = 1;
      if ((tmpobject.flags & (FLAG_FLIPX + FLAG_FLIPY + FLAG_ROTATE)) != 0)
        process_flip_rotation(tmpobject.flags, &scan);

      /* paint tile scanline */
      uint8_t const *srcpixel = get_bitmap_ptr(bitmap, scan.srcx, scan.srcy);
      uint32_t *target = dstscan;
      if (tmpobject.flags & FLAG_PRIORITY) {
        target = engine->priority;
        priority = true;
      }
      layer->render.blitters[1](srcpixel, bitmap->palette, target + dstx1, w,
                                scan.dx, 0, layer->render.blend);
    }
    object = object->next;
  }

  return priority;
}

/* draw modes */
enum {
  DRAW_SPRITE,
  DRAW_TILED_LAYER,
  DRAW_BITMAP_LAYER,
  DRAW_OBJECT_LAYER,
  MAX_DRAW_TYPE,
};

/* table of function pointers to draw procedures */
static const ScanDrawPtr draw_delegates[MAX_DRAW_TYPE][MAX_DRAW_MODE] = {
    {&DrawSpriteScanline, &DrawScalingSpriteScanline, NULL, NULL},
    {&DrawTiledScanline, &DrawTiledScanlineScaling, &DrawTiledScanlineAffine,
     &DrawTiledScanlinePixelMapping},
    {&DrawBitmapScanline, &DrawBitmapScanlineScaling, &DrawBitmapScanlineAffine,
     &DrawBitmapScanlinePixelMapping},
    {&DrawObjectScanline, NULL, NULL, NULL},
};

/* returns suitable draw procedure based on layer configuration */
ScanDrawPtr GetLayerDraw(Layer const *layer) {
  if (layer->tilemap != NULL)
    return draw_delegates[DRAW_TILED_LAYER][layer->render.mode];
  else if (layer->bitmap != NULL)
    return draw_delegates[DRAW_BITMAP_LAYER][layer->render.mode];
  else if (layer->objects != NULL)
    return draw_delegates[DRAW_OBJECT_LAYER][layer->render.mode];
  else
    return NULL;
}

/* returns suitable draw procedure based on sprite configuration */
ScanDrawPtr GetSpriteDraw(draw_t mode) {
  return draw_delegates[DRAW_SPRITE][mode];
}
