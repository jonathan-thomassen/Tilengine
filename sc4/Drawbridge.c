/*
 * Drawbridge.c — affine-transform arc animation for the SC4 drawbridge.
 *
 */

#include "Drawbridge.h"

#include <math.h>

#include "Tilengine.h"

/* ------------------------------------------------------------------ */

typedef struct {
  int layer;
  int hinge_x;
  int hinge_y;
  float progress;    /* 0 = flat, 1 = fully raised */
  float sin_theta;   /* cached sinf(progress * π/2) */
  bool affine_dirty; /* true when the affine transform needs to be re-sent */
  int tick;          /* frame counter for the 9-frame rate divider */
} DrawbridgeState;

static DrawbridgeState db;

/* ------------------------------------------------------------------ */

void DrawbridgeInit(int layer, int hinge_x, int hinge_y) {
  db.layer = layer;
  db.hinge_x = hinge_x;
  db.hinge_y = hinge_y;
  db.progress = 0.0f;
  db.sin_theta = 0.0f;
  db.affine_dirty = false;
  db.tick = 0;
}

void DrawbridgeSetProgress(float progress) {
  if (progress < 0.0f)
    progress = 0.0f;
  else if (progress > 1.0f)
    progress = 1.0f;
  if (progress == db.progress)
    return;
  db.progress = progress;
  db.sin_theta = sinf(progress * (float)(M_PI / 2.0));
  db.affine_dirty = true;
}

float DrawbridgeGetProgress(void) {
  return db.progress;
}

void DrawbridgeSetHinge(int hinge_x, int hinge_y) {
  db.hinge_x = hinge_x;
  db.hinge_y = hinge_y;
}

/* ------------------------------------------------------------------ */

bool DrawbridgeTick(void) {
  return db.tick++ % 9 == 0;
}

float DrawbridgeSurfaceY(int screen_x) {
  float d = (float)(db.hinge_x - screen_x); /* distance left of hinge */
  return (float)db.hinge_y - d * db.sin_theta;
}

int DrawbridgeHingeX(void) {
  return db.hinge_x;
}

/* Inverse of DrawbridgeSurfaceY: given a sprite's feet y, returns the minimum
 * screen x where the sprite no longer overlaps the bridge surface. */
int DrawbridgeMinX(int feet_y) {
  if (db.progress <= 0.0f || db.sin_theta < 1e-4f)
    return 0;
  float d = ((float)db.hinge_y - (float)feet_y) / db.sin_theta;
  return (db.hinge_x - (int)d);
}

void DrawbridgeTasks(void) {
  if (db.affine_dirty) {
    TLN_Affine affine = {
        .angle = db.progress * -90.0f,
        .dx = (float)db.hinge_x,
        .dy = (float)db.hinge_y,
        .sx = 1.0f,
        .sy = 1.0f,
    };
    TLN_SetLayerAffineTransform(db.layer, &affine);
    db.affine_dirty = false;
  }
}
