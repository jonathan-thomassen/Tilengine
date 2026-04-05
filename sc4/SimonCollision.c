#include "SimonCollision.h"

#include <stdio.h>

#include "LoadFile.h"
#include "Simon.h"
#include "Tilengine.h"

/* ---------------------------------------------------------------------------
 * col_definition lookup table
 *
 * col_thresh[H][N][row_idx] = first valid (V) column for that grid row,
 * or COL_THRESH_NONE if the row has no valid destination.
 *
 * Indexing (up-right movement, probe at sprite-local (8,0)):
 *   H        1..8  — block rows overlapping the top of the player box
 *   N        1..9  — block width in pixel columns at the probe
 *   row_idx  0..7  — maps to dy as: row_idx = 8 + dy
 *                    (row 0 = dy=-8 = topmost, row 7 = dy=-1 = just above)
 *   col_idx  0..8  — equals dx (column 0 = probe's current x, column dx = destination)
 * ------------------------------------------------------------------------- */
#define COL_THRESH_NONE 9
#define COL_DEF_MAX_H 8
#define COL_DEF_MAX_N 9
#define COL_DEF_ROWS 8

static int col_thresh[COL_DEF_MAX_H + 1][COL_DEF_MAX_N + 1][COL_DEF_ROWS];

void load_col_definition(void) {
  for (int h = 0; h <= COL_DEF_MAX_H; h++) {
    for (int n = 0; n <= COL_DEF_MAX_N; n++) {
      for (int r = 0; r < COL_DEF_ROWS; r++) {
        col_thresh[h][n][r] = COL_THRESH_NONE;
      }
    }
  }

  FILE *f = FileOpen("col_definition");
  if (f == NULL) {
    return;
  }

  int cur_H = 0;
  int cur_N = 0;
  int cur_row = 0;
  char line[64];
  while (fgets(line, sizeof(line), f) != NULL) {
    int h;
    int n;
    if (sscanf(line, "# %dx%d", &h, &n) == 2) {
      cur_H = h;
      cur_N = n;
      cur_row = 0;
      continue;
    }
    if (cur_H < 1 || cur_H > COL_DEF_MAX_H || cur_N < 1 || cur_N > COL_DEF_MAX_N) {
      continue;
    }
    if (line[0] == '/' || line[0] == '\n' || line[0] == '\r') {
      continue;
    }
    if (line[0] == '*' || line[0] == 'P') {
      continue; /* skip the probe/player row */
    }
    if (cur_row >= COL_DEF_ROWS) {
      continue;
    }

    int thresh = COL_THRESH_NONE;
    char *p = line;
    for (int col = 0; col < 9 && thresh == COL_THRESH_NONE; col++) {
      while (*p == ' ') {
        p++;
      }
      if (*p == 'V') {
        thresh = col;
      }
      while (*p && *p != ',') {
        p++;
      }
      if (*p == ',') {
        p++;
      }
    }
    col_thresh[cur_H][cur_N][cur_row] = thresh;
    cur_row++;
  }
  fclose(f);
}

/*
 * Extended col_definition_lookup with rotation and mirror support.
 *
 * The col_definition grids encode the up-right (dx>0, dy<0) movement case.
 * This function rotates and/or mirrors the grid before the lookup so all
 * eight probe orientations can be handled with one table.
 *
 * rotations  0..3 — number of 90° clockwise rotations applied to the grid:
 *              0 = up-right   (original)    needs dx>0, dy<0
 *              1 = down-right              needs dx>0, dy>0
 *              2 = down-left               needs dx<0, dy>0
 *              3 = up-left                 needs dx<0, dy<0
 * mirror_h   flip the grid horizontally (reverses the dx axis)
 * mirror_v   flip the grid vertically   (reverses the dy axis)
 *
 * The combined transform is T = Mirror ∘ Rotate (rotate first, then mirror).
 * To query the transformed grid the displacement is brought back to the
 * original frame via T_inv = Rotate_inv ∘ Mirror, and lookups use the same
 * col_thresh table.  When the destination is invalid the clamped vector is
 * transformed back into the caller's frame before being written to *dx/*dy.
 *
 * A single CW rotation step maps (u,v) → (−v, u) in screen coordinates.
 *
 * H, N: grid selector (same semantics as col_definition_lookup, interpreted
 * in the rotated frame).
 */
