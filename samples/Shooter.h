#ifndef SHOOTER_H
#define SHOOTER_H

#include "Tilengine.h"

#define WIDTH 400
#define HEIGHT 240

/* spritesets */
enum { SPRITESET_MAIN, SPRITESET_HELLARM, MAX_SPRITESET };

/* types of actors */
enum {
  TYPE_SHIP = 1,
  TYPE_CLAW,
  TYPE_BLADEB,
  TYPE_BLADES,
  TYPE_ENEMY,
  TYPE_EXPLOSION,
};

#define MAX_BULLETS 20
#define MAX_ENEMIES 10

/* actors */
enum {
  ACTOR_SHIP = 0,
  ACTOR_CLAW1 = 1,
  ACTOR_CLAW2 = 2,
  ACTOR_ENEMY1 = 3,
  ACTOR_BOSS = ACTOR_ENEMY1 + MAX_ENEMIES,
  ACTOR_BULLET1 = ACTOR_BOSS + 8,
  MAX_ACTOR = ACTOR_BULLET1 + MAX_BULLETS,
};

/* animations */
enum { SEQ_CLAW, SEQ_BLADE1, SEQ_BLADE2, SEQ_EXPLO1, SEQ_EXPLO2, MAX_SEQ };

extern TLN_Sequence sequences[MAX_SEQ];
extern TLN_Spriteset spritesets[MAX_SPRITESET];
extern unsigned int time;

#endif