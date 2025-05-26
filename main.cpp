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
#include <future>
#include <atomic>
#include <chrono>
#include <thread>
#include <algorithm>

#include "spdlog/spdlog.h"
#include "spdlog/async.h"
#include "spdlog/sinks/stdout_color_sinks.h"

// Forward declaration
struct PlayerState;
void HandleNextTrack(PlayerState& state);

// GLFW Error Callback
void glfw_error_callback(int error, const char* description) {
    spdlog::error("GLFW Error [{}]: {}", error, description);
}

// Player State Structure
struct PlayerState {
    ma_engine engine{};
    ma_sound sound{};
    bool sound_initialized = false;

    std::vector<std::string> track_list;
    int current_track_index = 0;
    bool is_playing = false;
    float volume = 1.0f;

    std::filesystem::path music_directory = "./music/";

    std::future<std::vector<std::string>> music_load_future;
    std::atomic<bool> is_loading_music{false};
    bool was_playing_before_async_load = false;
    std::string playing_song_before_async_load;

    std::atomic<bool> track_ended_flag{false};

    bool show_music_player_window = true; // For ImGui window closing
};

// Miniaudio Sound End Callback
void sound_end_callback(void* pUserData, ma_sound* pSound) {
    if (pUserData == nullptr) {
        spdlog::warn("sound_end_callback received null pUserData.");
        return;
    }
    auto* state = static_cast<PlayerState*>(pUserData);
    state->track_ended_flag.store(true);
}


// --- Initialization Functions ---
void InitializeSpdlog() {
    try {
        auto async_console_logger = spdlog::stdout_color_mt<spdlog::async_factory>("async_console_logger");
        spdlog::set_default_logger(async_console_logger);

        #ifndef NDEBUG
        spdlog::set_level(spdlog::level::debug);
        spdlog::flush_on(spdlog::level::debug);
        #else
        spdlog::set_level(spdlog::level::info);
        spdlog::flush_on(spdlog::level::info);
        #endif
        spdlog::info("Asynchronous Spdlog initialized.");
    } catch (const spdlog::spdlog_ex& ex) {
        fprintf(stderr, "Spdlog async initialization failed: %s\n", ex.what());
    }
}


bool InitializeGLFW(GLFWwindow*& window, const char* window_title) {
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        spdlog::critical("Failed to initialize GLFW");
        return false;
    }

    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE); // Keep backend window hidden
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    window = glfwCreateWindow(1, 1, window_title, nullptr, nullptr); // Hidden backend window
    if (!window) {
        spdlog::critical("Failed to create GLFW window");
        glfwTerminate();
        return false;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync
    spdlog::info("GLFW initialized successfully.");
    return true;
}

bool InitializeGLEW() {
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        spdlog::critical("Failed to initialize GLEW");
        return false;
    }
    spdlog::info("GLEW initialized successfully.");
    return true;
}

bool InitializeImGui(GLFWwindow* window, ImGuiIO*& io_ptr) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io_ptr = &io;

    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // Ensure viewports are enabled

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    if (!ImGui_ImplGlfw_InitForOpenGL(window, true)) {
        spdlog::critical("Failed to initialize ImGui GLFW backend");
        return false;
    }
    if (!ImGui_ImplOpenGL3_Init("#version 330")) {
        spdlog::critical("Failed to initialize ImGui OpenGL3 backend");
        return false;
    }
    spdlog::info("ImGui initialized successfully.");
    return true;
}

bool InitializeMiniaudio(PlayerState& state) {
    ma_result result = ma_engine_init(nullptr, &state.engine);
    if (result != MA_SUCCESS) {
        spdlog::critical("Failed to initialize miniaudio engine: {}", ma_result_description(result));
        return false;
    }
    state.sound_initialized = false;
    spdlog::info("Miniaudio engine initialized successfully.");
    return true;
}

