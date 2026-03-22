#include "Simon.h"

#include <stdlib.h>

#include "LoadFile.h"
#include "Sandblock.h"
#include "Tilengine.h"
#include "Whip.h"

#define HANGTIME 8
#define TERM_VELOCITY 10
#define AIR_TURN_DELAY 6
#define SIMON_HEIGHT 48
#define SIMON_START_POS ((Coords2d){.x = 33, .y = 146})
#define SIMON_WIDTH 32          /* total sprite width  (2 × 16 px tiles)   */
#define SIMON_SEG_W 16          /* width of one subsprite tile              */
#define SIMON_MAX_STAGES 8      /* max animation stages per section         */
#define SIMON_MAX_SEGS 8        /* max subsprites per stage                 */
#define WALK_FRAMES_PER_STAGE 8 /* game frames each walk frame is shown     */
#define JUMP_ARC_LEN_SHORT 37
#define JUMP_ARC_LEN_TALL 39
#define JUMP_ARC_LEN_HIGHER 41
#define BRIDGE_FLOOR_Y 32767

/* Short arc (tap): 38 key-frames → 37 deltas.  Apex y=111. */
static const int jump_arc_dy_short[JUMP_ARC_LEN_SHORT] = {
    -1, -5, -4, -5, -3, -4, -3, -2, -3, -1, -2, -1, -1, 0, 0, 0, 0, 0, 0,
    0,  0,  0,  0,  0,  0,  1,  1,  2,  1,  3,  2,  3,  4, 3, 5, 4, 6};

/* Tall arc (hold 2 frames): 40 key-frames → 39 deltas.  Apex y=106. */
static const int jump_arc_dy_tall[JUMP_ARC_LEN_TALL] = {
    -1, -5, -4, -5, -4, -4, -3, -3, -3, -2, -2, -1, -2, -1, 0, 0, 0, 0, 0, 0,
    0,  0,  0,  0,  0,  0,  1,  2,  1,  2,  2,  3,  3,  3,  4, 4, 5, 4, 6};

/* Higher arc (hold 3+ frames): 42 key-frames → 41 deltas.  Apex y=102. */
static const int jump_arc_dy_higher[JUMP_ARC_LEN_HIGHER] = {
    -1, -5, -4, -5, -4, -5, -3, -4, -3, -2, -3, -1, -2, -1, -1, 0, 0, 0, 0, 0, 0,
    0,  0,  0,  0,  0,  0,  1,  1,  2,  1,  3,  2,  3,  4,  3,  5, 4, 5, 4, 6};

#define PROBE_X_START 4
#define PROBE_X_STEP 16
#define PROBE_X_LIMIT 44
#define PROBE_X_OFFSET 24

#define PROBE_Y_OFFSET 36

typedef enum {
  DIR_NONE,
  DIR_LEFT,
  DIR_RIGHT,
} Direction;

typedef struct {
  int pic, dx, dy;
} SimonSeg;
typedef struct {
  SimonSeg segs[SIMON_MAX_SEGS];
  int count;
} SimonStage;
typedef struct {
  SimonStage stages[SIMON_MAX_STAGES];
  int num_stages;
} SimonSection;

static TLN_Spriteset simon;
static TLN_Bitmap simon_bmp;
static SimonSection sec_stand, sec_walk, sec_jump, sec_teeter, sec_crouch, sec_crouch_walk,
    sec_whip, sec_whip_jump, sec_crouch_whip, sec_whip_up, sec_whip_jump_up;
static int walk_anim_frame;

static Coords2d position;
static int y_velocity = 0;
static int apex_hang = 0;
static int jump_frame = 0;
static bool jump_arc_tall = false;
static bool jump_arc_higher = false;
static bool jump_arc_committed = false;
static SimonState state;
static Direction direction;

static bool camera_frozen = false;

static int layer_width; /* cached TLN_GetLayerWidth(1) — set once in SimonInit */

/* Per-frame snapshot of active, non-falling sandblocks — built once at the
 * top of SimonTasks() and consumed by all three collision functions. */
static SandblockState sb_cache[MAX_SANDBLOCKS];
static int sb_count;

static Direction air_dir = DIR_NONE;
static int dir_change_timer = 0;
static Direction prev_input = DIR_NONE;
static int move_frame = 0;

/* When set, replaces tile-based floor collision for the current frame.
 * BRIDGE_FLOOR_Y = inactive (no override). Set before calling SimonTasks(). */
