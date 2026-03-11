/*
 * Drawbridge.c — affine-transform arc animation for the SC4 drawbridge.
 *
 */

#include "Drawbridge.h"

#include "Tilengine.h"

/* 8.8 fixed-point type: values stored as real * 256 (matching SNES Mode 7
 * rotation registers M7A-M7D).  real = fix88_t / FIX88_ONE */
typedef int16_t fix88_t;
#define FIX88_ONE 256
/* Convert a float constant to fix88_t at compile time. */
#define FIX88(x) ((fix88_t)((x) * FIX88_ONE))

/* ------------------------------------------------------------------ */

typedef struct {
  int layer;
  int hinge_x;
  int hinge_y;
  int progress;      /* 0-134; index into baked_* tables */
  bool affine_dirty; /* true when the affine transform needs to be re-sent */
  int tick;          /* countdown for the 9-frame rate divider */
} DrawbridgeState;

static DrawbridgeState db;

/* Single trig table — sin(θ[i]) for i = 0..134.
 * cos(θ[i]) == sin(θ[134-i]), so no second table is needed. */
#define DB_STEPS 135
#define TRIG_COS(i) baked_sin[(DB_STEPS - 1) - (i)]

static const fix88_t baked_sin[DB_STEPS] = {
    FIX88(0.0),        FIX88(0.01171875), FIX88(0.0234375),  FIX88(0.03515625),
    FIX88(0.046875),   FIX88(0.05859375), FIX88(0.0703125),  FIX88(0.08203125),
    FIX88(0.09375),    FIX88(0.10546875), FIX88(0.1171875),  FIX88(0.12890625),
    FIX88(0.140625),   FIX88(0.15234375), FIX88(0.1640625),  FIX88(0.17578125),
    FIX88(0.1875),     FIX88(0.19921875), FIX88(0.2109375),  FIX88(0.22265625),
    FIX88(0.23046875), FIX88(0.2421875),  FIX88(0.25390625), FIX88(0.265625),
    FIX88(0.27734375), FIX88(0.2890625),  FIX88(0.30078125), FIX88(0.3125),
    FIX88(0.32421875), FIX88(0.33203125), FIX88(0.34375),    FIX88(0.35546875),
    FIX88(0.3671875),  FIX88(0.37890625), FIX88(0.38671875), FIX88(0.3984375),
    FIX88(0.41015625), FIX88(0.421875),   FIX88(0.4296875),  FIX88(0.44140625),
    FIX88(0.453125),   FIX88(0.4609375),  FIX88(0.47265625), FIX88(0.484375),
    FIX88(0.4921875),  FIX88(0.50390625), FIX88(0.51171875), FIX88(0.5234375),
    FIX88(0.53515625), FIX88(0.54296875), FIX88(0.5546875),  FIX88(0.5625),
    FIX88(0.57421875), FIX88(0.58203125), FIX88(0.58984375), FIX88(0.6015625),
    FIX88(0.609375),   FIX88(0.62109375), FIX88(0.62890625), FIX88(0.63671875),
    FIX88(0.6484375),  FIX88(0.65625),    FIX88(0.6640625),  FIX88(0.671875),
    FIX88(0.68359375), FIX88(0.69140625), FIX88(0.69921875), FIX88(0.70703125),
    FIX88(0.71484375), FIX88(0.72265625), FIX88(0.73046875), FIX88(0.73828125),
    FIX88(0.74609375), FIX88(0.75390625), FIX88(0.76171875), FIX88(0.76953125),
    FIX88(0.77734375), FIX88(0.78515625), FIX88(0.79296875), FIX88(0.80078125),
    FIX88(0.8046875),  FIX88(0.8125),     FIX88(0.8203125),  FIX88(0.828125),
    FIX88(0.83203125), FIX88(0.83984375), FIX88(0.84765625), FIX88(0.8515625),
    FIX88(0.859375),   FIX88(0.86328125), FIX88(0.87109375), FIX88(0.875),
    FIX88(0.8828125),  FIX88(0.88671875), FIX88(0.890625),   FIX88(0.8984375),
    FIX88(0.90234375), FIX88(0.90625),    FIX88(0.9140625),  FIX88(0.91796875),
    FIX88(0.921875),   FIX88(0.92578125), FIX88(0.9296875),  FIX88(0.93359375),
    FIX88(0.9375),     FIX88(0.94140625), FIX88(0.9453125),  FIX88(0.94921875),
    FIX88(0.953125),   FIX88(0.95703125), FIX88(0.9609375),  FIX88(0.96484375),
    FIX88(0.96875),    FIX88(0.96875),    FIX88(0.97265625), FIX88(0.9765625),
    FIX88(0.9765625),  FIX88(0.98046875), FIX88(0.984375),   FIX88(0.984375),
    FIX88(0.98828125), FIX88(0.98828125), FIX88(0.98828125), FIX88(0.9921875),
    FIX88(0.9921875),  FIX88(0.99609375), FIX88(0.99609375), FIX88(0.99609375),
    FIX88(0.99609375), FIX88(1.0),        FIX88(1.0),        FIX88(1.0),
    FIX88(1.0),        FIX88(1.0),        FIX88(1.0)};

/* Chain-sprite anchor — screen x and world y for the chain's bottom-left
 * corner at each animation step (triggered hinge = 221,175).
 * x: screen x; add xpos to get world x.
 * y: world y; sprite height (-128) and drift correction already folded in.
 * Struct layout keeps both fields in one cache-line load. */
