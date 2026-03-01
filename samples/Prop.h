#ifndef _PROP_H
#define _PROP_H

#include "Tilengine.h"

/** Maximum number of prop instances that can exist simultaneously. */
#define MAX_PROPS 16

/**
 * Maximum number of distinct spritesets that props can share.
 * Multiple props with the same name load the spriteset only once.
 */
#define MAX_PROP_TYPES 8

/** Initialises the prop system. Call once before PropSpawn(). */
void PropInit(void);

/** Frees all prop spritesets and disables all prop sprites. */
void PropDeinit(void);

/**
 * Spawns a static prop at the given world position.
 *
 * The spriteset is looked up by \p name (case-insensitive). If the same name
 * has been used before its spriteset is reused without reloading.
 *
 * \param name      Spriteset filename without extension (e.g. "pillar")
 * \param world_x   World x position (pixels from map origin, left edge)
 * \param world_y   World y position (pixels from map origin, top edge)
 * \return          Slot index on success, -1 if no free slot or load failed
 */
int PropSpawn(const char *name, int world_x, int world_y);

/**
 * Spawns a screen-fixed background prop rendered behind all tilemap layers.
 *
 * The prop is positioned at \p screen_x / \p screen_y in screen space and
 * never re-positioned during \c PropTasks(), so it does not scroll.
 * It is drawn before the first tilemap layer, appearing behind everything.
 *
 * \param name      Spriteset filename without extension (e.g. "moon")
 * \param screen_x  Fixed screen x position (pixels from left edge)
 * \param screen_y  Fixed screen y position (pixels from top edge)
 * \return          Slot index on success, -1 if no free slot or load failed
 */
int PropSpawnBackground(const char *name, int screen_x, int screen_y);

/**
 * Repositions all active prop sprites to match the current scroll offset.
 * Call once per frame before TLN_DrawFrame().
 *
 * \param xworld  Current horizontal world scroll offset
 */
void PropTasks(int xworld);

#endif
