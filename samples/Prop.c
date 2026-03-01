#include "Prop.h"
#include "Sandblock.h" /* for MAX_SANDBLOCKS — defines where our slots begin */
#include "Tilengine.h"
#include <stdbool.h>
#include <string.h>

/* Prop sprite slots follow Simon (0) and the sandblocks (1..MAX_SANDBLOCKS). */
#define SPRITE_BASE (1 + MAX_SANDBLOCKS)

/* A loaded spriteset shared by all props of the same name. */
typedef struct {
  char name[32];
  TLN_Spriteset ss;
} PropType;

typedef struct {
  bool active;
  bool fixed;   /* screen-fixed background prop (FLAG_BACKGROUND, no scroll) */
  int type_idx; /* index into types[] */
  int world_x;
  int world_y;
} Prop;

static PropType types[MAX_PROP_TYPES];
static int num_types;
static Prop props[MAX_PROPS];

/* Returns the type index for name, loading the spriteset if not seen before.
 * Returns -1 if the spriteset could not be loaded or the type table is full. */
static int find_or_load_type(const char *name) {
  for (int i = 0; i < num_types; i++) {
    if (!strcasecmp(types[i].name, name))
      return i;
  }
  if (num_types >= MAX_PROP_TYPES)
    return -1;
  TLN_Spriteset ss = TLN_LoadSpriteset(name);
  if (ss == NULL)
    return -1;
  types[num_types].ss = ss;
  strncpy(types[num_types].name, name, sizeof(types[num_types].name) - 1);
  types[num_types].name[sizeof(types[num_types].name) - 1] = '\0';
  return num_types++;
}

void PropInit(void) {
  num_types = 0;
  for (int i = 0; i < MAX_PROPS; i++) {
    props[i].active = false;
    props[i].fixed = false;
    TLN_DisableSprite(SPRITE_BASE + i);
  }
}

void PropDeinit(void) {
  for (int i = 0; i < MAX_PROPS; i++) {
    props[i].active = false;
    TLN_DisableSprite(SPRITE_BASE + i);
  }
  for (int i = 0; i < num_types; i++) {
    if (types[i].ss != NULL)
      TLN_DeleteSpriteset(types[i].ss);
    types[i].ss = NULL;
  }
  num_types = 0;
}

int PropSpawn(const char *name, int world_x, int world_y) {
  int type_idx = find_or_load_type(name);
  if (type_idx < 0)
    return -1;

  for (int i = 0; i < MAX_PROPS; i++) {
    if (props[i].active)
      continue;
    props[i].active = true;
    props[i].fixed = false;
    props[i].type_idx = type_idx;
    props[i].world_x = world_x;
    props[i].world_y = world_y;
    TLN_SetSpriteSet(SPRITE_BASE + i, types[type_idx].ss);
    TLN_SetSpritePicture(SPRITE_BASE + i, 0);
    return i;
  }
  return -1; /* no free slot */
}

int PropSpawnBackground(const char *name, int screen_x, int screen_y) {
  int type_idx = find_or_load_type(name);
  if (type_idx < 0)
    return -1;

  for (int i = 0; i < MAX_PROPS; i++) {
    if (props[i].active)
      continue;
    props[i].active = true;
    props[i].fixed = true;
    props[i].type_idx = type_idx;
    props[i].world_x = screen_x;
    props[i].world_y = screen_y;
    int slot = SPRITE_BASE + i;
    TLN_SetSpriteSet(slot, types[type_idx].ss);
    TLN_SetSpritePicture(slot, 0);
    /* render behind all tilemap layers */
    TLN_EnableSpriteFlag(slot, FLAG_BACKGROUND, true);
    /* position once — stays fixed on screen */
    TLN_SetSpritePosition(slot, screen_x, screen_y);
    return i;
  }
  return -1; /* no free slot */
}

void PropTasks(int xworld) {
  for (int i = 0; i < MAX_PROPS; i++) {
    if (!props[i].active)
      continue;
    if (props[i].fixed)
      continue; /* screen-fixed: position never changes after spawn */
    TLN_SetSpritePosition(SPRITE_BASE + i, props[i].world_x - xworld,
                          props[i].world_y);
  }
}
