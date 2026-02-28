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
