#include "Simon.h"
#include "Tilengine.h"

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
int xworld;
SimonState state;
Direction direction;

void SimonInit() {
  simon = TLN_LoadSpriteset("simon_walk");
  sp = TLN_LoadSequencePack("simon_walk.sqx");
  walk = TLN_FindSequence(sp, "walk");

  TLN_SetSpriteSet(0, simon);
  TLN_SetSpritePosition(0, x, y);

  SimonSetState(SIMON_IDLE);
  direction = DIR_RIGHT;
  x = 64;
  y = -48;
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
 * Returns true if any tile exists along a vertical strip at the given world
 * x position, sampled at three heights spanning the sprite body.
 *
 * \param world_x_pos  World x coordinate of the edge to test
 * \param sprite_y     Current y position of the sprite
 */
static bool has_wall_tile(int world_x_pos, int sprite_y) {
  for (int c = 8; c < 48; c += 16) {
    TLN_TileInfo ti;
    TLN_GetLayerTile(0, world_x_pos, sprite_y + c, &ti);
    if (ti.index)
      return true;
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

void SimonTasks(void) {
  int y2;
  int s0;
  Direction input = 0;
  bool jump = false;

  /* input */
  if (TLN_GetInput(INPUT_LEFT))
    input = DIR_LEFT;
  else if (TLN_GetInput(INPUT_RIGHT))
    input = DIR_RIGHT;
  if (TLN_GetInput(INPUT_A))
    jump = true;

  /* direction flags */
  if (input == DIR_RIGHT && direction == DIR_LEFT) {
    direction = input;
    TLN_EnableSpriteFlag(0, FLAG_FLIPX, false);
  }
  if (input == DIR_LEFT && direction == DIR_RIGHT) {
    direction = input;
    TLN_EnableSpriteFlag(0, FLAG_FLIPX, true);
  }

  int width = TLN_GetWidth();
  switch (state) {
  case SIMON_IDLE:
    if (input)
      SimonSetState(SIMON_WALKING);
    break;
  case SIMON_WALKING:
  case SIMON_JUMPING:
    if (input == DIR_RIGHT) {
      if (x < 112)
        x++;
      else {
        if (xworld < TLN_GetLayerWidth(1) - width)
          xworld++;
        else if (x < width - 16)
          x++;
      }
    } else if (input == DIR_LEFT) {
      if (x > 128)
        x--;
      else {
        if (xworld > 0)
          xworld--;
        else if (x > -4)
          x--;
      }
    }

    if (state == SIMON_WALKING && !input)
      SimonSetState(SIMON_IDLE);
    break;
  }

  if (jump && state != SIMON_JUMPING)
    SimonSetState(SIMON_JUMPING);

  /* check wall collisions */
  /* if (input == DIR_RIGHT && has_wall_tile(x + 24 + xworld, y)) {
    if (x > 0)
      x--;
    else
      xworld--;
  } else if (input == DIR_LEFT && has_wall_tile(x + xworld - 1, y)) {
    xworld++;
  } */

  /* gravity */
  s0 = sy;
  if (sy < 10)
    sy++;
  y2 = y + (sy >> 2);

  /* check tiles below */
  check_floor(x, xworld, &y2, &sy);

  if (s0 > 0 && sy == 0)
    SimonSetState(SIMON_IDLE);
  y = y2;

  /* reset if fallen below the viewport */
  if (y > TLN_GetHeight()) {
    y = 0;
    sy = 0;
    SimonSetState(SIMON_IDLE);
  }

  TLN_SetSpritePosition(0, x, y);
}

int SimonGetPosition(void) { return xworld; }