static void col_definition_lookup(int H, int N, int rotations, bool mirror_h, bool mirror_v,
                                  int *dx, int *dy) {
  if (H < 1 || H > COL_DEF_MAX_H || N < 1 || N > COL_DEF_MAX_N) {
    return;
  }

  /* Bring (dx,dy) into the original up-right frame.
   * T_inv = Rotate_inv ∘ Mirror; mirrors are self-inverse. */
  int u = *dx;
  int v = *dy;
  if (mirror_h) {
    u = -u;
  }
  if (mirror_v) {
    v = -v;
  }

  int inv_rot = (4 - (rotations % 4)) % 4;
  for (int i = 0; i < inv_rot; i++) { /* one CW step: (u,v) → (−v, u) */
    int tmp = u;
    u = -v;
    v = tmp;
  }

  /* Now (u,v) must be in the original up-right frame: u>0, v<0 */
  if (u <= 0 || v >= 0) {
    return;
  }

  int row_idx = COL_DEF_ROWS + v; /* v = orig_dy; row 0 = dy=-8 .. row 7 = dy=-1 */
  if (row_idx < 0 || row_idx >= COL_DEF_ROWS) {
    return;
  }

  if (u >= col_thresh[H][N][row_idx]) {
    return; /* V cell — valid, no change */
  }

  /* Invalid: clamp orig_dy to H-8, keeping orig_dx.
   * Then apply the forward transform T = Mirror ∘ Rotate to write back. */
  int cu = u;
  int cv = H - 8;

  for (int i = 0; i < (rotations % 4); i++) { /* one CW step */
    int tmp = cu;
    cu = -cv;
    cv = tmp;
  }
  if (mirror_h) {
    cu = -cu;
  }
  if (mirror_v) {
    cv = -cv;
  }

  *dx = cu;
  *dy = cv;
}

/* ---------------------------------------------------------------------------
 * Probing
 * ------------------------------------------------------------------------- */

static void move_left_probes(int world_x, int sprite_x, int sprite_y, int *dx, bool (*probes)[4]) {
  for (int i = 0; i < 4; i++) {
    if (probes[0][i]) {
      continue;
    }
    TLN_TileInfo h_tile;
    TLN_GetLayerTile(COLLISION_LAYER, world_x + sprite_x + SIMON_COL_X_OFFSET + *dx,
                     i == 0   ? sprite_y
                     : i == 3 ? sprite_y + (BLOCK_SIZE * i) - 3
                              : sprite_y + (BLOCK_SIZE * i) - 1,
                     &h_tile);

    if (!h_tile.empty) {
      *dx += TILE_SIZE - h_tile.xoffset;
      return;
    }
  }
}

static void move_right_probes(int world_x, int sprite_x, int sprite_y, int *dx, bool (*probes)[4]) {
  for (int i = 0; i < 4; i++) {
    if (probes[1][i]) {
      continue;
    }
    TLN_TileInfo h_tile;
    TLN_GetLayerTile(COLLISION_LAYER,
                     world_x + sprite_x + SIMON_COL_X_OFFSET + SIMON_COL_WIDTH + *dx,
                     i == 0   ? sprite_y
                     : i == 3 ? sprite_y + (BLOCK_SIZE * i) - 3
                              : sprite_y + (BLOCK_SIZE * i) - 1,
                     &h_tile);

    if (!h_tile.empty) {
      *dx -= h_tile.xoffset;
      return;
    }
  }
}

static void move_up_probes(int world_x, int sprite_x, int sprite_y, int *dy, bool (*probes)[4]) {
  for (int i = 0; i < 2; i++) {
    if (probes[i][0]) {
      continue;
    }
    TLN_TileInfo v_tile;
    TLN_GetLayerTile(COLLISION_LAYER,
                     i == 0 ? world_x + sprite_x + SIMON_COL_X_OFFSET
                            : world_x + sprite_x + SIMON_COL_X_OFFSET + (BLOCK_SIZE * i) - 1,
                     sprite_y + *dy, &v_tile);

    if (!v_tile.empty) {
      *dy += TILE_SIZE - v_tile.yoffset;
      return;
    }
  }
}

static void move_down_probes(int world_x, int sprite_x, int sprite_y, int *dy, bool (*probes)[4]) {
  for (int i = 0; i < 2; i++) {
    if (probes[i][3]) {
      continue;
    }
    TLN_TileInfo v_tile;
    TLN_GetLayerTile(COLLISION_LAYER,
                     i == 0 ? world_x + sprite_x + SIMON_COL_X_OFFSET
                            : world_x + sprite_x + SIMON_COL_X_OFFSET + (BLOCK_SIZE * i) - 1,
                     sprite_y + SIMON_COL_HEIGHT - 1 + *dy, &v_tile);

    if (!v_tile.empty) {
      *dy -= v_tile.yoffset;
      return;
    }
  }
}

