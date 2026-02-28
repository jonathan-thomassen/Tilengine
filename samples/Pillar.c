#include "Pillar.h"
#include "Sandblock.h" /* for MAX_SANDBLOCKS — defines where our slots start */
#include "Tilengine.h"
#include <stdbool.h>
#include <stddef.h>

/* Pillar sprite slots follow Simon (0) and the sandblocks (1..MAX_SANDBLOCKS)
 */
#define SPRITE_BASE (1 + MAX_SANDBLOCKS)

#define PILLAR_W 48
#define PILLAR_H 192

typedef struct {
  bool active;
  int world_x;
  int world_y;
} Pillar;

static TLN_Spriteset spriteset;
static Pillar pillars[MAX_PILLARS];

void PillarInit(void) {
  spriteset = TLN_LoadSpriteset("pillar");
  for (int i = 0; i < MAX_PILLARS; i++) {
    pillars[i].active = false;
    TLN_DisableSprite(SPRITE_BASE + i);
  }
}

void PillarDeinit(void) {
  for (int i = 0; i < MAX_PILLARS; i++)
    TLN_DisableSprite(SPRITE_BASE + i);
  if (spriteset != NULL)
    TLN_DeleteSpriteset(spriteset);
  spriteset = NULL;
}

int PillarSpawn(int world_x, int world_y) {
  for (int i = 0; i < MAX_PILLARS; i++) {
    if (pillars[i].active)
      continue;
    pillars[i].active = true;
    pillars[i].world_x = world_x;
    pillars[i].world_y = world_y;
    TLN_SetSpriteSet(SPRITE_BASE + i, spriteset);
    TLN_SetSpritePicture(SPRITE_BASE + i, 0);
    return i;
  }
  return -1; /* no free slot */
}

void PillarTasks(int xworld) {
  for (int i = 0; i < MAX_PILLARS; i++) {
    if (!pillars[i].active)
      continue;
    TLN_SetSpritePosition(SPRITE_BASE + i, pillars[i].world_x - xworld,
                          pillars[i].world_y);
  }
}

bool PillarCheckFloor(int sprite_x, int world_x, int *inout_y, int *inout_vy) {
  int foot_y = *inout_y + 46;
  for (int i = 0; i < MAX_PILLARS; i++) {
    if (!pillars[i].active)
      continue;
    for (int c = 8; c < 24; c += 8) {
      int foot_x = sprite_x + c + world_x;
      if (foot_x >= pillars[i].world_x &&
          foot_x < pillars[i].world_x + PILLAR_W &&
          foot_y >= pillars[i].world_y &&
          foot_y < pillars[i].world_y + PILLAR_H) {
        *inout_vy = 0;
        *inout_y = pillars[i].world_y - 46;
        return true;
      }
    }
  }
  return false;
}

bool PillarCheckWallRight(int sprite_x, int world_x, int sprite_y) {
  /* Mirror of Simon's check_wall_right: right edge = sprite_x + 24 in world
   * coords */
  int right_x = sprite_x + 24 + world_x;
  for (int c = 4; c < 44; c += 16) {
    int check_y = sprite_y + c;
    for (int i = 0; i < MAX_PILLARS; i++) {
      if (!pillars[i].active)
        continue;
      if (right_x >= pillars[i].world_x &&
          right_x < pillars[i].world_x + PILLAR_W &&
          check_y >= pillars[i].world_y &&
          check_y < pillars[i].world_y + PILLAR_H)
        return true;
    }
  }
  return false;
}

bool PillarCheckWallLeft(int sprite_x, int world_x, int sprite_y) {
  /* Mirror of Simon's check_wall_left: left edge = sprite_x in world coords */
  int left_x = sprite_x + world_x;
  for (int c = 4; c < 44; c += 16) {
    int check_y = sprite_y + c;
    for (int i = 0; i < MAX_PILLARS; i++) {
      if (!pillars[i].active)
        continue;
      if (left_x >= pillars[i].world_x &&
          left_x < pillars[i].world_x + PILLAR_W &&
          check_y >= pillars[i].world_y &&
          check_y < pillars[i].world_y + PILLAR_H)
        return true;
    }
  }
  return false;
}
