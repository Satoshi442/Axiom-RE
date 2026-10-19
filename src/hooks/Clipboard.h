#pragma once

#include <string>

namespace Clipboard {

// Copy text. Tries, in order: the preloader's own setClipboardText (uses the
// launcher's real Activity), then direct JNI, then keeps an in-app copy.
// Returns true only if the ANDROID system clipboard was updated.
bool Set(const char* text);

// Read text. Uses the system clipboard when it is in sync with our last copy;
// otherwise the in-app copy, so Copy -> Paste inside Axiom always works.
std::string Get();

// One-line description of the last Set/Get result (shown in Settings).
const char* Status();

// Point ImGui's own clipboard callbacks at this module (ImGui 1.90.x API).
void InstallImGui();

}
