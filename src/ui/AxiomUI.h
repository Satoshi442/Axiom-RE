#pragma once

#include <string>

namespace AxiomUI {

void Init(const std::string& modRoot);

bool IsVisible();
void Toggle();
void Show();
void Hide();
void Draw();

// Any thread: is this screen point on the Axiom window or the AX ball?
// TouchHook uses it to swallow touches that start there.
bool HitTest(float x, float y);

}
