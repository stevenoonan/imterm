#include "imgui.h"
#include "capture.h"
#include "serial/serial.h"

//#include <string>
#include <vector>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <string>
#include <iomanip>
#include <sstream>

using namespace std::chrono;
using namespace imterm;
using namespace serial;

namespace imterm {

    static bool serial_init = false;
    static Serial * serial;

	void CaptureWindowCreate(void) {

        static std::string line;
        static std::vector<std::string> lines{""};
        static int vscrollValue = 0;
        static bool capture_init = false;
        static const float charWidth = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, "A").x;

        static bool in_func = false;

        assert(in_func == false);

        in_func = true;


        if (serial_init) {
            size_t available = serial->available();

            if (available > 0) {

#ifdef LOG_TO_FILE
                ImGui::LogToFile(-1, "debug_log.txt");  // Redirect log output to console
#endif
                //ImGui::LogToTTY();

                std::string new_bytes = serial->read(available);
                std::string hex_bytes;
                std::stringstream ss;

#ifdef LOG_TO_FILE
                for (char c: new_bytes) {
                    ss << std::hex << std::setw(2) << std::setfill('0') << (int)(unsigned char)c << " ";
                }

                ImGui::LogText("[%s]\r<%s>\r", ss.str().c_str(), new_bytes.c_str());
#endif
                
                std::size_t index = 0;
                std::size_t copy_count = 0;
                bool add_line = false;

                while (index < new_bytes.size()) {
                    std::size_t eol_pos = std::string::npos;
                    eol_pos = new_bytes.find('\r', index);

                    if (eol_pos != std::string::npos) {
                        copy_count = (eol_pos - index);
                        add_line = true;
                    } else {
                        copy_count = new_bytes.size();
                    }

                    auto append_str = new_bytes.substr(index, copy_count);

#ifdef LOG_TO_FILE
                    ImGui::LogText("APPEND: %s\r", append_str.c_str());
#endif

                    lines.back() += append_str;

#ifdef LOG_TO_FILE
                    ImGui::LogText("lines.back() now: %s\r", lines.back().c_str());
#endif

                    index += copy_count;
                    if (add_line) {
                        index++;
                        lines.push_back("");
                        ImGui::LogText("NEW LINE\r");
                    }
                }

                ImGui::LogFinish();

            }
        }

