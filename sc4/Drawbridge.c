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
  float hinge_yf;    /* float cache of hinge_y, avoids repeated int→float cast */
  float progress;    /* 0 = flat, 1 = fully raised */
  float cos_theta;   /* cos(progress * π/2), passed directly to the affine SC call */
  float sin_theta;   /* sin(progress * π/2), passed directly to the affine SC call */
  float tan_theta;   /* sin/cos, cached for surface-Y queries */
  bool affine_dirty; /* true when the affine transform needs to be re-sent */
  int tick;          /* countdown for the 9-frame rate divider */
} DrawbridgeState;

static DrawbridgeState db;

/* ------------------------------------------------------------------ */

void DrawbridgeInit(int layer, int hinge_x, int hinge_y) {
  db.layer = layer;
  db.hinge_x = hinge_x;
  db.hinge_y = hinge_y;
  db.hinge_yf = (float)hinge_y;
  db.progress = 0.0f;
  db.cos_theta = 1.0f;
  db.sin_theta = 0.0f;
  db.tan_theta = 0.0f;
  db.affine_dirty = false;
  db.tick = 9;
}

void DrawbridgeSetProgress(float progress) {
  if (progress < 0.0f)
    progress = 0.0f;
  else if (progress > 1.0f)
    progress = 1.0f;
  if (progress == db.progress)
    return;
  db.progress = progress;
  /* Clamp slightly below 90° so cos stays non-zero (cos(0.999*π/2) ≈ 0.016). */
  float theta = (progress < 0.999f ? progress : 0.999f) * (float)M_PI_2;
  db.cos_theta = cosf(theta);
  db.sin_theta = sinf(theta);
  db.tan_theta = db.sin_theta / db.cos_theta;
  db.affine_dirty = true;
}

float DrawbridgeGetProgress(void) {
  return db.progress;
}

void DrawbridgeSetHinge(int hinge_x, int hinge_y) {
  db.hinge_x = hinge_x;
  db.hinge_y = hinge_y;
  db.hinge_yf = (float)hinge_y;
}

/* ------------------------------------------------------------------ */

bool DrawbridgeTick(void) {
  if (--db.tick <= 0) {
    db.tick = 9;
    return true;
  }
  return false;
}

float DrawbridgeSurfaceY(int screen_x) {
  float d = (float)(db.hinge_x - screen_x); /* distance left of hinge */
  return db.hinge_yf - d * db.tan_theta;
}

int DrawbridgeHingeX(void) {
  return db.hinge_x;
}

void DrawbridgeRotatedPoint(float rest_sx, float rest_sy, float *out_sx,
                             float *out_sy) {
  float rx = rest_sx - (float)db.hinge_x;
  float ry = rest_sy - (float)db.hinge_y;
  *out_sx = (float)db.hinge_x + rx * db.cos_theta - ry * db.sin_theta;
  *out_sy = (float)db.hinge_y + rx * db.sin_theta + ry * db.cos_theta;
}

void DrawbridgeTasks(void) {
  if (db.affine_dirty) {
    TLN_SetLayerTransformSC(db.layer,
        db.cos_theta, db.sin_theta,
        (float)db.hinge_x, (float)db.hinge_y,
        1.0f, 1.0f);
    db.affine_dirty = false;
  }
}
