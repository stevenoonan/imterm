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

    static bool serial_init = false;
    static Serial * serial;
    static std::string serial_name = "[Not Connected]";
    
    static TerminalData term_data;
    static TerminalState term_state(term_data, TerminalState::NewLineMode::Strict);
    static TerminalView term_view(term_data, term_state);
    static auto settings = CaptureSettings();

    void CaptureWindowCreate(void) {

        static bool capture_window_init = false;

        if (!capture_window_init) {
            capture_window_init = true;
            term_view.SetKeyboardInputAllowed(false);
        }

        

        ImGui::Begin(serial_name.c_str(), nullptr, ImGuiWindowFlags_HorizontalScrollbar /* | ImGuiWindowFlags_MenuBar */);
        ImGui::SetWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);

#ifdef SHOW_MENU
        Menu();
        //if (serial_init) ImGui::SetNextWindowFocus();
#endif
        term_view.Render("TerminalView");
        ImGui::End();

        if (serial_init) {

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

                while (term_state.TerminalOutputAvailable()) {
                    auto output = term_state.GetTerminalOutput();
                    serial->write(output);
                }
              
                term_view.SetCursorToEnd();
            }
        }
        else {
            ImGuiViewport* viewport = ImGui::GetMainViewport();
            PortSelectionWindow(viewport->ID);
        }

    }

#ifdef SHOW_MENU
    void Menu() {
        // Menu Bar
        if (ImGui::BeginMenuBar())
        {

            static int n = 0;
            ImVec2 textSize = ImGui::CalcTextSize("Add CR to LF");
            ImGuiStyle& style = ImGui::GetStyle();
            float comboArrowWidth = style.ItemInnerSpacing.x + ImGui::GetFrameHeight();
            ImGui::SetNextItemWidth(textSize.x + comboArrowWidth);
            //ImGui::Combo("Line Mode", &n, "Strict\0Add CR to LF\0Add LF to CR\0\0");

            ImGui::BeginCombo("MyCombo", "Selected Item");

            ImGui::SetTooltip("This is a tooltip for the combo box");

            // Add combo box items here

            ImGui::EndCombo();

            if (ImGui::BeginMenu("Line Mode"))
            {
                static int line_mode = 0;
                if (ImGui::MenuItem("Strict", NULL, (line_mode==0))) {
                    line_mode = 0;
                }
                if (ImGui::MenuItem("Add CR to LF", NULL, (line_mode == 1))) {
                    line_mode = 1;
                }
                if (ImGui::MenuItem("ADD LF to CR", NULL, (line_mode == 2))) {
                    line_mode = 2;
                }
                //if (ImGui::MenuItem("Quit", "Alt+F4")) {
                //}
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }
    }
#endif

    template<typename T>
    void DisplayCombo(ComboData<T>& combo_data) {

        ImGui::Text(combo_data.get_label());

        if (!combo_data.get_tool_tip().empty() && ImGui::IsItemHovered()) {
            ImGui::SetTooltip(combo_data.get_tool_tip().c_str());
        }

        ImGui::SameLine();
        ImGui::SetCursorPosX(combo_data.get_pos_x_combo());

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

        static const float input_x_offset = 138;

        static bool show_port_selection = true;
        static std::vector<serial::PortInfo> port_infos;
        static system_clock::time_point last_port_refresh_time = system_clock::now() - 100s;

        static ComboData<PortInfo> cbo_port_selection_data("Serial Port", input_x_offset);
        static ComboData<bytesize_t> cbo_data_bits_data("Data bits", input_x_offset);
        static ComboData<stopbits_t> cbo_stop_bits_data("Stop bits", input_x_offset);
        static ComboData<parity_t> cbo_parity_data("Parity", input_x_offset);
        static ComboData<flowcontrol_t> cbo_flow_control_data("Flow control", input_x_offset);
        static char baud_text[128] = "115200";

        static ComboData<TerminalState::NewLineMode> cbo_new_line_mode_data("New Line Mode", input_x_offset, 
            "A carriage return (CR) and line feed (LF) is needed\n"
            "for each new line of data. If the input data only\n"
            "only contains one or the other, select the mode\n"
            "that will add the missing element.");


        if (!show_port_selection) return;

        ImGui::OpenPopup("Setup");

        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowSize(ImVec2(400, 325), ImGuiCond_Always);
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        if (ImGui::BeginPopupModal("Setup", NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {

            if (system_clock::now() - last_port_refresh_time > 3s) {
                port_infos = serial::list_ports();
                last_port_refresh_time = system_clock::now();
                std::vector<imterm::ComboDataItem<serial::PortInfo>> items;
                for (serial::PortInfo& info : port_infos) {
                    imterm::ComboDataItem<serial::PortInfo> item(info.hardware_id, info.port, false, info);
                    items.push_back(item);
                }
                cbo_port_selection_data.set_items(items);
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

            if (port_infos.size() == 0) {
                ImGui::Text("There are no COM ports!");
            }
            else {
                DisplayCombo(cbo_port_selection_data);
            }

            ImGui::Text("Baud ");
            ImGui::SameLine();
            ImGui::SetCursorPosX(input_x_offset);
            
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
                    serial_name = cbo_port_selection_data.get_selected_data().port;

                    try {
                        serial = new Serial(serial_name, baud_int);
                        serial_init = true;
                    }
                    catch (const std::exception& e) {
                        std::cerr << "Could not open port. " << e.what() << "\n";
                        baud_int = -1;
                        serial_init = false;
                    }

                    if (serial_init) {

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

                        show_port_selection = false;
                        ImGui::CloseCurrentPopup();

                    }
                }
            }

            ImGui::EndPopup();
        }
    }

}