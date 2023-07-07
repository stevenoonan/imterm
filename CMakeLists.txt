
cmake_minimum_required(VERSION 3.20) 

if(POLICY CMP0048)
    #policy for VERSION in cmake 3.0
    cmake_policy(SET CMP0048 NEW)
endif()

cmake_policy(SET CMP0117 OLD)

# Copy font to build directory, it must placed before any targets are defined.
configure_file("resources/fonts/JetBrainsMono-Medium.ttf" ${CMAKE_BINARY_DIR} COPYONLY)


PROJECT(imterm LANGUAGES C CXX)

# Set C++20 standard
set(CMAKE_CXX_STANDARD 20) 
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DVK_PROTOTYPES")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DVK_PROTOTYPES")

# GLFW
if (GLFW_USE_SUBMODULE)
	set(GLFW_DIR deps/glfw) # Set this to point to an up-to-date GLFW repo
	option(GLFW_BUILD_EXAMPLES "Build the GLFW example programs" OFF)
	option(GLFW_BUILD_TESTS "Build the GLFW test programs" OFF)
	option(GLFW_BUILD_DOCS "Build the GLFW documentation" OFF)
	option(GLFW_INSTALL "Generate installation target" OFF)
	option(GLFW_DOCUMENT_INTERNALS "Include internals in documentation" OFF)
	add_subdirectory(${GLFW_DIR} binary_dir EXCLUDE_FROM_ALL)
	include_directories(${GLFW_DIR}/include)
	# Use vulkan headers from glfw:
	include_directories(${GLFW_DIR}/deps)
else()
	find_package(glfw3 CONFIG)
endif()

# Dear ImGui
if (IMGUI_USE_SUBMODULE)
	set(IMGUI_DIR deps/imgui)
	include_directories(${IMGUI_DIR} ${IMGUI_DIR}/backends ..)
	option(IMGUI_SHOW_DEMO_WINDOW "Display the Dear ImGui demo window" OFF)
	target_compile_definitions(imterm PUBLIC ImTextureID=ImU64 IMGUI_DISABLE_OBSOLETE_KEYIO=1)
	set(IMGUI_SRCS
		${IMGUI_DIR}/backends/imgui_impl_glfw.cpp 
		${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp 
		${IMGUI_DIR}/imgui.cpp 
		${IMGUI_DIR}/imgui_draw.cpp 
		${IMGUI_DIR}/imgui_demo.cpp 
		${IMGUI_DIR}/imgui_tables.cpp 
		${IMGUI_DIR}/imgui_widgets.cpp
	)
else()
	find_package(imgui CONFIG)
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
find_package(tomlplusplus)

# Vulkan
find_package(Vulkan REQUIRED)

# serial
add_subdirectory(deps/serial)
#target_link_libraries(imterm serial)
include_directories(deps/serial/include)

# Main code
set(SRC_DIR src)
set(IMTERM_SRCS
	${SRC_DIR}/capture.cpp
	${SRC_DIR}/capture.h
	${SRC_DIR}/coordinates.h
	${SRC_DIR}/escape_sequence_parser.cpp
	${SRC_DIR}/escape_sequence_parser.h
	${SRC_DIR}/imterm.cpp
	${SRC_DIR}/terminal_data.cpp
	${SRC_DIR}/terminal_data.h
	${SRC_DIR}/terminal_state.cpp
	${SRC_DIR}/terminal_state.h
	${SRC_DIR}/terminal_view.cpp
	${SRC_DIR}/terminal_view.h
	${SRC_DIR}/vector_timed.h
)

add_executable(imterm WIN32 
	${IMTERM_SRCS}
	${IMGUI_SRCS}
)

target_link_libraries(
	imterm
	serial
	beep
	imgui::imgui
	glfw
	Vulkan::Vulkan
	tomlplusplus::tomlplusplus
)


if (IMGUI_SHOW_DEMO_WINDOW)
    target_compile_definitions(imterm PUBLIC IMGUI_SHOW_DEMO_WINDOW)
endif()