static void move_up_right_probe_up_right(int world_x, int sprite_x, int sprite_y, int *dx,
                                         int *dy) {
  TLN_TileInfo h_tile;
  TLN_GetLayerTile(COLLISION_LAYER, world_x + sprite_x + SIMON_COL_X_OFFSET + SIMON_COL_WIDTH + *dx,
                   sprite_y, &h_tile);
  TLN_TileInfo v_tile;
  TLN_GetLayerTile(COLLISION_LAYER, world_x + sprite_x + SIMON_COL_X_OFFSET + SIMON_COL_WIDTH,
                   sprite_y + *dy, &v_tile);
  TLN_TileInfo hv_tile;
  TLN_GetLayerTile(COLLISION_LAYER, world_x + sprite_x + SIMON_COL_X_OFFSET + SIMON_COL_WIDTH + *dx,
                   sprite_y + *dy, &hv_tile);

  if (hv_tile.empty) {
    if (h_tile.empty) {
      if (v_tile.empty) {
        return;
      }
      *dy += TILE_SIZE - v_tile.yoffset;
      return;
    }
    if (v_tile.empty) {
      *dx -= h_tile.xoffset;
      return;
    }
    *dx -= h_tile.xoffset;
    *dy += TILE_SIZE - v_tile.yoffset;
    return;
  }
  if (h_tile.empty) {
    if (v_tile.empty) {
      int x_overlap =
          (world_x + sprite_x + SIMON_COL_X_OFFSET + SIMON_COL_WIDTH + *dx) - hv_tile.xoffset;
      int y_overlap = hv_tile.yoffset - (sprite_y + *dy);
      if (y_overlap < x_overlap) {
        *dx -= hv_tile.xoffset;
        return;
      }
      *dy += TILE_SIZE - hv_tile.yoffset;
      return;
    }
    *dy += TILE_SIZE - v_tile.yoffset;
    return;
  }
  if (v_tile.empty) {
    *dx -= h_tile.xoffset;
    return;
  }
  *dx -= h_tile.xoffset;
  *dy += TILE_SIZE - v_tile.yoffset;
}

static void move_up_left_probe_up_left(int world_x, int sprite_x, int sprite_y, int *dx, int *dy) {
  TLN_TileInfo h_tile;
  TLN_GetLayerTile(COLLISION_LAYER, world_x + sprite_x + SIMON_COL_X_OFFSET + *dx, sprite_y,
                   &h_tile);
  TLN_TileInfo v_tile;
  TLN_GetLayerTile(COLLISION_LAYER, world_x + sprite_x + SIMON_COL_X_OFFSET, sprite_y + *dy,
                   &v_tile);
  TLN_TileInfo hv_tile;
  TLN_GetLayerTile(COLLISION_LAYER, world_x + sprite_x + SIMON_COL_X_OFFSET + *dx, sprite_y + *dy,
                   &hv_tile);

  if (hv_tile.empty) {
    if (h_tile.empty) {
      if (v_tile.empty) {
        return;
      }
      *dy += TILE_SIZE - v_tile.yoffset;
      return;
    }
    *dx += TILE_SIZE - h_tile.xoffset;
    if (v_tile.empty) {
      return;
    }
    *dy += TILE_SIZE - v_tile.yoffset;
    return;
  }
  if (h_tile.empty) {
    if (v_tile.empty) {
      int x_overlap =
          (hv_tile.xoffset + BLOCK_SIZE) - (world_x + sprite_x + SIMON_COL_X_OFFSET + *dx);
      int y_overlap = hv_tile.yoffset - (sprite_y + *dy);
      if (y_overlap < x_overlap) {
        *dx += TILE_SIZE - hv_tile.xoffset;
        return;
      }
      *dy += TILE_SIZE - hv_tile.yoffset;
      return;
    }
    *dy += TILE_SIZE - v_tile.yoffset;
    return;
  }
  if (v_tile.empty) {
    *dx += TILE_SIZE - h_tile.xoffset;
    return;
  }
  *dx += TILE_SIZE - h_tile.xoffset;
  *dy += TILE_SIZE - v_tile.yoffset;
}

