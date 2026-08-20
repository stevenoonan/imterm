
cmake_minimum_required(VERSION 3.15)

if(POLICY CMP0048)
    #policy for VERSION in cmake 3.0
    cmake_policy(SET CMP0048 NEW)
endif()


PROJECT(imterm LANGUAGES C CXX)

include(CTest)

# Set C++20 standard
set(CMAKE_CXX_STANDARD 20) 
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DVK_PROTOTYPES")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DVK_PROTOTYPES")

option(GLFW_USE_SUBMODULE "Use deps/glfw instead of package manager provided glfw" OFF)
option(IMGUI_USE_SUBMODULE "Use deps/imgui instead of package manager provided imgui" OFF)
option(IMTERM_ENABLE_WARNINGS "Enable compiler warnings for first-party library and test targets" ON)
option(IMTERM_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(IMTERM_BUILD_FUZZER "Build the opt-in parser/state libFuzzer target" OFF)

function(imterm_enable_warnings target)
	if(NOT IMTERM_ENABLE_WARNINGS)
		return()
	endif()

	if(MSVC)
		target_compile_options(${target} PRIVATE /W4)
	else()
		target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
	endif()
endfunction()

function(imterm_enable_sanitizers target)
	if(NOT IMTERM_ENABLE_SANITIZERS)
		return()
	endif()

	if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
		target_compile_options(${target} PRIVATE
			-fsanitize=address,undefined
			-fno-omit-frame-pointer
		)
		target_link_options(${target} PRIVATE -fsanitize=address,undefined)
	else()
		message(FATAL_ERROR "IMTERM_ENABLE_SANITIZERS requires Clang or GCC")
	endif()
endfunction()

# GLFW
if (GLFW_USE_SUBMODULE AND EXISTS "${CMAKE_SOURCE_DIR}/deps/glfw/CMakeLists.txt")
	set(GLFW_DIR deps/glfw) # Set this to point to an up-to-date GLFW repo
	option(GLFW_BUILD_EXAMPLES "Build the GLFW example programs" OFF)
	option(GLFW_BUILD_TESTS "Build the GLFW test programs" OFF)
	option(GLFW_BUILD_DOCS "Build the GLFW documentation" OFF)
	option(GLFW_INSTALL "Generate installation target" OFF)
	option(GLFW_DOCUMENT_INTERNALS "Include internals in documentation" OFF)
	option(GLFW_BUILD_WAYLAND "Build GLFW with Wayland support" OFF)
	add_subdirectory(${GLFW_DIR} binary_dir EXCLUDE_FROM_ALL)
	include_directories(${GLFW_DIR}/include)
	# Use vulkan headers from glfw:
	include_directories(${GLFW_DIR}/deps)
else()
	if (GLFW_USE_SUBMODULE)
		message(WARNING "GLFW_USE_SUBMODULE=ON but deps/glfw is empty. Falling back to find_package(glfw3).")
	endif()
	find_package(glfw3 CONFIG REQUIRED)
endif()

# Dear ImGui
if (IMGUI_USE_SUBMODULE AND EXISTS "${CMAKE_SOURCE_DIR}/deps/imgui/imgui.cpp")
	set(IMGUI_DIR deps/imgui)
	include_directories(${IMGUI_DIR} ${IMGUI_DIR}/backends ..)
	option(IMGUI_SHOW_DEMO_WINDOW "Display the Dear ImGui demo window" OFF)
	add_library(imgui STATIC
		${IMGUI_DIR}/imgui.cpp
		${IMGUI_DIR}/imgui_draw.cpp
		${IMGUI_DIR}/imgui_demo.cpp
		${IMGUI_DIR}/imgui_tables.cpp
		${IMGUI_DIR}/imgui_widgets.cpp
	)
	add_library(imgui::imgui ALIAS imgui)
	target_include_directories(imgui PUBLIC ${IMGUI_DIR})
	target_compile_definitions(imgui PUBLIC ImTextureID=ImU64 IMGUI_DISABLE_OBSOLETE_KEYIO=1)
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
include_directories(${IMGUI_DIR})

# beep
add_subdirectory(deps/beep)
include_directories(deps/beep)

# toml++
find_package(tomlplusplus REQUIRED)

# Vulkan
find_package(Vulkan REQUIRED)

# serial
add_subdirectory(deps/serial)
#target_link_libraries(imterm serial)
include_directories(deps/serial/include)

# ImGuiFileDialog
add_subdirectory(deps/ImGuiFileDialog)
include_directories(deps/ImGuiFileDialog)

# Terminal core. Keep this target independent of ImGui, Vulkan, and serial I/O so
# its behavior can be exercised by tests without a display or hardware.
set(SRC_DIR src)
set(IMTERM_CORE_SRCS
	${SRC_DIR}/coordinates.h
	${SRC_DIR}/escape_sequence_parser.cpp
	${SRC_DIR}/escape_sequence_parser.h
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
	${SRC_DIR}/terminal_types.h
)

add_library(imterm_core STATIC ${IMTERM_CORE_SRCS})
target_include_directories(imterm_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/${SRC_DIR})
target_link_libraries(imterm_core PRIVATE beep)
imterm_enable_warnings(imterm_core)
imterm_enable_sanitizers(imterm_core)

# Main application
set(IMTERM_SRCS
	${SRC_DIR}/capture.cpp
	${SRC_DIR}/capture.h
	${SRC_DIR}/imterm.cpp
	${SRC_DIR}/terminal_view.cpp
	${SRC_DIR}/terminal_view.h
)

add_executable(imterm WIN32 
	${IMTERM_SRCS}
	${IMGUI_SRCS}
)

target_link_libraries(
	imterm
	imterm_core
	serial
	beep
	imgui::imgui
	glfw
	Vulkan::Vulkan
	tomlplusplus::tomlplusplus
	ImGuiFileDialog
)

target_compile_definitions(imterm PUBLIC ImTextureID=ImU64 IMGUI_DISABLE_OBSOLETE_KEYIO=1)
imterm_enable_sanitizers(imterm)


if (IMGUI_SHOW_DEMO_WINDOW)
    target_compile_definitions(imterm PUBLIC IMGUI_SHOW_DEMO_WINDOW)
endif()

if(BUILD_TESTING)
	find_package(GTest CONFIG REQUIRED)

	add_executable(imterm_tests
		tests/escape_sequence_parser_test.cpp
		tests/terminal_data_test.cpp
		tests/terminal_command_test.cpp
		tests/terminal_input_test.cpp
		tests/terminal_logger_test.cpp
		tests/terminal_state_test.cpp
	)
	target_link_libraries(imterm_tests PRIVATE imterm_core GTest::gtest_main)
	imterm_enable_warnings(imterm_tests)
	imterm_enable_sanitizers(imterm_tests)

	include(GoogleTest)
	gtest_discover_tests(imterm_tests DISCOVERY_MODE PRE_TEST)
endif()

if(IMTERM_BUILD_FUZZER)
	if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
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
	imterm_enable_warnings(imterm_fuzz_core)

	add_executable(imterm_parser_fuzz tests/parser_fuzz.cpp)
	target_link_libraries(imterm_parser_fuzz PRIVATE imterm_fuzz_core)
	target_compile_options(imterm_parser_fuzz PRIVATE
		-fsanitize=fuzzer,address,undefined
		-fno-omit-frame-pointer
	)
	target_link_options(imterm_parser_fuzz PRIVATE
		-fsanitize=fuzzer,address,undefined
	)
	imterm_enable_warnings(imterm_parser_fuzz)
endif()

#add_custom_target(copy_font 
#	COMMAND ${CMAKE_COMMAND} -E copy_if_different 
#		"${CMAKE_SOURCE_DIR}/resources/fonts/JetBrainsMono-Medium.ttf"
#		$<TARGET_FILE_DIR:${PROJECT_NAME}>
#)
#add_dependencies(${PROJECT_NAME} copy_font)


add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy_if_different 
                   "${CMAKE_SOURCE_DIR}/resources/fonts/JetBrainsMono-Medium.ttf"
                   $<TARGET_FILE_DIR:${PROJECT_NAME}>)
