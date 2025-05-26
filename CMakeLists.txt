cmake_minimum_required(VERSION 3.31)
project(AudioPlayer)

set(CMAKE_CXX_STANDARD 23)

find_package(imgui CONFIG REQUIRED)
find_package(Vulkan REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(GLEW CONFIG REQUIRED)

add_executable(AudioPlayer main.cpp
        backends/imgui_impl_glfw.cpp
        backends/imgui_impl_opengl3.cpp)

target_link_libraries(AudioPlayer PRIVATE glfw)
target_link_libraries(AudioPlayer PRIVATE spdlog::spdlog)
target_link_libraries(AudioPlayer PRIVATE imgui::imgui)
target_link_libraries(AudioPlayer PRIVATE Vulkan::Vulkan)
target_link_libraries(AudioPlayer PRIVATE GLEW::GLEW)