std::vector<std::string> ScanMusicDirectoryWorker(const std::filesystem::path& music_dir_path) {
    std::vector<std::string> found_tracks;
    if (!std::filesystem::exists(music_dir_path)) {
        spdlog::warn("Music directory '{}' does not exist. Attempting to create it.", music_dir_path.string());
        try {
            if (std::filesystem::create_directories(music_dir_path)) {
                spdlog::info("Successfully created music directory: '{}'", music_dir_path.string());
            } else {
                spdlog::error("Could not create music directory: '{}'", music_dir_path.string());
            }
        } catch (const std::filesystem::filesystem_error& e) {
            spdlog::error("Filesystem error while creating directory '{}': {}", music_dir_path.string(), e.what());
            return found_tracks;
        }
    }

    spdlog::info("Scanning for music files in: {}", music_dir_path.string());
    found_tracks.reserve(100);
    try {
        for (const auto& entry : std::filesystem::directory_iterator(music_dir_path)) {
            if (entry.is_regular_file()) {
                std::string extension = entry.path().extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
                if (extension == ".mp3" || extension == ".wav") {
                    found_tracks.push_back(entry.path().string());
                    spdlog::debug("Found track: {}", entry.path().string());
                }
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        spdlog::error("Filesystem error while reading music directory '{}': {}", music_dir_path.string(), e.what());
    }
    return found_tracks;
}

bool InitializeAndPlaySound(PlayerState& state, int track_index_to_play, bool start_playing); // Forward declaration

void TriggerLoadMusicFilesAsync(PlayerState& state, bool is_initial_load = false) {
    if (state.is_loading_music) {
        spdlog::info("Music loading already in progress.");
        return;
    }
    spdlog::info("Starting asynchronous music file loading...");
    state.is_loading_music = true;

    if (!is_initial_load) {
        state.was_playing_before_async_load = state.is_playing;
        if (!state.track_list.empty() && state.current_track_index >= 0 && state.current_track_index < static_cast<int>(state.track_list.size())) {
            state.playing_song_before_async_load = state.track_list[state.current_track_index];
        } else {
            state.playing_song_before_async_load.clear();
        }
        if (state.sound_initialized) {
            ma_sound_stop(&state.sound);
            spdlog::info("Sound stopped due to music list refresh.");
            ma_sound_uninit(&state.sound);
            state.sound_initialized = false;
            spdlog::debug("Uninitialized current sound due to music list refresh.");
        }
        state.is_playing = false;
    }
    state.music_load_future = std::async(std::launch::async, ScanMusicDirectoryWorker, state.music_directory);
}

void ProcessAsyncMusicLoadCompletion(PlayerState& state) {
    if (state.is_loading_music && state.music_load_future.valid()) {
        if (state.music_load_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            spdlog::info("Asynchronous music loading finished.");
            try {
                state.track_list = state.music_load_future.get();
                if (state.track_list.empty()) {
                    spdlog::warn("No audio files (.mp3, .wav) found in '{}'.", state.music_directory.string());
                } else {
                    spdlog::info("Loaded {} tracks.", state.track_list.size());
                }
            } catch (const std::exception& e) {
                spdlog::error("Exception during async music load get: {}", e.what());
                state.track_list.clear();
            }

            state.current_track_index = 0;
            state.is_playing = false;

            if (state.was_playing_before_async_load && !state.playing_song_before_async_load.empty()) {
                auto it = std::find(state.track_list.begin(), state.track_list.end(), state.playing_song_before_async_load);
                if (it != state.track_list.end()) {
                    int new_index = std::distance(state.track_list.begin(), it);
                    spdlog::info("Previously playing song '{}' found after refresh at new index {}.", std::filesystem::path(state.playing_song_before_async_load).filename().string(), new_index);
                    InitializeAndPlaySound(state, new_index, true);
                } else {
                    spdlog::info("Previously playing song '{}' not found after refresh.", std::filesystem::path(state.playing_song_before_async_load).filename().string());
                }
            }
            state.was_playing_before_async_load = false;
            state.playing_song_before_async_load.clear();
            state.is_loading_music = false;
        }
    }
}

void StopCurrentSound(PlayerState& state) {
    if (state.sound_initialized) {
        ma_sound_stop(&state.sound);
        if (!state.track_list.empty() && state.current_track_index >= 0 && state.current_track_index < static_cast<int>(state.track_list.size())) {
             spdlog::info("Sound stopped: {}", std::filesystem::path(state.track_list[state.current_track_index]).filename().string());
        } else {
             spdlog::info("Sound stopped (track info unavailable).");
        }
    }
    state.is_playing = false;
}

void UninitializeCurrentSound(PlayerState& state) {
    if (state.sound_initialized) {
        ma_sound_uninit(&state.sound);
        state.sound_initialized = false;
        spdlog::debug("Uninitialized current sound.");
    }
}

bool InitializeAndPlaySound(PlayerState& state, int track_index_to_play, bool start_playing) {
    UninitializeCurrentSound(state);

    if (state.track_list.empty() || track_index_to_play < 0 || track_index_to_play >= static_cast<int>(state.track_list.size())) {
        spdlog::error("Cannot play track: Invalid track index {} or empty track list.", track_index_to_play);
        state.is_playing = false;
        return false;
    }

    const char* filepath = state.track_list[track_index_to_play].c_str();
    ma_uint32 flags = MA_SOUND_FLAG_STREAM;
    ma_result result = ma_sound_init_from_file(&state.engine, filepath, flags, nullptr, nullptr, &state.sound);

    if (result != MA_SUCCESS) {
        spdlog::error("Failed to initialize sound from file '{}': {}", filepath, ma_result_description(result));
        state.sound_initialized = false;
        state.is_playing = false;
        return false;
    }

    state.sound_initialized = true;
    state.current_track_index = track_index_to_play;
    ma_sound_set_volume(&state.sound, state.volume);
    ma_sound_set_end_callback(&state.sound, sound_end_callback, &state);
    spdlog::info("Sound initialized: {}", std::filesystem::path(filepath).filename().string());

    if (start_playing) {
        ma_sound_start(&state.sound);
        state.is_playing = true;
        spdlog::info("Playback started: {}", std::filesystem::path(filepath).filename().string());
    } else {
        state.is_playing = false;
    }
    return true;
}

void HandlePlayPause(PlayerState& state) {
    if (state.track_list.empty()) {
        spdlog::warn("Play/Pause clicked, but no tracks are loaded.");
        return;
    }
     if (state.current_track_index < 0 || state.current_track_index >= static_cast<int>(state.track_list.size())) {
        spdlog::error("Play/Pause: Invalid current track index {}.", state.current_track_index);
        return;
    }
    std::string current_track_name = std::filesystem::path(state.track_list[state.current_track_index]).filename().string();

    if (state.is_playing) {
        spdlog::info("Pause button clicked for: {}", current_track_name);
        if (state.sound_initialized) {
            ma_sound_stop(&state.sound);
        }
        state.is_playing = false;
        spdlog::info("Playback paused: {}", current_track_name);
    } else {
        spdlog::info("Play button clicked for: {}", current_track_name);
        if (state.sound_initialized && !ma_sound_is_playing(&state.sound)) {
            ma_sound_start(&state.sound);
            state.is_playing = true;
            spdlog::info("Playback resumed: {}", current_track_name);
        } else {
            InitializeAndPlaySound(state, state.current_track_index, true);
        }
    }
}

void HandleNextTrack(PlayerState& state) {
    if (state.track_list.empty()) {
        spdlog::warn("Next track triggered, but no tracks are loaded.");
        return;
    }
    spdlog::info("Next track triggered.");
    int next_track_index = (state.current_track_index + 1) % state.track_list.size();
    bool was_playing = state.is_playing;

    InitializeAndPlaySound(state, next_track_index, was_playing);
    if (was_playing && !state.is_playing && state.sound_initialized) {
         spdlog::warn("Tried to auto-play next track, but an issue occurred or it was not started by InitializeAndPlaySound.");
    } else if (was_playing && state.is_playing) {
        spdlog::info("Now playing next track: {}", std::filesystem::path(state.track_list[next_track_index]).filename().string());
    } else if (!was_playing) {
        spdlog::info("Selected next track (paused/stopped): {}", std::filesystem::path(state.track_list[next_track_index]).filename().string());
    }
}

void HandleVolumeChange(PlayerState& state, float new_volume) {
    spdlog::debug("Volume slider interaction. New attempted volume: {:.2f}", new_volume);
    state.volume = new_volume;
    if (state.sound_initialized) {
        ma_sound_set_volume(&state.sound, state.volume);
        spdlog::info("Volume set to: {:.2f}", state.volume);
    } else {
        spdlog::debug("Volume changed to {:.2f}, but no sound is currently initialized to apply it to.", state.volume);
    }
}

// --- Main Loop and Rendering ---
void RenderUI(PlayerState& state) {
    // If the window is marked for closure (e.g. by user clicking 'x'), don't attempt to render it.
    // The main loop will catch this state and terminate.
    if (!state.show_music_player_window) {
        return;
    }

    // Pass &state.show_music_player_window to ImGui::Begin.
    // If the user clicks the 'x' on the ImGui window, ImGui will set this to false.
    // ImGui::Begin returns false if the window is collapsed, among other things.
    // We must always call ImGui::End() if ImGui::Begin() was called.
    if (ImGui::Begin("Music Player", &state.show_music_player_window)) {
        // ----- UI Content -----
        if (state.is_loading_music) {
            ImGui::Text("Loading music files...");
            ImGui::BeginDisabled(); // Disable button while loading
        }
        if (ImGui::Button("Refresh Music List")) {
            spdlog::info("'Refresh Music List' button clicked.");
            TriggerLoadMusicFilesAsync(state);
        }
        if (state.is_loading_music) {
            ImGui::EndDisabled();
        }
        ImGui::Separator();

        if (!state.track_list.empty()) {
            if (state.current_track_index < 0 || state.current_track_index >= static_cast<int>(state.track_list.size())) {
                spdlog::warn("Track index {} is out of bounds (0-{}). Resetting to 0.", state.current_track_index, state.track_list.size() -1 );
                state.current_track_index = 0;
                if(state.is_playing) StopCurrentSound(state);
                UninitializeCurrentSound(state);
            }

            if (state.current_track_index >= 0 && state.current_track_index < static_cast<int>(state.track_list.size())) {
                std::string track_name = std::filesystem::path(state.track_list[state.current_track_index]).filename().string();
                ImGui::Text("Now Playing: %s", track_name.c_str());

                if (ImGui::Button(state.is_playing ? "Pause" : "Play")) {
                    HandlePlayPause(state);
                }

                ImGui::SameLine();
                if (ImGui::Button("Next")) {
                    HandleNextTrack(state);
                }

                float current_volume = state.volume;
                if (ImGui::SliderFloat("Volume", &current_volume, 0.0f, 1.0f)) {
                    HandleVolumeChange(state, current_volume);
                }
            } else if (!state.is_loading_music) {
                 ImGui::Text("Current track index invalid. Please refresh or select a track.");
            }

        } else if (!state.is_loading_music) {
            ImGui::Text("No tracks found in '%s'", state.music_directory.string().c_str());
            ImGui::Text("Please add MP3 or WAV files and click 'Refresh Music List'.");
        }
        // ----- End UI Content -----
    }
    ImGui::End(); // Always call End if Begin was called.
}

void ProcessAudioEvents(PlayerState& state) {
    if (state.track_ended_flag.exchange(false)) {
        if (state.is_playing) {
            std::string ended_track_name = "Unknown Track";
            if (!state.track_list.empty() && state.current_track_index >=0 && state.current_track_index < static_cast<int>(state.track_list.size())) {
                 ended_track_name = std::filesystem::path(state.track_list[state.current_track_index]).filename().string();
            }
            spdlog::info("Track '{}' ended (callback). Playing next.", ended_track_name);
            HandleNextTrack(state);
        } else {
            spdlog::debug("Track ended (callback), but player was not in 'is_playing' state. Not proceeding to next.");
        }
    }
}

// --- Cleanup ---
void Cleanup(GLFWwindow* window, PlayerState& state) {
    spdlog::info("Starting cleanup...");
    UninitializeCurrentSound(state);
    ma_engine_uninit(&state.engine);
    spdlog::info("Miniaudio engine uninitialized.");

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    spdlog::info("ImGui shutdown.");

    if (window) {
        glfwDestroyWindow(window);
    }
    glfwTerminate();
    spdlog::info("GLFW terminated. Application finished.");
    spdlog::shutdown();
}


int main() {
    InitializeSpdlog();

    GLFWwindow* window = nullptr;
    ImGuiIO* imgui_io = nullptr;
    PlayerState playerState; // playerState.show_music_player_window defaults to true

    if (!InitializeGLFW(window, "Music Player Backend")) { Cleanup(window, playerState); return -1; }
    if (!InitializeGLEW()) { Cleanup(window, playerState); return -1; }
    if (!InitializeImGui(window, imgui_io)) { Cleanup(window, playerState); return -1; }
    if (!InitializeMiniaudio(playerState)) { Cleanup(window, playerState); return -1; }

    TriggerLoadMusicFilesAsync(playerState, true);

    spdlog::info("Main loop starting...");

    const double target_frame_time = 1.0 / 60.0;
    double last_frame_time = glfwGetTime();

    // Main loop continues as long as the (hidden) GLFW window isn't closed AND the ImGui window is not closed by the user.
    while (!glfwWindowShouldClose(window) && playerState.show_music_player_window) {
        double current_time = glfwGetTime();
        double elapsed_time = current_time - last_frame_time;

        if (elapsed_time < target_frame_time) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        last_frame_time = current_time;

        glfwPollEvents();

        ProcessAsyncMusicLoadCompletion(playerState);
        ProcessAudioEvents(playerState);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        RenderUI(playerState);

        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h); // For the backend window
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f); // Clear backend window (mostly unseen with viewports)
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        if (imgui_io && (imgui_io->ConfigFlags & ImGuiConfigFlags_ViewportsEnable)) {
            GLFWwindow* backup_current_context = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup_current_context);
        }
        glfwSwapBuffers(window); // For the backend window
    }

    Cleanup(window, playerState);
    return 0;
}