#include <gtest/gtest.h>

#include <array>
#include <string_view>
#include <utility>

#include "terminal_input.h"

namespace {

TEST(TerminalInputTest, EncodesSpecialKeys)
{
    using imterm::TerminalKey;
    const std::array cases{
        std::pair{TerminalKey::Backspace, std::string_view("\x7F")},
        std::pair{TerminalKey::Enter, std::string_view("\n")},
        std::pair{TerminalKey::Tab, std::string_view("\t")},
        std::pair{TerminalKey::Up, std::string_view("\x1B[A")},
        std::pair{TerminalKey::Down, std::string_view("\x1B[B")},
        std::pair{TerminalKey::Right, std::string_view("\x1B[C")},
        std::pair{TerminalKey::Left, std::string_view("\x1B[D")},
        std::pair{TerminalKey::Home, std::string_view("\x1B[1~")},
        std::pair{TerminalKey::Delete, std::string_view("\x1B[3~")},
        std::pair{TerminalKey::End, std::string_view("\x1B[4~")},
    };

    for (const auto& [key, expected] : cases) {
        EXPECT_EQ(imterm::GetTerminalKeySequence(key), expected);
    }
}

TEST(TerminalInputTest, EncodesAlphabeticControlCharacters)
{
    ASSERT_TRUE(imterm::GetControlCharacter('a').has_value());
    ASSERT_TRUE(imterm::GetControlCharacter('Z').has_value());
    EXPECT_EQ(*imterm::GetControlCharacter('a'), 0x01);
    EXPECT_EQ(*imterm::GetControlCharacter('c'), 0x03);
    EXPECT_EQ(*imterm::GetControlCharacter('Z'), 0x1A);
    EXPECT_FALSE(imterm::GetControlCharacter('1').has_value());
}

TEST(TerminalInputTest, UiShortcutsResolveToOneActionWithoutDeviceBytes)
{
    using namespace imterm;
    for (const auto& [letter, action] : {std::pair{'a', KeyAction::SelectAll}, {'v', KeyAction::Paste}, {'f', KeyAction::None}}) {
        const auto result = ResolveTerminalKey(ViewKey::Letter, {true, false, false}, letter);
        EXPECT_EQ(result.action, action);
        EXPECT_TRUE(result.bytes.empty());
    }
    EXPECT_EQ(ResolveTerminalKey(ViewKey::Insert, {true, false, false}).action, KeyAction::Copy);
    EXPECT_EQ(ResolveTerminalKey(ViewKey::Insert, {false, true, false}).action, KeyAction::Paste);
    EXPECT_EQ(ResolveTerminalKey(ViewKey::Zoom, {true, false, false}).action, KeyAction::None);
}

TEST(TerminalInputTest, CtrlAlphabetIncludesZAndLeavesInterruptAvailable)
{
    using namespace imterm;
    for (char letter = 'a'; letter <= 'z'; ++letter) {
        // Shift avoids the unshifted Select All/Paste shortcuts. Find remains
        // reserved, matching the controller's existing ownership of F.
        const auto result = ResolveTerminalKey(ViewKey::Letter, {true, true, false}, letter);
        if (letter == 'f') {
            EXPECT_EQ(result.action, KeyAction::None);
            EXPECT_TRUE(result.bytes.empty());
            continue;
        }
        EXPECT_EQ(result.action, KeyAction::Send);
        EXPECT_EQ(result.bytes, std::string(1, static_cast<char>(letter & 0x1f)));
    }
    EXPECT_EQ(ResolveTerminalKey(ViewKey::Letter, {true, false, false}, 'c').bytes, "\x03");
    EXPECT_EQ(ResolveTerminalKey(ViewKey::Letter, {true, false, false}, 'z').bytes, "\x1a");
    EXPECT_EQ(ResolveTerminalKey(ViewKey::Letter, {true, false, true}, 'c').action, KeyAction::None);
}

TEST(TerminalInputTest, NavigationAndDeviceKeysHaveDistinctModifierRules)
{
    using namespace imterm;
    EXPECT_EQ(ResolveTerminalKey(ViewKey::Home).bytes, "\x1b[1~");
    EXPECT_EQ(ResolveTerminalKey(ViewKey::End).bytes, "\x1b[4~");
    EXPECT_EQ(ResolveTerminalKey(ViewKey::Home, {true, true, false}).action, KeyAction::Top);
    EXPECT_EQ(ResolveTerminalKey(ViewKey::End, {true, false, false}).action, KeyAction::Bottom);
    EXPECT_EQ(ResolveTerminalKey(ViewKey::PageUp, {false, true, false}).action, KeyAction::PageUp);
    EXPECT_EQ(ResolveTerminalKey(ViewKey::PageDown).action, KeyAction::PageDown);
    EXPECT_EQ(ResolveTerminalKey(ViewKey::Left, {true, false, false}).bytes, "\x1b[D");
    EXPECT_EQ(ResolveTerminalKey(ViewKey::Delete, {false, true, false}).action, KeyAction::None);
    EXPECT_EQ(ResolveTerminalKey(ViewKey::Up, {true, false, false}).action, KeyAction::None);
}

} // namespace
