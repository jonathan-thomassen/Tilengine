#ifndef _ACTOR_H
#define _ACTOR_H

#include "../src/Tilengine.h"

typedef struct {
  int x1;
  int y1;
  int x2;
  int y2;
} Rect;

typedef struct Actor {
  int index;
  int type;
  int state;
  int w;
  int h;
  int x;
  int y;
  int vx;
  int vy;
  int life;
  Rect hitbox;
  unsigned int timers[4];
  void (*callback)(struct Actor *);
  unsigned char usrdata[64];
} Actor;

#ifdef __cplusplus
extern "C" {
#endif

bool CreateActors(int num);
bool DeleteActors(void);
int GetAvailableActor(int first, int len);
Actor *GetActor(int index);
Actor *SetActor(int index, int type, int x, int y, int w, int h,
                void (*callback)(struct Actor *));
void ReleaseActor(Actor *actor);
void UpdateActorHitbox(Actor *actor);
void TasksActors(unsigned int time);
bool CheckActorCollision(Actor const *actor1, Actor const *actor2);
void SetActorTimeout(Actor *actor, int timer, int timeout);
bool GetActorTimeout(Actor const *actor, int timer);

#ifdef __cplusplus
}
#endif

#endif