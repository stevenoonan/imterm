#pragma once
#include <array>
#include <cstdint>
#include "terminal_types.h"

namespace imterm {
using TerminalPalette = std::array<uint32_t, static_cast<size_t>(PaletteIndex::Max)>;
const TerminalPalette& GetDarkTerminalPalette();
const TerminalPalette& GetLightTerminalPalette();
const TerminalPalette& GetRetroBlueTerminalPalette();
} // namespace imterm
