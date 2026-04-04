#ifndef SIMON_COLLISION_H
#define SIMON_COLLISION_H

#define TILE_SIZE 16
#define SIMON_COL_WIDTH 16
#define SIMON_COL_HEIGHT 48
#define SIMON_COL_X_OFFSET 8

/* Load the col_definition lookup table from disk.  Must be called once
 * before any call to resolve_collision(). */
void load_col_definition(void);

/* Resolves a candidate displacement (*dx, *dy) against tile collision.
 * Selects the appropriate probe set for the movement direction and clamps
 * each axis to the nearest tile boundary.  Both dx and dy may be modified.
 *
 * \param world_x   Horizontal scroll offset (camera position)
 * \param sprite_x  Sprite screen-x position
 * \param sprite_y  Sprite screen-y position (top of bounding box)
 * \param dx        In/out: candidate horizontal displacement
 * \param dy        In/out: candidate vertical displacement
 */
void resolve_collision(int world_x, int sprite_x, int sprite_y, int *dx, int *dy);

#endif
