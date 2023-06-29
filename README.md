# imterm
Terminal with ANSI escape sequence support.

Serial port is the only interface currently supported. It is a major work in 
progress, a lot of cleanup and refactoring is currently needed. That being said,
it currently accomplishes my initial goal of being compatiable with ESP32 console
features (line editing, coloring, history, etc.).

Future enhancements will contains features that help me develop and debug 
embedded systems, such as timestamping and hexadecimal view.

Written in C++20 with multiplatform, hardware accelerated GUI based on [Dear ImGui](https://github.com/ocornut/imgui).
I have tested it in Windows and Linux. Mac should not be difficult to get working.

Uses [conan](https://conan.io/) and CMake. Submodules are currently used for 
`glfw` and `imgui` projects, but will likely be moved to conan soon.

The [terminal view](src/terminal_view.h) is originally based on [ImGuiColorTextEdit](https://github.com/BalazsJako/ImGuiColorTextEdit).

The below image shows `imterm` connecting to an ESP32 C6 [console](https://github.com/espressif/esp-idf/tree/master/examples/system/console) project.
Tab completion is shown.

![imterm with esp32 console](resources/images/imterm_v001.gif)