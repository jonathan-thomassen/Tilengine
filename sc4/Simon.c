#include "Simon.h"

#include "Sandblock.h"
#include "Tilengine.h"

#define HANGTIME 8
#define TERM_VELOCITY 10
#define AIR_TURN_DELAY 6
#define SIMON_HEIGHT 48

typedef enum { SIMON_IDLE, SIMON_WALKING, SIMON_JUMPING } SimonState;

typedef enum {
  DIR_NONE,
  DIR_LEFT,
  DIR_RIGHT,
} Direction;

static TLN_Spriteset simon;
static TLN_SequencePack sp;
static TLN_Sequence walk;

static int x;
static int y;
static int sy = 0;
static int apex_hang = 0;
static int xworld;
static SimonState state;
static Direction direction;

static bool camera_frozen = false;

static int
    layer_width; /* cached TLN_GetLayerWidth(1) — set once in SimonInit */

/* Per-frame snapshot of active, non-falling sandblocks — built once at the
 * top of SimonTasks() and consumed by all three collision functions. */
static SandblockState sb_cache[MAX_SANDBLOCKS];
static int sb_count;

static Direction air_dir = DIR_NONE;
static int dir_change_timer = 0;
static Direction prev_input = DIR_NONE;
static int move_frame = 0;

/* When set, replaces tile-based floor collision for the current frame.
 * 32767 = inactive (no override). Set before calling SimonTasks(). */
static int bridge_floor = 32767;

void SimonSetBridgeFloor(int feet_y) {
  bridge_floor = feet_y;
}
void SimonClearBridgeFloor(void) {
  bridge_floor = 32767;
}

void SimonInit(void) {
  simon = TLN_LoadSpriteset("simon_walk");
  sp = TLN_LoadSequencePack("simon_walk.sqx");
  walk = TLN_FindSequence(sp, "walk");

  TLN_SetSpriteSet(SIMON_SPRITE, simon);
  TLN_SetSpritePosition(SIMON_SPRITE, x, y);

  layer_width = TLN_GetLayerWidth(1);

  SimonSetState(SIMON_IDLE);
  direction = DIR_RIGHT;
  x = 33;
  y = 146;
}

void SimonDeinit(void) {
  TLN_DeleteSequencePack(sp);
  TLN_DeleteSpriteset(simon);
}

void SimonBringToFront(void) {
  /* Removing and re-adding the sprite moves it to the tail of the engine's
   * render list, ensuring it is drawn on top of all other sprites. */
  TLN_DisableSprite(SIMON_SPRITE);
  TLN_SetSpriteSet(SIMON_SPRITE, simon);
}

void SimonFreezeCamera(void) {
  camera_frozen = true;
}

void SimonSetState(int s) {
  if ((int)state == s)
    return;

  state = s;
  switch (state) {
    case SIMON_IDLE:
      TLN_DisableSpriteAnimation(SIMON_SPRITE);
      TLN_SetSpritePicture(SIMON_SPRITE, 0);
      break;

    case SIMON_WALKING:
      TLN_SetSpriteAnimation(SIMON_SPRITE, walk, 0);
      break;

    case SIMON_JUMPING:
      TLN_DisableSpriteAnimation(SIMON_SPRITE);
      TLN_SetSpritePicture(SIMON_SPRITE, 7);
      sy = -18;
      break;
  }
}

/**
 * Returns true if a solid tile is present on the right edge of the sprite
 * body, sampled at three heights.
 *
 * \param sprite_x  Screen x position of the sprite
 * \param world_x   Horizontal world scroll offset
 * \param sprite_y  Current y position of the sprite
 */
static bool check_wall_right(int sprite_x, int world_x, int sprite_y) {
  for (int c = 4; c < 44; c += 16) {
    TLN_TileInfo ti;
    TLN_GetLayerTile(COLLISION_LAYER, sprite_x + world_x + 24, sprite_y + c,
                     &ti);
    if (!ti.empty)
      return true;
  }
  int wall_x = sprite_x + world_x + 24;
  for (int i = 0; i < sb_count; i++) {
    const SandblockState *sb = &sb_cache[i];
    if (wall_x < sb->world_x || wall_x >= sb->world_x + SANDBLOCK_WIDTH)
      continue;
    /* cull blocks entirely above or below the sampled y range */
    if (sprite_y + 4 >= sb->world_y + SANDBLOCK_HEIGHT ||
        sprite_y + 36 < sb->world_y)
      continue;
    for (int c = 4; c < 44; c += 16) {
      if (sprite_y + c >= sb->world_y &&
          sprite_y + c < sb->world_y + SANDBLOCK_HEIGHT)
        return true;
    }
  }
  return false;
}

