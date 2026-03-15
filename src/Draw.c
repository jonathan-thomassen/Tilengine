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

#include <SDL3/SDL_timer.h>
#include <stdlib.h>
#include <string.h>

#include "Bitmap.h"
#include "Engine.h"
#include "ObjectList.h"
#include "Palette.h"
#include "Sprite.h"
#include "Tilemap.h"
#include "Tilengine.h"
#include "Tileset.h"

/* private prototypes */
static void DrawSpriteCollision(int nsprite, uint8_t const *srcpixel, uint16_t *dstpixel, int width,
                                int dx);
static void DrawSpriteCollisionScaling(int nsprite, uint8_t const *srcpixel, uint16_t *dstpixel,
                                       int width, int dx, int srcx);

/* blend-mask render-path profiling counters (accumulated per frame) */
uint64_t g_prof_linebuf_ticks = 0;
uint64_t g_prof_fillmask_ticks = 0;
uint64_t g_prof_blit_ticks = 0;
uint64_t g_prof_layers_ticks = 0;
uint64_t g_prof_sprites_ticks = 0;
uint64_t g_prof_per_layer_ticks[8] = {0};

static bool check_sprite_coverage(Sprite const *sprite, int nscan) {
    /* check sprite coverage */
    if (nscan < sprite->dstrect.y1 || nscan >= sprite->dstrect.y2) {
        return false;
    }
    if (sprite->dstrect.x2 < 0 || sprite->srcrect.x2 < 0) {
        return false;
    }
    if ((sprite->flags & FLAG_MASKED) && nscan >= engine->sprite_mask.top &&
        nscan <= engine->sprite_mask.bottom) {
        return false;
    }
    return true;
}

/* selects target scan buffer and sets build_mosaic flag */
static uint32_t *select_scan_buffer(Layer const *layer, int line, bool *build_mosaic) {
    *build_mosaic = false;
    if (layer->mosaic.h != 0) {
        if (line % layer->mosaic.h == 0) {
            *build_mosaic = true;
            return engine->linebuffer;
        }
        return NULL;
    }
    if (layer->render.mode >= MODE_TRANSFORM) {
        return engine->linebuffer;
    }
    return GetFramebufferLine(line);
}

/* draws the regular (non-mosaic) region respecting window invert and inside */
static bool draw_window_region(int nlayer, uint32_t *scan, int line, LayerWindow const *window,
                               bool inside, int framewidth) {
    Layer const *layer = &engine->layers[nlayer];
    bool priority = false;
    if (!window->invert) {
        if (inside) {
            priority |= layer->render.draw(nlayer, scan, line, window->x1, window->x2);
        }
    } else {
        if (inside) {
            priority |= layer->render.draw(nlayer, scan, line, 0, layer->window.x1);
            priority |= layer->render.draw(nlayer, scan, line, layer->window.x2, framewidth);
        } else {
            priority |= layer->render.draw(nlayer, scan, line, 0, framewidth);
        }
    }
    return priority;
}

/* blits the mosaic linebuffer to the framebuffer respecting window settings */
static void blit_mosaic_window(uint32_t *mosaic, uint32_t *scan, LayerWindow const *window,
                               bool inside, int framewidth, int windowwidth, uint8_t const *blend) {
    if (!window->invert) {
        if (inside) {
            Blit32_32(mosaic + window->x1, scan + window->x1, windowwidth, blend);
        }
    } else {
        if (inside) {
            Blit32_32(mosaic, scan, windowwidth, blend);
            Blit32_32(mosaic + window->x2, scan + window->x2, framewidth - window->x2, blend);
        } else {
            Blit32_32(mosaic, scan, framewidth, blend);
        }
    }
}

/* fills the clipped (outside-window) region with the window color */
static void blit_clipped_window(uint32_t *scan, LayerWindow const *window, bool inside,
                                int framewidth, int windowwidth) {
    if (window->color == 0) {
        return;
    }
    if (!window->invert) {
        if (inside) {
            BlitColor(scan, window->color, window->x1, window->blend);
            BlitColor(scan + window->x2, window->color, framewidth - window->x2, window->blend);
        } else {
            BlitColor(scan, window->color, framewidth, window->blend);
        }
    } else if (inside) {
        BlitColor(scan + window->x1, window->color, windowwidth, window->blend);
    }
}

/* fills engine->blend_mask for the given scanline by sampling the actual
 * tileset pixel index of each screen column from layer nmask: positions where
 * the tileset pixel is non-zero (opaque) are set to 1, transparent pixels
 * (palette index 0) or empty tiles leave the mask at 0.
 *
 * Optimization: compute a single row-pointer into the tileset pixel data once
 * per tile (rather than recalculating the full index inside a per-pixel loop)
 * and walk it forward (or backward for FLIPX), removing the multiply+shift
 * from every inner-loop iteration. */
