#include "imgui.h"
#include "capture.h"
#include "serial/serial.h"

//#include <string>
#include <vector>
#include <algorithm>

namespace imterm {


	void CaptureWindowCreate(void) {

        static bool serial_init = false;

        static std::string line;
        static serial::Serial * serial;
        static std::vector<std::string> lines;

        if (!serial_init) {
            //auto serial = new serial::Serial("COM13",115200,serial::Timeout(), serial::eightbits, serial::parity_none, serial::stopbits_one, serial::flowcontrol_none);
            serial = new serial::Serial("COM13", 115200);
            serial_init = true;
        }

        if (serial->available()) {
            line = serial->readline();
            lines.push_back(line);
        }
        else {
            //while (lines.size() < 1000000) {
            //    lines.push_back("line " + std::to_string(lines.size()));
            //}
        }

        // Child 1: no border, enable horizontal scrollbar
        {
            ImGuiWindowFlags window_flags = ImGuiWindowFlags_HorizontalScrollbar;
            //if (disable_mouse_wheel)
            //    window_flags |= ImGuiWindowFlags_NoScrollWithMouse;
            ImGui::BeginChild("ChildL", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f, 260), false, window_flags);
            //if (line.length()) {
            //    ImGui::Text(line.c_str());
            //}
            //for (int i = 0; i < 100; i++)
            //    ImGui::Text("%04d: scrollable region", i);

            //ImGui::Text(lines.)
            if (lines.size()) {
                for (const auto& l : lines) {
                    ImGui::Text(l.c_str());
                }
                
                
            }
            ImGui::SetScrollHereY(1);// *0.25f); // 0.0f:top, 0.5f:center, 1.0f:bottom
            //ImGui::SetScrollY(1);
            //ImGui::SetCursorPosY(ImGui::GetWindowHeight());

            

            ImGui::EndChild();
        }

	}

}