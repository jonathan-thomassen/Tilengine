#ifndef SIMON_H
#define SIMON_H

#define COLLISION_LAYER 5

void SimonInit(void);
void SimonDeinit(void);
void SimonTasks(void);
int SimonGetPosition(void);
void SimonSetState(int s);
void SimonSetPosition(int px, int py);

#endif