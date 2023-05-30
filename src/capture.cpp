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
    static TerminalView terminal;

    void CaptureWindowCreate(void) {

        static bool capture_window_init = false;

        if (!capture_window_init) {
            capture_window_init = true;
            terminal.SetKeyboardInputAllowed(false);
        }

        ImGui::Begin(serial_name.c_str(), nullptr, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_MenuBar);
        ImGui::SetWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
        if (serial_init) ImGui::SetNextWindowFocus();
        terminal.Render("TerminalView");
        ImGui::End();

        if (serial_init) {

            while (terminal.KeyboardInputAvailable()) {
                ImWchar keyboard_input_wide = terminal.GetKeyboardInput();
                uint8_t keyboard_input = static_cast<uint8_t>(keyboard_input_wide);
                serial->write(&keyboard_input, 1);
            }

            size_t available = serial->available();

            if (available > 0) {
 
                std::vector<uint8_t> buffer(available);
                serial->read(buffer, available);
                terminal.TerminalInput(buffer);

                while (terminal.TerminalOutputAvailable()) {
                    auto output = terminal.GetTerminalOutput();
                    serial->write(output);
                }
              
                terminal.SetCursorToEnd();
            }
        }
        else {
            ImGuiViewport* viewport = ImGui::GetMainViewport();
            PortSelectionWindow(viewport->ID);
        }

    }

    template<typename T>
    void DisplayCombo(ComboData<T>& combo_data) {

        ImGui::Text(combo_data.get_label());
        ImGui::SameLine();
        ImGui::SetCursorPosX(128);

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

        static bool show_port_selection = true;
        static std::vector<serial::PortInfo> port_infos;
        static system_clock::time_point last_port_refresh_time = system_clock::now() - 100s;

        static ComboData<PortInfo> cbo_port_selection_data("Serial Port");
        static ComboData<bytesize_t> cbo_data_bits_data("Data bits");
        static ComboData<stopbits_t> cbo_stop_bits_data("Stop bits");
        static ComboData<parity_t> cbo_parity_data("Parity");
        static ComboData<flowcontrol_t> cbo_flow_control_data("Flow control");
        static char baud_text[128] = "115200";


        if (!show_port_selection) return;

        ImGui::OpenPopup("Port Selection");

        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowSize(ImVec2(390, 250), ImGuiCond_Always);
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        if (ImGui::BeginPopupModal("Port Selection", NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {

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
                databits_items.push_back(ComboDataItem <bytesize_t>("5", false, bytesize_values[0]));
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

                // TODO:  Create a class for imterm.toml interfacing
                std::string cfg_file = "imterm.toml";
                if (!std::filesystem::exists(cfg_file)) {
                    auto tbl = toml::table{
                        { "default", toml::table{
                                { "serial", toml::table{
                                        {"serial_port", "COM1"},
                                        {"baud", "115200"},
                                        {"data_bits", "8"},
                                        {"stop_bits", "2"},
                                        {"parity", "none"},
                                        {"flow_control", "none"}
                                    }
                                }
                            }
                        },
                     };


                    auto tbl_default = tbl["default"];
                    auto tbl_serial = tbl_default["serial"];
                    auto baud = tbl["default"]["serial"]["baud"];
                    std::optional<std::string> baudstr = tbl["default"]["serial"]["baud"].value<std::string>();

                    std::ofstream cfg_stream(cfg_file);
                    if (cfg_stream.is_open()) {
                        cfg_stream << tbl;
                        cfg_stream.close();
                    }
                    
                }
                if (std::filesystem::exists(cfg_file)) {
                    auto config = toml::parse_file("imterm.toml");
                    auto serial_node = config["default"]["serial"];

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
                    
                }
            }

            if (port_infos.size() == 0) {
                ImGui::Text("There are no COM ports!");
            }
            else {
                DisplayCombo(cbo_port_selection_data);
            }

            ImGui::Text("Baud: ");
            ImGui::SameLine();
            ImGui::SetCursorPosX(128);
            
            ImGui::InputText("##Baud", baud_text, IM_ARRAYSIZE(baud_text));

            DisplayCombo(cbo_data_bits_data);
            DisplayCombo(cbo_stop_bits_data);
            DisplayCombo(cbo_parity_data);
            DisplayCombo(cbo_flow_control_data);

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

                        // Save data to file
                        auto tbl = toml::table{
                            { "default", toml::table{
                                    { "serial", toml::table{ } }
                                }
                            },
                        };
                        auto serial_node_tbl = tbl["default"]["serial"].as_table();
                        cbo_port_selection_data.put_selected_item(serial_node_tbl);
                        cbo_data_bits_data.put_selected_item(serial_node_tbl);
                        cbo_stop_bits_data.put_selected_item(serial_node_tbl);
                        cbo_parity_data.put_selected_item(serial_node_tbl);
                        cbo_flow_control_data.put_selected_item(serial_node_tbl);
                        serial_node_tbl->insert("baud", baud_text);

                        std::string cfg_file = "imterm.toml";
                        std::ofstream cfg_stream(cfg_file);
                        if (cfg_stream.is_open()) {
                            cfg_stream << tbl;
                            cfg_stream.close();
                        }

                        show_port_selection = false;
                        ImGui::CloseCurrentPopup();

                    }
                }
            }

            ImGui::EndPopup();
        }
    }

}