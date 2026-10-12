#pragma once

namespace Keyboard {

bool Install();
void Frame();
void PumpMainThreadJobs();

const char* Status();
int ShowCalls();

void ShowOfficial();
void HideOfficial();
void ShowJni();
void HideJni();

}