        // Child 1: no border, enable horizontal scrollbar
        {


            ImGuiWindowFlags window_flags = ImGuiWindowFlags_HorizontalScrollbar;
            //if (disable_mouse_wheel)
            //    window_flags |= ImGuiWindowFlags_NoScrollWithMouse;
            //ImGui::BeginChild("ChildL", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f, 260), false, window_flags);
            ImVec2 capture_size = ImVec2(ImGui::GetContentRegionAvail().x - 250, ImGui::GetContentRegionAvail().y);
            ImGui::BeginChild("ChildL", capture_size, false, window_flags);



            static bool lines_added = false;
            auto height = ImGui::GetContentRegionAvail().y;
            auto lineHeight = ImGui::GetTextLineHeightWithSpacing();
            int lineCount = (int)(height / lineHeight); // +1;

            if (!lines_added) {
                lines_added = true;

                // for (int i = 0; i < lineCount * 10000; i++) {
                //     lines.push_back("hello " + std::to_string(i));
                // }
            }


            //if (line.length()) {
            //    ImGui::Text(line.c_str());
            //}
            //for (int i = 0; i < 100; i++)
            //    ImGui::Text("%04d: scrollable region", i);

            ImDrawList* draw_list = ImGui::GetWindowDrawList();

            //ImGui::Text(lines.)
            if (lines.size()) {
                //for (const auto& l : lines) {
                //    ImGui::Text(l.c_str());
                //}
                std::size_t max_i = vscrollValue + lineCount;
                if (max_i >= lines.size()) max_i = lines.size() - 1;
                for (int i = vscrollValue; i <= max_i; i++) {
                    auto this_text_line = lines[i].c_str();
                    ImGui::Text(this_text_line);
                    if (ImGui::IsItemHovered()) {
                        auto this_text_line_length = lines[i].size();
                        //draw_list->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(255, 255, 0, 255));
                        float charWidth_wrong = ImGui::CalcTextSize("l").x;

                        

                        float min_x = ImGui::GetItemRectMin().x;


                        ImGuiIO& io = ImGui::GetIO();

                        //ImGui::SetCursorPos(ImVec2(10, 25));

                        if (ImGui::IsMousePosValid()) {
                            //ImGui::Text("Mouse pos: (%g, %g)", io.MousePos.x, io.MousePos.y);
                            float text_x = io.MousePos.x - min_x;
                            int char_index = (int)(text_x / charWidth);
                            float box_x_min = min_x + (char_index * charWidth);
                            float box_x_max = box_x_min + charWidth;
                            auto vec_min = ImVec2(box_x_min, ImGui::GetItemRectMin().y);
                            auto vec_max = ImVec2(box_x_max, ImGui::GetItemRectMax().y);
                            draw_list->AddRect(vec_min, vec_max, IM_COL32(255, 255, 0, 255));
                        }

                    }
                    
                }
                
            }

            //ImGui::Spacing

            //ImGui::SetScrollHereY(1);// *0.25f); // 0.0f:top, 0.5f:center, 1.0f:bottom
            //auto maxY = ImGui::GetScrollMaxY();
            //ImGui::SetScrollY(100);
            //ImGui::SetCursorPosY(ImGui::GetWindowHeight());

            
                
            

            ImGui::EndChild();

            ImGui::SameLine();

            assert(lines.size() <= INT_MAX);
            
            ImGui::VSliderInt("##int", ImVec2(36, capture_size.y), &vscrollValue, (int)lines.size(), 0);
            
        }

        {
            //ImGuiIO& io = ImGui::GetIO();

            //ImGui::SetCursorPos(ImVec2(10, 25));

            //if (ImGui::IsMousePosValid())
            //    ImGui::Text("Mouse pos: (%g, %g)", io.MousePos.x, io.MousePos.y);
            //else
            //    ImGui::Text("Mouse pos: <INVALID>");
            //ImGui::Text("Mouse delta: (%g, %g)", io.MouseDelta.x, io.MouseDelta.y);

            //int count = IM_ARRAYSIZE(io.MouseDown);
            //ImGui::Text("Mouse down:");         for (int i = 0; i < count; i++) if (ImGui::IsMouseDown(i)) { ImGui::SameLine(); ImGui::Text("b%d (%.02f secs)", i, io.MouseDownDuration[i]); }
            //ImGui::Text("Mouse clicked:");      for (int i = 0; i < count; i++) if (ImGui::IsMouseClicked(i)) { ImGui::SameLine(); ImGui::Text("b%d (%d)", i, ImGui::GetMouseClickedCount(i)); }
            //ImGui::Text("Mouse released:");     for (int i = 0; i < count; i++) if (ImGui::IsMouseReleased(i)) { ImGui::SameLine(); ImGui::Text("b%d", i); }
            //ImGui::Text("Mouse wheel: %.1f", io.MouseWheel);
            
            //draw_list->AddRectFilled(ImVec2(11,11), ImVec2(44, 44), IM_COL32(255, 0, 255, 255));
        }

        assert(in_func);

