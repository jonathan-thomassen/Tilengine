#ifndef _PILLAR_H
#define _PILLAR_H

#include "Tilengine.h"

/** Maximum number of pillars that can exist simultaneously. */
#define MAX_PILLARS 1

/**
 * Loads the pillar spriteset and clears all slots.
 * Must be called once before PillarSpawn().
 */
void PillarInit(void);

/** Frees all pillar resources. */
void PillarDeinit(void);

/**
 * Activates a pillar at the given world coordinates.
 *
 * \param world_x  World x position (pixels from map origin, left edge)
 * \param world_y  World y position (pixels from map origin, top edge)
 * \return         Slot index (0..MAX_PILLARS-1) on success, -1 if full
 */
int PillarSpawn(int world_x, int world_y);

/**
 * Updates screen positions of all active pillars.
 * Call once per frame before TLN_DrawFrame().
 *
 * \param xworld  Current horizontal world scroll offset
 */
void PillarTasks(int xworld);

/**
 * Checks whether Simon's feet have landed on top of any active pillar.
 * Resolves collision identically to a tilemap floor hit.
 *
 * \param sprite_x   Simon's screen x position
 * \param world_x    Horizontal world scroll offset
 * \param inout_y    Candidate new y; snapped to pillar top on hit
 * \param inout_vy   Vertical velocity; zeroed on landing
 * \return           true if a pillar top was hit
 */
bool PillarCheckFloor(int sprite_x, int world_x, int *inout_y, int *inout_vy);

/**
 * Returns true if Simon's right edge overlaps a pillar body.
 * Uses the same sampling pattern as Simon's tilemap wall check.
 *
 * \param sprite_x  Simon's screen x position
 * \param world_x   Horizontal world scroll offset
 * \param sprite_y  Simon's screen y position
 */
bool PillarCheckWallRight(int sprite_x, int world_x, int sprite_y);

/**
 * Returns true if Simon's left edge overlaps a pillar body.
 *
 * \param sprite_x  Simon's screen x position
 * \param world_x   Horizontal world scroll offset
 * \param sprite_y  Simon's screen y position
 */
bool PillarCheckWallLeft(int sprite_x, int world_x, int sprite_y);

#endif
