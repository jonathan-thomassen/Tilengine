#include "Torch.h"

#include <stdbool.h>
#include <stddef.h>

#include "Sandblock.h" /* for MAX_SANDBLOCKS */
#include "Tilengine.h"

/* Torch slots follow sandblocks: 1..MAX_SANDBLOCKS are sandblocks,
 * 1+MAX_SANDBLOCKS..1+MAX_SANDBLOCKS+MAX_TORCHES-1 are torches. */
#define SPRITE_BASE (1 + MAX_SANDBLOCKS)

#define MAX_PICTURE 3 /* pictures 0-3 = states 1-4 */

typedef struct {
  bool active;
  int world_x;
  int world_y;
} Torch;

static TLN_Spriteset spriteset;
static Torch torches[MAX_TORCHES];

void TorchInit(void) {
  spriteset = TLN_LoadSpriteset("torch");
  for (int i = 0; i < MAX_TORCHES; i++) {
    torches[i].active = false;
    TLN_DisableSprite(SPRITE_BASE + i);
  }
}

void TorchDeinit(void) {
  for (int i = 0; i < MAX_TORCHES; i++) TLN_DisableSprite(SPRITE_BASE + i);
  if (spriteset != NULL)
    TLN_DeleteSpriteset(spriteset);
  spriteset = NULL;
}

int TorchSpawn(int world_x, int world_y) {
  for (int i = 0; i < MAX_TORCHES; i++) {
    if (torches[i].active)
      continue;
    torches[i].active = true;
    torches[i].world_x = world_x;
    torches[i].world_y = world_y;
    TLN_SetSpriteSet(SPRITE_BASE + i, spriteset);
    TLN_SetSpritePicture(SPRITE_BASE + i, 0);
    return i;
  }
  return -1; /* no free slot */
}

void TorchTasks(int xworld) {
  for (int i = 0; i < MAX_TORCHES; i++) {
    if (!torches[i].active)
      continue;
    TLN_SetSpritePosition(SPRITE_BASE + i, torches[i].world_x - xworld,
                          torches[i].world_y);
  }
}

bool TorchGet(int index) {
  if (index < 0 || index >= MAX_TORCHES || !torches[index].active)
    return false;

  return true;
}
