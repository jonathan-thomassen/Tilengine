/*
 * Tilengine - The 2D retro graphics engine with raster effects
 * Copyright (C) 2015-2019 Marc Palacios Domenech <mailto:megamarc@hotmail.com>
 * All rights reserved
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * */

#ifdef _MSC_VER
#define inline __inline
#endif

#include "Animation.h"
#include "Debug.h"
#include "Engine.h"
#include "Palette.h"
#include "Sequence.h"
#include "Tables.h"
#include "Tilemap.h"
#include "Tilengine.h"
#include <string.h>

/* linear interploation */
static int lerp(int x, int x0, int x1, int fx0, int fx1) {
  return fx0 + (fx1 - fx0) * (x - x0) / (x1 - x0);
}

static inline void blendColors(uint8_t const *srcptr0, uint8_t const *srcptr1,
                               uint8_t *dstptr, uint8_t f0, uint8_t f1) {
  dstptr[0] = blendfunc(engine->bg.blend_table, srcptr0[0], f0) +
              blendfunc(engine->bg.blend_table, srcptr1[0], f1);
  dstptr[1] = blendfunc(engine->bg.blend_table, srcptr0[1], f0) +
              blendfunc(engine->bg.blend_table, srcptr1[1], f1);
  dstptr[2] = blendfunc(engine->bg.blend_table, srcptr0[2], f0) +
              blendfunc(engine->bg.blend_table, srcptr1[2], f1);
}

static void SetAnimation(Animation *animation, TLN_Sequence sequence,
                         animation_t type);
static void ColorCycle(struct Palette const *srcpalette, TLN_Palette dstpalette,
                       struct Strip const *strip);
static void ColorCycleBlend(TLN_Palette srcpalette, TLN_Palette dstpalette,
                            struct Strip const *strip, int t);
static void UpdatePaletteStrip(Animation *animation, struct Strip *strip,
                               int time);

/* updates animation state */
void UpdateAnimation(Animation *animation, int time) {
  TLN_Sequence sequence = animation->sequence;
  TLN_SequenceFrame const *frames = NULL;

  if (animation->type == TYPE_PALETTE) {
    struct Strip *strips = (struct Strip *)&sequence->data;
    for (int i = 0; i < sequence->count; i++) {
      UpdatePaletteStrip(animation, &strips[i], time);
    }
    return;
  }

  if (time < animation->timer)
    return;

  frames = (TLN_SequenceFrame *)&sequence->data;
  animation->timer = time + frames[animation->pos].delay;
  switch (animation->type) {
  case TYPE_SPRITE:
    TLN_SetSpritePicture(animation->nsprite, frames[animation->pos].index);
    break;

  case TYPE_TILESET:
    animation->tileset->tiles[sequence->target] =
        (uint16_t)frames[animation->pos].index;
    break;

  /* Fall through */
  /* Stop warning GNU C compiler	*/
  case TYPE_NONE:
  case TYPE_PALETTE:
    break;
  }

  /* next frame */
  animation->pos++;
  if (animation->pos != sequence->count)
    return;

  /* handle loop */
  if (animation->loop > 1) {
    animation->loop--;
    animation->pos = 0;
  } else if (animation->loop == 1) {
    animation->enabled = false;
  } else if (animation->loop == 0) {
    animation->pos = 0;
  }
}

/**
 * \brief
 * Checks the state of the animation for given sprite
 *
 * \param index
 * Id of the sprite to check (0 <= id < num_sprites)
 *
 * \returns
 * true if animation is running, false if it's finished or inactive
 */
bool TLN_GetAnimationState(int index) {
  if (index >= engine->numsprites) {
    TLN_SetLastError(TLN_ERR_IDX_SPRITE);
    return false;
  }

  TLN_SetLastError(TLN_ERR_OK);
  return engine->sprites[index].animation.enabled;
}

/*!
 * \brief
 * Starts a palette animation
 *
 * \param index
 * Id of the animation to set (0 <= id < num_animations)
 *
 * \param palette
 * Reference of the palette to be animated
 *
 * \param sequence
 * Reference of the sequence to assign
 *
 * \param blend
 * true for smooth frame interpolation, false for classic, discrete mode
 */
bool TLN_SetPaletteAnimation(int index, TLN_Palette palette,
                             TLN_Sequence sequence, bool blend) {
  Animation *animation = NULL;
  struct Strip *strips;

  TLN_SetLastError(TLN_ERR_OK);

  if (index >= engine->anim.num) {
    TLN_SetLastError(TLN_ERR_IDX_ANIMATION);
    return false;
  }

  if (engine->anim.items[index].sequence == sequence)
    return true;

  /* validate type */
  if (!CheckBaseObject(palette, OT_PALETTE) ||
      !CheckBaseObject(sequence, OT_SEQUENCE))
    return false;

  animation = &engine->anim.items[index];
  if (!animation->enabled)
    ListAppendNode(&engine->anim.list, index);
  SetAnimation(animation, sequence, TYPE_PALETTE);
  animation->palette = palette;
  animation->blend = blend;

  /* start timers */
  strips = (struct Strip *)&sequence->data;
  for (int c = 0; c < sequence->count; c++) {
    strips[c].timer = 0;
    strips[c].t0 = 0;
  }

  /* create auxiliary palette */
  if (animation->srcpalette == NULL)
    animation->srcpalette = TLN_CreatePalette(256);
  CopyBaseObject(animation->srcpalette, palette);

  return true;
}

