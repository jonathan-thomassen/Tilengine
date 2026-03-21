#ifndef WHIP_H
#define WHIP_H

#include <stdbool.h>

#include "Prop.h"      /* MAX_PROPS    */
#include "Sandblock.h" /* MAX_SANDBLOCKS */
#include "Torch.h"     /* MAX_TORCHES  */

/*
 * SIMON_SPRITE occupies slot (1 + MAX_SANDBLOCKS + MAX_TORCHES + MAX_PROPS).
 * The whip lash occupies the next MAX_WHIP_SPRITES slots — one per chain
 * segment that can be on-screen simultaneously.
 */
#define MAX_WHIP_SPRITES 12
#define WHIP_SPRITE_BASE (1 + MAX_SANDBLOCKS + MAX_TORCHES + MAX_PROPS + 1)

/** Total duration of a whip swing, in game frames. */
#define WHIP_DURATION 18

/**
 * Loads the whip0 spriteset and clears the sprite slot.
 * Must be called after SimonInit() so that all earlier sprite slots are
 * already claimed, keeping render order correct.
 */
void WhipInit(void);

/** Frees whip resources and disables the sprite slot. */
void WhipDeinit(void);

/**
 * Returns true while a whip swing is in progress.
 * Simon's movement input should be suppressed whenever this is true.
 */
bool WhipIsActive(void);

/**
 * Polls INPUT_B (keyboard X) to start a swing, then advances the frame
 * counter and repositions the whip sprite relative to Simon each frame.
 * Call once per game frame, after SimonTasks().
 */
void WhipTasks(void);

#endif /* WHIP_H */
