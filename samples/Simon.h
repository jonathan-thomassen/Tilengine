#ifndef _SIMON_H
#define _SIMON_H

#include "Tilengine.h"

#define COLISSION_LAYER 5

void SimonInit(void);
void SimonDeinit(void);
void SimonTasks(void);
int SimonGetPosition(void);
void SimonSetState(int s);
void SimonSetPosition(int px, int py);

#endif