static int bridge_floor = BRIDGE_FLOOR_Y;
/* Tolerance window in pixels, scaled 0-8 with bridge progress.
 * At 0 Simon sits exactly on top; at 8 he can stand 8 px into the bridge. */
static int bridge_tolerance = 0;

void SimonSetBridgeFloor(int feet_y) { bridge_floor = feet_y; }
void SimonSetBridgeTolerance(int tol) { bridge_tolerance = tol; }
void SimonClearBridgeFloor(void) {
  bridge_floor = BRIDGE_FLOOR_Y;
  bridge_tolerance = 0;
}

/* ---------------------------------------------------------------------------
 * Internal helpers: spriteset loader, section parser, renderer
 * ------------------------------------------------------------------------- */

static TLN_Spriteset load_grid_spriteset(const char *txt_name, const char *png_name,
                                         TLN_Bitmap *out_bitmap) {
  FILE *f = FileOpen(txt_name);
  if (f == NULL) {
    return NULL;
  }

  int tw = 0;
  int th = 0;
  int cols = 0;

  char line[64];
  while (fgets(line, sizeof(line), f) != NULL) {
    int v;
    if (sscanf(line, " w = %d", &v) == 1) {
      tw = v;
    } else if (sscanf(line, " h = %d", &v) == 1) {
      th = v;
    } else if (sscanf(line, " cols = %d", &v) == 1) {
      cols = v;
    }
  }
  fclose(f);

  if (tw <= 0 || th <= 0 || cols <= 0) {
    return NULL;
  }

  TLN_Bitmap bmp = TLN_LoadBitmap(png_name);
  if (bmp == NULL) {
    return NULL;
  }

  int rows = TLN_GetBitmapHeight(bmp) / th;
  int total = rows * cols;

  TLN_SpriteData *data = (TLN_SpriteData *)malloc((size_t)total * sizeof(TLN_SpriteData));
  if (data == NULL) {
    TLN_DeleteBitmap(bmp);
    return NULL;
  }

  for (int r = 0; r < rows; r++) {
    for (int c = 0; c < cols; c++) {
      TLN_SpriteData *e = &data[(r * cols) + c];
      snprintf(e->name, sizeof(e->name), "s%d", (r * cols) + c);
      e->x = c * tw;
      e->y = r * th;
      e->w = tw;
      e->h = th;
    }
  }

  TLN_Spriteset ss = TLN_CreateSpriteset(bmp, data, total);
  free(data);
  /* Do NOT delete bmp here — the spriteset holds a reference to it.
   * The caller is responsible for deleting it after the spriteset is freed. */
  *out_bitmap = bmp;
  return ss;
}

/*
 * Parses a single named group from a simon_map.txt-style file into *out.
 * Group format mirrors whip0_map0.txt: "# name" header, then "N:" stage
 * headers, then "sP = ( dx, dy)" subsprite lines.  No flip flags are used
 * for Simon — mirroring is applied uniformly when facing left.
 */
static void load_simon_section(const char *filename, const char *section, SimonSection *out) {
  out->num_stages = 0;
  for (int stage = 0; stage < SIMON_MAX_STAGES; stage++) {
    out->stages[stage].count = 0;
  }

  FILE *file = FileOpen(filename);
  if (file == NULL) {
    return;
  }

  bool in_section = false;
  int cur_stage = -1;
  char line[128];
  while (fgets(line, sizeof(line), file) != NULL) {
    if (line[0] == '#') {
      char name[64] = "";
      sscanf(line, "# %63s", name);
      in_section = (strcmp(name, section) == 0);
      cur_stage = -1;
      continue;
    }
    if (!in_section) {
      continue;
    }

    int idx = -1;
    if (sscanf(line, " %d :", &idx) == 1 && idx >= 0 && idx < SIMON_MAX_STAGES) {
      cur_stage = idx;
      if (cur_stage + 1 > out->num_stages) {
        out->num_stages = cur_stage + 1;
      }
      continue;
    }

    if (cur_stage < 0 || out->stages[cur_stage].count >= SIMON_MAX_SEGS) {
      continue;
    }

    int pic = -1;
    int dx = 0;
    int dy = 0;
    if (sscanf(line, " s%d = ( %d , %d )", &pic, &dx, &dy) < 3 || pic < 0) {
      continue;
    }

    SimonSeg *seg = &out->stages[cur_stage].segs[out->stages[cur_stage].count++];
    seg->pic = pic;
    seg->dx = dx;
    seg->dy = dy;
  }
  fclose(file);
}

