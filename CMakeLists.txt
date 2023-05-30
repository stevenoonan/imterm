# Example usage:
#  mkdir build
#  cd build
#  cmake -g "Visual Studio 14 2015" .. 



cmake_minimum_required(VERSION 3.20) 

if(POLICY CMP0048)
    #policy for VERSION in cmake 3.0
    cmake_policy(SET CMP0048 NEW)
endif()

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
set(GLFW_DIR deps/glfw) # Set this to point to an up-to-date GLFW repo
option(GLFW_BUILD_EXAMPLES "Build the GLFW example programs" OFF)
option(GLFW_BUILD_TESTS "Build the GLFW test programs" OFF)
option(GLFW_BUILD_DOCS "Build the GLFW documentation" OFF)
option(GLFW_INSTALL "Generate installation target" OFF)
option(GLFW_DOCUMENT_INTERNALS "Include internals in documentation" OFF)
add_subdirectory(${GLFW_DIR} binary_dir EXCLUDE_FROM_ALL)
include_directories(${GLFW_DIR}/include)

# Dear ImGui
set(IMGUI_DIR deps/imgui)
include_directories(${IMGUI_DIR} ${IMGUI_DIR}/backends ..)
option(IMGUI_SHOW_DEMO_WINDOW "Display the Dear ImGui demo window" OFF)

# beep
add_subdirectory(deps/beep)
include_directories(deps/beep)

# toml++
find_package(tomlplusplus)


# Libraries
find_package(Vulkan REQUIRED)
#find_library(VULKAN_LIBRARY
  #NAMES vulkan vulkan-1)
#set(LIBRARIES "glfw;${VULKAN_LIBRARY}")
set(LIBRARIES "glfw;Vulkan::Vulkan;tomlplusplus::tomlplusplus;beep")
#set(LIBRARIES "glfw;Vulkan::Vulkan")

# Use vulkan headers from glfw:
include_directories(${GLFW_DIR}/deps)



include_directories(deps/ImGuiColorTextEdit)

file(GLOB sources src/*.cpp)

add_executable(imterm WIN32 
	${sources} 
	${IMGUI_DIR}/backends/imgui_impl_glfw.cpp 
	${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp 
	${IMGUI_DIR}/imgui.cpp 
	${IMGUI_DIR}/imgui_draw.cpp 
	${IMGUI_DIR}/imgui_demo.cpp 
	${IMGUI_DIR}/imgui_tables.cpp 
	${IMGUI_DIR}/imgui_widgets.cpp
	)

target_link_libraries(imterm ${LIBRARIES})
target_compile_definitions(imterm PUBLIC ImTextureID=ImU64 IMGUI_DISABLE_OBSOLETE_KEYIO=1)
if (IMGUI_SHOW_DEMO_WINDOW)
    target_compile_definitions(imterm PUBLIC IMGUI_SHOW_DEMO_WINDOW)
endif()

add_subdirectory(deps/serial)
target_link_libraries(imterm serial)
include_directories(deps/serial/include)


