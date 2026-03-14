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
    int progress;      /* 0-134; index into baked_* tables */
    bool affine_dirty; /* true when the affine transform needs to be re-sent */
    int tick;          /* countdown for the 9-frame rate divider */
} DrawbridgeState;

static DrawbridgeState db_state;

/* Single trig table — sin(θ[i]) for i = 0..134.
 * cos(θ[i]) == sin(θ[134-i]), so no second table is needed. */

#define TRIG_COS(i) baked_sin[(DB_STEPS - 1) - (i)]

static const float baked_sin[DB_STEPS] = {
    0.0F,        0.01171875F, 0.0234375F,  0.03515625F, 0.046875F,   0.05859375F, 0.0703125F,
    0.08203125F, 0.09375F,    0.10546875F, 0.1171875F,  0.12890625F, 0.140625F,   0.15234375F,
    0.1640625F,  0.17578125F, 0.1875F,     0.19921875F, 0.2109375F,  0.22265625F, 0.23046875F,
    0.2421875F,  0.25390625F, 0.265625F,   0.27734375F, 0.2890625F,  0.30078125F, 0.3125F,
    0.32421875F, 0.33203125F, 0.34375F,    0.35546875F, 0.3671875F,  0.37890625F, 0.38671875F,
    0.3984375F,  0.41015625F, 0.421875F,   0.4296875F,  0.44140625F, 0.453125F,   0.4609375F,
    0.47265625F, 0.484375F,   0.4921875F,  0.50390625F, 0.51171875F, 0.5234375F,  0.53515625F,
    0.54296875F, 0.5546875F,  0.5625F,     0.57421875F, 0.58203125F, 0.58984375F, 0.6015625F,
    0.609375F,   0.62109375F, 0.62890625F, 0.63671875F, 0.6484375F,  0.65625F,    0.6640625F,
    0.671875F,   0.68359375F, 0.69140625F, 0.69921875F, 0.70703125F, 0.71484375F, 0.72265625F,
    0.73046875F, 0.73828125F, 0.74609375F, 0.75390625F, 0.76171875F, 0.76953125F, 0.77734375F,
    0.78515625F, 0.79296875F, 0.80078125F, 0.8046875F,  0.8125F,     0.8203125F,  0.828125F,
    0.83203125F, 0.83984375F, 0.84765625F, 0.8515625F,  0.859375F,   0.86328125F, 0.87109375F,
    0.875F,      0.8828125F,  0.88671875F, 0.890625F,   0.8984375F,  0.90234375F, 0.90625F,
    0.9140625F,  0.91796875F, 0.921875F,   0.92578125F, 0.9296875F,  0.93359375F, 0.9375F,
    0.94140625F, 0.9453125F,  0.94921875F, 0.953125F,   0.95703125F, 0.9609375F,  0.96484375F,
    0.96875F,    0.96875F,    0.97265625F, 0.9765625F,  0.9765625F,  0.98046875F, 0.984375F,
    0.984375F,   0.98828125F, 0.98828125F, 0.98828125F, 0.9921875F,  0.9921875F,  0.99609375F,
    0.99609375F, 0.99609375F, 0.99609375F, 1.0F,        1.0F,        1.0F,        1.0F,
    1.0F,        1.0F};

/* Q8 fixed-point tangent table: baked_tan[i] = round(sin[i]/cos[i] * 256).
 * Entry 134 is 0 (sentinel; cos==0 is caught by the guard in DrawbridgeSurfaceY
 * before the table is ever indexed). */
static const int baked_tan[DB_STEPS] = {
    0,    3,    6,    9,    12,   15,   18,   21,   24,   27,   30,   33,   36,    39,    42,
    46,   49,   52,   55,   58,   61,   64,   67,   70,   74,   77,   81,   84,    88,    90,
    94,   97,   101,  105,  107,  111,  115,  119,  122,  126,  130,  133,  137,   142,   145,
    149,  152,  157,  162,  166,  171,  174,  179,  183,  188,  192,  197,  203,   207,   212,
    218,  223,  228,  233,  240,  245,  250,  256,  262,  268,  274,  281,  288,   294,   301,
    309,  316,  324,  333,  341,  349,  357,  366,  377,  384,  396,  405,  416,   430,   439,
    453,  462,  478,  492,  503,  521,  538,  550,  571,  590,  610,  625,  648,   672,   698,
    726,  746,  778,  811,  848,  887,  930,  977,  1024, 1080, 1123, 1185, 1260,  1344,  1434,
    1542, 1661, 1799, 1970, 2167, 2418, 2720, 3109, 3627, 4369, 5461, 7282, 10923, 21845, 0,
};

/* Chain-sprite anchor — screen x and world y for the chain's bottom-left
 * corner at each animation step (triggered hinge = 221,175).
 * x: screen x; add xpos to get world x.
 * y: world y; sprite height (-128) and drift correction already folded in.
 * Struct layout keeps both fields in one cache-line load. */
