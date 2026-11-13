#include "terminal_keyboard.h"
#include "imgui.h"
#include "utf8.h"

namespace imterm {
std::vector<KeyboardAction> ReadTerminalKeyboard()
{
    std::vector<KeyboardAction> result;
    if (!ImGui::IsWindowFocused()) return result;
    auto& io = ImGui::GetIO();
    io.WantCaptureKeyboard = true;
    io.WantTextInput = true;
    const KeyModifiers modifiers{io.ConfigMacOSXBehaviors ? io.KeySuper : io.KeyCtrl,
        io.KeyShift, io.ConfigMacOSXBehaviors ? io.KeyCtrl : io.KeyAlt};
    const auto add = [&](ViewKey key, char letter = 0) {
        auto action = ResolveTerminalKey(key, modifiers, letter);
        if (action.action != KeyAction::None) result.push_back({std::move(action), modifiers.shift});
    };
    static constexpr std::pair<ImGuiKey, ViewKey> keys[] = {
        {ImGuiKey_Delete, ViewKey::Delete}, {ImGuiKey_Backspace, ViewKey::Backspace},
        {ImGuiKey_Enter, ViewKey::Enter}, {ImGuiKey_KeypadEnter, ViewKey::Enter},
        {ImGuiKey_Tab, ViewKey::Tab}, {ImGuiKey_Insert, ViewKey::Insert},
        {ImGuiKey_UpArrow, ViewKey::Up}, {ImGuiKey_DownArrow, ViewKey::Down},
        {ImGuiKey_LeftArrow, ViewKey::Left}, {ImGuiKey_RightArrow, ViewKey::Right},
        {ImGuiKey_PageUp, ViewKey::PageUp}, {ImGuiKey_PageDown, ViewKey::PageDown},
        {ImGuiKey_Home, ViewKey::Home}, {ImGuiKey_End, ViewKey::End}
    };
    for (const auto& [physical, key] : keys) if (ImGui::IsKeyPressed(physical)) add(key);
    for (int key = ImGuiKey_A; key <= ImGuiKey_Z; ++key) {
        if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(key)))
            add(ViewKey::Letter, static_cast<char>('a' + key - ImGuiKey_A));
    }
    // AltGr is commonly reported as Ctrl+Alt. Its text events must survive
    // while the key policy suppresses the accompanying control-byte mapping.
    if (!modifiers.shortcut || modifiers.alternate) {
        for (const auto scalar : io.InputQueueCharacters)
            result.push_back({{KeyAction::Send, utf8::Encode(static_cast<uint32_t>(scalar))}, false});
    }
    return result;
}
} // namespace imterm