static void fill_blend_mask_scanline(int nmask, int nscan) {
    Layer const *layer = &engine->layers[nmask];
    int framewidth = engine->framebuffer.width;
    memset(engine->blend_mask, 0, framewidth);

    if (!layer->flags.ok || layer->tilemap == NULL) {
        return;
    }

    struct Tilemap const *tilemap = layer->tilemap;
    struct Tileset const *tileset = tilemap->tilesets[0];

    int x = 0;
    int xpos = (layer->hstart + x) % layer->width;
    int xtile = xpos >> tileset->hshift;
    int srcx = xpos & GetTilesetHMask(tileset);

    int ypos = (layer->vstart + nscan) % layer->height;
    int ytile = ypos >> tileset->vshift;
    int srcy_base = ypos & GetTilesetVMask(tileset);

    while (x < framewidth) {
        int tilewidth = tileset->width - srcx;
        int x1 = x + tilewidth;
        if (x1 > framewidth) {
            x1 = framewidth;
        }
        int width = x1 - x;

        union Tile const *tile = &tilemap->tiles[((ptrdiff_t)ytile * tilemap->cols) + xtile];

        if (tile->index != 0) {
            struct Tileset const *ts = tilemap->tilesets[tile->tileset];
            int tile_index = ts->tiles[tile->index] - 1;
            int srcy = (tile->flags & FLAG_FLIPY) ? ts->height - srcy_base - 1 : srcy_base;
            /* pointer to the first pixel of this tile's row in the data array */
            const uint8_t *row =
                &ts->data[((((ptrdiff_t)tile_index << ts->vshift) + srcy) << ts->hshift)];
            uint8_t *out = &engine->blend_mask[x];
            if (tile->flags & FLAG_FLIPX) {
                /* walk backward: first sample column is (width-1 - srcx_offset) from right */
                const uint8_t *p = row + (ts->width - 1 - srcx);
                for (int i = 0; i < width; i++) {
                    if (*p-- != 0)
                        out[i] = 1;
                }
            } else {
                const uint8_t *p = row + srcx;
                for (int i = 0; i < width; i++) {
                    if (*p++ != 0)
                        out[i] = 1;
                }
            }
        }

        x += width;
        if (++xtile >= tilemap->cols) {
            xtile = 0;
        }
        srcx = 0;
    }
}

/* draw background scanline taking into account mosaic and windowing effects */
static bool draw_background_scanline(int nlayer, int line) {
    Layer *layer = &engine->layers[nlayer];
    LayerWindow const *window = &layer->window;
    uint32_t *mosaic = layer->mosaic.buffer;
    const bool inside = (line >= window->y1 && line <= window->y2) != 0;
    const int framewidth = engine->framebuffer.width;
    const int windowwidth = layer->window.x2 - layer->window.x1;
    bool priority = false;
    bool build_mosaic = false;

    /* per-pixel blend mask path: render layer to linebuffer without blend,
     * then composite onto framebuffer using the mask layer's tile coverage. */
    if (layer->blend_mask_layer >= 0 && engine->blend_mask != NULL) {
        uint8_t *saved_blend = layer->render.blend;
        ScanBlitPtr saved_blitters[2] = {layer->render.blitters[0], layer->render.blitters[1]};
        uint32_t *lb = engine->linebuffer;
        uint32_t *fb = GetFramebufferLine(line);

        /* temporarily switch to non-blend blitters so pixels land in linebuffer
         * as plain RGBA values ready for the masked composite below. */
        bool scaling = layer->render.mode == MODE_SCALING;
        layer->render.blend = NULL;
        layer->render.blitters[0] = SelectBlitter(false, scaling, false);
        layer->render.blitters[1] = SelectBlitter(true, scaling, false);

        memset(lb, 0, framewidth * sizeof(uint32_t));
        uint64_t t0 = SDL_GetPerformanceCounter();
        priority |= draw_window_region(nlayer, lb, line, window, inside, framewidth);
        uint64_t t1 = SDL_GetPerformanceCounter();

        layer->render.blend = saved_blend;
        layer->render.blitters[0] = saved_blitters[0];
        layer->render.blitters[1] = saved_blitters[1];

        engine->blend_mask_blend = saved_blend;
        fill_blend_mask_scanline(layer->blend_mask_layer, line);
        uint64_t t2 = SDL_GetPerformanceCounter();
        Blit32_32_Masked(lb, fb, engine->blend_mask, saved_blend, framewidth);
        uint64_t t3 = SDL_GetPerformanceCounter();

        g_prof_linebuf_ticks += t1 - t0;
        g_prof_fillmask_ticks += t2 - t1;
        g_prof_blit_ticks += t3 - t2;
        return priority;
    }

    uint32_t *scan = select_scan_buffer(layer, line, &build_mosaic);
    if (scan == engine->linebuffer && scan != NULL) {
        memset(scan, 0, framewidth * sizeof(uint32_t));
    }

    if (scan != NULL) {
        priority |= draw_window_region(nlayer, scan, line, window, inside, framewidth);
    }

    scan = GetFramebufferLine(line);

    /* build mosaic to linebuffer */
    if (build_mosaic) {
        if (mosaic != NULL) {
            memset(mosaic, 0, framewidth * sizeof(uint32_t));
        }
        BlitMosaic(engine->linebuffer, mosaic, framewidth, layer->mosaic.w, NULL);
    }

    if (layer->mosaic.h != 0) {
        blit_mosaic_window(mosaic, scan, window, inside, framewidth, windowwidth,
                           layer->render.blend);
    } else if (layer->render.mode >= MODE_TRANSFORM) {
        Blit32_32(engine->linebuffer, scan, framewidth, layer->render.blend);
    }

    blit_clipped_window(scan, window, inside, framewidth, windowwidth);

    return priority;
}