        in_func = false;

	}
    

    

    template<typename T>
    void DisplayCombo(ComboData<T>& combo_data) {

        ImGui::Text(combo_data.get_label());
        ImGui::SameLine();
        //ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 5);
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


        if (!show_port_selection) return;

        ImGui::SetNextWindowViewport(id);
        ImGui::Begin("Port Selection", &show_port_selection);   // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)

        if (system_clock::now() - last_port_refresh_time > 3s) {
            port_infos = serial::list_ports();
            last_port_refresh_time = system_clock::now();
            std::vector<imterm::ComboDataItem<serial::PortInfo>> items;
            for (serial::PortInfo& info: port_infos) {
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
            // databits_items.push_back(ComboDataItem<bytesize_t>("5", false, &bytesize_values[0]));
            // databits_items.push_back(ComboDataItem<bytesize_t>("6", false, &bytesize_values[1]));
            // databits_items.push_back(ComboDataItem<bytesize_t>("7", false, &bytesize_values[2]));
            // databits_items.push_back(ComboDataItem<bytesize_t>("8", true, &bytesize_values[3]));
            cbo_data_bits_data.set_items(databits_items);

            static stopbits_t stopbits_values[] = {
                stopbits_one,
                stopbits_one_point_five,
                stopbits_two
            };

            std::vector<ComboDataItem<stopbits_t>> stopbits_items;
            // stopbits_items.push_back(ComboDataItem<stopbits_t>("1",true, &stopbits_values[0]));
            // stopbits_items.push_back(ComboDataItem<stopbits_t>("1.5",false, &stopbits_values[1]));
            // stopbits_items.push_back(ComboDataItem<stopbits_t>("2",false, &stopbits_values[2]));
            cbo_stop_bits_data.set_items(stopbits_items);

            static parity_t parity_values[] = {
                parity_none,
                parity_even,
                parity_mark,
                parity_space
            };

            std::vector<ComboDataItem<parity_t>> parity_items;
            // parity_items.push_back(ComboDataItem<parity_t>("None", true, &parity_values[0]));
            // parity_items.push_back(ComboDataItem<parity_t>("Even", false, &parity_values[1]));
            // parity_items.push_back(ComboDataItem<parity_t>("Mark", false, &parity_values[2]));
            // parity_items.push_back(ComboDataItem<parity_t>("Space", false, &parity_values[3]));
            cbo_parity_data.set_items(parity_items);

            static flowcontrol_t flowcontrol_values[] = {
                flowcontrol_none,
                flowcontrol_hardware,
                flowcontrol_software
            };

            std::vector<ComboDataItem<flowcontrol_t>> flow_control_items;
            // flow_control_items.push_back(ComboDataItem<flowcontrol_t>("None", true, &flowcontrol_values[0]));
            // flow_control_items.push_back(ComboDataItem<flowcontrol_t>("Hardware", false, &flowcontrol_values[1]));
            // flow_control_items.push_back(ComboDataItem<flowcontrol_t>("Software", true, &flowcontrol_values[2]));
            cbo_flow_control_data.set_items(flow_control_items);
        }

        if (port_infos.size() == 0) {
            ImGui::Text("There are no COM ports!");
        } else {
            DisplayCombo(cbo_port_selection_data);
        }

        ImGui::Text("Baud: ");
        ImGui::SameLine();
        ImGui::SetCursorPosX(128);
        static char baud_text[128] = "115200";
        ImGui::InputText("##Baud:", baud_text, IM_ARRAYSIZE(baud_text));
        
        DisplayCombo(cbo_data_bits_data);
        DisplayCombo(cbo_stop_bits_data);
        DisplayCombo(cbo_parity_data);
        DisplayCombo(cbo_flow_control_data);

        if (ImGui::Button("Save")) {

            int baud_int = 9600;

            try {
                baud_int = std::stoi(std::string(baud_text));
            } catch (const std::exception& e) {
                std::cerr << "Could not convert baud " << e.what() << "\n";
            }

            //auto serial = new Serial("COM13",115200,serial::Timeout(), serial::eightbits, serial::parity_none, serial::stopbits_one, serial::flowcontrol_none);
            serial = new Serial(cbo_port_selection_data.get_selected_data().port, baud_int);
            serial_init = true;

            show_port_selection = false;
        }

        ImGui::End();
    }

}