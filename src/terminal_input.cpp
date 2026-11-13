#include "terminal_input.h"

#include <stdexcept>

namespace imterm {

std::string_view GetTerminalKeySequence(TerminalKey key)
{
    switch (key) {
    case TerminalKey::Backspace:
        return "\x7F";
    case TerminalKey::Enter:
        return "\n";
    case TerminalKey::Tab:
        return "\t";
    case TerminalKey::Up:
        return "\x1B[A";
    case TerminalKey::Down:
        return "\x1B[B";
    case TerminalKey::Right:
        return "\x1B[C";
    case TerminalKey::Left:
        return "\x1B[D";
    case TerminalKey::Home:
        return "\x1B[1~";
    case TerminalKey::Delete:
        return "\x1B[3~";
    case TerminalKey::End:
        return "\x1B[4~";
    }

    throw std::invalid_argument("Unknown terminal key");
}

std::optional<char> GetControlCharacter(char ascii_character)
{
    if (ascii_character >= 'a' && ascii_character <= 'z') {
        return static_cast<char>(ascii_character & 0x1F);
    }
    if (ascii_character >= 'A' && ascii_character <= 'Z') {
        return static_cast<char>(ascii_character & 0x1F);
    }
    return std::nullopt;
}

KeyResult ResolveTerminalKey(ViewKey key, KeyModifiers modifiers, char letter)
{
    const auto [shortcut, shift, alternate] = modifiers;
    if (alternate) return {};
    const auto send = [](TerminalKey value) {
        return KeyResult{KeyAction::Send, std::string(GetTerminalKeySequence(value))};
    };
    switch (key) {
    case ViewKey::Letter:
        if (!shortcut) return {};
        if (letter >= 'A' && letter <= 'Z') letter = static_cast<char>(letter - 'A' + 'a');
        if (letter == 'f') return {}; // Capture owns Find, including its modifiers.
        if (!shift) {
            if (letter == 'v') return {KeyAction::Paste, {}};
            if (letter == 'a') return {KeyAction::SelectAll, {}};
        }
        if (const auto control = GetControlCharacter(letter)) return {KeyAction::Send, std::string(1, *control)};
        return {};
    case ViewKey::Insert:
        if (shortcut && !shift) return {KeyAction::Copy, {}};
        if (!shortcut && shift) return {KeyAction::Paste, {}};
        return {};
    case ViewKey::Delete: return !shortcut && !shift ? send(TerminalKey::Delete) : KeyResult{};
    case ViewKey::Backspace: return !shortcut && !shift ? send(TerminalKey::Backspace) : KeyResult{};
    case ViewKey::Enter: return !shortcut && !shift ? send(TerminalKey::Enter) : KeyResult{};
    case ViewKey::Tab: return !shortcut ? send(TerminalKey::Tab) : KeyResult{};
    case ViewKey::Up: return !shortcut ? send(TerminalKey::Up) : KeyResult{};
    case ViewKey::Down: return !shortcut ? send(TerminalKey::Down) : KeyResult{};
    case ViewKey::Left: return send(TerminalKey::Left);
    case ViewKey::Right: return send(TerminalKey::Right);
    case ViewKey::PageUp: return {KeyAction::PageUp, {}};
    case ViewKey::PageDown: return {KeyAction::PageDown, {}};
    case ViewKey::Home: return shortcut ? KeyResult{KeyAction::Top, {}} : send(TerminalKey::Home);
    case ViewKey::End: return shortcut ? KeyResult{KeyAction::Bottom, {}} : send(TerminalKey::End);
    case ViewKey::Zoom: return {}; // Capture owns interface zoom.
    }
    return {};
}

} // namespace imterm
