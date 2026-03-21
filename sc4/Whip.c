#include "Whip.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "LoadFile.h" /* FileOpen() – honours TLN_SetLoadPath()  */
#include "Simon.h"    /* SimonGetScreenX/Y, SimonFacingRight      */
#include "Tilengine.h"

/*
 * The 32x24 whip0.png is divided into a 4-column x 3-row grid of 8x8 tiles,
 * giving 12 subsprites (picture indices 0-11):
 *
 *   s0-s3   (row 0, y=0)
 *   s4-s7   (row 1, y=8)
 *   s8-s11  (row 2, y=16)
 */

#define SEG_SIZE 8        /* each subsprite is 8x8 px                     */
#define SIMON_SPRITE_W 32 /* width of Simon's sprite in pixels             */
#define NUM_STAGES 3      /* number of whip animation stages               */

/*
 * Number of frames each animation stage is shown.
 * The sum of all entries equals the total visible window; the remaining
 * (WHIP_DURATION - sum) frames are a brief invisible retract pause before
 * control returns to Simon.  Add or change values here to adjust timing.
 */
static const int stage_durations[NUM_STAGES] = {
    5, 5, 13}; /* 5 frames for stage 0, 5 frames for stage 1, 13 frames for stage 2 */

/* Returns the total number of frames across all stages. */
static int total_visible_frames(void) {
  int total = 0;
  for (int i = 0; i < NUM_STAGES; i++)
    total += stage_durations[i];
  return total;
}

/* Returns the stage index for the given frame, based on stage_durations. */
static int frame_to_stage(int frame) {
  int f = frame;
  for (int i = 0; i < NUM_STAGES - 1; i++) {
    if (f < stage_durations[i])
      return i;
    f -= stage_durations[i];
  }
  return NUM_STAGES - 1;
}

/*
 * Subsprite descriptor loaded from whip0_mapN.txt.
 * Positions are defined for Simon facing RIGHT (the default sprite orientation).
 * When Simon faces left, dx is mirrored as (SIMON_SPRITE_W - dx - SEG_SIZE)
 * and flip_h is inverted.
 */
typedef struct {
  int pic;     /* picture index in the spriteset (0-11)        */
  int dx;      /* x offset from Simon's top-left (left-facing) */
  int dy;      /* y offset from Simon's top-left               */
  bool flip_h; /* sprite's own horizontal flip                 */
  bool flip_v; /* sprite's own vertical flip                   */
} WhipSeg;

/* Runtime stage data: segments loaded from whip0_mapN.txt. */
static struct {
  WhipSeg segs[MAX_WHIP_SPRITES];
  int count; /* number of valid entries (0 = stage not loaded) */
} stages[NUM_STAGES];

static TLN_Spriteset spriteset;

/* Current frame within the swing; >= WHIP_DURATION means inactive. */
static int swing_frame;

/* Facing direction frozen at the moment the swing was triggered. */
static bool swing_facing_right;

/* Edge-detect state: prevents re-triggering while INPUT_B is held. */
static bool prev_pressed;

static void disable_all_segments(void) {
  for (int i = 0; i < MAX_WHIP_SPRITES; i++) {
    TLN_DisableSprite(WHIP_SPRITE_BASE + i);
  }
}

/*
 * Parses a single map file containing multiple numbered sections into stages[].
 *
 * File format:
 *   N:                     <- section header; N is the stage index (0-based)
 *   sP = ( dx, dy) [flags] <- subsprite line: P=picture index, dx/dy=offsets,
 *                             optional flags 'h' (flip-X) and/or 'v' (flip-Y)
 *
 * Multiple sections can appear in any order.  Blank lines and unrecognised
 * lines are silently skipped.
 */
static void load_map_file(const char *filename) {
  FILE *f = FileOpen(filename);
  if (f == NULL) {
    return;
  }

  int cur_stage = -1;
  char line[128];
  while (fgets(line, sizeof(line), f) != NULL) {
    /* Try section header: "N:" */
    int idx = -1;
    if (sscanf(line, " %d :", &idx) == 1 && idx >= 0 && idx < NUM_STAGES) {
      cur_stage = idx;
      continue;
    }

    if (cur_stage < 0 || stages[cur_stage].count >= MAX_WHIP_SPRITES) {
      continue;
    }

    int pic = -1;
    int dx = 0;
    int dy = 0;
    char flags[8] = "";

    int matched = sscanf(line, " s%d = ( %d , %d ) %7s", &pic, &dx, &dy, flags);
    if (matched < 3 || pic < 0) {
      continue;
    }

    WhipSeg *s = &stages[cur_stage].segs[stages[cur_stage].count];
    s->pic = pic;
    s->dx = dx;
    s->dy = dy;
    s->flip_h = (strchr(flags, 'h') != NULL);
    s->flip_v = (strchr(flags, 'v') != NULL);
    stages[cur_stage].count++;
  }

  fclose(f);
}

void WhipInit(void) {
  spriteset = TLN_LoadSpriteset("whip0");
  swing_frame = WHIP_DURATION; /* inactive */
  prev_pressed = false;
  disable_all_segments();

  /* Load all stage layouts from a single file.  A missing file or a section
   * that is absent leaves that stage's count = 0 (no sprites shown). */
  load_map_file("whip0_map0.txt");
}

void WhipDeinit(void) {
  disable_all_segments();
  if (spriteset != NULL) {
    TLN_DeleteSpriteset(spriteset);
    spriteset = NULL;
  }
  for (int i = 0; i < NUM_STAGES; i++) {
    stages[i].count = 0;
  }
}

bool WhipIsActive(void) { return swing_frame < WHIP_DURATION; }

void WhipTasks(void) {
  bool pressed = TLN_GetInput(INPUT_B) != 0;

  /* Rising-edge trigger: start a new swing only on a fresh press and while
   * no swing is already running. */
  if (pressed && !prev_pressed && !WhipIsActive()) {
    swing_frame = 0;
    swing_facing_right = SimonFacingRight();
  }
  prev_pressed = pressed;

  if (!WhipIsActive()) {
    return;
  }

  if (swing_frame < total_visible_frames()) {
    int stage = frame_to_stage(swing_frame);
    int sx = SimonGetScreenX();
    int sy = SimonGetScreenY();
    int count = stages[stage].count;

    for (int seg = 0; seg < MAX_WHIP_SPRITES; seg++) {
      if (seg < count) {
        const WhipSeg *s = &stages[stage].segs[seg];
        int wx;
        bool fh;
        if (swing_facing_right) {
          wx = sx + s->dx;
          fh = s->flip_h;
        } else {
          wx = sx + (SIMON_SPRITE_W - s->dx - SEG_SIZE);
          fh = !s->flip_h;
        }
        TLN_SetSpriteSet(WHIP_SPRITE_BASE + seg, spriteset);
        TLN_SetSpritePicture(WHIP_SPRITE_BASE + seg, s->pic);
        TLN_EnableSpriteFlag(WHIP_SPRITE_BASE + seg, FLAG_FLIPX, fh);
        TLN_EnableSpriteFlag(WHIP_SPRITE_BASE + seg, FLAG_FLIPY, s->flip_v);
        TLN_SetSpritePosition(WHIP_SPRITE_BASE + seg, wx, sy + s->dy);
      } else {
        TLN_DisableSprite(WHIP_SPRITE_BASE + seg);
      }
    }
  } else {
    /* Retract phase: hide all segments for the remaining frames. */
    disable_all_segments();
  }

  swing_frame++;

  /* Animation complete — ensure all segments are disabled. */
  if (swing_frame >= WHIP_DURATION) {
    disable_all_segments();
  }
}
