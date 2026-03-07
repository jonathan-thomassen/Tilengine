/*
 * Drawbridge.c — affine-transform arc animation for the SC4 drawbridge.
 *
 */

#include "Drawbridge.h"

#include "Tilengine.h"

/* ------------------------------------------------------------------ */

typedef struct {
  int layer;
  int hinge_x;
  int hinge_y;
  float progress; /* 0 = flat, 1 = fully raised */
} DrawbridgeState;

static DrawbridgeState db;

/* ------------------------------------------------------------------ */

void DrawbridgeInit(int layer, int hinge_x, int hinge_y) {
  db.layer = layer;
  db.hinge_x = hinge_x;
  db.hinge_y = hinge_y;
  db.progress = 0.0f;
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

void DrawbridgeTasks() {
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