/* Renders one stage of a section using the current position and direction. */
static void render_section_stage(const SimonSection *sec, int stage_idx) {
  if (sec->num_stages == 0) {
    for (int i = 0; i < MAX_SIMON_SPRITES; i++) {
      TLN_DisableSprite(SIMON_SPRITE_BASE + i);
    }
    return;
  }
  if (stage_idx >= sec->num_stages) {
    stage_idx = sec->num_stages - 1;
  }
  const SimonStage *st = &sec->stages[stage_idx];
  bool facing_right = (direction == DIR_RIGHT);
  for (int i = 0; i < MAX_SIMON_SPRITES; i++) {
    if (i < st->count) {
      const SimonSeg *s = &st->segs[i];
      int wx = position.x + ((int)facing_right ? s->dx : (SIMON_WIDTH - s->dx - SIMON_SEG_W));
      TLN_SetSpriteSet(SIMON_SPRITE_BASE + i, simon);
      TLN_SetSpritePicture(SIMON_SPRITE_BASE + i, s->pic);
      TLN_EnableSpriteFlag(SIMON_SPRITE_BASE + i, FLAG_FLIPX, (bool)!facing_right);
      TLN_EnableSpriteFlag(SIMON_SPRITE_BASE + i, FLAG_FLIPY, false);
      TLN_SetSpritePosition(SIMON_SPRITE_BASE + i, wx, position.y + s->dy);
    } else {
      TLN_DisableSprite(SIMON_SPRITE_BASE + i);
    }
  }
}

/*
 * Selects the correct section and stage for the current animation state and
 * calls render_section_stage().  Called at the end of every SimonTasks()
 * and from any function that moves Simon outside the normal update loop.
 */
static void render_current_state(void) {
  const SimonSection *sec;
  int stage = 0;
  if (WhipIsActive()) {
    if (state == SIMON_JUMPING) {
      sec = (int)WhipIsUp() ? &sec_whip_jump_up : &sec_whip_jump;
    } else if (state == SIMON_CROUCHING || state == SIMON_CROUCH_WALKING ||
               state == SIMON_CROUCH_WHIPPING) {
      sec = &sec_crouch_whip;
    } else if (WhipIsUp()) {
      sec = &sec_whip_up;
    } else {
      sec = &sec_whip;
    }
    stage = WhipGetStage();
    if (stage >= sec->num_stages) {
      stage = sec->num_stages - 1;
    }
  } else {
    switch (state) {
    case SIMON_WALKING:
      sec = &sec_walk;
      if (sec->num_stages > 0) {
        stage = (walk_anim_frame / WALK_FRAMES_PER_STAGE) % sec->num_stages;
      }
      break;
    case SIMON_JUMPING:
      sec = &sec_jump;
      break;
    case SIMON_TEETER:
      sec = &sec_teeter;
      break;
    case SIMON_CROUCHING:
    case SIMON_CROUCH_WHIPPING:
      sec = &sec_crouch;
      break;
    case SIMON_CROUCH_WALKING:
      sec = &sec_crouch_walk;
      if (sec_crouch_walk.num_stages > 0) {
        stage = (walk_anim_frame / WALK_FRAMES_PER_STAGE) % sec_crouch_walk.num_stages;
      }
      break;
    default:
      sec = &sec_stand;
      break;
    }
  }
  render_section_stage(sec, stage);
}