/*!
 * \brief
 * Sets the source palette of a color cycle animation
 *
 * \param index
 * Id of the animation to set (0 <= id < num_animations)
 *
 * \param palette
 * Reference of the palette to assign
 *
 * \remarks
 * Use this function to change the palette assigned to a color cycle animation
 * running. This is useful to combine color cycling and palette interpolation at
 * the same time
 */
bool TLN_SetPaletteAnimationSource(int index, TLN_Palette palette) {
  Animation *animation = NULL;

  if (index >= engine->anim.num) {
    TLN_SetLastError(TLN_ERR_IDX_ANIMATION);
    return false;
  }

  if (!CheckBaseObject(palette, OT_PALETTE))
    return false;

  animation = &engine->anim.items[index];
  CopyBaseObject(animation->srcpalette, palette);
  CopyBaseObject(animation->palette, palette);

  TLN_SetLastError(TLN_ERR_OK);
  return true;
}

bool SetTilesetAnimation(TLN_Tileset tileset, int index,
                         TLN_Sequence sequence) {
  Animation *animation = NULL;

  if (index >= tileset->sp->num_sequences) {
    TLN_SetLastError(TLN_ERR_IDX_ANIMATION);
    return false;
  }

  /* validate type */
  if (!CheckBaseObject(sequence, OT_SEQUENCE))
    return false;

  animation = &tileset->animations[index];
  SetAnimation(animation, sequence, TYPE_TILESET);
  animation->tileset = tileset;

  TLN_SetLastError(TLN_ERR_OK);
  return true;
}

/*!
 * \brief
 * Starts a sprite animation
 *
 * \param nsprite
 * Id of the sprite to animate (0 <= id < num_sprites)
 *
 * \param sequence
 * Reference of the sequence to assign
 *
 * \param loop
 * amount of times to loop, 0=infinite
 *
 * \see
 * Animations
 */
bool TLN_SetSpriteAnimation(int nsprite, TLN_Sequence sequence, int loop) {
  Sprite *sprite;
  Animation *animation = NULL;

  if (nsprite >= engine->numsprites) {
    TLN_SetLastError(TLN_ERR_IDX_SPRITE);
    return false;
  }

  /* validate type */
  if (!CheckBaseObject(sequence, OT_SEQUENCE))
    return false;

  sprite = &engine->sprites[nsprite];
  animation = &sprite->animation;
  SetAnimation(animation, sequence, TYPE_SPRITE);
  animation->nsprite = nsprite;
  animation->loop = loop;

  TLN_SetLastError(TLN_ERR_OK);
  return true;
}

/*!
 * \brief
 * Sets animation delay for single frame of given sprite animation
 *
 * \param index
 * Id of the sprite with animation (0 <= id < num_sprites)
 *
 * \param frame
 * Id of animation frame to change delay in (0 <= id < sequence->count)
 *
 * \param delay
 * New animation frame delay to set
 *
 * \see
 * Animations
 *
 */
bool TLN_SetAnimationDelay(int index, int frame, int delay) {
  Animation *animation;
  TLN_SequenceFrame *frames = NULL;

  if (index >= engine->anim.num || index < 0) {
    TLN_SetLastError(TLN_ERR_IDX_SPRITE);
    return false;
  }

  animation = &engine->sprites[index].animation;
  frames = (TLN_SequenceFrame *)animation->sequence->data;

  if (frame >= animation->sequence->count || frame < 0) {
    TLN_SetLastError(TLN_ERR_IDX_ANIMATION);
    return false;
  }

  frames[frame].delay = delay;

  TLN_SetLastError(TLN_ERR_OK);
  return true;
}

/*!
 * \brief
 * Finds an available (unused) animation
 *
 * \returns
 * Index of the first unused animation (starting from 0) or -1 if none found
 */
int TLN_GetAvailableAnimation(void) {

  TLN_SetLastError(TLN_ERR_OK);
  for (int c = 0; c < engine->anim.num; c++) {
    if (!engine->anim.items[c].enabled)
      return c;
  }
  return -1;
}

/*!
 * \brief
 * Disables the color cycle animation so it stops playing
 *
 * \param index
 * Id of the animation to set (0 <= id < num_animations)
 *
 * \see
 * Animations
 */