typedef struct {
  int x;
  int y;
} ChainPos;
static const ChainPos chain_pos[DB_STEPS] = {
    {80, 40},   {80, 38},   {80, 37},   {80, 35},   {80, 34},   {81, 32},
    {81, 30},   {81, 29},   {81, 27},   {82, 26},   {82, 24},   {82, 23},
    {82, 21},   {83, 19},   {83, 18},   {83, 16},   {84, 15},   {84, 13},
    {85, 12},   {85, 10},   {85, 9},    {86, 7},    {86, 6},    {87, 4},
    {87, 3},    {88, 1},    {89, 0},    {89, -2},   {90, -3},   {90, -5},
    {91, -6},   {92, -8},   {92, -9},   {93, -11},  {94, -12},  {94, -14},
    {95, -15},  {96, -16},  {97, -18},  {98, -19},  {98, -21},  {99, -22},
    {100, -23}, {101, -25}, {102, -26}, {103, -27}, {104, -29}, {105, -30},
    {105, -31}, {106, -33}, {107, -34}, {108, -35}, {109, -36}, {110, -38},
    {111, -39}, {113, -40}, {114, -41}, {115, -42}, {116, -44}, {117, -45},
    {118, -46}, {119, -47}, {120, -48}, {121, -49}, {123, -50}, {124, -52},
    {125, -53}, {126, -54}, {127, -55}, {129, -56}, {130, -57}, {131, -58},
    {133, -59}, {134, -60}, {135, -61}, {136, -62}, {138, -63}, {139, -63},
    {140, -64}, {142, -65}, {143, -66}, {145, -67}, {146, -68}, {147, -69},
    {149, -69}, {150, -70}, {152, -71}, {153, -72}, {155, -72}, {156, -73},
    {158, -74}, {159, -74}, {161, -75}, {162, -76}, {164, -76}, {165, -77},
    {167, -78}, {168, -78}, {170, -79}, {171, -79}, {173, -80}, {174, -80},
    {176, -81}, {177, -81}, {179, -82}, {181, -82}, {182, -82}, {184, -83},
    {185, -83}, {187, -84}, {189, -84}, {190, -84}, {192, -84}, {193, -85},
    {195, -85}, {197, -85}, {198, -85}, {200, -86}, {202, -86}, {203, -86},
    {205, -86}, {207, -86}, {208, -86}, {210, -86}, {211, -86}, {213, -86},
    {215, -87}, {216, -87}, {218, -87}, {220, -86}, {221, -86}, {223, -86},
    {225, -86}, {226, -86}, {228, -86}};

/* ------------------------------------------------------------------ */

void DrawbridgeInit(int layer, int hinge_x, int hinge_y) {
  db.layer = layer;
  db.hinge_x = hinge_x;
  db.hinge_y = hinge_y;
  db.progress = 0;
  db.affine_dirty = false;
  db.tick = 9;
}

void DrawbridgeSetProgress(int progress) {
  if (progress < 0)
    progress = 0;
  else if (progress > 134)
    progress = 134;
  if (progress == db.progress)
    return;
  db.progress = progress;
  db.affine_dirty = true;
}

int DrawbridgeGetProgress(void) {
  return db.progress;
}

void DrawbridgeSetHinge(int hinge_x, int hinge_y) {
  db.hinge_x = hinge_x;
  db.hinge_y = hinge_y;
  db.affine_dirty = true;
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
  int d = db.hinge_x - screen_x; /* distance left of hinge */
  int c = TRIG_COS(db.progress);
  int s = baked_sin[db.progress];
  /* tan = s/c; the DB_TRIG_ONE factors cancel: d*s/c (integer division). */
  if (c == 0)
    return (float)db.hinge_y; /* bridge fully vertical */
  return (float)db.hinge_y - (float)(d * s) / (float)c;
}

int DrawbridgeHingeX(void) {
  return db.hinge_x;
}

void DrawbridgeChainPos(int *out_x, int *out_y) {
  const ChainPos *p = &chain_pos[db.progress];
  *out_x = p->x;
  *out_y = p->y;
}

void DrawbridgeRotatedPoint(float rest_sx, float rest_sy, float *out_sx,
                            float *out_sy) {
  /* Convert fixed-point to float once; multiply at boundary. */
  float cf = (float)TRIG_COS(db.progress) * (1.0f / FIX88_ONE);
  float sf = (float)baked_sin[db.progress] * (1.0f / FIX88_ONE);
  float rx = rest_sx - (float)db.hinge_x;
  float ry = rest_sy - (float)db.hinge_y;
  *out_sx = (float)db.hinge_x + rx * cf - ry * sf;
  *out_sy = (float)db.hinge_y + rx * sf + ry * cf;
}

void DrawbridgeTasks(void) {
  if (db.affine_dirty) {
    int p = db.progress;
    /* Convert fixed-point to float at the API boundary only. */
    float cf = (float)TRIG_COS(p) * (1.0f / FIX88_ONE);
    float sf = (float)baked_sin[p] * (1.0f / FIX88_ONE);
    /* Standard 2-D rotation matrix [[cos,-sin],[sin,cos]].
     * TLN_SetLayerTransformMatrix inverts it internally. */
    TLN_SetLayerTransformMatrix(db.layer, cf, -sf, sf, cf, db.hinge_x,
                                db.hinge_y);
    db.affine_dirty = false;
  }
}