/**
 * Returns true if a solid tile is present on the left edge of the sprite
 * body, sampled at three heights.
 *
 * \param sprite_x  Screen x position of the sprite
 * \param world_x   Horizontal world scroll offset
 * \param sprite_y  Current y position of the sprite
 */
static bool check_wall_left(int sprite_x, int world_x, int sprite_y) {
  for (int c = 4; c < 44; c += 16) {
    TLN_TileInfo ti;
    TLN_GetLayerTile(COLLISION_LAYER, sprite_x + world_x + 8, sprite_y + c,
                     &ti);
    if (!ti.empty)
      return true;
  }
  int wall_x = sprite_x + world_x + 8;
  for (int i = 0; i < sb_count; i++) {
    const SandblockState *sb = &sb_cache[i];
    if (wall_x < sb->world_x || wall_x >= sb->world_x + SANDBLOCK_WIDTH)
      continue;
    /* cull blocks entirely above or below the sampled y range */
    if (sprite_y + 4 >= sb->world_y + SANDBLOCK_HEIGHT ||
        sprite_y + 36 < sb->world_y)
      continue;
    for (int c = 4; c < 44; c += 16) {
      if (sprite_y + c >= sb->world_y &&
          sprite_y + c < sb->world_y + SANDBLOCK_HEIGHT)
        return true;
    }
  }
  return false;
}

static void move_right(int width) {
  int x_pre = x;
  int xw_pre = xworld;
  if (!camera_frozen && xworld < layer_width - width && x >= 112)
    xworld++;
  else if (x < 112 || x < width - 16)
    x++;
  if (check_wall_right(x, xworld, y)) {
    x = x_pre;
    xworld = xw_pre;
  }
}

static void move_left(void) {
  int x_pre = x;
  int xw_pre = xworld;
  if (!camera_frozen && xworld > 0 && x <= 128)
    xworld--;
  else if (x > -4)
    x--;
  if (check_wall_left(x, xworld, y)) {
    x = x_pre;
    xworld = xw_pre;
  }
}

/**
 * Checks for solid tiles directly above the sprite's head and resolves
 * vertical collision. Stops upward velocity and pushes Simon down below
 * the tile.
 *
 * \param sprite_x    World x position of the sprite
 * \param world_x     Horizontal world scroll offset
 * \param inout_y     Pointer to the candidate new y position; adjusted
 *                    downward when a tile is hit
 * \param inout_vy    Pointer to the vertical velocity; zeroed on ceiling hit
 * \param prev_y      The y position from the previous frame, used to snap
 *                    back without overshooting
 * \return            true if a ceiling tile was hit
 */
static bool check_ceiling(int sprite_x, int world_x, int *inout_y,
                          int *inout_vy, int prev_y) {
  for (int c = 8; c < 24; c += 8) {
    TLN_TileInfo ti;
    TLN_GetLayerTile(COLLISION_LAYER, sprite_x + c + world_x, *inout_y, &ti);
    if (!ti.empty) {
      *inout_vy = 0;
      *inout_y = prev_y; /* restore to position before the frame's movement */
      return true;
    }
  }
  return false;
}

/**
 * Checks for solid tiles directly below the sprite's feet and resolves
 * vertical collision. Scans two sample points (x+8, x+16) one tile-height
 * below the sprite.
 *
 * \param sprite_x    World x position of the sprite
 * \param world_x     Horizontal world scroll offset
 * \param inout_y     Pointer to the candidate new y position; adjusted upward
 *                    when a tile is hit
 * \param inout_vy    Pointer to the vertical velocity; zeroed on landing
 */
