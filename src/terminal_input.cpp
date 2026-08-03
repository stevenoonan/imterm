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

} // namespace imterm
