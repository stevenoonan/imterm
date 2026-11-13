#pragma once

#include <optional>
#include <string>
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

enum class ViewKey {
    Letter, Delete, Backspace, Enter, Tab, Insert, Up, Down, Left, Right,
    PageUp, PageDown, Home, End, Zoom
};
enum class KeyAction { None, Send, Copy, Paste, SelectAll, PageUp, PageDown, Top, Bottom };
struct KeyModifiers { bool shortcut = false; bool shift = false; bool alternate = false; };
struct KeyResult { KeyAction action = KeyAction::None; std::string bytes; };

// One physical key resolves to one action. UI shortcuts never also send a
// control byte; ordinary Ctrl+C remains the device's interrupt character.
KeyResult ResolveTerminalKey(ViewKey key, KeyModifiers modifiers = {}, char letter = 0);

} // namespace imterm
