#pragma once

#include <string>

namespace AxiomUI {

void Init(const std::string& modRoot);

bool IsVisible();
void Toggle();
void Show();
void Hide();
void Draw();

}