static void move_down_right_probe_down_right(int world_x, int sprite_x, int sprite_y, int *dx,
                                             int *dy) {
  TLN_TileInfo h_tile;
  TLN_GetLayerTile(COLLISION_LAYER, world_x + sprite_x + SIMON_COL_X_OFFSET + SIMON_COL_WIDTH + *dx,
                   sprite_y + SIMON_COL_HEIGHT - 1, &h_tile);
  TLN_TileInfo v_tile;
  TLN_GetLayerTile(COLLISION_LAYER, world_x + sprite_x + SIMON_COL_X_OFFSET + SIMON_COL_WIDTH,
                   sprite_y + SIMON_COL_HEIGHT - 1 + *dy, &v_tile);
  TLN_TileInfo hv_tile;
  TLN_GetLayerTile(COLLISION_LAYER, world_x + sprite_x + SIMON_COL_X_OFFSET + SIMON_COL_WIDTH + *dx,
                   sprite_y + SIMON_COL_HEIGHT - 1 + *dy, &hv_tile);

  if (hv_tile.empty) {
    if (h_tile.empty) {
      if (v_tile.empty) {
        return;
      }
      *dy -= v_tile.yoffset;
      return;
    }
    *dx -= h_tile.xoffset;
    if (v_tile.empty) {
      return;
    }
    *dy -= v_tile.yoffset;
    return;
  }
  if (h_tile.empty) {
    if (v_tile.empty) {
      int x_overlap =
          (world_x + sprite_x + SIMON_COL_X_OFFSET + SIMON_COL_WIDTH + *dx) - hv_tile.xoffset;
      int y_overlap = (sprite_y + SIMON_COL_HEIGHT - 1 + *dy) - hv_tile.yoffset;
      if (y_overlap < x_overlap) {
        *dx -= hv_tile.xoffset;
        return;
      }
      *dy -= hv_tile.yoffset;
      return;
    }
    *dy -= v_tile.yoffset;
    return;
  }
  if (v_tile.empty) {
    *dx -= h_tile.xoffset;
    return;
  }
  *dx -= h_tile.xoffset;
  *dy -= v_tile.yoffset;
}

static void move_down_left_probe_down_left(int world_x, int sprite_x, int sprite_y, int *dx,
                                           int *dy) {
  TLN_TileInfo h_tile;
  TLN_GetLayerTile(COLLISION_LAYER, world_x + sprite_x + SIMON_COL_X_OFFSET + *dx,
                   sprite_y + SIMON_COL_HEIGHT - 1, &h_tile);
  TLN_TileInfo v_tile;
  TLN_GetLayerTile(COLLISION_LAYER, world_x + sprite_x + SIMON_COL_X_OFFSET,
                   sprite_y + SIMON_COL_HEIGHT - 1 + *dy, &v_tile);
  TLN_TileInfo hv_tile;
  TLN_GetLayerTile(COLLISION_LAYER, world_x + sprite_x + SIMON_COL_X_OFFSET + *dx,
                   sprite_y + SIMON_COL_HEIGHT - 1 + *dy, &hv_tile);

  if (hv_tile.empty) {
    if (h_tile.empty) {
      if (v_tile.empty) {
        return;
      }
      *dy -= v_tile.yoffset;
      return;
    }
    *dx += TILE_SIZE - h_tile.xoffset;
    if (v_tile.empty) {
      return;
    }
    *dy -= v_tile.yoffset;
    return;
  }
  if (h_tile.empty) {
    if (v_tile.empty) {
      int x_overlap =
          (hv_tile.xoffset + BLOCK_SIZE) - (world_x + sprite_x + SIMON_COL_X_OFFSET + *dx);
      int y_overlap = (sprite_y + SIMON_COL_HEIGHT - 1 + *dy) - hv_tile.yoffset;
      if (y_overlap < x_overlap) {
        *dx += TILE_SIZE - hv_tile.xoffset;
        return;
      }
      *dy -= hv_tile.yoffset;
      return;
    }
    *dy -= v_tile.yoffset;
    return;
  }
  *dx += TILE_SIZE - h_tile.xoffset;
  if (v_tile.empty) {
    return;
  }
  *dy -= v_tile.yoffset;
}

