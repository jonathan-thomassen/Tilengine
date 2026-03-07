#ifndef DRAWBRIDGE_H
#define DRAWBRIDGE_H

#include <stdbool.h>

void DrawbridgeInit(int layer, int hinge_x, int hinge_y);
void DrawbridgeSetProgress(float progress);
float DrawbridgeGetProgress(void);
void DrawbridgeSetHinge(int hinge_x, int hinge_y);
void DrawbridgeTasks(void);

/**
 * Advance the drawbridge tick counter.
 * Returns true on the first call and then once every 9 calls, providing
 * a 9-frame rate divider for the animation progress.
 * Must be called exactly once per game frame while the animation is active.
 */
bool DrawbridgeTick(void);

/**
 * Returns the screen y-coordinate of the bridge surface at the given screen x.
 * Uses the hinge position and current progress to compute the rotated height.
 * Returns hinge_y when progress is 0 (bridge flat).
 */
float DrawbridgeSurfaceY(int screen_x);

/** Returns the screen x of the bridge hinge (the fixed right-side anchor). */
int DrawbridgeHingeX(void);

/**
 * Returns the minimum screen x at which a sprite's feet (at feet_y) no longer
 * overlap the rising bridge surface.  Returns 0 when progress is 0 or the
 * bridge angle is negligible.
 */
int DrawbridgeMinX(int feet_y);

#endif
