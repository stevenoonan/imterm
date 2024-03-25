cmake_minimum_required(VERSION 3.6)

project(ImGuiFileDialog)

find_package(imgui CONFIG REQUIRED)

add_library(ImGuiFileDialog STATIC
    ImGuiFileDialog.cpp
    ImGuiFileDialog.h
    ImGuiFileDialogConfig.h
)

message(STATUS "IMGUI_DIR = " ../../${IMGUI_DIR})
#message(STATUS "CMAKE_CURRENT_SOURCE_DIR = " ${CMAKE_CURRENT_SOURCE_DIR})

target_include_directories(ImGuiFileDialog PUBLIC 
	${CMAKE_CURRENT_SOURCE_DIR}
    #../../${IMGUI_DIR}
)

target_link_libraries(ImGuiFileDialog
	imgui::imgui
	#glfw
	#Vulkan::Vulkan
)