static void move_up_right_probe_up_left(int world_x, int sprite_x, int sprite_y, int *dx, int *dy) {
  TLN_TileInfo v_tile;
  TLN_GetLayerTile(COLLISION_LAYER, world_x + sprite_x + SIMON_COL_X_OFFSET, sprite_y + *dy,
                   &v_tile);
  TLN_TileInfo hv_tile;
  TLN_GetLayerTile(COLLISION_LAYER, world_x + sprite_x + SIMON_COL_X_OFFSET + *dx, sprite_y + *dy,
                   &hv_tile);

  if (hv_tile.empty) {
    if (v_tile.empty) {
      return;
    }
    int h = MAX_VELOCITY - (sprite_y - (v_tile.yoffset + BLOCK_SIZE));
    int n = v_tile.xoffset - (world_x + sprite_x + SIMON_COL_X_OFFSET);

    col_definition_lookup(h, n, 0, false, false, dx, dy);
    return;
  }
  *dy += TILE_SIZE - hv_tile.yoffset;
}

static void move_up_right_probe_down_right(int world_x, int sprite_x, int sprite_y, int *dx,
                                           int *dy) {
  TLN_TileInfo h_tile;
  TLN_GetLayerTile(COLLISION_LAYER, world_x + sprite_x + SIMON_COL_X_OFFSET + SIMON_COL_WIDTH + *dx,
                   sprite_y + SIMON_COL_HEIGHT - 1, &h_tile);
  TLN_TileInfo hv_tile;
  TLN_GetLayerTile(COLLISION_LAYER, world_x + sprite_x + SIMON_COL_X_OFFSET + SIMON_COL_WIDTH + *dx,
                   sprite_y + SIMON_COL_HEIGHT - 1 + *dy, &hv_tile);

  if (hv_tile.empty) {
    if (h_tile.empty) {
      return;
    }
    int h =
        MAX_VELOCITY - (world_x + sprite_x + SIMON_COL_X_OFFSET + SIMON_COL_WIDTH - h_tile.xoffset);
    int n = h_tile.yoffset - sprite_y;

    col_definition_lookup(h, n, 1, false, true, dx, dy);
    return;
  }
  *dx -= hv_tile.xoffset;
}

static void move_y_left_probes_left(int world_x, int sprite_x, int sprite_y, int *dx,
                                    const int *dy) {
  for (int i = 1; i < 3; i++) {
    TLN_TileInfo h_tile;
    TLN_GetLayerTile(COLLISION_LAYER, world_x + sprite_x + SIMON_COL_X_OFFSET + *dx,
                     sprite_y + (BLOCK_SIZE * i) - 1, &h_tile);
    TLN_TileInfo hv_tile;
    TLN_GetLayerTile(COLLISION_LAYER, world_x + sprite_x + SIMON_COL_X_OFFSET + *dx,
                     sprite_y + (BLOCK_SIZE * i) - 1 + *dy, &hv_tile);

    if ((int)hv_tile.empty && (int)h_tile.empty) {
      continue;
    }

    if (!h_tile.empty) {
      *dx += TILE_SIZE - h_tile.xoffset;
    } else if (!hv_tile.empty) {
      *dx += TILE_SIZE - hv_tile.xoffset;
    }

    return;
  }
}

static void move_y_right_probes_right(int world_x, int sprite_x, int sprite_y, int *dx,
                                      const int *dy) {
  for (int i = 1; i < 3; i++) {
    TLN_TileInfo h_tile;
    TLN_GetLayerTile(COLLISION_LAYER,
                     world_x + sprite_x + SIMON_COL_X_OFFSET + SIMON_COL_WIDTH + *dx,
                     sprite_y + (BLOCK_SIZE * i) - 1, &h_tile);
    TLN_TileInfo hv_tile;
    TLN_GetLayerTile(COLLISION_LAYER,
                     world_x + sprite_x + SIMON_COL_X_OFFSET + SIMON_COL_WIDTH + *dx,
                     sprite_y + (BLOCK_SIZE * i) - 1 + *dy, &hv_tile);

    if ((int)hv_tile.empty && (int)h_tile.empty) {
      continue;
    }

    if (!h_tile.empty) {
      *dx -= h_tile.xoffset;
    } else if (!hv_tile.empty) {
      *dx -= hv_tile.xoffset;
    }

    return;
  }
}

