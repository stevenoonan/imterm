#include <vector>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <string>
#include <iomanip>
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
    bool enable_logging = false;
    bool& auto_scroll = session.autoScroll;
    std::string serial_name = "[Not Connected]";
    bool setup_saved = false;

    explicit Impl(CaptureSession::TransportFactory factory) : session(std::move(factory)) {}

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
        if (setup_saved && session.GetState() == CaptureSession::State::Connected) {
            settings.write();
            setup_saved = false;
        }
        else if (!session.IsBusy()) setup_saved = false;
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
        interface_zoom.current = settings.get_zoom_factor();
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
        settings.set_zoom_factor(normalized);
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
                            auto next = *parameters;
                            next.port = info.port;
                            session.Connect(next);
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
            
            if (ImGui::BeginMenu("Options")) {


                if (ImGui::BeginMenu("New Line Mode"))
                {

                    auto new_line_mode = term_state->GetNewLineMode();
                    const char* line_mode_text;
                    if (new_line_mode == TerminalState::NewLineMode::AddCrToLf) {
                        line_mode_text = "+LF   ";
                    }
                    else if (new_line_mode == TerminalState::NewLineMode::AddLfToCr) {
                        line_mode_text = "+CR   ";
                    }
                    else {
                        line_mode_text = "Strict";
                        new_line_mode = TerminalState::NewLineMode::Strict;
                    }

                    //ImGui::MenuItem("New Line Mode", NULL, false, false);

                    if (ImGui::MenuItem("Strict", NULL, (new_line_mode == TerminalState::NewLineMode::Strict))) {
                        term_state->SetNewLineMode(TerminalState::NewLineMode::Strict);
                    }
                    if (ImGui::MenuItem("Add CR to LF", NULL, (new_line_mode == TerminalState::NewLineMode::AddCrToLf))) {
                        term_state->SetNewLineMode(TerminalState::NewLineMode::AddCrToLf);
                    }
                    if (ImGui::MenuItem("ADD LF to CR", NULL, (new_line_mode == TerminalState::NewLineMode::AddLfToCr))) {
                        term_state->SetNewLineMode(TerminalState::NewLineMode::AddLfToCr);
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
                        enable_logging = ops.Enabled;
                        term_log->SetOptions(ops);
                    }
                    if (ImGui::MenuItem("Line Numbers", NULL, ops.LineNumbers, true)) {
                        ops.LineNumbers = !ops.LineNumbers;
                        term_log->SetOptions(ops);
                    }
                    if (ImGui::MenuItem("Timestamps", NULL, ops.TimeStamps, true)) {
                        ops.TimeStamps = !ops.TimeStamps;
                        term_log->SetOptions(ops);
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
    void DisplayCombo(ComboData<T>& combo_data) {

        ImGui::TextUnformatted(combo_data.get_label());

        if (!combo_data.get_tool_tip().empty() && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", combo_data.get_tool_tip().c_str());
        }

        ImGui::SameLine();
        ImGui::SetCursorPosX(combo_data.get_input_x_position());
        ImGui::SetNextItemWidth(combo_data.get_input_width());

        if (ImGui::BeginCombo(combo_data.get_hidden_label(), combo_data.get_preview_value(), combo_data.get_flags()))
        {
            for (ComboDataItem<T> item : combo_data.get_items()) {
                if (ImGui::Selectable(item.get_display_c_str(), item.get_is_selected())){
                    combo_data.set_selected_item(item);
                }

                // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
                if (item.get_is_selected()) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    }

    void PortSelectionWindow(ImGuiID id) {

        const auto layout = MeasureConnectionDialogLayout(4);
        const float input_x_offset = layout.input_width;
        const float input_width = layout.input_width;
        const float line_height = layout.line_height;
        const float extra_vertical_spacing = layout.extra_vertical_spacing;

        static std::vector<serial::PortInfo> port_infos;

        static ComboData<PortInfo> cbo_port_selection_data("Serial Port", input_x_offset);
        static ComboData<bytesize_t> cbo_data_bits_data("Data bits", input_x_offset);
        static ComboData<stopbits_t> cbo_stop_bits_data("Stop bits", input_x_offset);
        static ComboData<parity_t> cbo_parity_data("Parity", input_x_offset);
        static ComboData<flowcontrol_t> cbo_flow_control_data("Flow control", input_x_offset);
        static char baud_text[128] = "115200";

        static ComboData<TerminalState::NewLineMode> cbo_new_line_mode_data("New Line Mode", input_x_offset, input_width,
            "A carriage return (CR) and line feed (LF) is needed\n"
            "for each new line of data. If the input data only\n"
            "contains one or the other, select the mode that\n"
            "will add the missing element.");

        static std::vector<ComboDataBase*> cbo_datas = {
            &cbo_port_selection_data,
            &cbo_data_bits_data,
            &cbo_stop_bits_data,
            &cbo_parity_data,
            &cbo_flow_control_data,
            &cbo_new_line_mode_data
        };


        ImGuiStyle& style = ImGui::GetStyle();
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        
        for (ComboDataBase* comboData : cbo_datas) {
            comboData->set_input_x_position(input_x_offset);
            comboData->set_input_width(input_width);
        }

        int lines = raw_capture.GetError().empty() ? 12 : 13;
        const float window_width = input_x_offset + input_width + style.WindowPadding.x;
        const auto& error = session.GetError();
        const float error_height = error.empty() ? 0.0f
            : ImGui::CalcTextSize(error.c_str(), nullptr, false,
                window_width - style.WindowPadding.x * 2.0f).y + style.ItemSpacing.y;

        ImGui::OpenPopup("Setup");

        ImVec2 center = viewport->GetCenter();
        ImVec2 window_size = ImVec2(window_width, (line_height * lines) + extra_vertical_spacing + error_height);
        ImGui::SetNextWindowSize(window_size, ImGuiCond_Always);
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));

        if (ImGui::BeginPopupModal("Setup", NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {

            ConnectionError();

            static std::vector<imterm::ComboDataItem<serial::PortInfo>> port_items;

            auto port_infos_tuple = serial::list_ports_cached(port_cache_duration);
            if (std::get<0>(port_infos_tuple)) {
                // There is new data
                port_items.clear();
                port_infos = std::get<1>(port_infos_tuple);
                for (serial::PortInfo& info : port_infos) {
                    imterm::ComboDataItem<serial::PortInfo> item(info.hardware_id, info.port, false, info);
                    port_items.push_back(item);
                }
                cbo_port_selection_data.set_items(port_items);
            }

            static bool init_options = true;
            if (init_options) {
                init_options = false;

                static bytesize_t bytesize_values[] = {
                    fivebits,
                    sixbits,
                    sevenbits,
                    eightbits
                };

                std::vector<ComboDataItem<bytesize_t>> databits_items;
                databits_items.push_back(ComboDataItem<bytesize_t>("5", false, bytesize_values[0]));
                databits_items.push_back(ComboDataItem<bytesize_t>("6", false, bytesize_values[1]));
                databits_items.push_back(ComboDataItem<bytesize_t>("7", false, bytesize_values[2]));
                databits_items.push_back(ComboDataItem<bytesize_t>("8", true, bytesize_values[3]));
                cbo_data_bits_data.set_items(databits_items);

                static stopbits_t stopbits_values[] = {
                    stopbits_one,
                    stopbits_one_point_five,
                    stopbits_two
                };

                std::vector<ComboDataItem<stopbits_t>> stopbits_items;
                stopbits_items.push_back(ComboDataItem<stopbits_t>("1",true, stopbits_values[0]));
                stopbits_items.push_back(ComboDataItem<stopbits_t>("1.5",false, stopbits_values[1]));
                stopbits_items.push_back(ComboDataItem<stopbits_t>("2",false, stopbits_values[2]));
                cbo_stop_bits_data.set_items(stopbits_items);

                static parity_t parity_values[] = {
                    parity_none,
                    parity_even,
                    parity_mark,
                    parity_space
                };

                std::vector<ComboDataItem<parity_t>> parity_items;
                parity_items.push_back(ComboDataItem<parity_t>("None", true, parity_values[0]));
                parity_items.push_back(ComboDataItem<parity_t>("Even", false, parity_values[1]));
                parity_items.push_back(ComboDataItem<parity_t>("Mark", false, parity_values[2]));
                parity_items.push_back(ComboDataItem<parity_t>("Space", false, parity_values[3]));
                cbo_parity_data.set_items(parity_items);

                static flowcontrol_t flowcontrol_values[] = {
                    flowcontrol_none,
                    flowcontrol_hardware,
                    flowcontrol_software
                };

                std::vector<ComboDataItem<flowcontrol_t>> flow_control_items;
                flow_control_items.push_back(ComboDataItem<flowcontrol_t>("None", true, flowcontrol_values[0]));
                flow_control_items.push_back(ComboDataItem<flowcontrol_t>("Hardware", false, flowcontrol_values[1]));
                flow_control_items.push_back(ComboDataItem<flowcontrol_t>("Software", true, flowcontrol_values[2]));
                cbo_flow_control_data.set_items(flow_control_items);

                static TerminalState::NewLineMode new_line_mode_values[] = {
                    TerminalState::NewLineMode::Strict,
                    TerminalState::NewLineMode::AddCrToLf,
                    TerminalState::NewLineMode::AddLfToCr
                };

                std::vector<ComboDataItem<TerminalState::NewLineMode>> new_line_mode_items;
                new_line_mode_items.push_back(ComboDataItem<TerminalState::NewLineMode>("Strict", true, new_line_mode_values[0]));
                new_line_mode_items.push_back(ComboDataItem<TerminalState::NewLineMode>("Add CR to LF", false, new_line_mode_values[1]));
                new_line_mode_items.push_back(ComboDataItem<TerminalState::NewLineMode>("Add LF to CR", true, new_line_mode_values[2]));
                cbo_new_line_mode_data.set_items(new_line_mode_items);
                
                auto serial_node = settings.get_serial_settings();

                cbo_port_selection_data.set_selected_item(serial_node);
                cbo_data_bits_data.set_selected_item(serial_node);
                cbo_stop_bits_data.set_selected_item(serial_node);
                cbo_parity_data.set_selected_item(serial_node);
                cbo_flow_control_data.set_selected_item(serial_node);

                std::optional<std::string> value_str = serial_node["baud"].value<std::string>();

                if (value_str.has_value()) {
                    const auto count = value_str->copy(baud_text, sizeof(baud_text) - 1);
                    baud_text[count] = 0;
                }

                auto input_node = settings.get_input_settings();
                cbo_new_line_mode_data.set_selected_item(input_node);
                    
                
            }

            if (cbo_port_selection_data.get_items().size() == 0) {
                ImGui::Text("There are no COM ports!");
            }
            else {
                DisplayCombo(cbo_port_selection_data);
            }

            ImGui::Text("Baud ");
            ImGui::SameLine();
            ImGui::SetCursorPosX(input_x_offset);
            ImGui::SetNextItemWidth(input_width);
            ImGui::InputText("##Baud", baud_text, IM_ARRAYSIZE(baud_text));

            DisplayCombo(cbo_data_bits_data);
            DisplayCombo(cbo_stop_bits_data);
            DisplayCombo(cbo_parity_data);
            DisplayCombo(cbo_flow_control_data);
            ImGui::Spacing();
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::Spacing();
            DisplayCombo(cbo_new_line_mode_data);
            
            static std::string log_file_path = TerminalLogger::GetDefaultLogPath().string();
            ImGui::TextUnformatted("Enable logging ");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Log files will be saved to\n%s", log_file_path.c_str());
            }
            
            ImGui::SameLine();
            ImGui::SetCursorPosX(input_x_offset);
            ImGui::Checkbox("##enable_logging", &enable_logging);
            
            ImGui::SameLine();
            float xPos = ImGui::GetCursorPosX();
            auto x = input_width - (ImGui::GetCursorPosX() - input_x_offset);

            if (!enable_logging) ImGui::BeginDisabled();
            // open Dialog Simple
            if (ImGui::Button("Choose Path", ImVec2(x,0.0f)))
                ImGuiFileDialog::Instance()->OpenDialog(
                    "ChooseFileDlgKey", "Choose Path", nullptr, log_file_path,
                    1,
                    nullptr,
                    ImGuiFileDialogFlags_Modal
                );

            if (!enable_logging) ImGui::EndDisabled();



            //ImGuiFileDialog::Instance()->Path

            // display
            if (ImGuiFileDialog::Instance()->Display("ChooseFileDlgKey"))
            {
                // action if OK
                if (ImGuiFileDialog::Instance()->IsOk())
                {
                    log_file_path = ImGuiFileDialog::Instance()->GetFilePathName();
                    std::string filePath = ImGuiFileDialog::Instance()->GetCurrentPath();
                    // action
                }

                // close
                ImGuiFileDialog::Instance()->Close();
            }

            if (ImGui::Button(raw_capture.IsActive() ? "Stop raw capture" : "Start raw capture...")) {
                if (raw_capture.IsActive()) StopRawCapture();
                else OpenRawCaptureDialog();
            }
            if (raw_capture.IsActive()) {
                ImGui::SameLine();
                ImGui::Text("%llu bytes", static_cast<unsigned long long>(raw_capture.GetByteCount()));
            }
            if (!raw_capture.GetError().empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Raw capture error");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", raw_capture.GetError().c_str());
            }
            ShowRawCaptureDialog();

            ImGui::NewLine();

            if (ImGui::Button("Save", ImVec2(120, 0))) {

                int baud_int = 115200;

                try {
                    baud_int = std::stoi(std::string(baud_text));
                }
                catch (const std::exception& e) {
                    std::cerr << "Could not convert baud " << e.what() << "\n";
                    baud_int = -1;
                }

                if (baud_int != -1) {



                    if (cbo_port_selection_data.item_is_selected()) {

                        serial_name = cbo_port_selection_data.get_selected_data().port;

                        SerialParameters parameters;
                        parameters.port = serial_name;
                        parameters.baudrate = baud_int;
                        parameters.bytesize = cbo_data_bits_data.get_selected_data();
                        parameters.parity = cbo_parity_data.get_selected_data();
                        parameters.stopbits = cbo_stop_bits_data.get_selected_data();
                        parameters.flowcontrol = cbo_flow_control_data.get_selected_data();
                        setup_saved = session.Connect(parameters);
                        auto log_options = term_log->GetOptions();
                        log_options.Enabled = enable_logging;
                        term_log->SetOptions(log_options);
                    }

                    if (setup_saved) {

                        auto serial_node_tbl = settings.get_serial_settings().as_table();
                        cbo_port_selection_data.put_selected_item(serial_node_tbl);
                        cbo_data_bits_data.put_selected_item(serial_node_tbl);
                        cbo_stop_bits_data.put_selected_item(serial_node_tbl);
                        cbo_parity_data.put_selected_item(serial_node_tbl);
                        cbo_flow_control_data.put_selected_item(serial_node_tbl);
                        serial_node_tbl->insert("baud", baud_text);

                        auto input_node_tbl = settings.get_input_settings().as_table();
                        cbo_new_line_mode_data.put_selected_item(input_node_tbl);

                        term_state->SetNewLineMode(cbo_new_line_mode_data.get_selected_data());

                        ImGui::CloseCurrentPopup();

                    }
                }
            }

            ImGui::EndPopup();
        }
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

    Capture::Capture(CaptureSession::TransportFactory factory) : mImpl(std::make_unique<Impl>(std::move(factory))) {}
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
