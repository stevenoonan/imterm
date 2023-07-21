#include <vector>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <string>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <streambuf>
#include <string>
#include <string_view>
#include <filesystem>

#include "imgui.h"
#include "capture.h"
#include "serial/serial.h"
#include "terminal_view.h"


using namespace std::chrono;
using namespace imterm;
using namespace serial;
using namespace ImGui;
using namespace std::literals;

namespace imterm {

    enum class ConnectionStage {
        not_connected = 0,
        connected = 1,
        reconnecting = 2,
        reconfiguring = 3
    };

    static ConnectionStage serial_init = ConnectionStage::not_connected;
    static Serial * serial;
    static std::string serial_name = "[Not Connected]";
    static std::string connection_message = "";
    static bool auto_reconnect = true;
    static bool render_view = false;
    
    static TerminalLogger term_log({.Enabled=false});
    static TerminalData term_data(&term_log);
    static TerminalState term_state(term_data, TerminalState::NewLineMode::Strict);
    static TerminalView term_view(term_data, term_state, TerminalView::Options());
    static auto settings = CaptureSettings();

    constexpr std::chrono::seconds port_cache_duration = 1s;

    void CaptureWindowCreate(void) {

        static bool capture_window_init = false;

        if (!capture_window_init) {
            capture_window_init = true;
        }


        ImGui::Begin(serial_name.c_str(), nullptr, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoTitleBar);

        Menu();

        if (render_view) term_view.Render("TerminalView");
        ImGui::End();

        if (serial && serial->isOpen()) {

            try {

                while (term_view.KeyboardInputAvailable()) {
                    ImWchar keyboard_input_wide = term_view.GetKeyboardInput();
                    uint8_t keyboard_input = static_cast<uint8_t>(keyboard_input_wide);
                    serial->write(&keyboard_input, 1);
                }

                size_t available = serial->available();

                if (available > 0) {

                    std::vector<uint8_t> buffer(available);
                    serial->read(buffer, available);
                    term_state.Input(buffer);

                    term_view.SetCursorToEnd();
                }

                while (term_state.TerminalOutputAvailable()) {
                    auto output = term_state.GetTerminalOutput();
                    serial->write(output);
                }

            }
            catch (const serial::IOException& ex) {
                std::cerr << "Error occurred: " << ex.what() << std::endl;
                CloseSerialPort();
            }
        }

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        
        if (serial_init == ConnectionStage::reconnecting) {
            ReconnectionWindow(viewport->ID);
        }
        else if ((serial_init == ConnectionStage::not_connected) || (serial_init == ConnectionStage::reconfiguring)) {
            PortSelectionWindow(viewport->ID);
        }

    }