static void move_up_left_probe_up_right(int world_x, int sprite_x, int sprite_y, int *dx, int *dy) {
  TLN_TileInfo v_tile;
  TLN_GetLayerTile(COLLISION_LAYER, world_x + sprite_x + SIMON_COL_X_OFFSET + SIMON_COL_WIDTH,
                   sprite_y + *dy, &v_tile);
  TLN_TileInfo hv_tile;
  TLN_GetLayerTile(COLLISION_LAYER, world_x + sprite_x + SIMON_COL_X_OFFSET + SIMON_COL_WIDTH + *dx,
                   sprite_y + *dy, &hv_tile);

  if (hv_tile.empty) {
    if (v_tile.empty) {
      return;
    }
    int h = TILE_SIZE - (sprite_y - (v_tile.yoffset + BLOCK_SIZE));
    int n = v_tile.xoffset - (world_x + sprite_x + SIMON_COL_X_OFFSET + SIMON_COL_WIDTH);

    col_definition_lookup(h, n, 0, true, false, dx, dy);
    return;
  }
  *dy += TILE_SIZE - hv_tile.yoffset;
}

static void move_up_left_probe_down_left(int world_x, int sprite_x, int sprite_y, int *dx,
                                         int *dy) {
  TLN_TileInfo h_tile;
  TLN_GetLayerTile(COLLISION_LAYER, world_x + sprite_x + SIMON_COL_X_OFFSET + *dx,
                   sprite_y + SIMON_COL_HEIGHT - 1, &h_tile);
  TLN_TileInfo hv_tile;
  TLN_GetLayerTile(COLLISION_LAYER, world_x + sprite_x + SIMON_COL_X_OFFSET + *dx,
                   sprite_y + SIMON_COL_HEIGHT - 1 + *dy, &hv_tile);

  if (hv_tile.empty) {
    if (h_tile.empty) {
      return;
    }
    int h = TILE_SIZE - (world_x + sprite_x + SIMON_COL_X_OFFSET - h_tile.xoffset);
    int n = h_tile.yoffset - sprite_y;

    col_definition_lookup(h, n, 1, true, true, dx, dy);
    return;
  }
  *dx += TILE_SIZE - hv_tile.xoffset;
}

static void move_down_right_probe_down_left(int world_x, int sprite_x, int sprite_y, int *dx,
                                            int *dy) {
  TLN_TileInfo v_tile;
  TLN_GetLayerTile(COLLISION_LAYER, world_x + sprite_x + SIMON_COL_X_OFFSET,
                   sprite_y + SIMON_COL_HEIGHT - 1 + *dy, &v_tile);
  TLN_TileInfo hv_tile;
  TLN_GetLayerTile(COLLISION_LAYER, world_x + sprite_x + SIMON_COL_X_OFFSET + *dx,
                   sprite_y + SIMON_COL_HEIGHT - 1 + *dy, &hv_tile);

  if (hv_tile.empty) {
    if (v_tile.empty) {
      return;
    }
    int h = TILE_SIZE - ((sprite_y + SIMON_COL_HEIGHT - 1) - v_tile.yoffset);
    int n = v_tile.xoffset - (world_x + sprite_x + SIMON_COL_X_OFFSET);

    col_definition_lookup(h, n, 0, false, true, dx, dy);
    return;
  }
  *dy -= hv_tile.yoffset;
}

static void move_down_right_probe_up_right(int world_x, int sprite_x, int sprite_y, int *dx,
                                           int *dy) {
  TLN_TileInfo h_tile;
  TLN_GetLayerTile(COLLISION_LAYER, world_x + sprite_x + SIMON_COL_X_OFFSET + SIMON_COL_WIDTH + *dx,
                   sprite_y, &h_tile);
  TLN_TileInfo hv_tile;
  TLN_GetLayerTile(COLLISION_LAYER, world_x + sprite_x + SIMON_COL_X_OFFSET + SIMON_COL_WIDTH + *dx,
                   sprite_y + *dy, &hv_tile);

  if (hv_tile.empty) {
    if (h_tile.empty) {
      return;
    }
    int h =
        TILE_SIZE - (world_x + sprite_x + SIMON_COL_X_OFFSET + SIMON_COL_WIDTH - h_tile.xoffset);
    int n = h_tile.yoffset + BLOCK_SIZE - sprite_y;

    col_definition_lookup(h, n, 1, false, false, dx, dy);
    return;
  }
  *dx -= hv_tile.xoffset;
}