/* Draws the next scanline of the frame started with TLN_BeginFrame() or
 * TLN_BeginWindowFrame() */
/* fills the background with bitmap or solid color */
static void fill_background(uint32_t *scan, int size, int line) {
    if (engine->bg.bitmap && engine->bg.palette) {
        if (size > engine->bg.bitmap->width) {
            size = engine->bg.bitmap->width;
        }
        if (line < engine->bg.bitmap->height) {
            engine->bg.blit_fast(TLN_GetBitmapPtr(engine->bg.bitmap, 0, line), engine->bg.palette,
                                 scan, size, 1, 0, NULL);
        }
    } else if (engine->bg.color) {
        BlitColor(scan, engine->bg.color, size, NULL);
    }
}

/* updates layer scroll position when world or layer is dirty */
static void update_layer_if_dirty(int c) {
    Layer *layer = &engine->layers[c];
    if (!engine->world.dirty && !layer->flags.dirty) {
        return;
    }
    const int lx = (int)((float)engine->world.x * layer->world.xfactor) - layer->world.offsetx;
    const int ly = (int)((float)engine->world.y * layer->world.yfactor) - layer->world.offsety;
    TLN_SetLayerPosition(c, lx, ly);
    layer->flags.dirty = false;
}

/* draws all non-priority background layers; returns true if any have priority
 * tiles */
static bool draw_regular_layers(int line) {
    bool priority = false;
    if (engine->numlayers == 0) {
        return priority;
    }
    if (engine->priority != NULL) {
        memset(engine->priority, 0, engine->framebuffer.width * sizeof(uint32_t));
    }
    for (int c = engine->numlayers - 1; c >= 0; c--) {
        update_layer_if_dirty(c);
        Layer const *layer = &engine->layers[c];
        if ((int)layer->flags.ok && !layer->flags.priority) {
            uint64_t tPL0 = SDL_GetPerformanceCounter();
            priority |= draw_background_scanline(c, line);
            uint64_t tPL1 = SDL_GetPerformanceCounter();
            if (c < 8)
                g_prof_per_layer_ticks[c] += tPL1 - tPL0;
        }
    }
    return priority;
}

/* updates sprite world-space position when dirty */
static void update_sprite_if_dirty(Sprite *sprite) {
    if (!GetSpriteFlag(sprite, SPRITE_FLAG_WORLD_SPACE)) {
        return;
    }
    if (!GetSpriteFlag(sprite, SPRITE_FLAG_DIRTY) && !engine->world.dirty) {
        return;
    }
    sprite->pos.x = sprite->world_pos.x - engine->world.x;
    sprite->pos.y = sprite->world_pos.y - engine->world.y;
    UpdateSprite(sprite);
    SetSpriteFlag(sprite, SPRITE_FLAG_DIRTY, false);
}

/* draws all background sprites (FLAG_BACKGROUND) — rendered below every layer
 */
static void draw_background_sprites(uint32_t *scan, int line) {
    if (engine->numsprites == 0) {
        return;
    }
    List const *list = &engine->list_sprites;
    int index = list->first;
    while (index != -1) {
        Sprite *sprite = &engine->sprites[index];
        update_sprite_if_dirty(sprite);
        if ((int)check_sprite_coverage(sprite, line) && (sprite->flags & FLAG_BACKGROUND)) {
            sprite->funcs.draw(index, scan, line, 0, 0);
        }
        index = sprite->list_node.next;
    }
}

/* draws a single sprite scanline via the blend mask when the sprite has
 * SPRITE_FLAG_BLEND_MASK set; otherwise draws it directly onto scan. */
