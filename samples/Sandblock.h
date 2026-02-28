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

/**
 * Checks whether Simon's feet have landed on any active sandblock and
 * resolves the collision identically to a tilemap floor hit.
 *
 * \param sprite_x   Simon's screen x position
 * \param world_x    Horizontal world scroll offset
 * \param inout_y    Candidate new y; snapped to block top on hit
 * \param inout_vy   Vertical velocity; zeroed on landing
 * \return           true if a sandblock floor was hit
 */
bool SandblockCheckFloor(int sprite_x, int world_x, int *inout_y,
                         int *inout_vy);

#endif