static void move_down_left_probe_down_right(int world_x, int sprite_x, int sprite_y, int *dx,
                                            int *dy) {
  TLN_TileInfo v_tile;
  TLN_GetLayerTile(COLLISION_LAYER, world_x + sprite_x + SIMON_COL_X_OFFSET + SIMON_COL_WIDTH,
                   sprite_y + SIMON_COL_HEIGHT - 1 + *dy, &v_tile);
  TLN_TileInfo hv_tile;
  TLN_GetLayerTile(COLLISION_LAYER, world_x + sprite_x + SIMON_COL_X_OFFSET + SIMON_COL_WIDTH + *dx,
                   sprite_y + SIMON_COL_HEIGHT - 1 + *dy, &hv_tile);

  if (hv_tile.empty) {
    if (v_tile.empty) {
      return;
    }
    int h = TILE_SIZE - ((sprite_y + SIMON_COL_HEIGHT - 1) - v_tile.yoffset);
    int n = v_tile.xoffset - (world_x + sprite_x + SIMON_COL_X_OFFSET + SIMON_COL_WIDTH);

    col_definition_lookup(h, n, 0, true, true, dx, dy);
    return;
  }
  *dy -= hv_tile.yoffset;
}

static void move_down_left_probe_up_left(int world_x, int sprite_x, int sprite_y, int *dx,
                                         int *dy) {
  TLN_TileInfo h_tile;
  TLN_GetLayerTile(COLLISION_LAYER, world_x + sprite_x + SIMON_COL_X_OFFSET + *dx, sprite_y,
                   &h_tile);
  TLN_TileInfo hv_tile;
  TLN_GetLayerTile(COLLISION_LAYER, world_x + sprite_x + SIMON_COL_X_OFFSET + *dx, sprite_y + *dy,
                   &hv_tile);

  if (hv_tile.empty) {
    if (h_tile.empty) {
      return;
    }
    int h = TILE_SIZE - (world_x + sprite_x + SIMON_COL_X_OFFSET - h_tile.xoffset);
    int n = h_tile.yoffset + BLOCK_SIZE - sprite_y;

    col_definition_lookup(h, n, 1, true, false, dx, dy);
    return;
  }
  *dx += TILE_SIZE - hv_tile.xoffset;
}

/* After each diagonal probe, dispatch to the appropriate single-axis probe
 * set when one axis has been zeroed by collision.  Returns true if the
 * caller should stop (one or both axes are done). */
static bool dispatch_single_axis(int world_x, int sprite_x, int sprite_y, int *dx, int *dy,
                                 bool (*probes)[4]) {
  if (*dx == 0) {
    if (*dy < 0) {
      move_up_probes(world_x, sprite_x, sprite_y, dy, probes);
    } else if (*dy > 0) {
      move_down_probes(world_x, sprite_x, sprite_y, dy, probes);
    }
    return true;
  }
  if (*dy == 0) {
    if (*dx < 0) {
      move_left_probes(world_x, sprite_x, sprite_y, dx, probes);
    } else if (*dx > 0) {
      move_right_probes(world_x, sprite_x, sprite_y, dx, probes);
    }
    return true;
  }
  return false;
}

static void move_up_right_probes(int world_x, int sprite_x, int sprite_y, int *dx, int *dy,
                                 bool (*probes)[4]) {
  move_up_right_probe_up_right(world_x, sprite_x, sprite_y, dx, dy);
  probes[1][0] = true;
  if (dispatch_single_axis(world_x, sprite_x, sprite_y, dx, dy, probes)) {
    return;
  }

  move_y_right_probes_right(world_x, sprite_x, sprite_y, dx, dy);
  probes[1][1] = true;
  probes[1][2] = true;
  if (dispatch_single_axis(world_x, sprite_x, sprite_y, dx, dy, probes)) {
    return;
  }

  move_up_right_probe_up_left(world_x, sprite_x, sprite_y, dx, dy);
  probes[0][0] = true;
  if (dispatch_single_axis(world_x, sprite_x, sprite_y, dx, dy, probes)) {
    return;
  }

  move_up_right_probe_down_right(world_x, sprite_x, sprite_y, dx, dy);
  probes[1][3] = true;
}

static void move_up_left_probes(int world_x, int sprite_x, int sprite_y, int *dx, int *dy,
                                bool (*probes)[4]) {
  move_up_left_probe_up_left(world_x, sprite_x, sprite_y, dx, dy);
  probes[0][0] = true;
  if (dispatch_single_axis(world_x, sprite_x, sprite_y, dx, dy, probes)) {
    return;
  }

  move_y_left_probes_left(world_x, sprite_x, sprite_y, dx, dy);
  probes[0][1] = true;
  probes[0][2] = true;
  if (dispatch_single_axis(world_x, sprite_x, sprite_y, dx, dy, probes)) {
    return;
  }

  move_up_left_probe_up_right(world_x, sprite_x, sprite_y, dx, dy);
  probes[1][0] = true;
  if (dispatch_single_axis(world_x, sprite_x, sprite_y, dx, dy, probes)) {
    return;
  }

  move_up_left_probe_down_left(world_x, sprite_x, sprite_y, dx, dy);
  probes[1][3] = true;
}

