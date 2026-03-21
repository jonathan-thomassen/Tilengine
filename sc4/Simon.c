#include "Simon.h"

#include "Sandblock.h"
#include "Tilengine.h"

#define HANGTIME 8
#define TERM_VELOCITY 10
#define AIR_TURN_DELAY 6
#define SIMON_HEIGHT 48
#define SIMON_START_POS ((Coords2d){.x = 33, .y = 146})
#define JUMP_SPRITE 7
#define TEETER_SPRITE 8
#define INITIAL_JUMP_VELOCITY -18
#define BRIDGE_FLOOR_Y 32767

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

static TLN_Spriteset simon;
static TLN_SequencePack sequence_pack;
static TLN_Sequence walk;

static Coords2d position;
static int y_velocity = 0;
static int apex_hang = 0;
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

void SimonInit(void) {
    simon = TLN_LoadSpriteset("simon_walk");
    sequence_pack = TLN_LoadSequencePack("simon_walk.sqx");
    walk = TLN_FindSequence(sequence_pack, "walk");

    position = SIMON_START_POS;

    TLN_SetSpriteSet(SIMON_SPRITE, simon);
    TLN_SetSpritePosition(SIMON_SPRITE, position.x, position.y);

    layer_width = TLN_GetLayerWidth(1);

    SimonSetState(SIMON_IDLE);
    direction = DIR_RIGHT;
}

void SimonDeinit(void) {
    TLN_DeleteSequencePack(sequence_pack);
    TLN_DeleteSpriteset(simon);
}

void SimonBringToFront(void) {
    /* Removing and re-adding the sprite moves it to the tail of the engine's
     * render list, ensuring it is drawn on top of all other sprites. */
    TLN_DisableSprite(SIMON_SPRITE);
    TLN_SetSpriteSet(SIMON_SPRITE, simon);
}

void SimonFreezeCamera(void) { camera_frozen = true; }

void SimonSetState(SimonState new_state) {
    if (state == new_state) {
        return;
    }

    state = new_state;
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
        TLN_SetSpritePicture(SIMON_SPRITE, JUMP_SPRITE);
        y_velocity = INITIAL_JUMP_VELOCITY;
        break;

    case SIMON_TEETER:
        TLN_DisableSpriteAnimation(SIMON_SPRITE);
        TLN_SetSpritePicture(SIMON_SPRITE, TEETER_SPRITE);
        break;
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
        TLN_GetLayerTile(COLLISION_LAYER, pos.x + pos.scroll_x + x_offset, pos.y + probe_x,
                         &tile_info);
        if (!tile_info.empty) {
            return true;
        }
    }
    int wall_x = pos.x + pos.scroll_x + x_offset;
    for (int i = 0; i < sb_count; i++) {
        const SandblockState *sandblock_state = &sb_cache[i];
        if (wall_x < sandblock_state->world_x ||
            wall_x >= sandblock_state->world_x + SANDBLOCK_WIDTH) {
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
    }
}

/** Advances vertical velocity by one step, respecting apex hang. */
static void advance_gravity(void) {
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
    /* rising: gentle arc (>> 2); falling: medium pull (/ 3) */
    int new_y = position.y + (y_velocity > 0 ? y_velocity / 3 : y_velocity >> 2);
    if (y_velocity < 0 &&
        (int)check_ceiling(position.x, position.scroll_x, &new_y, &y_velocity, position.y)) {
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

    if (TLN_GetInput(INPUT_LEFT)) {
        input = DIR_LEFT;
    } else if (TLN_GetInput(INPUT_RIGHT)) {
        input = DIR_RIGHT;
    }
    if (TLN_GetInput(INPUT_A)) {
        jump = true;
    }

    apply_movement(input, TLN_GetWidth());

    if ((int)jump && state != SIMON_JUMPING) {
        SimonSetState(SIMON_JUMPING);
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

    /* Teeter: idle on the bridge surface with no player input. */
    if (state == SIMON_IDLE && bridge_floor != BRIDGE_FLOOR_Y && input == DIR_NONE) {
        SimonSetState(SIMON_TEETER);
    } else if (state == SIMON_TEETER && bridge_floor == BRIDGE_FLOOR_Y) {
        SimonSetState(SIMON_IDLE);
    }

    TLN_SetSpritePosition(SIMON_SPRITE, position.x, position.y);
}

int SimonGetPosition(void) { return position.scroll_x; }

void SimonSetPosition(Coords2d pos) {
    position.x = pos.x;
    position.y = pos.y;
    position.scroll_x = 0;
    TLN_SetSpritePosition(SIMON_SPRITE, position.x, position.y);
}

void SimonPushRight(int pixels) {
    position.x += pixels;
    /* clamp to one sprite-width past the right screen edge */
    if (position.x > TLN_GetWidth()) {
        position.x = TLN_GetWidth();
    }
    TLN_SetSpritePosition(SIMON_SPRITE, position.x, position.y);
}

int SimonGetScreenX(void) { return position.x; }

void SimonSetScreenX(int screen_x) {
    position.x = screen_x;
    TLN_SetSpritePosition(SIMON_SPRITE, position.x, position.y);
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
    TLN_SetSpritePosition(SIMON_SPRITE, position.x, position.y);
}

int SimonGetFeetY(void) { return position.y + SIMON_HEIGHT; }

void SimonSetWorldX(int new_world_x) { position.scroll_x = new_world_x; }
