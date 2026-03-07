#ifndef SHIP_H
#define SHIP_H

#include "Actor.h"

Actor *CreateShip(void);
Actor *CreateClaw(int id);
Actor *CreateShot(int type, int x, int y);

#endif
