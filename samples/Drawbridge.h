#pragma once

#include "Tilengine.h"

void DrawbridgeInit(int layer, int hinge_x, int hinge_y);
void DrawbridgeSetProgress(float progress);
float DrawbridgeGetProgress(void);
void DrawbridgeSetHinge(int hinge_x, int hinge_y);
void DrawbridgeTasks();
