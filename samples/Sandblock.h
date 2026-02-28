#ifndef _SANDBLOCK_H
#define _SANDBLOCK_H

#include "Tilengine.h"

/** Maximum number of sandblocks that can exist simultaneously. */
#define MAX_SANDBLOCKS 4

/**
 * Loads the sandblock spriteset and clears all slots.
 * Must be called once before SandblockSpawn().
 */
void SandblockInit(void);

/** Frees all sandblock resources. */
void SandblockDeinit(void);

/**
 * Activates a sandblock at the given world coordinates.
 *
 * \param world_x  World x position (pixels from map origin)
 * \param world_y  World y position (top of block, pixels from map origin)
 * \return         Slot index (0..MAX_SANDBLOCKS-1) on success, -1 if full
 */
int SandblockSpawn(int world_x, int world_y);

/**
 * Updates screen positions of all active sandblocks.
 * Call once per frame before TLN_DrawFrame().
 *
 * \param xworld  Current horizontal world scroll offset
 */
void SandblockTasks(int xworld);

#endif
