#include "Simon.h"
#include "Sandblock.h"
#include "Tilengine.h"

#define HANGTIME 8
#define TERM_VELOCITY 10
#define AIR_TURN_DELAY 6
#define COLISSION_LAYER 4

typedef enum { SIMON_IDLE, SIMON_WALKING, SIMON_JUMPING } SimonState;

typedef enum {
  DIR_NONE,
  DIR_LEFT,
  DIR_RIGHT,
} Direction;

TLN_Spriteset simon;
TLN_SequencePack sp;
TLN_Sequence walk;

int x;
int y;
int sy = 0;
int apex_hang = 0;
int xworld;
SimonState state;
Direction direction;

static Direction air_dir = DIR_NONE;
static int dir_change_timer = 0;
static Direction prev_input = DIR_NONE;
static int move_frame = 0;

void SimonInit() {
  simon = TLN_LoadSpriteset("simon_walk");
  sp = TLN_LoadSequencePack("simon_walk.sqx");
  walk = TLN_FindSequence(sp, "walk");

  TLN_SetSpriteSet(0, simon);
  TLN_SetSpritePosition(0, x, y);

  SimonSetState(SIMON_IDLE);
  direction = DIR_RIGHT;
  x = 33;
  y = 146;
}

void SimonDeinit(void) {
  TLN_DeleteSequencePack(sp);
  TLN_DeleteSpriteset(simon);
}

void SimonSetState(int s) {
  if (state == s)
    return;

  state = s;
  switch (state) {
  case SIMON_IDLE:
    TLN_DisableSpriteAnimation(0);
    TLN_SetSpritePicture(0, 0);
    break;

  case SIMON_WALKING:
    TLN_SetSpriteAnimation(0, walk, 0);
    break;

  case SIMON_JUMPING:
    TLN_DisableSpriteAnimation(0);
    TLN_SetSpritePicture(0, 7);
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
    TLN_GetLayerTile(4, sprite_x + 24 + world_x, sprite_y + c, &ti);
    if (!ti.empty)
      return true;
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
    TLN_GetLayerTile(COLISSION_LAYER, sprite_x + world_x, sprite_y + c, &ti);
    if (!ti.empty)
      return true;
  }
  return false;
}

static void move_right(int width) {
  int x_pre = x;
  int xw_pre = xworld;
  if (x < 112)
    x++;
  else if (xworld < TLN_GetLayerWidth(1) - width)
    xworld++;
  else if (x < width - 16)
    x++;
  if (check_wall_right(x, xworld, y)) {
    x = x_pre;
    xworld = xw_pre;
  }
}

static void move_left(void) {
  int x_pre = x;
  int xw_pre = xworld;
  if (x > 128)
    x--;
  else if (xworld > 0)
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
    TLN_GetLayerTile(4, sprite_x + c + world_x, *inout_y, &ti);
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
    TLN_GetLayerTile(4, sprite_x + c + world_x, *inout_y + 46, &ti);
    if (!ti.empty) {
      *inout_vy = 0;
      *inout_y -= ti.yoffset;
      break;
    }
  }
}

static void update_facing(Direction input) {
  if (input == DIR_RIGHT && direction == DIR_LEFT) {
    direction = input;
    TLN_EnableSpriteFlag(0, FLAG_FLIPX, false);
  }
  if (input == DIR_LEFT && direction == DIR_RIGHT) {
    direction = input;
    TLN_EnableSpriteFlag(0, FLAG_FLIPX, true);
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
  check_floor(x, xworld, &y2, &sy);
  SandblockCheckFloor(x, xworld, &y2, &sy);
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
  Direction input = DIR_NONE;
  bool jump = false;

  if (TLN_GetInput(INPUT_LEFT))
    input = DIR_LEFT;
  else if (TLN_GetInput(INPUT_RIGHT))
    input = DIR_RIGHT;
  if (TLN_GetInput(INPUT_A))
    jump = true;

  int s0 = sy;
  advance_gravity();
  apply_collisions(s0);

  apply_movement(input, TLN_GetWidth());

  if (jump && state != SIMON_JUMPING)

    TLN_SetSpritePosition(0, x, y);
}

int SimonGetPosition(void) { return xworld; }

void SimonSetPosition(int px, int py) {
  x = px;
  y = py;
  xworld = 0;
  TLN_SetSpritePosition(0, x, y);
}