bool TLN_DisablePaletteAnimation(int index) {
  Animation *animation;

  if (index >= engine->anim.num) {
    TLN_SetLastError(TLN_ERR_IDX_ANIMATION);
    return false;
  }

  animation = &engine->anim.items[index];
  if (animation->enabled)
    ListUnlinkNode(&engine->anim.list, index);

  animation->enabled = false;
  animation->type = TYPE_NONE;
  animation->sequence = NULL;
  ListUnlinkNode(&engine->anim.list, index);
  TLN_SetLastError(TLN_ERR_OK);
  return true;
}

/*!
 * \brief Pauses animation for the given sprite
 *
 * \param index Id of the sprite to pause animation (0 <= id < num_sprites)
 * \see Animations TLN_ResumeSpriteAnimation
 */
bool TLN_PauseSpriteAnimation(int index) {
  Sprite *sprite;
  Animation *animation;

  if (index >= engine->numsprites) {
    TLN_SetLastError(TLN_ERR_IDX_SPRITE);
    return false;
  }

  sprite = &engine->sprites[index];
  animation = &sprite->animation;
  animation->paused = true;
  TLN_SetLastError(TLN_ERR_OK);
  return true;
}

/*!
 * \brief Restores animation for the given sprite
 *
 * \param index Id of the sprite to resume animation (0 <= id < num_sprites)
 * \see Animations TLN_PauseSpriteAnimation
 */
bool TLN_ResumeSpriteAnimation(int index) {
  Sprite *sprite;
  Animation *animation;

  if (index >= engine->numsprites) {
    TLN_SetLastError(TLN_ERR_IDX_SPRITE);
    return false;
  }

  sprite = &engine->sprites[index];
  animation = &sprite->animation;
  animation->paused = false;
  TLN_SetLastError(TLN_ERR_OK);
  return true;
}

/*!
 * \brief
 * Disables animation for the given sprite
 *
 * \param index
 * Id of the spriteto set (0 <= id < num_sprites)
 *
 * \see
 * Animations
 */
bool TLN_DisableSpriteAnimation(int index) {
  Sprite *sprite;
  Animation *animation;

  if (index >= engine->numsprites) {
    TLN_SetLastError(TLN_ERR_IDX_SPRITE);
    return false;
  }

  sprite = &engine->sprites[index];
  animation = &sprite->animation;
  animation->enabled = false;
  animation->type = TYPE_NONE;
  animation->sequence = NULL;
  TLN_SetLastError(TLN_ERR_OK);
  return true;
}

/* animation commons */
static void SetAnimation(Animation *animation, TLN_Sequence sequence,
                         animation_t type) {
  animation->timer = 0;
  animation->enabled = true;
  animation->sequence = sequence;
  animation->type = type;
  animation->loop = 0;
  animation->pos = 0;
}

/* updates single palette strip */
static void UpdatePaletteStrip(Animation *animation, struct Strip *strip,
                               int time) {
  /* next frame */
  if (time >= strip->timer) {
    strip->timer = time + strip->delay;
    strip->pos = (strip->pos + 1) % strip->count;
    strip->t0 = time;
    if (!animation->blend)
      ColorCycle(animation->srcpalette, animation->palette, strip);
  }

  /* interpolate */
  if (animation->blend)
    ColorCycleBlend(animation->srcpalette, animation->palette, strip, time);
}

/* regular color cycle */
static void ColorCycle(struct Palette const *srcpalette, TLN_Palette dstpalette,
                       struct Strip const *strip) {
  int c;
  uint32_t const *srcptr = GetPaletteData(srcpalette, strip->first);
  uint32_t *dstptr = GetPaletteData(dstpalette, strip->first);
  int count = strip->count;
  int steps = strip->pos;

  if (strip->dir) {
    for (c = 0; c < count; c++)
      dstptr[c] = srcptr[(c - steps + count) % count];
  } else {
    for (c = 0; c < count; c++)
      dstptr[c] = srcptr[(c + steps) % count];
  }
}

/* blended color cycle */
static void ColorCycleBlend(TLN_Palette srcpalette, TLN_Palette dstpalette,
                            struct Strip const *strip, int t) {
  int idx0;
  int idx1;
  uint8_t const *srcptr0;
  uint8_t const *srcptr1;
  uint8_t *dstptr;

  int t0 = strip->t0;
  int t1 = strip->timer;
  int count = strip->count;
  int steps = strip->pos;

  /* map [t0 - t1] to [0 - 255] */
  int f1 = lerp(t, t0, t1, 0, 255);
  int f0 = 255 - f1;

  for (int c = 0; c < count; c++) {
    if (strip->dir) {
      idx0 = (c - steps + count) % count;
      idx1 = (c - steps - 1 + count) % count;
    } else {
      idx0 = (c + steps) % count;
      idx1 = (c + steps + 1) % count;
    }

    srcptr0 = (uint8_t *)GetPaletteData(srcpalette, strip->first + idx0);
    srcptr1 = (uint8_t *)GetPaletteData(srcpalette, strip->first + idx1);
    dstptr = (uint8_t *)GetPaletteData(dstpalette, strip->first + c);
    blendColors(srcptr0, srcptr1, dstptr, (uint8_t)f0, (uint8_t)f1);
  }
}
