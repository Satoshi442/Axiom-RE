#pragma once

#include <string>

namespace Clipboard {

// Copy text to the Android system clipboard (also kept in an internal copy,
// so in-app copy/paste still works if the JNI route fails).
bool Set(const char* text);

// Read the system clipboard; falls back to the internal copy.
std::string Get();

// Point ImGui's own clipboard callbacks at this module, so ImGui's built-in
// InputText menu (Cut / Copy / Paste / Select all) uses the system clipboard.
void InstallImGui();

}