static void draw_sprite_with_blend_mask(int index, Sprite const *sprite, uint32_t *scan, int line) {
    if (GetSpriteFlag(sprite, SPRITE_FLAG_BLEND_MASK) && engine->blend_mask &&
        engine->blend_mask_blend) {
        const int fw = engine->framebuffer.width;
        int x1 = sprite->dstrect.x1 > 0 ? sprite->dstrect.x1 : 0;
        int x2 = sprite->dstrect.x2 < fw ? sprite->dstrect.x2 : fw;
        if (x1 < x2) {
            uint32_t *lb = engine->linebuffer;
            memset(lb + x1, 0, (x2 - x1) * sizeof(uint32_t));
            sprite->funcs.draw(index, lb, line, 0, 0);
            Blit32_32_Masked(lb + x1, scan + x1, engine->blend_mask + x1, engine->blend_mask_blend,
                             x2 - x1);
        }
    } else {
        sprite->funcs.draw(index, scan, line, 0, 0);
    }
}

/* draws all non-priority sprites; returns true if any priority sprites exist */
static bool draw_regular_sprites(uint32_t *scan, int line) {
    bool sprite_priority = false;
    if (engine->numsprites == 0) {
        return sprite_priority;
    }
    if (engine->collision != NULL) {
        memset(engine->collision, -1, engine->framebuffer.width * sizeof(uint16_t));
    }
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
        } else if ((int)has_coverage && !has_priority) {
            draw_sprite_with_blend_mask(index, sprite, scan, line);
        } else if ((int)has_coverage && (int)has_priority) {
            sprite_priority = true;
        }
        index = sprite->list_node.next;
    }
    return sprite_priority;
}

/* draws all priority background layers */
static void draw_priority_layers(int line) {
    for (int c = engine->numlayers - 1; c >= 0; c--) {
        Layer const *layer = &engine->layers[c];
        if ((int)layer->flags.ok && (int)layer->flags.priority) {
            draw_background_scanline(c, line);
        }
    }
}

/* overlays the priority tile buffer onto the framebuffer scanline */
static void overlay_priority_pixels(uint32_t *scan) {
    uint32_t const *src = engine->priority;
    uint32_t *dst = scan;
    for (int c = 0; c < engine->framebuffer.width; c++) {
        if (*src) {
            *dst = *src;
        }
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
        if ((int)check_sprite_coverage(sprite, line) && (sprite->flags & FLAG_PRIORITY)) {
            draw_sprite_with_blend_mask(index, sprite, scan, line);
        }
        index = sprite->list_node.next;
    }
}

/* Draws the next scanline of the frame started with TLN_BeginFrame() or
 * TLN_BeginWindowFrame() */
bool DrawScanline(void) {
    int line = engine->timing.line;
    uint32_t *scan = GetFramebufferLine(line);

    if (engine->callbacks.raster) {
        engine->callbacks.raster(line);
    }

    fill_background(scan, engine->framebuffer.width, line);
    draw_background_sprites(scan, line); /* behind all layers */

    uint64_t tL0 = SDL_GetPerformanceCounter();
    bool background_priority = draw_regular_layers(line);
    uint64_t tL1 = SDL_GetPerformanceCounter();
    bool sprite_priority = draw_regular_sprites(scan, line);
    uint64_t tL2 = SDL_GetPerformanceCounter();
    g_prof_layers_ticks += tL1 - tL0;
    g_prof_sprites_ticks += tL2 - tL1;

    if (background_priority) {
        overlay_priority_pixels(scan);
    }

    if (sprite_priority) {
        draw_priority_sprites(scan, line);
    }

    /* Priority layers are drawn last so they appear above all sprites. */
    if (engine->numlayers > 0) {
        draw_priority_layers(line);
    }

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
    if (flags & FLAG_FLIPY) {
        scan->srcy = scan->height - scan->srcy - 1;
    }
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
        if (flags & FLAG_FLIPY) {
            scan->srcx = scan->width - scan->srcx - 1;
        }
    } else {
        /* H/V flip */
        if (flags & FLAG_FLIPX) {
            scan->dx = -scan->dx;
            scan->srcx = scan->width - scan->srcx - 1;
        }
        if (flags & FLAG_FLIPY) {
            scan->srcy = scan->height - scan->srcy - 1;
        }
    }
}