static void move_down_right_probes(int world_x, int sprite_x, int sprite_y, int *dx, int *dy,
                                   bool (*probes)[4]) {
  move_down_right_probe_down_right(world_x, sprite_x, sprite_y, dx, dy);
  probes[1][3] = true;
  if (dispatch_single_axis(world_x, sprite_x, sprite_y, dx, dy, probes)) {
    return;
  }

  move_y_right_probes_right(world_x, sprite_x, sprite_y, dx, dy);
  probes[1][1] = true;
  probes[1][2] = true;
  if (dispatch_single_axis(world_x, sprite_x, sprite_y, dx, dy, probes)) {
    return;
  }

  move_down_right_probe_down_left(world_x, sprite_x, sprite_y, dx, dy);
  probes[0][3] = true;
  if (dispatch_single_axis(world_x, sprite_x, sprite_y, dx, dy, probes)) {
    return;
  }

  move_down_right_probe_up_right(world_x, sprite_x, sprite_y, dx, dy);
  probes[1][0] = true;
}

static void move_down_left_probes(int world_x, int sprite_x, int sprite_y, int *dx, int *dy,
                                  bool (*probes)[4]) {
  move_down_left_probe_down_left(world_x, sprite_x, sprite_y, dx, dy);
  probes[0][3] = true;
  if (dispatch_single_axis(world_x, sprite_x, sprite_y, dx, dy, probes)) {
    return;
  }

  move_y_left_probes_left(world_x, sprite_x, sprite_y, dx, dy);
  probes[0][1] = true;
  probes[0][2] = true;
  if (dispatch_single_axis(world_x, sprite_x, sprite_y, dx, dy, probes)) {
    return;
  }

  move_down_left_probe_down_right(world_x, sprite_x, sprite_y, dx, dy);
  probes[1][3] = true;
  if (dispatch_single_axis(world_x, sprite_x, sprite_y, dx, dy, probes)) {
    return;
  }

  move_down_left_probe_up_left(world_x, sprite_x, sprite_y, dx, dy);
  probes[0][0] = true;
}

/*
 * Resolves a candidate displacement (*dx, *dy) against tile collision.
 *
 * Selects the probe set for the displacement direction, then for each probe
 * checks whether the destination pixel would be inside a solid tile.  Uses
 * single-axis retests to determine which axis caused the collision, and
 * clamps only that axis to stop just outside the tile boundary.  When
 * neither axis alone would enter the tile (a true diagonal corner), both
 * axes are clamped.  The result is the most restrictive valid displacement
 * across all five probes.
 *
 * Maximum displacement in either axis is 8 pixels (one frame at top speed);
 * tiles are 16 pixels wide, so at most one tile boundary can be crossed per
 * frame and the single-pass loop is sufficient.
 *
 * \param world_x   Horizontal scroll offset (camera position)
 * \param sprite_x  Sprite screen-x position
 * \param sprite_y  Sprite screen-y position (top of bounding box)
 * \param dx        In/out: candidate horizontal displacement, clamped on return
 * \param dy        In/out: candidate vertical displacement, clamped on return
 */
void resolve_collision(int world_x, int sprite_x, int sprite_y, int *dx, int *dy) {
  bool probes[2][4] = {{false}};

  if (*dx < 0) {
    if (*dy < 0) {
      move_up_left_probes(world_x, sprite_x, sprite_y, dx, dy, probes);
    } else if (*dy > 0) {
      move_down_left_probes(world_x, sprite_x, sprite_y, dx, dy, probes);
    } else {
      move_left_probes(world_x, sprite_x, sprite_y, dx, probes);
    }
  } else if (*dx > 0) {
    if (*dy < 0) {
      move_up_right_probes(world_x, sprite_x, sprite_y, dx, dy, probes);
    } else if (*dy > 0) {
      move_down_right_probes(world_x, sprite_x, sprite_y, dx, dy, probes);
    } else {
      move_right_probes(world_x, sprite_x, sprite_y, dx, probes);
    }
  } else {
    if (*dy < 0) {
      move_up_probes(world_x, sprite_x, sprite_y, dy, probes);
    } else if (*dy > 0) {
      move_down_probes(world_x, sprite_x, sprite_y, dy, probes);
    }
  }
}
