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
  float progress; /* 0 = flat, 1 = fully raised */
  int tick;       /* frame counter for the 9-frame rate divider */
} DrawbridgeState;

static DrawbridgeState db;

/* ------------------------------------------------------------------ */

void DrawbridgeInit(int layer, int hinge_x, int hinge_y) {
  db.layer = layer;
  db.hinge_x = hinge_x;
  db.hinge_y = hinge_y;
  db.progress = 0.0f;
  db.tick = 0;
}

void DrawbridgeSetProgress(float progress) {
  if (progress < 0.0f)
    progress = 0.0f;
  else if (progress > 1.0f)
    progress = 1.0f;
  db.progress = progress;
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
  float theta = db.progress * (float)(M_PI / 2.0);
  float d = (float)(db.hinge_x - screen_x); /* distance left of hinge */
  return (float)db.hinge_y - d * sinf(theta);
}

void DrawbridgeTasks(void) {
  if (db.progress > 0.0f) {
    TLN_Affine affine = {
        .angle = db.progress * -90.0f,
        .dx = (float)db.hinge_x,
        .dy = (float)db.hinge_y,
        .sx = 1.0f,
        .sy = 1.0f,
    };
    TLN_SetLayerAffineTransform(db.layer, &affine);
  }
}