/* ---------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void SimonInit(void) {
  simon = load_grid_spriteset("simon.txt", "simon.png", &simon_bmp);
  load_simon_section("simon_map.txt", "stand", &sec_stand);
  load_simon_section("simon_map.txt", "walk", &sec_walk);
  load_simon_section("simon_map.txt", "jump", &sec_jump);
  load_simon_section("simon_map.txt", "teeter", &sec_teeter);
  load_simon_section("simon_map.txt", "crouch", &sec_crouch);
  load_simon_section("simon_map.txt", "crouch-walk", &sec_crouch_walk);
  load_simon_section("simon_map.txt", "whip", &sec_whip);
  load_simon_section("simon_map.txt", "jump-whip", &sec_whip_jump);
  load_simon_section("simon_map.txt", "crouch-whip", &sec_crouch_whip);
  load_simon_section("simon_map.txt", "whip-up", &sec_whip_up);
  load_simon_section("simon_map.txt", "jump-whip-up", &sec_whip_jump_up);

  position = SIMON_START_POS;
  layer_width = TLN_GetLayerWidth(1);
  state = SIMON_IDLE;
  direction = DIR_RIGHT;
  walk_anim_frame = 0;

  render_current_state();
}

void SimonDeinit(void) {
  if (simon != NULL) {
    TLN_DeleteSpriteset(simon);
    simon = NULL;
  }
  if (simon_bmp != NULL) {
    TLN_DeleteBitmap(simon_bmp);
    simon_bmp = NULL;
  }
}

void SimonBringToFront(void) {
  /* Disabling then re-rendering all segments moves them to the tail of the
   * engine's render list, ensuring Simon draws on top of all other sprites. */
  for (int i = 0; i < MAX_SIMON_SPRITES; i++) {
    TLN_DisableSprite(SIMON_SPRITE_BASE + i);
  }
  render_current_state();
}

void SimonFreezeCamera(void) { camera_frozen = true; }

void SimonSetState(SimonState new_state) {
  if (state == new_state) {
    return;
  }
  state = new_state;
  if (state == SIMON_JUMPING) {
    jump_frame = 0;
    jump_arc_tall = false;
    jump_arc_higher = false;
    jump_arc_committed = false;
    y_velocity = 0;
  } else if (state == SIMON_WALKING) {
    walk_anim_frame = 0;
  }
}

/**
 * Returns true if a solid tile is present at the given horizontal edge of the
 * sprite body, sampled at three heights.
 *
 * \param pos       Screen position of the sprite (pos.scroll_x is the world offset)
 * \param x_offset  Horizontal offset from pos.x to the edge being probed
 */
static bool check_wall(Coords2d pos, int x_offset) {
  for (int probe_x = PROBE_X_START; probe_x < PROBE_X_LIMIT; probe_x += PROBE_X_STEP) {
    TLN_TileInfo tile_info;
    TLN_GetLayerTile(COLLISION_LAYER, pos.x + pos.scroll_x + x_offset, pos.y + probe_x, &tile_info);
    if (!tile_info.empty) {
      return true;
    }
  }
  int wall_x = pos.x + pos.scroll_x + x_offset;
  for (int i = 0; i < sb_count; i++) {
    const SandblockState *sandblock_state = &sb_cache[i];
    if (wall_x < sandblock_state->world_x || wall_x >= sandblock_state->world_x + SANDBLOCK_WIDTH) {
      continue;
    }
    /* cull blocks entirely above or below the sampled y range */
    if (pos.y + PROBE_X_START >= sandblock_state->world_y + SANDBLOCK_HEIGHT ||
        pos.y + PROBE_Y_OFFSET < sandblock_state->world_y) {
      continue;
    }
    for (int probe_x = PROBE_X_START; probe_x < PROBE_X_LIMIT; probe_x += PROBE_X_STEP) {
      if (pos.y + probe_x >= sandblock_state->world_y &&
          pos.y + probe_x < sandblock_state->world_y + SANDBLOCK_HEIGHT) {
        return true;
      }
    }
  }
  return false;
}

static void move_right(int width) {
  Coords2d pos_pre = position;
  if (!camera_frozen && position.scroll_x < layer_width - width && position.x >= 112) {
    position.scroll_x++;
  } else if (position.x < 112 || position.x < width - 16) {
    position.x++;
  }
  if (check_wall(position, PROBE_X_OFFSET)) {
    position = pos_pre;
  }
}

