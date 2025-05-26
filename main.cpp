#define MINIAUDIO_IMPLEMENTATION
#include <imgui.h>
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_glfw.h"
#include "GL/glew.h"
#include "GLFW/glfw3.h"
#include "miniaudio.h"
#include <filesystem>
#include <vector>
#include <string>
#include <iostream>
#include "spdlog/spdlog.h"

// Set the Error callback for GLFW
void error_callback(int error , const char* description) {
    spdlog::error("GLFW Error: {}, Description: {} ",error,description);
}

int main() {
    // Init GLFW
    glfwSetErrorCallback(error_callback);
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }

    // Set necessary Window Hints for GLFW
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef ___APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT,GL_TRUE);
#endif

    // Create GLFW window
    auto *window = glfwCreateWindow(400,300,"Music Player",nullptr,nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    // Enable vsync (垂直同步)
    glfwSwapInterval(1);

    // Initialize GLEW
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::cerr << "Failed to initialize GLEW" << std::endl;
        spdlog::error("Failed to initialize GLEW");
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window,true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // Initialize miniaudio
    ma_engine engine;
    ma_result result = ma_engine_init(nullptr,&engine);
    if (result != MA_SUCCESS) {
        std::cerr << "Failed to initialize miniaudio: " << result << std::endl;
        spdlog::error("Failed to initialize miniaudio");
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    // Load music files from default directory
    std::vector<std::string> track_list;
    std::string music_dir = "./music/";
    for (const auto &entry: std::filesystem::directory_iterator(music_dir)) {
        if (entry.path().extension() == ".mp3" || entry.path().extension() == ".wav") {
            track_list.push_back(entry.path().string());
        }
    }
    if (track_list.empty()) {
        std::cerr << "No audio files found in " << music_dir << std::endl;
        spdlog::debug("No audio files found in  {}",music_dir);
    }

    // Music player state
    int track_index = 0;
    bool is_playing = false;
    float volume = 1.0f;
    ma_sound sound;

    // Main loop
    spdlog::info("Main Loop Started");
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Start ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // ImGui window for music player controls
        ImGui::Begin("Music Player");
        if (!track_list.empty()) {
            std::string track_name = std::filesystem::path(track_list[track_index]).filename().string();
            ImGui::Text(track_name.c_str());

            // Play/Pause button
            if (ImGui::Button(is_playing ? "Pause" : "Play")) {
                if (is_playing) {
                    ma_sound_stop(&sound);
                    is_playing = false;
                } else {
                    if (!ma_sound_is_playing(&sound)) {
                        ma_sound_init_from_file(&engine,track_list[track_index].c_str(),0,nullptr,nullptr,&sound);
                        ma_sound_set_volume(&sound,volume);
                        ma_sound_start(&sound);
                    }
                    is_playing = true;
                }
            }

            // Next track button
            ImGui::SameLine();
            if (ImGui::Button("Next")) {
                ma_sound_uninit(&sound);
                track_index = (track_index+1)% track_list.size();
                if (is_playing) {
                    ma_sound_init_from_file(&engine, track_list[track_index].c_str(), 0, nullptr, nullptr, &sound);
                    ma_sound_set_volume(&sound, volume);
                    ma_sound_start(&sound);
                }
            }

            // Volume slider
            if (ImGui::SliderFloat("Volume", &volume, 0.0f,1.0f)) {
                ma_sound_set_volume(&sound,volume);
                ma_engine_set_volume(&engine,volume);
            }
        } else {
            ImGui::Text("No tracks found in %s", music_dir.c_str());
        }
        ImGui::End();

        // Render
        glClearColor(0.1f,0.1f,0.1f,1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    // Cleanup
    ma_sound_uninit(&sound);
    ma_engine_uninit(&engine);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}