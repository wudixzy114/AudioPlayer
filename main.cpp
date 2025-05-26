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
#include "spdlog/spdlog.h"

// Set the Error callback for GLFW
void error_callback(int error , const char* description) {
    spdlog::error("GLFW Error: {}, Description: {} ",error,description);
}

int main() {
    // Init GLFW
    glfwSetErrorCallback(error_callback);
    if (!glfwInit()) {
        spdlog::critical("Failed to initialize GLFW");
        return -1;
    }

    // Set necessary Window Hints for GLFW
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT,GL_TRUE);
#endif

    // Create GLFW window
    auto *window = glfwCreateWindow(1,1,"Music Player Backend",nullptr,nullptr);
    if (!window) {
        spdlog::critical("Failed to create GLFW window");
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    // Enable vsync (垂直同步)
    glfwSwapInterval(1);

    // Initialize GLEW
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        spdlog::critical("Failed to initialize GLEW");
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    ImGui_ImplGlfw_InitForOpenGL(window,true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // Initialize miniaudio
    ma_engine engine;
    ma_result result = ma_engine_init(nullptr,&engine);
    if (result != MA_SUCCESS) {
        spdlog::critical("Failed to initialize miniaudio: {}", ma_result_description(result));
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    // Load music files from default directory
    std::vector<std::string> track_list;
    std::string music_dir = "./music/";
    if (!std::filesystem::exists(music_dir)) {
        spdlog::warn("Music directory {} does not exist. Creating it.", music_dir);
        std::filesystem::create_directory(music_dir);
    }
    try {
        for (const auto &entry: std::filesystem::directory_iterator(music_dir)) {
            if (entry.path().extension() == ".mp3" || entry.path().extension() == ".wav") {
                track_list.push_back(entry.path().string());
                spdlog::info("Found track: {}", entry.path().string());
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        spdlog::error("Filesystem error while reading music directory: {}", e.what());
    }
    if (track_list.empty()) {
        spdlog::warn("No audio files found in  {}",music_dir);
    }

    // Music player state
    int track_index = 0;
    bool is_playing = false;
    float volume = 1.0f;
    ma_sound sound;
    bool sound_initialized = false;

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
            if (track_index < 0 || track_index >= static_cast<int>(track_list.size())) {
                spdlog::debug("Unknown Reason Causing track_index error");
                track_index = 0;
            }
            std::string track_name = std::filesystem::path(track_list[track_index]).filename().string();
            ImGui::Text(track_name.c_str());

            // Play/Pause button
            if (ImGui::Button(is_playing ? "Pause" : "Play")) {
                if (is_playing) {
                    if (sound_initialized) {
                        ma_sound_stop(&sound);
                    }
                    is_playing = false;
                    spdlog::info("Paused: {}", track_name);
                } else {
                    if (sound_initialized && !ma_sound_is_playing(&sound)) {
                        ma_sound_start(&sound);
                    } else {
                        if (sound_initialized) {
                            ma_sound_uninit(&sound);
                        }
                        result = ma_sound_init_from_file(&engine,track_list[track_index].c_str(),0,nullptr,nullptr,&sound);
                        if (result != MA_SUCCESS) {
                            spdlog::error("Failed to init sound from file {}: {}", track_list[track_index], ma_result_description(result));
                            sound_initialized = false;
                        } else {
                            sound_initialized = true;
                            is_playing = false;
                            ma_sound_set_volume(&sound, volume);
                            ma_sound_start(&sound);
                            spdlog::info("Playing: {}", track_name);
                        }
                    }
                    if (sound_initialized) {
                        is_playing = true;
                    }
                }
            }

            // Next track button
            ImGui::SameLine();
            if (ImGui::Button("Next")) {
                if (sound_initialized) {
                    ma_sound_uninit(&sound);
                    sound_initialized = false;
                }
                track_index = (track_index+1)% track_list.size();
                spdlog::info("Next track selected (index {}): {}", track_index, std::filesystem::path(track_list[track_index]).filename().string());
                result = ma_sound_init_from_file(&engine, track_list[track_index].c_str(), 0, nullptr, nullptr, &sound);
                if (result == MA_SUCCESS) {
                    sound_initialized = true;
                    ma_sound_set_volume(&sound, volume);
                    ma_sound_start(&sound);
                    is_playing = true;
                    spdlog::info("Auto-playing next track: {}", std::filesystem::path(track_list[track_index]).filename().string());
                } else {
                    spdlog::error("Failed to init sound for next track {}: {}", track_list[track_index], ma_result_description(result));
                    sound_initialized = false;
                    is_playing = false;
                }
            }

            // Volume slider
            if (ImGui::SliderFloat("Volume", &volume, 0.0f,1.0f)) {
                if (sound_initialized) {
                    ma_sound_set_volume(&sound,volume);
                }
                spdlog::debug("Volume changed to: {}", volume);
            }
        } else {
            ImGui::Text("No tracks found in %s", music_dir.c_str());
            ImGui::Text("Please add MP3 or WAV files to that directory.");
        }
        ImGui::End();

        // Render
        int display_w;
        int display_h;
        glfwGetFramebufferSize(window,&display_w,&display_h);
        glViewport(0,0,display_w,display_h);
        glClearColor(0.1f,0.1f,0.1f,1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            auto *backup_current_context = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup_current_context);
        }
        glfwSwapBuffers(window);

        if (sound_initialized && is_playing && !track_list.empty()) {
            if (ma_sound_at_end(&sound)) {
                spdlog::info("Track {} ended, playing next.", std::filesystem::path(track_list[track_index]).filename().string());
                ma_sound_uninit(&sound); // 反初始化当前声音
                sound_initialized = false;
                track_index = (track_index + 1) % track_list.size();
                result = ma_sound_init_from_file(&engine, track_list[track_index].c_str(), 0, nullptr, nullptr, &sound);
                if (result == MA_SUCCESS) {
                    sound_initialized = true;
                    ma_sound_set_volume(&sound, volume);
                    ma_sound_start(&sound);
                    is_playing = true; // 确保状态正确
                    spdlog::info("Auto-playing next track: {}", std::filesystem::path(track_list[track_index]).filename().string());
                } else {
                    spdlog::error("Failed to init sound for auto-next track {}: {}", track_list[track_index], ma_result_description(result));
                    is_playing = false; // 播放失败
                }
            }
        }
    }

    // Cleanup
    spdlog::info("Cleaning up resources.");
    if (sound_initialized) {
        ma_sound_uninit(&sound);
    }
    ma_engine_uninit(&engine);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    spdlog::info("Application terminated.");
    return 0;
}