static void move_left(void) {
  Coords2d pos_pre = position;
  if (!camera_frozen && position.scroll_x > 0 && position.x <= 128) {
    position.scroll_x--;
  } else if (position.x > -4) {
    position.x--;
  }
  if (check_wall(position, 8)) {
    position = pos_pre;
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
static bool check_ceiling(int sprite_x, int world_x, int *inout_y, int *inout_vy, int prev_y) {
  for (int c = 8; c < 24; c += 8) {
    TLN_TileInfo tile_info;
    TLN_GetLayerTile(COLLISION_LAYER, sprite_x + c + world_x, *inout_y, &tile_info);
    if (!tile_info.empty) {
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
static void check_floor(int sprite_x, int world_x, int *inout_y, int *inout_vy) {
  for (int c = 8; c < 24; c += 8) {
    TLN_TileInfo tile_info;
    TLN_GetLayerTile(COLLISION_LAYER, sprite_x + c + world_x, *inout_y + 46, &tile_info);
    if (!tile_info.empty) {
      *inout_vy = 0;
      *inout_y -= tile_info.yoffset;
      return;
    }
  }
  int foot_y = *inout_y + 46;
  for (int i = 0; i < sb_count; i++) {
    const SandblockState *sandblock_state = &sb_cache[i];
    /* cull blocks whose x range can't contain either foot sample */
    if (sprite_x + 16 + world_x < sandblock_state->world_x ||
        sprite_x + 8 + world_x >= sandblock_state->world_x + SANDBLOCK_WIDTH) {
      continue;
    }
    for (int c = 8; c < 24; c += 8) {
      int foot_x = sprite_x + c + world_x;
      if (foot_x >= sandblock_state->world_x &&
          foot_x < sandblock_state->world_x + SANDBLOCK_WIDTH &&
          foot_y >= sandblock_state->world_y &&
          foot_y < sandblock_state->world_y + SANDBLOCK_HEIGHT) {
        *inout_vy = 0;
        *inout_y = sandblock_state->world_y - 46;
        SandblockMarkStood(sandblock_state->index);
        return;
      }
    }
  }
}

static void update_facing(Direction input) {
  if ((input == DIR_RIGHT && direction == DIR_LEFT) ||
      (input == DIR_LEFT && direction == DIR_RIGHT)) {
    direction = input;
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
  bool changing_dir = (state == SIMON_JUMPING && input != DIR_NONE && input != air_dir) != 0;
  if (changing_dir) {
    dir_change_timer++;
  } else {
    dir_change_timer = 0;
  }
  return changing_dir;
}

/** Commits the direction change and moves Simon one (or two) pixels. */
static void execute_move(Direction input, int width, bool changing_dir) {
  if (changing_dir) {
    air_dir = input; /* commit new direction after delay */
  }
  update_facing(input); /* flip sprite only when movement commits */
  if (input == DIR_RIGHT) {
    move_right(width);
    if (++move_frame % 4 == 0) {
      move_right(width);
    }
  } else if (input == DIR_LEFT) {
    move_left();
    if (++move_frame % 4 == 0) {
      move_left();
    }
  }
}

/**
 * Handles air-throttle tracking and drives the movement state machine.
 * Horizontally moving Simon one pixel per call, subject to direction-change
 * delay when airborne.
 */
static void apply_movement(Direction input, int width) {
  bool changing_dir = update_air_throttle(input);

  bool first_frame = (prev_input == DIR_NONE && input != DIR_NONE) != 0;
  prev_input = input;

  switch (state) {
  case SIMON_TEETER:
  case SIMON_IDLE:
    if (input) {
      SimonSetState(SIMON_WALKING);
    }
    break;
  case SIMON_WALKING:
  case SIMON_JUMPING:
    if (!first_frame && (!changing_dir || dir_change_timer > AIR_TURN_DELAY)) {
      execute_move(input, width, changing_dir);
    } else {
      move_frame = 0;
    }
    if (state == SIMON_WALKING && !input) {
      SimonSetState(SIMON_IDLE);
    }
    break;
  case SIMON_CROUCHING:
    /* Directional input while crouching starts a crouch-walk. */
    if (input) {
      SimonSetState(SIMON_CROUCH_WALKING);
      execute_move(input, width, changing_dir);
    }
    break;
  case SIMON_CROUCH_WALKING:
    if (!first_frame && (!changing_dir || dir_change_timer > AIR_TURN_DELAY)) {
      execute_move(input, width, changing_dir);
    } else {
      move_frame = 0;
    }
    if (!input) {
      SimonSetState(SIMON_CROUCHING);
    }
    break;
  case SIMON_CROUCH_WHIPPING:
    /* No movement during a crouched whip swing. */
    break;
  }
}

/** Advances vertical velocity by one step, respecting apex hang. */
static void advance_gravity(void) {
  if (state == SIMON_JUMPING) {
    return; /* vertical movement is table-driven; handled in apply_collisions */
  }
  if (y_velocity >= TERM_VELOCITY) {
    return;
  }
  if (y_velocity == 0 && apex_hang < HANGTIME) {
    apex_hang++;
    return;
  }
  if (y_velocity != 0) {
    apex_hang = 0;
  }
  /* accelerate twice as fast on the way down for a snappier fall */
  y_velocity += (y_velocity > 0) ? 2 : 1;
}

/**
 * Applies ceiling/floor collision and detects landing.
 * \param start_y_velocity  Vertical velocity captured before advance_gravity() was called.
 */
static void apply_collisions(int start_y_velocity) {
  int arc_len = jump_arc_higher ? JUMP_ARC_LEN_HIGHER
                : jump_arc_tall ? JUMP_ARC_LEN_TALL
                                : JUMP_ARC_LEN_SHORT;
  int dy;
  if (state == SIMON_JUMPING) {
    const int *arc = jump_arc_higher ? jump_arc_dy_higher
                     : jump_arc_tall ? jump_arc_dy_tall
                                     : jump_arc_dy_short;
    if (jump_frame < arc_len) {
      dy = arc[jump_frame++];
    } else {
      dy = TERM_VELOCITY; /* constant fall after arc ends */
    }
    y_velocity = dy; /* proxy: keeps landing detection working */
  } else {
    /* non-jumping states: original velocity-based movement */
    dy = (y_velocity > 0 ? y_velocity / 3 : y_velocity >> 2);
  }
  int new_y = position.y + dy;
  if (dy < 0 &&
      (int)check_ceiling(position.x, position.scroll_x, &new_y, &y_velocity, position.y)) {
    jump_frame = arc_len; /* ceiling hit: skip to post-arc fall */
    apex_hang = 0;
  }
  if (bridge_floor < BRIDGE_FLOOR_Y) {
    /* Bridge surface replaces tile floor check entirely — prevents castle
     * approach tiles from fighting the bridge geometry.  Uses >= so that
     * standing still (feet == floor) also zeroes y_velocity every frame, stopping
     * apex_hang from triggering gravity accumulation between snaps.
     * Uses the same y+46 probe offset as check_floor so the snap landing
     * position matches the tile-floor baseline exactly at progress=0.
     * bridge_tolerance widens the band: Simon can enter up to
     * bridge_tolerance px into the surface from above. */
    if (new_y + 46 >= bridge_floor - bridge_tolerance) {
      new_y = bridge_floor - 46 + bridge_tolerance;
      y_velocity = 0;
      apex_hang = 0;
    }
  } else {
    check_floor(position.x, position.scroll_x, &new_y, &y_velocity);
  }
  if (start_y_velocity > 0 && y_velocity == 0) {
    SimonSetState(SIMON_IDLE);
  }
  position.y = new_y;
  if (position.y > TLN_GetHeight()) {
    position.y = 0;
    y_velocity = 0;
    SimonSetState(SIMON_IDLE);
  }
}

void SimonTasks(void) {
  sb_count = SandblockSnapshot(sb_cache);

  Direction input = DIR_NONE;
  bool jump = false;
  bool crouch_held = (int)TLN_GetInput(INPUT_DOWN) != 0;

  /* While whipping in the air, allow directional movement but suppress jump.
   * On the ground, suppress all input so Simon stays planted.
   * While crouching (stationary), allow left/right for crouch-walk but not jump. */
  bool whip_airborne = ((int)WhipIsActive() && (state == SIMON_JUMPING)) != 0;
  bool is_crouching = (state == SIMON_CROUCHING || state == SIMON_CROUCH_WALKING ||
                       state == SIMON_CROUCH_WHIPPING) != 0;
  if (!WhipIsActive() || (int)whip_airborne) {
    if (TLN_GetInput(INPUT_LEFT)) {
      input = DIR_LEFT;
    } else if (TLN_GetInput(INPUT_RIGHT)) {
      input = DIR_RIGHT;
    }
    if (!WhipIsActive() && !is_crouching && (int)TLN_GetInput(INPUT_A)) {
      jump = true;
    }
  }

  apply_movement(input, TLN_GetWidth());

  if ((int)jump && state != SIMON_JUMPING) {
    SimonSetState(SIMON_JUMPING);
  }

  /* Arc-type commitment: deferred until the earliest frame the button *could*
   * have been released to distinguish all three hold durations.
   * Frame 1: if INPUT_A already released → short arc (tap).
   * Frame 2: if INPUT_A still held → higher arc (3+ frames); released → tall. */
  if (state == SIMON_JUMPING && !jump_arc_committed) {
    if (jump_frame == 1 && !TLN_GetInput(INPUT_A)) {
      jump_arc_committed = true; /* short: tall and higher stay false */
    } else if (jump_frame == 2) {
      jump_arc_committed = true;
      if (TLN_GetInput(INPUT_A)) {
        jump_arc_higher = true;
      } else {
        jump_arc_tall = true;
      }
    }
  }

  int start_y_velocity = y_velocity;
  advance_gravity();
  apply_collisions(start_y_velocity);

  /* If collisions landed Simon into IDLE but a direction is still held,
   * promote immediately to WALKING so the idle sprite never shows for one
   * frame. */
  if (state == SIMON_IDLE && input != DIR_NONE) {
    SimonSetState(SIMON_WALKING);
  }

  /* Crouch: down held while on the ground (not jumping, not whipping). */
  bool on_ground = (state != SIMON_JUMPING);
  if ((int)crouch_held && (int)on_ground && !WhipIsActive()) {
    if (state == SIMON_WALKING) {
      SimonSetState(SIMON_CROUCH_WALKING);
    } else if (state != SIMON_CROUCHING && state != SIMON_CROUCH_WALKING &&
               state != SIMON_CROUCH_WHIPPING) {
      SimonSetState(SIMON_CROUCHING);
    }
  } else if (state == SIMON_CROUCH_WHIPPING) {
    /* S released while crouch-whipping: hold crouch until whip finishes. */
    if (!WhipIsActive()) {
      SimonSetState((int)crouch_held ? SIMON_CROUCHING : SIMON_IDLE);
    }
  } else if ((state == SIMON_CROUCHING || state == SIMON_CROUCH_WALKING) &&
             (!crouch_held || !on_ground)) {
    SimonSetState(state == SIMON_CROUCH_WALKING ? SIMON_WALKING : SIMON_IDLE);
  }

  /* Transition crouching states to CROUCH_WHIPPING when whip fires. */
  if ((int)WhipIsActive() && (int)on_ground &&
      (state == SIMON_CROUCHING || state == SIMON_CROUCH_WALKING)) {
    SimonSetState(SIMON_CROUCH_WHIPPING);
  }

  /* Teeter: idle on the bridge surface with no player input. */
  if (state == SIMON_IDLE && bridge_floor != BRIDGE_FLOOR_Y && input == DIR_NONE) {
    SimonSetState(SIMON_TEETER);
  } else if (state == SIMON_TEETER && bridge_floor == BRIDGE_FLOOR_Y) {
    SimonSetState(SIMON_IDLE);
  }

  if (state == SIMON_WALKING || state == SIMON_CROUCH_WALKING) {
    walk_anim_frame++;
  }
  render_current_state();
}

int SimonGetPosition(void) { return position.scroll_x; }

void SimonSetPosition(Coords2d pos) {
  position.x = pos.x;
  position.y = pos.y;
  position.scroll_x = 0;
  render_current_state();
}

void SimonPushRight(int pixels) {
  position.x += pixels;
  /* clamp to one sprite-width past the right screen edge */
  if (position.x > TLN_GetWidth()) {
    position.x = TLN_GetWidth();
  }
  render_current_state();
}

int SimonGetScreenX(void) { return position.x; }

void SimonSetScreenX(int screen_x) {
  position.x = screen_x;
  render_current_state();
}

void SimonSetFeetY(int feet_y) {
  SimonPinFeetY(feet_y);

  /* Pinning feet to a surface counts as landing: cancel any in-progress jump
   * so the jump sprite clears and the player can jump again next frame. */
  if (state == SIMON_JUMPING) {
    SimonSetState(SIMON_IDLE);
  }
}

void SimonPinFeetY(int feet_y) {
  /* Position-only correction: does not change state.
   * Zeroes y_velocity and resets apex_hang so advance_gravity cannot accumulate
   * velocity on surfaces without collision tiles (e.g. the drawbridge deck). */
  position.y = feet_y - SIMON_HEIGHT;
  y_velocity = 0;
  apex_hang = 0;
  render_current_state();
}

int SimonGetFeetY(void) { return position.y + SIMON_HEIGHT; }

void SimonSetWorldX(int new_world_x) { position.scroll_x = new_world_x; }

int SimonGetScreenY(void) { return position.y; }

bool SimonIsCrouching(void) {
  return (state == SIMON_CROUCHING || state == SIMON_CROUCH_WALKING ||
          state == SIMON_CROUCH_WHIPPING) != 0;
}

bool SimonFacingRight(void) { return direction == DIR_RIGHT; }
