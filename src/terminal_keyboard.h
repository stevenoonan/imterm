#pragma once
#include <vector>
#include "terminal_input.h"

namespace imterm {
// ImGui adapter only. Modifier policy and byte sequences live in terminal_input.
struct KeyboardAction { KeyResult result; bool extendSelection = false; };
std::vector<KeyboardAction> ReadTerminalKeyboard();
} // namespace imterm
