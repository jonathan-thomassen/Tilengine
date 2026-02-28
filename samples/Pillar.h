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

#endif
