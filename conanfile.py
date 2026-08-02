import os

from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMake, CMakeDeps, cmake_layout
from conan.tools.files import copy
import platform


class ImGuiExample(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps", "CMakeToolchain"
    default_options = {
        "glfw/*:with_wayland": True,
        "glfw/*:with_x11": True,
    }

    def requirements(self):
        self.requires("tomlplusplus/3.4.0")
        self.requires("glfw/3.4")
        self.requires("imgui/1.92.8-docking")
        self.requires("vulkan-loader/1.4.350.0")
        self.requires("vulkan-headers/1.4.350.0")
        if self.settings.os == "Linux":
            self.requires("libalsa/1.2.13")

    def generate(self):

        imgui_res_bindings = os.path.join(self.dependencies["imgui"].package_folder,
            "res", "bindings")

        imterm_conan_imgui_bindings = os.path.join(self.source_folder, "deps", "conan", "imgui", "bindings")
        
        copy(self, "*glfw*", imgui_res_bindings, imterm_conan_imgui_bindings)
        #copy(self, "*opengl3*", imgui_res_bindings, imterm_conan_imgui_bindings)
        copy(self, "*vulkan*", imgui_res_bindings, imterm_conan_imgui_bindings)
        copy(self, "*win32*", imgui_res_bindings, imterm_conan_imgui_bindings)

    def layout(self):
        cmake_layout(self)
