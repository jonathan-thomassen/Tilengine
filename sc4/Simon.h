#ifndef SIMON_H
#define SIMON_H

#include "Prop.h"
#include "Sandblock.h"
#include "Torch.h"

#define COLLISION_LAYER 5

/* Simon occupies the last sprite slot so he renders on top of all props. */
#define SIMON_SPRITE (1 + MAX_SANDBLOCKS + MAX_TORCHES + MAX_PROPS)

void SimonInit(void);
void SimonDeinit(void);
void SimonTasks(void);
int SimonGetPosition(void);
void SimonSetState(int s);
void SimonSetPosition(int px, int py);

/* Re-inserts Simon at the end of Tilengine's sprite render list so he
 * always draws on top of torches and props that were spawned after init. */
void SimonBringToFront(void);

/**
 * Freezes the camera: after this call move_left/move_right only update the
 * screen x, never xworld. SimonGetPosition() continues to return the locked
 * scroll value so all layer positions stay fixed automatically.
 */
void SimonFreezeCamera(void);

/**
 * Pushes Simon rightward by \p pixels on screen, clamped to the screen edge.
 * Used by the drawbridge animation to carry Simon off screen as the bridge rises.
 */
void SimonPushRight(int pixels);

/** Returns Simon's current screen x position. */
int SimonGetScreenX(void);

/** Sets Simon's screen x position without affecting the world scroll offset. */
void SimonSetScreenX(int screen_x);

/**
 * Sets Simon's y so that his feet land on the given screen y coordinate.
 * Used during the drawbridge animation to track the rising bridge surface.
 */
void SimonSetFeetY(int feet_y);

/** Returns the screen y coordinate of Simon's feet (bottom of sprite). */
int SimonGetFeetY(void);

#endif