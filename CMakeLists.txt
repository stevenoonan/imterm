cmake_minimum_required(VERSION 3.24)
project(imterm LANGUAGES C CXX)

include(CTest)
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build configuration" FORCE)
endif()

option(GLFW_USE_SUBMODULE "Use deps/glfw instead of package manager provided glfw" OFF)
option(IMGUI_USE_SUBMODULE "Use deps/imgui instead of package manager provided imgui" OFF)
option(IMTERM_ENABLE_WARNINGS "Enable compiler warnings for first-party targets" ON)
option(IMTERM_WARNINGS_AS_ERRORS "Treat first-party warnings as errors" OFF)
option(IMTERM_ENABLE_GRAPHICS_TESTS "Run startup/failure/resize tests (requires a display and Vulkan driver)" OFF)
option(IMGUI_SHOW_DEMO_WINDOW "Display the Dear ImGui demo window" OFF)
option(IMTERM_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(IMTERM_BUILD_FUZZER "Build the opt-in parser/state libFuzzer target" OFF)

function(imterm_enable_warnings target)
	if(NOT IMTERM_ENABLE_WARNINGS AND NOT IMTERM_WARNINGS_AS_ERRORS)
		return()
	endif()

	if(MSVC)
		target_compile_options(${target} PRIVATE /W4)
        if(IMTERM_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
	else()
		target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(IMTERM_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
	endif()
endfunction()

function(imterm_enable_sanitizers target)
	if(NOT IMTERM_ENABLE_SANITIZERS)
		return()
	endif()

	if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU" AND NOT MSVC)
		target_compile_options(${target} PRIVATE
			-fsanitize=address,undefined
			-fno-omit-frame-pointer
		)
		target_link_options(${target} PRIVATE -fsanitize=address,undefined)
	else()
		message(FATAL_ERROR "IMTERM_ENABLE_SANITIZERS requires GCC or Clang with the GNU driver")
	endif()
endfunction()

# GLFW
if (GLFW_USE_SUBMODULE AND EXISTS "${CMAKE_SOURCE_DIR}/deps/glfw/CMakeLists.txt")
	set(GLFW_DIR deps/glfw)
	option(GLFW_BUILD_EXAMPLES "Build the GLFW example programs" OFF)
	option(GLFW_BUILD_TESTS "Build the GLFW test programs" OFF)
	option(GLFW_BUILD_DOCS "Build the GLFW documentation" OFF)
	option(GLFW_INSTALL "Generate installation target" OFF)
	option(GLFW_DOCUMENT_INTERNALS "Include internals in documentation" OFF)
	option(GLFW_BUILD_WAYLAND "Build GLFW with Wayland support" OFF)
	add_subdirectory(${GLFW_DIR} deps/glfw-build EXCLUDE_FROM_ALL)
else()
	if (GLFW_USE_SUBMODULE)
		message(WARNING "GLFW_USE_SUBMODULE=ON but deps/glfw is empty. Falling back to find_package(glfw3).")
	endif()
	find_package(glfw3 CONFIG REQUIRED)
endif()

# Dear ImGui
if (IMGUI_USE_SUBMODULE AND EXISTS "${CMAKE_SOURCE_DIR}/deps/imgui/imgui.cpp")
	set(IMGUI_DIR deps/imgui)
	add_library(imgui STATIC
		${IMGUI_DIR}/imgui.cpp
		${IMGUI_DIR}/imgui_draw.cpp
		${IMGUI_DIR}/imgui_demo.cpp
		${IMGUI_DIR}/imgui_tables.cpp
		${IMGUI_DIR}/imgui_widgets.cpp
	)
	add_library(imgui::imgui ALIAS imgui)
	target_include_directories(imgui SYSTEM PUBLIC ${IMGUI_DIR})
	target_compile_definitions(imgui PUBLIC ImTextureID=ImU64 IMGUI_DISABLE_OBSOLETE_KEYIO=1 IMGUI_USE_WCHAR32=1)
	set(IMGUI_SRCS
		${IMGUI_DIR}/backends/imgui_impl_glfw.cpp 
		${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp 
	)
else()
	if (IMGUI_USE_SUBMODULE)
		message(WARNING "IMGUI_USE_SUBMODULE=ON but deps/imgui is empty. Falling back to find_package(imgui).")
	endif()
	find_package(imgui CONFIG REQUIRED)
	set(IMGUI_DIR deps/conan/imgui/bindings)
	set(IMGUI_SRCS
		${IMGUI_DIR}/imgui_impl_glfw.cpp
		${IMGUI_DIR}/imgui_impl_vulkan.cpp
		${IMGUI_DIR}/imgui_impl_glfw.h
		${IMGUI_DIR}/imgui_impl_vulkan.h
	)
endif()

# beep
add_subdirectory(deps/beep)
target_include_directories(beep SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/deps/beep)

# toml++
find_package(tomlplusplus REQUIRED)

# Vulkan
find_package(Vulkan REQUIRED)

# serial
add_subdirectory(deps/serial)
target_include_directories(serial SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/deps/serial/include)

# ImGuiFileDialog
add_subdirectory(deps/ImGuiFileDialog)
target_compile_features(ImGuiFileDialog PRIVATE cxx_std_17)
target_include_directories(ImGuiFileDialog SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/deps/ImGuiFileDialog)

# Keep backend/example warnings outside the first-party policy. Include paths and
# ABI definitions propagate through dependencies, never through directory scope.
add_library(imterm_backends STATIC ${IMGUI_SRCS})
if(IMGUI_USE_SUBMODULE AND TARGET imgui)
    target_include_directories(imterm_backends SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/${IMGUI_DIR}/backends)
else()
    target_include_directories(imterm_backends SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/${IMGUI_DIR})
endif()
target_link_libraries(imterm_backends PUBLIC imgui::imgui glfw Vulkan::Vulkan)
target_compile_definitions(imterm_backends PUBLIC IMGUI_USE_WCHAR32=1 ImTextureID=ImU64 IMGUI_DISABLE_OBSOLETE_KEYIO=1)

function(imterm_configure_target target)
    target_compile_features(${target} PUBLIC cxx_std_20)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
    if(WIN32)
        target_compile_definitions(${target} PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
    endif()
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8 /permissive-)
        # Standard C environment/time APIs are used on all platforms.
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    imterm_enable_warnings(${target})
    imterm_enable_sanitizers(${target})
endfunction()

# Terminal core. Keep this target independent of ImGui, Vulkan, and serial I/O so
# its behavior can be exercised by tests without a display or hardware.
set(SRC_DIR src)
set(IMTERM_CORE_SRCS
	${SRC_DIR}/coordinates.h
	${SRC_DIR}/escape_sequence_parser.cpp
	${SRC_DIR}/escape_sequence_parser.h
	${SRC_DIR}/interface_zoom.cpp
	${SRC_DIR}/interface_zoom.h
	${SRC_DIR}/raw_serial_capture.cpp
	${SRC_DIR}/raw_serial_capture.h
	${SRC_DIR}/terminal_data.cpp
	${SRC_DIR}/terminal_data.h
	${SRC_DIR}/terminal_command.cpp
	${SRC_DIR}/terminal_command.h
	${SRC_DIR}/terminal_coordinates.h
	${SRC_DIR}/terminal_state.cpp
	${SRC_DIR}/terminal_state.h
	${SRC_DIR}/terminal_logger.cpp
	${SRC_DIR}/terminal_logger.h
	${SRC_DIR}/terminal_input.cpp
	${SRC_DIR}/terminal_input.h
	${SRC_DIR}/terminal_selection.cpp
	${SRC_DIR}/terminal_selection.h
	${SRC_DIR}/terminal_viewport.cpp
	${SRC_DIR}/terminal_viewport.h
	${SRC_DIR}/terminal_palette.cpp
	${SRC_DIR}/terminal_palette.h
	${SRC_DIR}/utf8.h
	${SRC_DIR}/terminal_types.h
)

add_library(imterm_core STATIC ${IMTERM_CORE_SRCS})
target_include_directories(imterm_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/${SRC_DIR})
target_link_libraries(imterm_core PRIVATE beep)
imterm_configure_target(imterm_core)

find_package(Threads REQUIRED)
add_library(imterm_session STATIC
	${SRC_DIR}/capture_settings.cpp
	${SRC_DIR}/capture_settings.h
	${SRC_DIR}/capture_session.cpp
	${SRC_DIR}/capture_session.h
	${SRC_DIR}/serial_transport.cpp
	${SRC_DIR}/serial_transport.h
)
target_link_libraries(imterm_session PUBLIC imterm_core serial PRIVATE Threads::Threads tomlplusplus::tomlplusplus)
imterm_configure_target(imterm_session)

add_library(imterm_view STATIC
	${SRC_DIR}/terminal_view.cpp
	${SRC_DIR}/terminal_view.h
	${SRC_DIR}/terminal_renderer.cpp
	${SRC_DIR}/terminal_renderer.h
	${SRC_DIR}/terminal_keyboard.cpp
	${SRC_DIR}/terminal_keyboard.h
)
target_link_libraries(imterm_view PUBLIC imterm_core imgui::imgui)
target_compile_definitions(imterm_view PUBLIC ImTextureID=ImU64 IMGUI_DISABLE_OBSOLETE_KEYIO=1)
imterm_configure_target(imterm_view)

# The shell is shared by the desktop entry point and real graphics lifecycle tests.
add_library(imterm_application STATIC
    src/application.cpp
    src/application.h
    src/vulkan_renderer.cpp
    src/vulkan_renderer.h
    src/capture.cpp
    src/capture.h
    src/connection_dialog_layout.h
)
target_link_libraries(imterm_application PUBLIC imterm_session imterm_view
    PRIVATE imterm_backends ImGuiFileDialog)
imterm_configure_target(imterm_application)
if(IMGUI_SHOW_DEMO_WINDOW)
    target_compile_definitions(imterm_application PRIVATE IMGUI_SHOW_DEMO_WINDOW)
endif()

add_executable(imterm WIN32 src/imterm.cpp)
target_link_libraries(imterm PRIVATE imterm_application)
imterm_configure_target(imterm)

if(BUILD_TESTING)
	find_package(GTest CONFIG REQUIRED)

	add_executable(imterm_tests
		tests/capture_settings_test.cpp
		tests/capture_session_test.cpp
		tests/escape_sequence_parser_test.cpp
		tests/interface_zoom_test.cpp
		tests/raw_serial_capture_test.cpp
		tests/terminal_data_test.cpp
		tests/terminal_command_test.cpp
		tests/terminal_input_test.cpp
		tests/terminal_selection_test.cpp
		tests/utf8_test.cpp
		tests/terminal_logger_test.cpp
		tests/terminal_state_test.cpp
	)
	target_link_libraries(imterm_tests PRIVATE
		imterm_core
		imterm_session
		GTest::gtest_main
		tomlplusplus::tomlplusplus
	)
	imterm_configure_target(imterm_tests)

	include(GoogleTest)
	gtest_discover_tests(imterm_tests DISCOVERY_MODE PRE_TEST PROPERTIES TIMEOUT 60 LABELS "core")

	# Exercise UI layout and draw state without a display or renderer backend.
	add_executable(imterm_ui_tests
		tests/capture_ui_test.cpp
		tests/interface_zoom_ui_test.cpp
		tests/terminal_encoding_ui_test.cpp
		tests/terminal_view_ui_test.cpp
		${SRC_DIR}/capture.cpp
	)
	target_link_libraries(imterm_ui_tests PRIVATE
		imterm_core
		imterm_session
		imterm_view
		imgui::imgui
		GTest::gtest_main
		tomlplusplus::tomlplusplus
		ImGuiFileDialog
	)
	if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
		target_link_libraries(imterm_ui_tests PRIVATE serial)
	endif()
	target_compile_definitions(imterm_ui_tests PRIVATE ImTextureID=ImU64 IMGUI_DISABLE_OBSOLETE_KEYIO=1)
	imterm_configure_target(imterm_ui_tests)
	gtest_discover_tests(imterm_ui_tests DISCOVERY_MODE PRE_TEST PROPERTIES TIMEOUT 60 LABELS "ui")

    if(IMTERM_ENABLE_GRAPHICS_TESTS)
        add_executable(imterm_application_tests tests/application_test.cpp)
        target_link_libraries(imterm_application_tests PRIVATE imterm_application GTest::gtest_main)
        imterm_configure_target(imterm_application_tests)
        # One process tests repeated initialization and failure recovery. GLFW
        # requires the main thread; never run these cases concurrently in-process.
        add_test(NAME ApplicationLifecycle COMMAND imterm_application_tests)
        set_tests_properties(ApplicationLifecycle PROPERTIES TIMEOUT 180 LABELS "graphics")
        add_custom_command(TARGET imterm_application_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/resources/fonts/JetBrainsMono-Medium.ttf"
                "$<TARGET_FILE_DIR:imterm_application_tests>" VERBATIM)
    endif()
endif()

if(IMTERM_BUILD_FUZZER)
	if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR MSVC)
		message(FATAL_ERROR "IMTERM_BUILD_FUZZER requires Clang with libFuzzer support")
	endif()

	# Build a separately instrumented copy so coverage and UB checks include the
	# parser/state implementation rather than only the fuzz harness.
	add_library(imterm_fuzz_core STATIC ${IMTERM_CORE_SRCS})
	target_include_directories(imterm_fuzz_core PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR}/${SRC_DIR}
	)
	target_link_libraries(imterm_fuzz_core PRIVATE beep)
	target_compile_options(imterm_fuzz_core PRIVATE
		-fsanitize=fuzzer-no-link,address,undefined
		-fno-omit-frame-pointer
	)
	imterm_configure_target(imterm_fuzz_core)

	add_executable(imterm_parser_fuzz tests/parser_fuzz.cpp)
	target_link_libraries(imterm_parser_fuzz PRIVATE imterm_fuzz_core)
	target_compile_options(imterm_parser_fuzz PRIVATE
		-fsanitize=fuzzer,address,undefined
		-fno-omit-frame-pointer
	)
	target_link_options(imterm_parser_fuzz PRIVATE
		-fsanitize=fuzzer,address,undefined
	)
	imterm_configure_target(imterm_parser_fuzz)
endif()

add_custom_command(TARGET imterm POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/resources/fonts/JetBrainsMono-Medium.ttf"
        "$<TARGET_FILE_DIR:imterm>"
    VERBATIM)
