#include "Sandblock.h"
#include "Tilengine.h"
#include <stdbool.h>
#include <stddef.h>

/* Tilengine sprite slots reserved for sandblocks start right after Simon (0) */
#define SPRITE_BASE 1

/* Pixel dimensions of one sandblock sprite */
#define SANDBLOCK_W 16
#define SANDBLOCK_H 16

typedef struct {
  bool active;
  int world_x;
  int world_y;
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
    int screen_x = blocks[i].world_x - xworld;
    int screen_y = blocks[i].world_y;
    TLN_SetSpritePosition(SPRITE_BASE + i, screen_x, screen_y);
  }
}

bool SandblockCheckFloor(int sprite_x, int world_x, int *inout_y,
                         int *inout_vy) {
  int foot_y = *inout_y + 46;
  for (int i = 0; i < MAX_SANDBLOCKS; i++) {
    if (!blocks[i].active)
      continue;
    for (int c = 8; c < 24; c += 8) {
      int foot_x = sprite_x + c + world_x;
      if (foot_x >= blocks[i].world_x &&
          foot_x < blocks[i].world_x + SANDBLOCK_W &&
          foot_y >= blocks[i].world_y &&
          foot_y < blocks[i].world_y + SANDBLOCK_H) {
        *inout_vy = 0;
        *inout_y = blocks[i].world_y - 46;
        return true;
      }
    }
  }
  return false;
}
