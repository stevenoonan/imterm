#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "terminal_data.h"
#include "terminal_state.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* input, size_t size)
{
    auto data = std::make_shared<imterm::TerminalData>();
    imterm::TerminalState state(
        data, imterm::TerminalState::NewLineMode::Strict);
    state.SetBounds(Coordinates(24, 79));
    state.Input(std::span<const uint8_t>(input, size));
    return 0;
}
