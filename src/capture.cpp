#include <vector>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <string>
#include <iomanip>
#include <initializer_list>
#include <sstream>
#include <fstream>
#include <streambuf>
#include <string>
#include <string_view>
#include <filesystem>
#include <memory>

#include "imgui.h"
#include "capture.h"
#include "connection_dialog_layout.h"
#include "serial/serial.h"
#include "terminal_view.h"
#include "capture_session.h"
#include "ImGuiFileDialog.h"

using namespace std::chrono;
using namespace imterm;
using namespace serial;
using namespace ImGui;
using namespace std::literals;

namespace imterm {

struct Capture::Impl {
    CaptureSession session;
    std::shared_ptr<TerminalData> term_data = session.GetData();
    std::shared_ptr<TerminalState> term_state = session.GetTerminalState();
    TerminalLogger* term_log = &session.GetLogger();
    std::unique_ptr<TerminalView> term_view = std::make_unique<TerminalView>(
        term_data, term_state, TerminalView::Options());
    RawSerialCapture& raw_capture = session.GetRawCapture();
    ImGuiFileDialog raw_capture_dialog;
    CaptureSettings settings;
    SettingsValues draft;
    std::array<char, 11> baud_text{};
    std::string setup_error;
    ImGuiFileDialog log_path_dialog;
    bool& auto_scroll = session.autoScroll;
    std::string serial_name = "[Not Connected]";

    explicit Impl(CaptureSession::TransportFactory factory, std::filesystem::path settingsPath)
        : session(std::move(factory)), settings(std::move(settingsPath)) {
        settings.Load();
        draft = settings.Get();
        SetBaudText(draft.serial.baudrate);
        ApplyPreferences(draft);
    }

    void SetBaudText(uint32_t baud) {
        const auto text = std::to_string(baud);
        baud_text.fill(0);
        std::copy_n(text.begin(), std::min(text.size(), baud_text.size() - 1), baud_text.begin());
    }

    void ApplyPreferences(const SettingsValues& values) {
        term_state->SetNewLineMode(values.newlineMode);
        term_log->SetBasePath(values.logPath);
        term_log->SetOptions(values.logging);
    }

    bool Connect(const SettingsValues& values) {
        if (session.IsBusy()) return false;
        setup_error = CaptureSettings::Validate(values);
        if (!setup_error.empty() || !settings.Set(values)) return false;
        if (!session.Connect(settings.Get().serial)) return false;
        draft = settings.Get();
        SetBaudText(draft.serial.baudrate);
        ApplyPreferences(draft);
        if (draft.zoomFactor != GetInterfaceZoomFactor()) interface_zoom.pending = draft.zoomFactor;
        settings.Save();
        return true;
    }

    void SavePreferences() {
        auto values = settings.Get();
        values.newlineMode = term_state->GetNewLineMode();
        values.logging = term_log->GetOptions();
        values.logPath = term_log->GetBasePath();
        values.zoomFactor = GetInterfaceZoomFactor();
        if (settings.Set(values)) settings.Save();
        draft.newlineMode = values.newlineMode;
        draft.logging = values.logging;
        draft.logPath = values.logPath;
        draft.zoomFactor = values.zoomFactor;
    }

    void Update() {
        const bool received = session.Poll();
        if (term_state != session.GetTerminalState()) {
            const auto options = term_view->GetOptions();
            term_state = session.GetTerminalState();
            term_view = std::make_unique<TerminalView>(term_data, term_state, options);
            find_state.owner = nullptr;
            find_state.active_match.reset();
        }
        if (received && auto_scroll && !find_state.open) term_view->SetCursorToEnd();
        serial_name = session.GetParameters() ? session.GetParameters()->port : "[Not Connected]";
    }

    void StopRawCapture() {
        if (!raw_capture.Stop()) {
            std::cerr << raw_capture.GetError() << '\n';
        }
    }