/* draw scanline of tiled background */
static bool DrawTiledScanline(int nlayer, uint32_t *dstpixel, int nscan, int tx1, int tx2) {
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

    /* cache loop-invariant values */
    struct Palette *const layer_palette = layer->palette;
    const int layer_height = layer->height;

    /* fill whole scanline */
    int column = x % tileset->width;
    while (x < tx2) {
        /* column offset: update ypos */
        int ypos;
        if (layer->column) {
            ypos = (layer->vstart + nscan + layer->column[column]) % layer_height;
            if (ypos < 0) {
                ypos = layer_height + ypos;
            }
        } else {
            ypos = (layer->vstart + nscan) % layer_height;
        }

        int ytile = ypos >> tileset->vshift;
        scan.srcy = ypos & GetTilesetVMask(tileset);

        const union Tile *tile = &tilemap->tiles[((ptrdiff_t)ytile * tilemap->cols) + xtile];

        /* get effective tile width */
        int tilewidth = tileset->width - scan.srcx;
        int x1 = x + tilewidth;
        if (x1 > tx2) {
            x1 = tx2;
        }
        int width = x1 - x;

        /* paint if not empty tile */
        if (tile->index != 0) {
            const struct Tileset *tileset2 = tilemap->tilesets[tile->tileset];
            const uint16_t tile_index = tileset2->tiles[tile->index] - 1;

            /* selects suitable palette */
            TLN_Palette palette = tileset2->palette;
            if (layer_palette != NULL) {
                palette = layer_palette;
            } else if (engine->palettes[tile->palette] != NULL) {
                palette = engine->palettes[tile->palette];
            }

            /* process rotate & flip flags */
            scan.dx = 1;
            if ((tile->flags & (FLAG_FLIPX + FLAG_FLIPY + FLAG_ROTATE)) != 0) {
                process_flip_rotation(tile->flags, &scan);
            }

            /* paint tile scanline */
            const uint8_t *srcpixel = &GetTilesetPixel(tileset2, tile_index, scan.srcx, scan.srcy);
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
        if (++xtile >= tilemap->cols) {
            xtile = 0;
        }
        scan.srcx = 0;
        column += 1;
    }
    return priority;
}

/* draw scanline of tiled background with scaling */
static bool DrawTiledScanlineScaling(int nlayer, uint32_t *dstpixel, int nscan, int tx1, int tx2) {
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

    /* cache loop-invariant values */
    struct Palette *const layer_palette = layer->palette;
    const fix_t xfactor = layer->scale.xfactor;
    const fix_t scale_dy = layer->scale.dy;
    const int layer_height = layer->height;

    /* fill whole scanline */
    fix_t fix_x = int2fix(x);
    int column = x % tileset->width;
    while (x < tx2) {
        /* column offset: update ypos */
        int ypos = nscan;
        if (layer->column) {
            ypos += layer->column[column];
        }

        ypos = layer->vstart + fix2int(ypos * scale_dy);
        if (ypos < 0) {
            ypos = layer_height + ypos;
        } else {
            ypos = ypos % layer_height;
        }

        int ytile = ypos >> tileset->vshift;
        scan.srcy = ypos & GetTilesetVMask(tileset);

        /* get effective tile width */
        int tilewidth = tileset->width - scan.srcx;
        fix_t dx = int2fix(tilewidth);
        fix_t fix_tilewidth = tilewidth * xfactor;
        fix_x += fix_tilewidth;
        int x1 = fix2int(fix_x);
        int tilescalewidth = x1 - x;
        if (tilescalewidth) {
            dx /= tilescalewidth;
        } else {
            dx = 0;
        }

        /* right clip */
        if (x1 > tx2) {
            x1 = tx2;
        }
        int width = x1 - x;

        /* paint if tile is not empty */
        const union Tile *tile = &tilemap->tiles[((ptrdiff_t)ytile * tilemap->cols) + xtile];
        if (tile->index != 0) {
            const struct Tileset *tileset2 = tilemap->tilesets[tile->tileset];
            const uint16_t tile_index = tileset2->tiles[tile->index] - 1;

            /* selects suitable palette */
            TLN_Palette palette = tileset2->palette;
            if (layer_palette != NULL) {
                palette = layer_palette;
            } else if (engine->palettes[tile->palette] != NULL) {
                palette = engine->palettes[tile->palette];
            }

            /* process flip flags */
            scan.dx = dx;
            if ((tile->flags & (FLAG_FLIPX + FLAG_FLIPY)) != 0) {
                process_flip(tile->flags, &scan);
            }

            /* paint tile scanline */
            const uint8_t *srcpixel = &GetTilesetPixel(tileset2, tile_index, scan.srcx, scan.srcy);
            uint32_t *dst = dstpixel;
            if (tile->flags & FLAG_PRIORITY) {
                dst = engine->priority;
                priority = true;
            }

            int line = GetTilesetLine(tileset2, tile_index, scan.srcy);
            bool color_key = *(tileset2->color_key + line);
            layer->render.blitters[color_key](srcpixel, palette, dst + x, width, scan.dx, 0,
                                              layer->render.blend);
        }

        /* next tile */
        x = x1;
        if (++xtile >= tilemap->cols) {
            xtile = 0;
        }
        scan.srcx = 0;
        column += 1;
    }
    return priority;
}

/* draw scanline of tiled background with affine transform */
static bool DrawTiledScanlineAffine(int nlayer, uint32_t *dstpixel, int nscan, int tx1, int tx2) {
    const Layer *layer = (const Layer *)&engine->layers[nlayer];
    bool priority = false;
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
    uint32_t *prioritypixel = engine->priority + tx1;

    while (tx1 < tx2) {
        xpos = abs(fix2int(x1) + layer->width) % layer->width;
        ypos = abs(fix2int(y1) + layer->height) % layer->height;

        int xtile = xpos >> tileset->hshift;
        int ytile = ypos >> tileset->vshift;

        scan.srcx = xpos & GetTilesetHMask(tileset);
        scan.srcy = ypos & GetTilesetVMask(tileset);
        const union Tile *tile = &tilemap->tiles[((ptrdiff_t)ytile * tilemap->cols) + xtile];

        /* paint if not empty tile */
        if (tile->index != 0) {
            const struct Tileset *tileset2 = tilemap->tilesets[tile->tileset];
            const uint16_t tile_index = tileset2->tiles[tile->index] - 1;

            /* process flip & rotation flags */
            if ((tile->flags & (FLAG_FLIPX + FLAG_FLIPY + FLAG_ROTATE)) != 0) {
                process_flip_rotation(tile->flags, &scan);
            }

            /* paint RGB pixel value (skip palette index 0 = transparent) */
            const struct Palette *palette =
                layer->palette != NULL ? layer->palette : tileset2->palette;
            const uint8_t pix = GetTilesetPixel(tileset2, tile_index, scan.srcx, scan.srcy);
            if (pix != 0) {
                bool is_priority = (tile->flags & FLAG_PRIORITY) != 0;
                uint32_t *target = (int)is_priority ? prioritypixel : dstpixel;
                *target = palette->data[pix];
                priority |= is_priority;
            }
        }

        /* next pixel */
        tx1 += 1;
        x1 += dx;
        y1 += dy;
        dstpixel += 1;
        prioritypixel += 1;
    }
    return priority;
}

/* draw scanline of tiled background with per-pixel mapping */
static bool DrawTiledScanlinePixelMapping(int nlayer, uint32_t *dstpixel, int nscan, int tx1,
                                          int tx2) {
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
        &layer->pixel_map[((ptrdiff_t)nscan * engine->framebuffer.width) + x];

    scan.width = scan.height = scan.stride = tileset->width;

    while (x < tx2) {
        int xpos = abs(hstart + pixel_map->dx) % layer->width;
        int ypos = abs(vstart + pixel_map->dy) % layer->height;

        int xtile = xpos >> tileset->hshift;
        int ytile = ypos >> tileset->vshift;

        scan.srcx = xpos & GetTilesetHMask(tileset);
        scan.srcy = ypos & GetTilesetVMask(tileset);
        const union Tile *tile = &tilemap->tiles[((ptrdiff_t)ytile * tilemap->cols) + xtile];

        /* paint if not empty tile */
        if (tile->index != 0) {
            const struct Tileset *tileset2 = tilemap->tilesets[tile->tileset];
            const uint16_t tile_index = tileset2->tiles[tile->index] - 1;

            /* process flip & rotation flags */
            if ((tile->flags & (FLAG_FLIPX + FLAG_FLIPY + FLAG_ROTATE)) != 0) {
                process_flip_rotation(tile->flags, &scan);
            }

            /* paint RGB pixel value */
            const struct Palette *palette =
                layer->palette != NULL ? layer->palette : tileset2->palette;
            *dstpixel = palette->data[GetTilesetPixel(tileset2, tile_index, scan.srcx, scan.srcy)];
        }

        /* next pixel */
        x += 1;
        dstpixel += 1;
        pixel_map += 1;
    }
    return false;
}

/* draw sprite scanline */
static bool DrawSpriteScanline(int nsprite, uint32_t *dstscan, int nscan, int tx1 [[maybe_unused]],
                               int tx2 [[maybe_unused]]) {
    Sprite *sprite = &engine->sprites[nsprite];

    Tilescan scan = {0};
    scan.srcx = sprite->srcrect.x1;
    scan.srcy = sprite->srcrect.y1 + (nscan - sprite->dstrect.y1);
    scan.width = sprite->info->w;
    scan.height = sprite->info->h;
    scan.stride = sprite->pixel_data.pitch;

    /* disable rotation for non-squared sprites */
    uint16_t flags = (uint16_t)sprite->flags;
    if ((flags & FLAG_ROTATE) && sprite->info->w != sprite->info->h) {
        flags &= ~FLAG_ROTATE;
    }

    const int w = sprite->dstrect.x2 - sprite->dstrect.x1;

    /* process rotate & flip flags */
    scan.dx = 1;
    if ((flags & (FLAG_FLIPX + FLAG_FLIPY + FLAG_ROTATE)) != 0) {
        process_flip_rotation(flags, &scan);
    }

    /* blit scanline */
    uint8_t const *srcpixel =
        sprite->pixel_data.pixels + ((ptrdiff_t)scan.srcy * sprite->pixel_data.pitch) + scan.srcx;
    uint32_t *dstpixel = dstscan + sprite->dstrect.x1;
    sprite->funcs.blitter(srcpixel, sprite->palette, dstpixel, w, scan.dx, 0, sprite->blend);

    if (GetSpriteFlag(sprite, SPRITE_FLAG_DO_COLLISION)) {
        uint16_t *collision_pixel = engine->collision + sprite->dstrect.x1;
        DrawSpriteCollision(nsprite, srcpixel, collision_pixel, w, scan.dx);
    }
    return true;
}

/* draw sprite scanline with scaling */
static bool DrawScalingSpriteScanline(int nsprite, uint32_t *dstscan, int nscan,
                                      int tx1 [[maybe_unused]], int tx2 [[maybe_unused]]) {
    Sprite *sprite = &engine->sprites[nsprite];

    int srcx = sprite->srcrect.x1;
    int srcy = sprite->srcrect.y1 + ((nscan - sprite->dstrect.y1) * sprite->inc.y);
    int dstw = sprite->dstrect.x2 - sprite->dstrect.x1;

    /* H/V flip */
    int dx;
    if (sprite->flags & FLAG_FLIPX) {
        srcx = int2fix(sprite->info->w) - srcx;
        dx = -sprite->inc.x;
    } else {
        dx = sprite->inc.x;
    }
    if (sprite->flags & FLAG_FLIPY) {
        srcy = int2fix(sprite->info->h) - srcy;
    }

    /* blit scanline */
    uint8_t const *srcpixel =
        sprite->pixel_data.pixels + ((ptrdiff_t)fix2int(srcy) * sprite->pixel_data.pitch);
    uint32_t *dstpixel = dstscan + sprite->dstrect.x1;
    sprite->funcs.blitter(srcpixel, sprite->palette, dstpixel, dstw, dx, srcx, sprite->blend);

    if (GetSpriteFlag(sprite, SPRITE_FLAG_DO_COLLISION)) {
        uint16_t *collision_pixel = engine->collision + sprite->dstrect.x1;
        DrawSpriteCollisionScaling(nsprite, srcpixel, collision_pixel, dstw, dx, srcx);
    }
    return true;
}

/* updates per-pixel sprite collision buffer */
static void DrawSpriteCollision(int nsprite, uint8_t const *srcpixel, uint16_t *dstpixel, int width,
                                int dx) {
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
static void DrawSpriteCollisionScaling(int nsprite, uint8_t const *srcpixel, uint16_t *dstpixel,
                                       int width, int dx, int srcx) {
    while (width) {
        uint32_t src = *(srcpixel + (srcx / (1 << FIXED_BITS)));
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
static bool DrawBitmapScanline(int nlayer, uint32_t *dstpixel, int nscan, int tx1, int tx2) {
    const Layer *layer = (const Layer *)&engine->layers[nlayer];

    /* target lines */
    int x = tx1;
    dstpixel += x;
    int ypos = (layer->vstart + nscan) % layer->height;
    int xpos = (layer->hstart + x) % layer->width;

    /* draws bitmap scanline */
    TLN_Bitmap bitmap = layer->bitmap;
    TLN_Palette palette = layer->palette != NULL ? layer->palette : bitmap->palette;
    while (x < tx2) {
        /* get effective width */
        int width = layer->width - xpos;
        int x1 = x + width;
        if (x1 > tx2) {
            x1 = tx2;
        }
        width = x1 - x;

        uint8_t const *srcpixel = get_bitmap_ptr(bitmap, xpos, ypos);
        layer->render.blitters[1](srcpixel, palette, dstpixel, width, 1, 0, layer->render.blend);
        x += width;
        dstpixel += width;
        xpos = 0;
    }
    return false;
}

/* draws regular bitmap scanline for bitmap-based layer with scaling */
static bool DrawBitmapScanlineScaling(int nlayer, uint32_t *dstpixel, int nscan, int tx1, int tx2) {
    const Layer *layer = (const Layer *)&engine->layers[nlayer];

    /* target line */
    int x = tx1;
    dstpixel += x;
    int xpos = (layer->hstart + fix2int(x * layer->scale.dx)) % layer->width;

    /* fill whole scanline */
    const struct Bitmap *bitmap = layer->bitmap;
    TLN_Palette palette = layer->palette != NULL ? layer->palette : bitmap->palette;
    fix_t fix_x = int2fix(x);
    while (x < tx2) {
        int ypos = layer->vstart + fix2int(nscan * layer->scale.dy);
        if (ypos < 0) {
            ypos = layer->height + ypos;
        } else {
            ypos = ypos % layer->height;
        }

        /* get effective width */
        int width = layer->width - xpos;
        fix_t dx = int2fix(width);
        fix_t fix_tilewidth = width * layer->scale.xfactor;
        fix_x += fix_tilewidth;
        int x1 = fix2int(fix_x);
        int tilescalewidth = x1 - x;
        if (tilescalewidth) {
            dx /= tilescalewidth;
        } else {
            dx = 0;
        }

        /* right clipping */
        if (x1 > tx2) {
            x1 = tx2;
        }
        width = x1 - x;

        /* draw bitmap scanline */
        uint8_t const *srcpixel = (uint8_t *)get_bitmap_ptr(bitmap, xpos, ypos);
        layer->render.blitters[1](srcpixel, palette, dstpixel, width, dx, 0, layer->render.blend);

        /* next */
        dstpixel += width;
        x = x1;
        xpos = 0;
    }
    return false;
}

/* draws regular bitmap scanline for bitmap-based layer with affine transform */
static bool DrawBitmapScanlineAffine(int nlayer, uint32_t *dstpixel, int nscan, int tx1, int tx2) {
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
    const struct Palette *palette = layer->palette != NULL ? layer->palette : bitmap->palette;
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
static bool DrawBitmapScanlinePixelMapping(int nlayer, uint32_t *dstpixel, int nscan, int tx1,
                                           int tx2) {
    const Layer *layer = (const Layer *)&engine->layers[nlayer];
    bool priority = false;

    /* target lines */
    int x = tx1;
    dstpixel += x;

    const int hstart = layer->hstart + layer->width;
    const int vstart = layer->vstart + layer->height;
    const struct Bitmap *bitmap = layer->bitmap;
    const TLN_PixelMap *pixel_map =
        &layer->pixel_map[((ptrdiff_t)nscan * engine->framebuffer.width) + x];
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
static bool DrawObjectScanline(int nlayer, uint32_t *dstpixel, int nscan, int tx1, int tx2) {
    const Layer *layer = (const Layer *)&engine->layers[nlayer];
    struct Object *object = layer->objects->list;
    struct Object tmpobject = {0};

    int x1 = layer->hstart + tx1;
    int x2 = layer->hstart + tx2;
    int y = layer->vstart + nscan;
    uint32_t *dstscan = dstpixel;
    bool priority = false;

    while (object != NULL) {
        /* swap width & height for rotated objects */
        memcpy(&tmpobject, object, sizeof(struct Object));
        if (tmpobject.flags & FLAG_ROTATE) {
            tmpobject.width = object->height;
            tmpobject.height = object->width;
        }

        if ((int)IsObjectInLine(&tmpobject, x1, x2, y) && (int)tmpobject.visible &&
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
            if (dstx2 > tx2) {
                dstx2 = tx2;
            }
            int w = dstx2 - dstx1;

            TLN_Bitmap bitmap = tmpobject.bitmap;
            scan.width = bitmap->width;
            scan.height = bitmap->height;
            scan.stride = bitmap->pitch;

            /* process rotate & flip flags */
            scan.dx = 1;
            if ((tmpobject.flags & (FLAG_FLIPX + FLAG_FLIPY + FLAG_ROTATE)) != 0) {
                process_flip_rotation(tmpobject.flags, &scan);
            }

            /* paint tile scanline */
            uint8_t const *srcpixel = get_bitmap_ptr(bitmap, scan.srcx, scan.srcy);
            uint32_t *target = dstscan;
            if (tmpobject.flags & FLAG_PRIORITY) {
                target = engine->priority;
                priority = true;
            }
            layer->render.blitters[1](srcpixel, bitmap->palette, target + dstx1, w, scan.dx, 0,
                                      layer->render.blend);
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
    if (layer->tilemap != NULL) {
        return draw_delegates[DRAW_TILED_LAYER][layer->render.mode];
    }
    if (layer->bitmap != NULL)
        return draw_delegates[DRAW_BITMAP_LAYER][layer->render.mode];
    else if (layer->objects != NULL)
        return draw_delegates[DRAW_OBJECT_LAYER][layer->render.mode];
    else
        return NULL;
}

/* returns suitable draw procedure based on sprite configuration */
ScanDrawPtr GetSpriteDraw(draw_t mode) { return draw_delegates[DRAW_SPRITE][mode]; }
