#include "Sandblock.h"
#include "Tilengine.h"
#include <stdbool.h>
#include <stddef.h>

/* Tilengine sprite slots reserved for sandblocks start right after Simon (0) */
#define SPRITE_BASE 1

/* Pixel dimensions of one sandblock sprite */
#define SANDBLOCK_W 16
#define SANDBLOCK_H 16

#define MAX_PICTURE 3 /* pictures 0-3 = states 1-4 */

/* Cumulative stood_frames thresholds at which each transition fires.
 * 0→1 after 30 frames, 1→2 after 15 more (45 total), 2→3 after 15 more (60). */
static const int STATE_THRESHOLDS[MAX_PICTURE] = {30, 45, 60};

/* After this many cumulative stood_frames the block begins to fall. */
#define FALL_THRESHOLD 75

/* Matches TERM_VELOCITY / falling pixel-conversion in Simon.c */
#define BLOCK_TERM_VEL 10

static int picture_for_frames(int frames) {
  int p = 0;
  while (p < MAX_PICTURE && frames >= STATE_THRESHOLDS[p])
    p++;
  return p;
}

typedef struct {
  bool active;
  bool stood_this_frame; /* set by SandblockCheckFloor, cleared by Tasks */
  bool falling;          /* true once the block has been triggered to fall */
  int world_x;
  int world_y;
  int stood_frames; /* cumulative frames Simon has stood on this block */
  int vy;           /* vertical velocity when falling (always positive)  */
} Sandblock;

static TLN_Spriteset spriteset;
static Sandblock blocks[MAX_SANDBLOCKS];

void SandblockInit(void) {
  spriteset = TLN_LoadSpriteset("sandblock");
  for (int i = 0; i < MAX_SANDBLOCKS; i++) {
    blocks[i].active = false;
    TLN_DisableSprite(SPRITE_BASE + i);
  }
}

void SandblockDeinit(void) {
  for (int i = 0; i < MAX_SANDBLOCKS; i++)
    TLN_DisableSprite(SPRITE_BASE + i);
  if (spriteset != NULL)
    TLN_DeleteSpriteset(spriteset);
  spriteset = NULL;
}

int SandblockSpawn(int world_x, int world_y) {
  for (int i = 0; i < MAX_SANDBLOCKS; i++) {
    if (blocks[i].active)
      continue;
    blocks[i].active = true;
    blocks[i].stood_this_frame = false;
    blocks[i].falling = false;
    blocks[i].stood_frames = 0;
    blocks[i].vy = 0;
    blocks[i].world_x = world_x;
    blocks[i].world_y = world_y;
    TLN_SetSpriteSet(SPRITE_BASE + i, spriteset);
    TLN_SetSpritePicture(SPRITE_BASE + i, 0);
    return i;
  }
  return -1; /* no free slot */
}

void SandblockTasks(int xworld) {
  for (int i = 0; i < MAX_SANDBLOCKS; i++) {
    if (!blocks[i].active)
      continue;

    if (blocks[i].falling) {
      /* Apply same falling gravity as Simon (sy > 0 branch). */
      if (blocks[i].vy < BLOCK_TERM_VEL)
        blocks[i].vy += 2;
      blocks[i].world_y += blocks[i].vy / 3;
      /* Deactivate once off the bottom of the screen. */
      if (blocks[i].world_y > TLN_GetHeight() + SANDBLOCK_H) {
        blocks[i].active = false;
        TLN_DisableSprite(SPRITE_BASE + i);
        continue;
      }
    }

    if (blocks[i].stood_this_frame) {
      int old_picture = picture_for_frames(blocks[i].stood_frames);
      blocks[i].stood_frames++;
      int new_picture = picture_for_frames(blocks[i].stood_frames);

      if (new_picture != old_picture)
        TLN_SetSpritePicture(SPRITE_BASE + i, new_picture);

      /* Trigger fall once Simon has stood on the crumbled block long enough. */
      if (!blocks[i].falling && blocks[i].stood_frames >= FALL_THRESHOLD) {
        blocks[i].falling = true;
        blocks[i].vy = 1;
      }

      blocks[i].stood_this_frame = false;
    }

    TLN_SetSpritePosition(SPRITE_BASE + i, blocks[i].world_x - xworld,
                          blocks[i].world_y);
  }
}

bool SandblockCheckFloor(int sprite_x, int world_x, int *inout_y,
                         int *inout_vy) {
  int foot_y = *inout_y + 46;
  for (int i = 0; i < MAX_SANDBLOCKS; i++) {
    if (!blocks[i].active || blocks[i].falling)
      continue;
    for (int c = 8; c < 24; c += 8) {
      int foot_x = sprite_x + c + world_x;
      if (foot_x >= blocks[i].world_x &&
          foot_x < blocks[i].world_x + SANDBLOCK_W &&
          foot_y >= blocks[i].world_y &&
          foot_y < blocks[i].world_y + SANDBLOCK_H) {
        *inout_vy = 0;
        *inout_y = blocks[i].world_y - 46;
        blocks[i].stood_this_frame = true;
        return true;
      }
    }
  }
  return false;
}
