#pragma once

#include <optional>
#include <string_view>

namespace imterm {

enum class TerminalKey {
    Backspace,
    Enter,
    Tab,
    Up,
    Down,
    Right,
    Left,
    Home,
    Delete,
    End,
};

std::string_view GetTerminalKeySequence(TerminalKey key);
std::optional<char> GetControlCharacter(char ascii_character);

} // namespace imterm