static void check_floor(int sprite_x, int world_x, int *inout_y,
                        int *inout_vy) {
  for (int c = 8; c < 24; c += 8) {
    TLN_TileInfo ti;
    TLN_GetLayerTile(COLLISION_LAYER, sprite_x + c + world_x, *inout_y + 46,
                     &ti);
    if (!ti.empty) {
      *inout_vy = 0;
      *inout_y -= ti.yoffset;
      return;
    }
  }
  int foot_y = *inout_y + 46;
  for (int i = 0; i < sb_count; i++) {
    const SandblockState *sb = &sb_cache[i];
    /* cull blocks whose x range can't contain either foot sample */
    if (sprite_x + 16 + world_x < sb->world_x ||
        sprite_x + 8 + world_x >= sb->world_x + SANDBLOCK_WIDTH)
      continue;
    for (int c = 8; c < 24; c += 8) {
      int foot_x = sprite_x + c + world_x;
      if (foot_x >= sb->world_x && foot_x < sb->world_x + SANDBLOCK_WIDTH &&
          foot_y >= sb->world_y && foot_y < sb->world_y + SANDBLOCK_HEIGHT) {
        *inout_vy = 0;
        *inout_y = sb->world_y - 46;
        SandblockMarkStood(sb->index);
        return;
      }
    }
  }
}

static void update_facing(Direction input) {
  if (input == DIR_RIGHT && direction == DIR_LEFT) {
    direction = input;
    TLN_EnableSpriteFlag(SIMON_SPRITE, FLAG_FLIPX, false);
  }
  if (input == DIR_LEFT && direction == DIR_RIGHT) {
    direction = input;
    TLN_EnableSpriteFlag(SIMON_SPRITE, FLAG_FLIPX, true);
  }
}

/**
 * Updates air-throttle state and returns whether Simon is changing direction
 * mid-air.
 */
static bool update_air_throttle(Direction input) {
  if (state != SIMON_JUMPING) {
    air_dir = input;
    dir_change_timer = 0;
  } else if (input == DIR_NONE) {
    /* released in the air — treat next press as a new direction change */
    air_dir = DIR_NONE;
  }
  bool changing_dir =
      (state == SIMON_JUMPING && input != DIR_NONE && input != air_dir);
  if (changing_dir)
    dir_change_timer++;
  else
    dir_change_timer = 0;
  return changing_dir;
}

/** Commits the direction change and moves Simon one (or two) pixels. */
static void execute_move(Direction input, int width, bool changing_dir) {
  if (changing_dir)
    air_dir = input;    /* commit new direction after delay */
  update_facing(input); /* flip sprite only when movement commits */
  if (input == DIR_RIGHT) {
    move_right(width);
    if (++move_frame % 4 == 0)
      move_right(width);
  } else if (input == DIR_LEFT) {
    move_left();
    if (++move_frame % 4 == 0)
      move_left();
  }
}

/**
 * Handles air-throttle tracking and drives the movement state machine.
 * Horizontally moving Simon one pixel per call, subject to direction-change
 * delay when airborne.
 */
static void apply_movement(Direction input, int width) {
  bool changing_dir = update_air_throttle(input);

  bool first_frame = (prev_input == DIR_NONE && input != DIR_NONE);
  prev_input = input;

  switch (state) {
    case SIMON_IDLE:
      if (input)
        SimonSetState(SIMON_WALKING);
      break;
    case SIMON_WALKING:
    case SIMON_JUMPING:
      if (!first_frame && (!changing_dir || dir_change_timer > AIR_TURN_DELAY))
        execute_move(input, width, changing_dir);
      else
        move_frame = 0;
      if (state == SIMON_WALKING && !input)
        SimonSetState(SIMON_IDLE);
      break;
  }
}

/** Advances vertical velocity by one step, respecting apex hang. */
static void advance_gravity(void) {
  if (sy >= TERM_VELOCITY)
    return;
  if (sy == 0 && apex_hang < HANGTIME) {
    apex_hang++;
    return;
  }
  if (sy != 0)
    apex_hang = 0;
  /* accelerate twice as fast on the way down for a snappier fall */
  sy += (sy > 0) ? 2 : 1;
}