    void OpenRawCaptureDialog() {
        const auto directory = raw_capture.GetPath().empty()
            ? std::filesystem::current_path()
            : raw_capture.GetPath().parent_path();
        raw_capture_dialog.OpenDialog(
            "RawRxCapture", "Save raw RX capture", ".bin", directory.string(),
            "raw-rx.bin", 1, nullptr,
            ImGuiFileDialogFlags_Modal | ImGuiFileDialogFlags_ConfirmOverwrite);
    }

    void ShowRawCaptureDialog() {
        if (raw_capture_dialog.Display("RawRxCapture", ImGuiWindowFlags_NoCollapse,
                ImVec2(600, 350))) {
            if (raw_capture_dialog.IsOk()
                && !raw_capture.Start(raw_capture_dialog.GetFilePathName())) {
                std::cerr << raw_capture.GetError() << '\n';
            }
            raw_capture_dialog.Close();
        }
    }

    void RawCaptureStatus() {
        if (raw_capture.IsActive()) {
            ImGui::Text("Raw RX: %llu bytes | %s",
                static_cast<unsigned long long>(raw_capture.GetByteCount()),
                raw_capture.GetPath().filename().string().c_str());
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", raw_capture.GetPath().string().c_str());
            }
        }
        if (!raw_capture.GetError().empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                "Raw capture error (hover for details)");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", raw_capture.GetError().c_str());
            }
        }
    }

    void ConnectionError() {
        if (session.GetError().empty()) return;
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
        ImGui::TextWrapped("%s", session.GetError().c_str());
        ImGui::PopStyleColor();
    }


    static constexpr std::chrono::seconds port_cache_duration = 1s;

    struct InterfaceZoomState {
        float current = DefaultInterfaceZoom;
        std::optional<float> pending;
        std::optional<ImGuiStyle> dpi_scaled_base_style;
        std::optional<int> preserved_top_line;
        float wheel_accumulator = 0.0f;
    };

    InterfaceZoomState interface_zoom;

    float GetInterfaceZoomFactor() {
        return interface_zoom.pending.value_or(interface_zoom.current);
    }

    void InitializeInterfaceZoom() {
        interface_zoom.dpi_scaled_base_style = ImGui::GetStyle();
        interface_zoom.current = settings.Get().zoomFactor;
        ImGuiStyle& style = ImGui::GetStyle();
        style = *interface_zoom.dpi_scaled_base_style;
        style.ScaleAllSizes(interface_zoom.current);
        style.FontScaleMain = interface_zoom.current;
    }

    void RequestInterfaceZoom(float factor) {
        const float normalized = NormalizeInterfaceZoom(factor);
        if (normalized == GetInterfaceZoomFactor()) {
            return;
        }

        if (!interface_zoom.preserved_top_line && term_view) {
            const auto& geometry = term_view->GetLastRenderGeometry();
            if (geometry.mValid) {
                interface_zoom.preserved_top_line = geometry.mFirstVisibleLineNo;
            }
        }
        interface_zoom.pending = normalized;
        SavePreferences();
    }

    void ApplyPendingInterfaceZoom() {
        if (!interface_zoom.pending) {
            return;
        }
        if (!interface_zoom.dpi_scaled_base_style) {
            InitializeInterfaceZoom();
        }

        interface_zoom.current = *interface_zoom.pending;
        interface_zoom.pending.reset();
        ImGuiStyle& style = ImGui::GetStyle();
        style = *interface_zoom.dpi_scaled_base_style;
        style.ScaleAllSizes(interface_zoom.current);
        style.FontScaleMain = interface_zoom.current;

        if (interface_zoom.preserved_top_line && term_view) {
            term_view->SetTopVisibleLine(*interface_zoom.preserved_top_line);
        }
        interface_zoom.preserved_top_line.reset();
    }

    void QueueInterfaceZoomWheel(float wheel_delta) {
        interface_zoom.wheel_accumulator += wheel_delta;
        float requested = GetInterfaceZoomFactor();
        while (interface_zoom.wheel_accumulator >= 1.0f) {
            requested = StepInterfaceZoom(requested, 1);
            interface_zoom.wheel_accumulator -= 1.0f;
        }
        while (interface_zoom.wheel_accumulator <= -1.0f) {
            requested = StepInterfaceZoom(requested, -1);
            interface_zoom.wheel_accumulator += 1.0f;
        }
        RequestInterfaceZoom(requested);
    }

    void HandleInterfaceZoomShortcuts() {
        ImGuiIO& io = ImGui::GetIO();
        const bool modifier = io.ConfigMacOSXBehaviors
            ? io.KeySuper : io.KeyCtrl;
        const bool alt = io.ConfigMacOSXBehaviors
            ? io.KeyCtrl : io.KeyAlt;
        if (!modifier || alt) {
            return;
        }

        float requested = GetInterfaceZoomFactor();
        bool keyboard_request = false;
        if (ImGui::IsKeyPressed(ImGuiKey_Equal)
            || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd)) {
            requested = StepInterfaceZoom(requested, 1);
            keyboard_request = true;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_Minus)
            || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract)) {
            requested = StepInterfaceZoom(requested, -1);
            keyboard_request = true;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_0)
            || ImGui::IsKeyPressed(ImGuiKey_Keypad0)) {
            requested = DefaultInterfaceZoom;
            keyboard_request = true;
        }

        if (keyboard_request) {
            RequestInterfaceZoom(requested);
        }
    }

    struct FindState {
        bool open = false;
        bool request_focus = false;
        bool match_case = false;
        size_t current_match = 0;
        size_t total_matches = 0;
        std::array<char, 1024> query{};
        Coordinates anchor;
        std::optional<TextRange> active_match;
        const TerminalView* owner = nullptr;
    };

    FindState find_state;

    void RefreshFindStatus() {
        const TextMatchStatus status = term_data->GetTextMatchStatus(
            find_state.query.data(), find_state.active_match,
            find_state.match_case);
        find_state.current_match = status.mCurrent;
        find_state.total_matches = status.mTotal;
    }

    void ApplyFindResult(std::optional<TextRange> result) {
        find_state.active_match = result;
        if (result) {
            term_view->SetSelection(result->mStart, result->mEnd);
            term_view->Reveal(result->mStart);
        }
        else {
            term_view->ClearSelection();
        }
        RefreshFindStatus();
    }

    void FindFromAnchor() {
        const std::string_view query(find_state.query.data());
        if (query.empty()) {
            find_state.active_match.reset();
            find_state.current_match = 0;
            find_state.total_matches = 0;
            term_view->ClearSelection();
            return;
        }
        ApplyFindResult(term_data->FindNextText(
            query, find_state.anchor, find_state.match_case));
    }

    void FindNext() {
        const std::string_view query(find_state.query.data());
        if (query.empty()) {
            FindFromAnchor();
            return;
        }
        const Coordinates from = find_state.active_match
            ? find_state.active_match->mEnd
            : find_state.anchor;
        ApplyFindResult(term_data->FindNextText(
            query, from, find_state.match_case));
    }

    void FindPrevious() {
        const std::string_view query(find_state.query.data());
        if (query.empty()) {
            FindFromAnchor();
            return;
        }
        const Coordinates before = find_state.active_match
            ? find_state.active_match->mStart
            : find_state.anchor;
        ApplyFindResult(term_data->FindPreviousText(
            query, before, find_state.match_case));
    }

    void OpenFind() {
        if (!term_view || !term_data) {
            return;
        }
        if (find_state.open) {
            find_state.request_focus = true;
            return;
        }

        find_state.open = true;
        find_state.request_focus = true;
        find_state.active_match.reset();
        find_state.owner = term_view.get();
        find_state.anchor = term_view->HasSelection()
            ? term_view->GetSelectionEnd()
            : term_view->GetCursorPosition();

        if (term_view->HasSelection()
            && term_view->GetSelectionStart().mLine
                == term_view->GetSelectionEnd().mLine) {
            const std::string selected = term_view->GetSelectedText();
            if (!selected.empty() && selected.size() < find_state.query.size()) {
                find_state.query.fill('\0');
                std::copy(selected.begin(), selected.end(), find_state.query.begin());
            }
        }

        FindFromAnchor();
    }

    void CloseFind() {
        find_state.open = false;
        find_state.request_focus = false;
    }

    void FindBar() {
        if (!find_state.open || !term_view || !term_data) {
            return;
        }

        if (find_state.owner != term_view.get()) {
            find_state.owner = term_view.get();
            find_state.anchor = term_view->GetCursorPosition();
            find_state.active_match.reset();
            FindFromAnchor();
        }
        else if (find_state.active_match
            && !term_data->TextMatches(
                *find_state.active_match, find_state.query.data(),
                find_state.match_case)) {
            find_state.active_match.reset();
            term_view->ClearSelection();
            RefreshFindStatus();
        }
        else if (term_view->IsTextChanged()) {
            RefreshFindStatus();
        }

        ImGui::PushID("FindBar");
        const float controls_width = ImGui::CalcTextSize(
            "Previous  Next  Match Case  X  000 of 000").x
            + ImGui::GetStyle().ItemSpacing.x * 7.0f;
        ImGui::SetNextItemWidth(std::max(
            120.0f, ImGui::GetContentRegionAvail().x - controls_width));
        if (find_state.request_focus) {
            ImGui::SetKeyboardFocusHere();
        }
        const bool query_changed = ImGui::InputTextWithHint(
            "##Query", "Find text", find_state.query.data(),
            find_state.query.size(), ImGuiInputTextFlags_AutoSelectAll);
        const bool query_active = ImGui::IsItemActive();
        const bool enter_pressed = query_active
            && (ImGui::IsKeyPressed(ImGuiKey_Enter)
                || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter));
        const bool escape_pressed = ImGui::IsKeyPressed(ImGuiKey_Escape);
        find_state.request_focus = false;

        ImGui::SameLine();
        const bool previous_pressed = ImGui::Button("Previous");
        ImGui::SameLine();
        const bool next_pressed = ImGui::Button("Next");
        ImGui::SameLine();
        const bool case_changed = ImGui::Checkbox(
            "Match Case", &find_state.match_case);
        ImGui::SameLine();
        const bool close_pressed = ImGui::Button("X");

        if (query_changed || case_changed) {
            FindFromAnchor();
        }
        else if (previous_pressed || (enter_pressed && ImGui::GetIO().KeyShift)) {
            FindPrevious();
        }
        else if (next_pressed || enter_pressed) {
            FindNext();
        }

        if (find_state.query[0] != '\0') {
            ImGui::SameLine();
            if (find_state.total_matches == 0) {
                ImGui::TextColored(
                    ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "0 of 0");
            }
            else {
                ImGui::Text(
                    "%zu of %zu", find_state.current_match,
                    find_state.total_matches);
            }
        }

        if (close_pressed || escape_pressed) {
            CloseFind();
        }
        ImGui::PopID();
    }

    void CaptureWindowCreate(void) {
        ImGui::Begin(serial_name.c_str(), nullptr, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoTitleBar);

        HandleInterfaceZoomShortcuts();

        ImGuiIO& io = ImGui::GetIO();
        const bool find_modifier = io.ConfigMacOSXBehaviors
            ? io.KeySuper : io.KeyCtrl;
        const bool find_alt = io.ConfigMacOSXBehaviors
            ? io.KeyCtrl : io.KeyAlt;
        if (term_view && find_modifier && !io.KeyShift && !find_alt
            && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
            && ImGui::IsKeyPressed(ImGuiKey_F)) {
            OpenFind();
        }

        Menu();

        FindBar();

        RawCaptureStatus();

        ConnectionError();
        SettingsStatus();

        if (term_view) {
            term_view->Render("TerminalView");
        }

        ImGui::End();

        if (session.GetState() == CaptureSession::State::Connected) ShowRawCaptureDialog();

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ReconnectionWindow(viewport->ID);
        if (!session.IsBusy() && session.GetState() != CaptureSession::State::Connected) {
            PortSelectionWindow(viewport->ID);
        }
    }

    void Menu() {

        if (ImGui::BeginMenuBar())
        {

            const auto& parameters = session.GetParameters();
            const std::string menu_port_string = parameters ? parameters->port : "No Connection";
            if (ImGui::BeginMenu(menu_port_string.c_str())) {
                if (ImGui::MenuItem("Auto Reconnect", nullptr, session.GetAutoReconnect()))
                    session.SetAutoReconnect(!session.GetAutoReconnect());
                if (ImGui::MenuItem("Parameters...", nullptr, false, !session.IsBusy()))
                    session.BeginReconfiguration();
                if (ImGui::MenuItem("Disconnect", nullptr, false, session.CanSend()))
                    session.Disconnect();
                if (parameters && ImGui::BeginMenu("Select New Port", session.CanSend())) {
                    auto ports = serial::list_ports_cached(port_cache_duration);
                    for (const auto& info : std::get<1>(ports)) {
                        const bool current = info.port == parameters->port;
                        if (ImGui::MenuItem(info.port.c_str(), nullptr, current) && !current) {
                            auto next = settings.Get();
                            next.serial = *parameters;
                            next.serial.port = info.port;
                            Connect(next);
                        }
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }

            const char * autoScrollOn = "Auto Scroll On";
            const char * autoScrollOff = "Auto Scroll Off";

            if (ImGui::MenuItem(auto_scroll?autoScrollOn:autoScrollOff)) {
                auto_scroll = !auto_scroll;
            }

            if (ImGui::MenuItem("Clear", nullptr, false, term_view != nullptr)) {
                term_view->ClearContents();
                find_state.active_match.reset();
                find_state.current_match = 0;
                find_state.total_matches = 0;
                find_state.anchor = Coordinates();
            }

            if (ImGui::MenuItem("Find", nullptr, false, term_view != nullptr)) {
                OpenFind();
            }

            if (ImGui::BeginMenu("Options")) {


                if (ImGui::BeginMenu("New Line Mode"))
                {

                    auto new_line_mode = term_state->GetNewLineMode();

                    if (ImGui::MenuItem("Strict", NULL, (new_line_mode == TerminalState::NewLineMode::Strict))) {
                        term_state->SetNewLineMode(TerminalState::NewLineMode::Strict);
                        SavePreferences();
                    }
                    if (ImGui::MenuItem("Add CR to LF", NULL, (new_line_mode == TerminalState::NewLineMode::AddCrToLf))) {
                        term_state->SetNewLineMode(TerminalState::NewLineMode::AddCrToLf);
                        SavePreferences();
                    }
                    if (ImGui::MenuItem("ADD LF to CR", NULL, (new_line_mode == TerminalState::NewLineMode::AddLfToCr))) {
                        term_state->SetNewLineMode(TerminalState::NewLineMode::AddLfToCr);
                        SavePreferences();
                    }

                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("View"))
                {
                    if (ImGui::BeginMenu("Zoom")) {
                        const bool mac_shortcuts = ImGui::GetIO().ConfigMacOSXBehaviors;
                        if (ImGui::MenuItem(
                                "Zoom In", mac_shortcuts ? "Cmd++" : "Ctrl++")) {
                            RequestInterfaceZoom(StepInterfaceZoom(
                                GetInterfaceZoomFactor(), 1));
                        }
                        if (ImGui::MenuItem(
                                "Zoom Out", mac_shortcuts ? "Cmd+-" : "Ctrl+-")) {
                            RequestInterfaceZoom(StepInterfaceZoom(
                                GetInterfaceZoomFactor(), -1));
                        }
                        if (ImGui::MenuItem(
                                "Reset Zoom", mac_shortcuts ? "Cmd+0" : "Ctrl+0")) {
                            RequestInterfaceZoom(DefaultInterfaceZoom);
                        }

                        ImGui::Separator();
                        const float current_zoom = GetInterfaceZoomFactor();
                        for (float factor : InterfaceZoomLevels) {
                            const std::string label = std::to_string(
                                static_cast<int>(std::lround(factor * 100.0f))) + "%";
                            if (ImGui::MenuItem(
                                    label.c_str(), nullptr, current_zoom == factor)) {
                                RequestInterfaceZoom(factor);
                            }
                        }
                        ImGui::EndMenu();
                    }

                    if (term_view) {
                        auto ops = term_view->GetOptions();
                        if (ImGui::MenuItem("Line Numbers", NULL, ops.LineNumbers, true)) {
                            ops.LineNumbers = !ops.LineNumbers;
                            term_view->SetOptions(ops);
                        }
                        if (ImGui::MenuItem("Timestamps", NULL, ops.TimeStamps, true)) {
                            ops.TimeStamps = !ops.TimeStamps;
                            term_view->SetOptions(ops);
                        }
                    }

                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("Log"))
                {
                    auto ops = term_log->GetOptions();
                    if (ImGui::MenuItem("Enabled", NULL, ops.Enabled, true)) {
                        ops.Enabled = !ops.Enabled;
                        term_log->Close();
                        term_log->SetOptions(ops);
                        SavePreferences();
                    }
                    if (ImGui::MenuItem("Line Numbers", NULL, ops.LineNumbers, true)) {
                        ops.LineNumbers = !ops.LineNumbers;
                        term_log->SetOptions(ops);
                        SavePreferences();
                    }
                    if (ImGui::MenuItem("Timestamps", NULL, ops.TimeStamps, true)) {
                        ops.TimeStamps = !ops.TimeStamps;
                        term_log->SetOptions(ops);
                        SavePreferences();
                    }

                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu(raw_capture.IsActive() ? "Raw Capture On" : "Raw Capture")) {
                    if (ImGui::MenuItem("Start...", nullptr, false, !raw_capture.IsActive())) {
                        OpenRawCaptureDialog();
                    }
                    if (ImGui::MenuItem("Stop", nullptr, false, raw_capture.IsActive())) {
                        StopRawCapture();
                    }
                    ImGui::Separator();
                    ImGui::TextUnformatted("Received bytes, unchanged. No timestamps or TX.");
                    if (!raw_capture.GetPath().empty()) {
                        ImGui::Text("%llu bytes", static_cast<unsigned long long>(raw_capture.GetByteCount()));
                        ImGui::TextUnformatted(raw_capture.GetPath().string().c_str());
                    }
                    ImGui::EndMenu();
                }

                ImGui::EndMenu();
            }
            
            

            if (session.CanSend()) {
                if (ImGui::MenuItem(session.GetDTR() ? "DTR=1" : "DTR=0"))
                    session.SetDTR(!session.GetDTR());
                if (ImGui::MenuItem(session.GetRTS() ? "RTS=1" : "RTS=0", nullptr, false, session.CanSetRTS()))
                    session.SetRTS(!session.GetRTS());
                if (const auto& signals = session.GetSignals()) {
                    ImGui::MenuItem(signals->cts ? "CTS=1" : "CTS=0", nullptr, false, false);
                    ImGui::MenuItem(signals->dsr ? "DSR=1" : "DSR=0", nullptr, false, false);
                    ImGui::MenuItem(signals->dcd ? "DCD=1" : "DCD=0", nullptr, false, false);
                }
                else {
                    ImGui::MenuItem("Modem status unavailable", nullptr, false, false);
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                        ImGui::SetTooltip("%s", session.GetSignalError().c_str());
                }
            }

            ImGui::EndMenuBar();
        }
    }

    template<typename T>
    void DisplayCombo(const char* label, T& value, float offset, float width,
        std::initializer_list<ComboDataItem<T>> items) {
        ImGui::TextUnformatted(label);
        ImGui::SameLine();
        ImGui::SetCursorPosX(offset);
        ImGui::SetNextItemWidth(width);
        const auto selected = std::find_if(items.begin(), items.end(),
            [&](const auto& item) { return item.value == value; });
        const auto id = std::string("##") + label;
        if (ImGui::BeginCombo(id.c_str(), selected == items.end() ? "Select Value" : selected->label.c_str())) {
            for (const auto& item : items) {
                const bool current = item.value == value;
                if (ImGui::Selectable(item.label.c_str(), current)) value = item.value;
                if (current) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    void SettingsStatus() {
        if (!settings.GetWarnings().empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Configuration notice (hover)");
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
                for (const auto& message : settings.GetWarnings()) ImGui::TextWrapped("%s", message.c_str());
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        }
        for (const auto* message : std::initializer_list<const std::string*>{&setup_error, &settings.GetWriteError()}) {
            if (message->empty()) continue;
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.3f, 1.0f));
            ImGui::TextWrapped("%s", message->c_str());
            ImGui::PopStyleColor();
        }
    }

    void PortSelectionWindow(ImGuiID) {
        const auto layout = MeasureConnectionDialogLayout(4);
        const float offset = layout.input_width;
        const float width = layout.input_width;
        const auto& style = ImGui::GetStyle();
        const float window_width = offset + width + style.WindowPadding.x;
        const float wrap_width = window_width - style.WindowPadding.x * 2.0f;
        float status_height = settings.GetWarnings().empty() ? 0.0f : ImGui::GetTextLineHeightWithSpacing();
        for (const auto* text : std::initializer_list<const std::string*>{&session.GetError(), &setup_error, &settings.GetWriteError()}) {
            if (!text->empty())
                status_height += ImGui::CalcTextSize(text->c_str(), nullptr, false, wrap_width).y + style.ItemSpacing.y;
        }
        ImGui::OpenPopup("Setup");
        ImGui::SetNextWindowSize(ImVec2(window_width,
            layout.line_height * 12 + layout.extra_vertical_spacing + status_height), ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        if (!ImGui::BeginPopupModal("Setup", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) return;

        ConnectionError();
        SettingsStatus();
        // Keep a saved port selectable even while unplugged. A failed open then
        // produces a useful transport error rather than silently picking a port.
        const auto ports = serial::list_ports_cached(port_cache_duration);
        std::vector<ComboDataItem<std::string>> port_items;
        for (const auto& info : std::get<1>(ports)) port_items.emplace_back(info.port, info.port);
        if (std::none_of(port_items.begin(), port_items.end(),
            [&](const auto& item) { return item.value == draft.serial.port; }))
            port_items.emplace_back(draft.serial.port + " (not detected)", draft.serial.port);
        ImGui::TextUnformatted("Serial Port");
        ImGui::SameLine();
        ImGui::SetCursorPosX(offset);
        ImGui::SetNextItemWidth(width);
        if (ImGui::BeginCombo("##Serial Port", draft.serial.port.c_str())) {
            for (const auto& item : port_items) {
                const bool selected = item.value == draft.serial.port;
                if (ImGui::Selectable(item.label.c_str(), selected)) draft.serial.port = item.value;
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::TextUnformatted("Baud");
        ImGui::SameLine();
        ImGui::SetCursorPosX(offset);
        ImGui::SetNextItemWidth(width);
        ImGui::InputText("##Baud", baud_text.data(), baud_text.size());
        DisplayCombo<bytesize_t>("Data bits", draft.serial.bytesize, offset, width,
            {{"5", fivebits}, {"6", sixbits}, {"7", sevenbits}, {"8", eightbits}});
        DisplayCombo<stopbits_t>("Stop bits", draft.serial.stopbits, offset, width,
            {{"1", stopbits_one}, {"1.5", stopbits_one_point_five}, {"2", stopbits_two}});
        DisplayCombo<parity_t>("Parity", draft.serial.parity, offset, width,
            {{"None", parity_none}, {"Odd", parity_odd}, {"Even", parity_even}, {"Mark", parity_mark}, {"Space", parity_space}});
        DisplayCombo<flowcontrol_t>("Flow control", draft.serial.flowcontrol, offset, width,
            {{"None", flowcontrol_none}, {"Hardware", flowcontrol_hardware}, {"Software", flowcontrol_software}});
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::Spacing();
        DisplayCombo<TerminalState::NewLineMode>("New Line Mode", draft.newlineMode, offset, width,
            {{"Strict", TerminalState::NewLineMode::Strict}, {"Add CR to LF", TerminalState::NewLineMode::AddCrToLf},
             {"Add LF to CR", TerminalState::NewLineMode::AddLfToCr}});
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("If the device sends only CR or LF, select the mode\n"
                "that adds the missing character to start a new line.");
        ImGui::TextUnformatted("Enable logging");
        ImGui::SameLine();
        ImGui::SetCursorPosX(offset);
        ImGui::Checkbox("##enable_logging", &draft.logging.Enabled);
        ImGui::SameLine();
        if (ImGui::Button("Choose Path"))
            log_path_dialog.OpenDialog("LogPath", "Choose log directory", nullptr, draft.logPath.string(),
                1, nullptr, ImGuiFileDialogFlags_Modal);
        ImGui::TextUnformatted("Log directory (hover for path)");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", draft.logPath.string().c_str());
        if (log_path_dialog.Display("LogPath")) {
            if (log_path_dialog.IsOk()) draft.logPath = log_path_dialog.GetCurrentPath();
            log_path_dialog.Close();
        }
        ImGui::NewLine();
        if (ImGui::Button("Save", ImVec2(120, 0))) {
            const auto baud = CaptureSettings::ParseBaud(baud_text.data());
            if (!baud) setup_error = "Baud must be a whole number from 1 to 4294967295.";
            else {
                draft.serial.baudrate = *baud;
                if (Connect(draft)) ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }

    void ReconnectionWindow(ImGuiID) {
        for (const char* title : {"Connecting...", "Reconnecting..."}) {
            const bool active = session.IsBusy()
                && (session.GetState() == CaptureSession::State::Connecting
                    ? std::string_view(title) == "Connecting..."
                    : std::string_view(title) == "Reconnecting...");
            if (active) ImGui::OpenPopup(title);
            if (!ImGui::IsPopupOpen(title)) continue;
            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                if (!active) ImGui::CloseCurrentPopup();
                else {
                    ImGui::Text("%s %s", title, serial_name.c_str());
                    if (!session.GetError().empty()) ImGui::TextWrapped("%s", session.GetError().c_str());
                    if (ImGui::Button("Cancel")) {
                        session.Cancel();
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::EndPopup();
            }
        }
    }
};

    Capture::Capture(CaptureSession::TransportFactory factory, std::filesystem::path settingsPath)
        : mImpl(std::make_unique<Impl>(std::move(factory), std::move(settingsPath))) {}
    bool Capture::Connect(const SettingsValues& values) { return mImpl->Connect(values); }
    const CaptureSettings& Capture::GetSettings() const { return mImpl->settings; }
    Capture::~Capture() = default;
    void Capture::Render() { mImpl->CaptureWindowCreate(); }
    void Capture::Update() {
        // Keyboard bytes are produced by the previous frame. Drain them even
        // while disconnected so stale commands never reach another connection.
        std::vector<uint8_t> input;
        while (mImpl->term_view->KeyboardInputAvailable())
            input.push_back(mImpl->term_view->GetKeyboardInput());
        if (!input.empty()) mImpl->session.Send(input);
        mImpl->Update();
    }
    void Capture::Shutdown() { mImpl->session.Shutdown(); }
    CaptureSession& Capture::GetSession() { return mImpl->session; }
    float Capture::GetInterfaceZoomFactor() { return mImpl->GetInterfaceZoomFactor(); }
    void Capture::InitializeInterfaceZoom() { mImpl->InitializeInterfaceZoom(); }
    void Capture::ApplyPendingInterfaceZoom() { mImpl->ApplyPendingInterfaceZoom(); }
    void Capture::QueueInterfaceZoomWheel(float delta) { mImpl->QueueInterfaceZoomWheel(delta); }
}