    void Menu() {

        if (ImGui::BeginMenuBar())
        {

            std::string menu_port_string;

            if (serial) {
                menu_port_string = serial->getPort();
            }
            else {
                menu_port_string = "No Connection";
            }

            if (ImGui::BeginMenu(menu_port_string.c_str()))
            {
                
                if (ImGui::MenuItem("Auto Reconnect", NULL, auto_reconnect, true)) {
                    auto_reconnect = !auto_reconnect;
                }

                ImGui::Separator();

                ImGui::MenuItem("Select Parameters", NULL, false, false);

                std::ostringstream oss;

                if (serial == nullptr) {

                    auto serial_node = settings.get_serial_settings();

                    std::optional<std::string> op_baud_str = serial_node["baud"].value<std::string>();
                    std::optional<std::string> op_data_bits_str = serial_node["data_bits"].value<std::string>();
                    std::optional<std::string> op_parity_str = serial_node["parity"].value<std::string>();
                    std::optional<std::string> op_stop_bits_str = serial_node["stop_bits"].value<std::string>();

                    oss << (op_baud_str.has_value() ? op_baud_str.value() : "???");
                    oss << std::string(" ");
                    oss << (op_data_bits_str.has_value() ? op_data_bits_str.value() : "?");
                    oss << (op_parity_str.has_value() ? op_parity_str.value().substr(1) : "?");
                    oss << (op_stop_bits_str.has_value() ? op_stop_bits_str.value() : "?");

                } else {

                    uint32_t baud = serial->getBaudrate();

                    // TODO: Flow control and Timeouts

                    oss << serial->getBaudrate() << " " << (int)serial->getBytesize();
                    switch (serial->getParity()) {
                    case serial::parity_none:
                        oss << "N";
                        break;
                    case serial::parity_odd:
                        oss << "O";
                        break;
                    case serial::parity_even:
                        oss << "E";
                        break;
                    case serial::parity_mark:
                        oss << "M";
                        break;
                    case serial::parity_space:
                        oss << "S";
                        break;
                    }

                    switch (serial->getStopbits()) {
                    case serial::stopbits_one:
                        oss << "1";
                        break;
                    case serial::stopbits_one_point_five:
                        oss << "1.5";
                        break;
                    case serial::stopbits_two:
                        oss << "2";
                        break;
                    }

                }

                if (ImGui::MenuItem(oss.str().c_str(), NULL, false)) {
                    serial_init = ConnectionStage::reconfiguring;
                }

                if (serial) {
                    ImGui::Separator();

                    ImGui::MenuItem("Select New Port", NULL, false, false);

                    auto port_infos_tuple = serial::list_ports_cached(port_cache_duration);
                    auto port_infos = std::get<1>(port_infos_tuple);
                    for (serial::PortInfo& info : port_infos) {
                        bool current_port = (info.port == serial->getPort());
                        if (ImGui::MenuItem(info.port.c_str(), NULL, current_port, true)) {
                            if (!current_port) {
                                try {
                                    term_log.SetPostfix(info.port.c_str());
                                    serial->setPort(info.port.c_str());
                                }
                                catch (const serial::IOException& ex) {
                                    std::cerr << "Error occurred: " << ex.what() << std::endl;
                                    CloseSerialPort();
                                    serial_init = ConnectionStage::reconfiguring;
                                    connection_message = "** Unable to open " + info.port + " **";
                                }

                            }
                        }
                    }
                }

                ImGui::EndMenu();
            }

            
            if (ImGui::BeginMenu("Options")) {


                if (ImGui::BeginMenu("New Line Mode"))
                {

                    auto new_line_mode = term_state.GetNewLineMode();
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
                        term_state.SetNewLineMode(TerminalState::NewLineMode::Strict);
                    }
                    if (ImGui::MenuItem("Add CR to LF", NULL, (new_line_mode == TerminalState::NewLineMode::AddCrToLf))) {
                        term_state.SetNewLineMode(TerminalState::NewLineMode::AddCrToLf);
                    }
                    if (ImGui::MenuItem("ADD LF to CR", NULL, (new_line_mode == TerminalState::NewLineMode::AddLfToCr))) {
                        term_state.SetNewLineMode(TerminalState::NewLineMode::AddLfToCr);
                    }

                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("View"))
                {
                    auto ops = term_view.GetOptions();
                    if (ImGui::MenuItem("Line Numbers", NULL, ops.LineNumbers, true)) {
                        ops.LineNumbers = !ops.LineNumbers;
                        term_view.SetOptions(ops);
                    }
                    if (ImGui::MenuItem("Timestamps", NULL, ops.TimeStamps, true)) {
                        ops.TimeStamps = !ops.TimeStamps;
                        term_view.SetOptions(ops);
                    }

                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("Log"))
                {
                    auto ops = term_log.GetOptions();
                    if (ImGui::MenuItem("Enabled", NULL, ops.Enabled, true)) {
                        ops.Enabled = !ops.Enabled;
                        term_log.SetOptions(ops);
                    }
                    if (ImGui::MenuItem("Line Numbers", NULL, ops.LineNumbers, true)) {
                        ops.LineNumbers = !ops.LineNumbers;
                        term_log.SetOptions(ops);
                    }
                    if (ImGui::MenuItem("Timestamps", NULL, ops.TimeStamps, true)) {
                        ops.TimeStamps = !ops.TimeStamps;
                        term_log.SetOptions(ops);
                    }

                    ImGui::EndMenu();
                }

                ImGui::EndMenu();
            }
            
            

            if (serial && serial->isOpen()) {

                static bool dtr = false;
                static bool rts = false;

                try {

                    if (ImGui::MenuItem(dtr ? "DTR=1" : "DTR=0", NULL, false, true)) {
                        dtr = !dtr;
                        serial->setDTR(dtr);
                    }
                    if (ImGui::MenuItem(rts ? "RTS=1" : "RTS=0", NULL, false, true)) {
                        rts = !rts;
                        serial->setRTS(rts);
                    }
                    ImGui::MenuItem(serial->getCTS() ? "CTS=1" : "CTS=0", NULL, false, false);
                    ImGui::MenuItem(serial->getDSR() ? "DSR=1" : "DSR=0", NULL, false, false);
                    ImGui::MenuItem(serial->getCD() ? "DCD=1" : "DCD=0", NULL, false, false);

                }
                catch (const serial::IOException& ex) {
                    std::cerr << "Error occurred: " << ex.what() << std::endl;
                    CloseSerialPort();
                }
            }

            ImGui::EndMenuBar();
        }
    }

    void CloseSerialPort() {
        try {

            bool was_open = false;

            if (serial) {
                was_open = serial->isOpen();
                serial->close();
            }

            if (auto_reconnect && was_open) {
                serial_init = ConnectionStage::reconnecting;
            }
            else {
                serial_init = ConnectionStage::not_connected;
            }

        }
        catch (const serial::IOException& ex) {
            std::cerr << "Error occurred: " << ex.what() << std::endl;
        }
    }

    void OpenSerialPort(const std::string& port, uint32_t baudrate, serial::Timeout timeout,
        bytesize_t bytesize, parity_t parity, stopbits_t stopbits,
        flowcontrol_t flowcontrol) {
        
        CloseSerialPort();

        try {
            serial = new Serial(
                port,
                baudrate,
                timeout,
                bytesize,
                parity,
                stopbits,
                flowcontrol
            );
            serial_init = ConnectionStage::connected;
            render_view = true;
            term_log.SetPostfix(port);
            auto ops = term_log.GetOptions();
            ops.Enabled = true;
            term_log.SetOptions(ops);
        }
        catch (const std::exception& e) {
            std::cerr << "Could not open port. " << e.what() << "\n";
            connection_message = "** Unable to open " + port + " **";
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

        static float current_dpi_scale = -1.0f; // make invalid // 96 DPI
        static float input_x_offset = 200;
        static float input_width = 200;
        static float line_height = 50;
        static float extra_vertical_spacing = 30;

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
        
        if (current_dpi_scale != viewport->DpiScale) {
            current_dpi_scale = viewport->DpiScale;
            ImVec2 text_size = ImGui::CalcTextSize(std::string(20, '0').c_str());
            input_x_offset = text_size.x;
            input_width = input_x_offset;

            for (ComboDataBase* comboData : cbo_datas) {
                comboData->set_input_x_position(input_x_offset);
                comboData->set_input_width(input_x_offset);
            }
            
            ImFont* font = ImGui::GetFont();
            line_height = ((style.FramePadding.y * 2) + font->FontSize + style.ItemSpacing.y);
            
            extra_vertical_spacing = 
                style.PopupBorderSize
                + (style.ItemSpacing.y * 4)  // 4 calls to ImGui::Spacing()
                + 1                          // 1 call  to ImGui::Separator();
                + (style.FramePadding.y * 2) // top and bottom
                + 1;                         // 1 extra needed at some DPIs
        }

        int lines = 10;
        if (connection_message != "") {
            lines++;
        }

        ImGui::OpenPopup("Setup");

        ImVec2 center = viewport->GetCenter();
        ImVec2 window_size = ImVec2(input_x_offset + input_width + style.WindowPadding.x, (line_height * lines) + extra_vertical_spacing);
        ImGui::SetNextWindowSize(window_size, ImGuiCond_Always);
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));

        if (ImGui::BeginPopupModal("Setup", NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {

            if (connection_message != "") {
                ImGui::SetCursorPosX((window_size.x - ImGui::CalcTextSize(connection_message.c_str()).x) * 0.5f);
                ImGui::TextUnformatted(connection_message.c_str());
            }

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
                    value_str->copy(baud_text, sizeof(baud_text) - 1);
                    baud_text[value_str->size()] = 0;
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

                    serial_init = ConnectionStage::not_connected;

                    if (cbo_port_selection_data.item_is_selected()) {

                        connection_message = "";

                        serial_name = cbo_port_selection_data.get_selected_data().port;

                        OpenSerialPort(
                            serial_name, 
                            baud_int,
                            serial::Timeout(
                                50, /* inter_byte_timeout_ */
                                50, /* read_timeout_constant_ */
                                10, /* read_timeout_multiplier_ */
                                50, /* write_timeout_constant_ */
                                10  /* write_timeout_multiplier_ */
                            ),
                            cbo_data_bits_data.get_selected_data(),
                            cbo_parity_data.get_selected_data(),
                            cbo_stop_bits_data.get_selected_data(),
                            serial::flowcontrol_none
                        );
                    }

                    if (serial_init == ConnectionStage::connected) {

                        auto serial_node_tbl = settings.get_serial_settings().as_table();
                        cbo_port_selection_data.put_selected_item(serial_node_tbl);
                        cbo_data_bits_data.put_selected_item(serial_node_tbl);
                        cbo_stop_bits_data.put_selected_item(serial_node_tbl);
                        cbo_parity_data.put_selected_item(serial_node_tbl);
                        cbo_flow_control_data.put_selected_item(serial_node_tbl);
                        serial_node_tbl->insert("baud", baud_text);

                        auto input_node_tbl = settings.get_input_settings().as_table();
                        cbo_new_line_mode_data.put_selected_item(input_node_tbl);

                        settings.write();

                        term_state.SetNewLineMode(cbo_new_line_mode_data.get_selected_data());

                        ImGui::CloseCurrentPopup();

                    }
                }
            }

            ImGui::EndPopup();
        }
    }

    void ReconnectionWindow(ImGuiID id) {

        static float current_dpi_scale = -1.0f; // make invalid // 96 DPI
        static float input_x_offset = 200;
        static float input_width = 200;
        static float line_height = 50;
        static float extra_vertical_spacing = 30;

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
            "only contains one or the other, select the mode\n"
            "that will add the missing element.");

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

        if (current_dpi_scale != viewport->DpiScale) {
            current_dpi_scale = viewport->DpiScale;
            ImVec2 text_size = ImGui::CalcTextSize(std::string(20, '0').c_str());
            input_x_offset = text_size.x;
            input_width = input_x_offset;

            for (ComboDataBase* comboData : cbo_datas) {
                comboData->set_input_x_position(input_x_offset);
                comboData->set_input_width(input_x_offset);
            }

            ImFont* font = ImGui::GetFont();
            line_height = ((style.FramePadding.y * 2) + font->FontSize + style.ItemSpacing.y);

            extra_vertical_spacing =
                style.PopupBorderSize
                + (style.ItemSpacing.y * 2)  // 2 calls to ImGui::Spacing()
                + (style.FramePadding.y * 2) // top and bottom
                + 1;                         // 1 extra needed at some DPIs
        }

        ImGui::OpenPopup("Reconnecting...");

        ImVec2 center = viewport->GetCenter();
        ImVec2 window_size = ImVec2(input_x_offset + input_width + style.WindowPadding.x, (line_height * 3) + extra_vertical_spacing);
        ImGui::SetNextWindowSize(window_size, ImGuiCond_Always);
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));

        if (ImGui::BeginPopupModal("Reconnecting...", NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {

            static system_clock::time_point last_attempt_time = system_clock::now() - 1s;

            // We should not have gotten here if serial is null
            assert(serial);
            assert(!serial->isOpen());

            std::string attempt_string = std::string("Attempting to reconnect to " + serial->getPort());
            ImGui::SetCursorPosX((window_size.x - ImGui::CalcTextSize(attempt_string.c_str()).x) * 0.5f);
            ImGui::TextUnformatted(attempt_string.c_str());

            if ((system_clock::now() - last_attempt_time) > 1s) {

                last_attempt_time = system_clock::now();

                try {
                    serial->open();
                    serial_init = ConnectionStage::connected;
                }
                catch (const std::exception& ex) {
                    std::cerr << "Error occurred: " << ex.what() << std::endl;
                }

            }

            ImGui::Spacing();
            ImGui::Spacing();

            if (ImGui::Button("Cancel", ImVec2(window_size.x - (style.WindowPadding.x * 2), 0))) {
                serial_init = ConnectionStage::not_connected;
            }

            ImGui::EndPopup();
        }
    }

}