/**
 * Applies ceiling/floor collision and detects landing.
 * \param s0  Vertical velocity captured before advance_gravity() was called.
 */
static void apply_collisions(int s0) {
  /* rising: gentle arc (>> 2); falling: medium pull (/ 3) */
  int y2 = y + (sy > 0 ? sy / 3 : sy >> 2);
  if (sy < 0 && check_ceiling(x, xworld, &y2, &sy, y))
    apex_hang = 0;
  if (bridge_floor < 32767) {
    /* Bridge surface replaces tile floor check entirely — prevents castle
     * approach tiles from fighting the bridge geometry.  Uses >= so that
     * standing still (feet == floor) also zeroes sy every frame, stopping
     * apex_hang from triggering gravity accumulation between snaps. */
    if (y2 + SIMON_HEIGHT >= bridge_floor) {
      y2 = bridge_floor - SIMON_HEIGHT;
      sy = 0;
      apex_hang = 0;
    }
  } else {
    check_floor(x, xworld, &y2, &sy);
  }
  if (s0 > 0 && sy == 0)
    SimonSetState(SIMON_IDLE);
  y = y2;
  if (y > TLN_GetHeight()) {
    y = 0;
    sy = 0;
    SimonSetState(SIMON_IDLE);
  }
}

void SimonTasks(void) {
  sb_count = SandblockSnapshot(sb_cache);

  Direction input = DIR_NONE;
  bool jump = false;

  if (TLN_GetInput(INPUT_LEFT))
    input = DIR_LEFT;
  else if (TLN_GetInput(INPUT_RIGHT))
    input = DIR_RIGHT;
  if (TLN_GetInput(INPUT_A))
    jump = true;

  apply_movement(input, TLN_GetWidth());

  if (jump && state != SIMON_JUMPING)
    SimonSetState(SIMON_JUMPING);

  int s0 = sy;
  advance_gravity();
  apply_collisions(s0);

  /* If collisions landed Simon into IDLE but a direction is still held,
   * promote immediately to WALKING so the idle sprite never shows for one
   * frame. */
  if (state == SIMON_IDLE && input != DIR_NONE)
    SimonSetState(SIMON_WALKING);

  TLN_SetSpritePosition(SIMON_SPRITE, x, y);
}

int SimonGetPosition(void) {
  return xworld;
}

void SimonSetPosition(int px, int py) {
  x = px;
  y = py;
  xworld = 0;
  TLN_SetSpritePosition(SIMON_SPRITE, x, y);
}

void SimonPushRight(int pixels) {
  x += pixels;
  /* clamp to one sprite-width past the right screen edge */
  if (x > TLN_GetWidth())
    x = TLN_GetWidth();
  TLN_SetSpritePosition(SIMON_SPRITE, x, y);
}

int SimonGetScreenX(void) {
  return x;
}

void SimonSetScreenX(int screen_x) {
  x = screen_x;
  TLN_SetSpritePosition(SIMON_SPRITE, x, y);
}

void SimonSetFeetY(int feet_y) {
  y = feet_y - SIMON_HEIGHT;
  sy = 0; /* suppress gravity so physics doesn't fight the forced position */
  apex_hang =
      0; /* reset hang timer so gravity can't accumulate on no-tile surfaces */
  /* Pinning feet to a surface counts as landing: cancel any in-progress jump
   * so the jump sprite clears and the player can jump again next frame. */
  if (state == SIMON_JUMPING)
    SimonSetState(SIMON_IDLE);
  TLN_SetSpritePosition(SIMON_SPRITE, x, y);
}

void SimonPinFeetY(int feet_y) {
  /* Position-only correction: does not change state.
   * Zeroes sy and resets apex_hang so advance_gravity cannot accumulate
   * velocity on surfaces without collision tiles (e.g. the drawbridge deck). */
  y = feet_y - SIMON_HEIGHT;
  sy = 0;
  apex_hang = 0;
  TLN_SetSpritePosition(SIMON_SPRITE, x, y);
}

int SimonGetFeetY(void) {
  return y + SIMON_HEIGHT;
}

void SimonSetWorldX(int wx) {
  xworld = wx;
}
