#pragma once

namespace TouchHook {

bool Install();
void Uninstall();

float X();
float Y();
float RawX();
float RawY();
bool Down();
int Source();

void SetDisplaySize(float w, float h);
void SetRotMode(int mode);   // -1 auto, 0 none, 1..3 rotations
int RotMode();

}