static const ChainPos chain_pos[DB_STEPS] = {
    {80, 40},   {80, 38},   {80, 37},   {80, 35},   {80, 34},   {81, 32},   {81, 30},   {81, 29},
    {81, 27},   {82, 26},   {82, 24},   {82, 23},   {82, 21},   {83, 19},   {83, 18},   {83, 16},
    {84, 15},   {84, 13},   {85, 12},   {85, 10},   {85, 9},    {86, 7},    {86, 6},    {87, 4},
    {87, 3},    {88, 1},    {89, 0},    {89, -2},   {90, -3},   {90, -5},   {91, -6},   {92, -8},
    {92, -9},   {93, -11},  {94, -12},  {94, -14},  {95, -15},  {96, -16},  {97, -18},  {98, -19},
    {98, -21},  {99, -22},  {100, -23}, {101, -25}, {102, -26}, {103, -27}, {104, -29}, {105, -30},
    {105, -31}, {106, -33}, {107, -34}, {108, -35}, {109, -36}, {110, -38}, {111, -39}, {113, -40},
    {114, -41}, {115, -42}, {116, -44}, {117, -45}, {118, -46}, {119, -47}, {120, -48}, {121, -49},
    {123, -50}, {124, -52}, {125, -53}, {126, -54}, {127, -55}, {129, -56}, {130, -57}, {131, -58},
    {133, -59}, {134, -60}, {135, -61}, {136, -62}, {138, -63}, {139, -63}, {140, -64}, {142, -65},
    {143, -66}, {145, -67}, {146, -68}, {147, -69}, {149, -69}, {150, -70}, {152, -71}, {153, -72},
    {155, -72}, {156, -73}, {158, -74}, {159, -74}, {161, -75}, {162, -76}, {164, -76}, {165, -77},
    {167, -78}, {168, -78}, {170, -79}, {171, -79}, {173, -80}, {174, -80}, {176, -81}, {177, -81},
    {179, -82}, {181, -82}, {182, -82}, {184, -83}, {185, -83}, {187, -84}, {189, -84}, {190, -84},
    {192, -84}, {193, -85}, {195, -85}, {197, -85}, {198, -85}, {200, -86}, {202, -86}, {203, -86},
    {205, -86}, {207, -86}, {208, -86}, {210, -86}, {211, -86}, {213, -86}, {215, -87}, {216, -87},
    {218, -87}, {220, -86}, {221, -86}, {223, -86}, {225, -86}, {226, -86}, {228, -86}};

/* ------------------------------------------------------------------ */

void DrawbridgeInit(int layer, int hinge_x, int hinge_y) {
    db_state.layer = layer;
    db_state.hinge_x = hinge_x;
    db_state.hinge_y = hinge_y;
    db_state.progress = 0;
    db_state.affine_dirty = true;
    db_state.tick = DB_TICK_RATE;
}

void DrawbridgeAdvance(void) {
    if (db_state.progress >= DB_STEPS - 1) {
        return;
    }
    db_state.progress++;
    db_state.affine_dirty = true;
}

int DrawbridgeGetProgress(void) { return db_state.progress; }

void DrawbridgeSetHinge(int hinge_x, int hinge_y) {
    db_state.hinge_x = hinge_x;
    db_state.hinge_y = hinge_y;
    db_state.affine_dirty = true;
}

/* ------------------------------------------------------------------ */

bool DrawbridgeTick(void) {
    if (--db_state.tick <= 0) {
        db_state.tick = DB_TICK_RATE;
        return true;
    }
    return false;
}

int DrawbridgeSurfaceY(int screen_x) {
    int d = db_state.hinge_x - screen_x; /* distance left of hinge */
    int p = db_state.progress;
    if (p >= DB_STEPS - 1) {
        return db_state.hinge_y; /* bridge fully vertical (cos == 0) */
    }
    /* Q8 fixed-point: tan*256 is baked_tan[p].  +128 rounds to nearest. */
    return db_state.hinge_y - ((d * baked_tan[p] + 128) >> 8);
}

int DrawbridgeHingeX(void) { return db_state.hinge_x; }

ChainPos DrawbridgeChainPos(void) { return chain_pos[db_state.progress]; }

void DrawbridgeTasks(void) {
    if (db_state.affine_dirty) {
        float cf = TRIG_COS(db_state.progress);
        float sf = baked_sin[db_state.progress];
        /* Standard 2-D rotation matrix [[cos,-sin],[sin,cos]].
         * TLN_SetLayerTransformMatrix inverts it internally. */
        TLN_SetLayerTransformMatrix(db_state.layer, cf, -sf, sf, cf, db_state.hinge_x,
                                    db_state.hinge_y);
        db_state.affine_dirty = false;
    }
}
