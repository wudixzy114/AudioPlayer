cmake_minimum_required(VERSION 3.31)
project(AudioPlayer)

set(CMAKE_CXX_STANDARD 23)

find_package(spdlog CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(GLEW CONFIG REQUIRED)

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/imgui)
set(IMGUI_SOURCES
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
        ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

add_executable(AudioPlayer main.cpp
        ${IMGUI_SOURCES})

target_include_directories(AudioPlayer PRIVATE
${IMGUI_DIR}
${IMGUI_DIR}/backends)

target_link_libraries(AudioPlayer PRIVATE glfw)
target_link_libraries(AudioPlayer PRIVATE spdlog::spdlog)
target_link_libraries(AudioPlayer PRIVATE GLEW::GLEW)

