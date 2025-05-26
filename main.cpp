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
#include "spdlog/spdlog.h" // Make sure spdlog is included and set up

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

    std::string music_directory = "./music/";

    // Ensure engine and sound are properly uninitialized if necessary
    // This is more of a conceptual note for RAII if PlayerState were a class
    // For a struct used this way, manual cleanup is handled in the main Cleanup function.
};

// --- Initialization Functions ---
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

    window = glfwCreateWindow(1, 1, window_title, nullptr, nullptr);
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
    io_ptr = &io; // Assign to the output parameter

    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

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
    state.sound_initialized = false; // Sound itself is not loaded yet
    spdlog::info("Miniaudio engine initialized successfully.");
    return true;
}

void LoadMusicFiles(PlayerState& state) {
    state.track_list.clear();
    if (!std::filesystem::exists(state.music_directory)) {
        spdlog::warn("Music directory '{}' does not exist. Attempting to create it.", state.music_directory);
        try {
            if (std::filesystem::create_directory(state.music_directory)) {
                spdlog::info("Successfully created music directory: '{}'", state.music_directory);
            } else {
                spdlog::error("Could not create music directory: '{}'", state.music_directory);
                // Proceeding, but track list will be empty.
            }
        } catch (const std::filesystem::filesystem_error& e) {
            spdlog::error("Filesystem error while creating directory '{}': {}", state.music_directory, e.what());
            return; // Can't proceed if directory creation fails badly
        }
    }

    spdlog::info("Scanning for music files in: {}", state.music_directory);
    try {
        for (const auto& entry : std::filesystem::directory_iterator(state.music_directory)) {
            if (entry.is_regular_file()) {
                std::string extension = entry.path().extension().string();
                if (extension == ".mp3" || extension == ".wav") {
                    state.track_list.push_back(entry.path().string());
                    spdlog::info("Found track: {}", entry.path().string());
                }
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        spdlog::error("Filesystem error while reading music directory '{}': {}", state.music_directory, e.what());
    }

    if (state.track_list.empty()) {
        spdlog::warn("No audio files (.mp3, .wav) found in '{}'.", state.music_directory);
    } else {
        spdlog::info("Loaded {} tracks.", state.track_list.size());
    }
    state.current_track_index = 0; // Reset index
    state.is_playing = false;
    if (state.sound_initialized) { // Uninitialize any previously loaded sound
        ma_sound_uninit(&state.sound);
        state.sound_initialized = false;
    }
}

// --- Audio Control Functions ---
void StopCurrentSound(PlayerState& state) {
    if (state.sound_initialized) {
        ma_sound_stop(&state.sound);
        spdlog::info("Sound stopped: {}", std::filesystem::path(state.track_list[state.current_track_index]).filename().string());
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
    UninitializeCurrentSound(state); // Ensure previous sound is cleaned up

    if (state.track_list.empty() || track_index_to_play < 0 || track_index_to_play >= static_cast<int>(state.track_list.size())) {
        spdlog::error("Cannot play track: Invalid track index {} or empty track list.", track_index_to_play);
        state.is_playing = false;
        return false;
    }

    const char* filepath = state.track_list[track_index_to_play].c_str();
    ma_result result = ma_sound_init_from_file(&state.engine, filepath, 0, nullptr, nullptr, &state.sound);

    if (result != MA_SUCCESS) {
        spdlog::error("Failed to initialize sound from file '{}': {}", filepath, ma_result_description(result));
        state.sound_initialized = false;
        state.is_playing = false;
        return false;
    }

    state.sound_initialized = true;
    state.current_track_index = track_index_to_play; // Update current track index
    ma_sound_set_volume(&state.sound, state.volume);
    spdlog::info("Sound initialized: {}", std::filesystem::path(filepath).filename().string());

    if (start_playing) {
        ma_sound_start(&state.sound);
        state.is_playing = true;
        spdlog::info("Playback started: {}", std::filesystem::path(filepath).filename().string());
    } else {
        state.is_playing = false; // Initialized but not started (e.g. if just loading next track)
    }
    return true;
}

// --- UI Interaction Handlers ---
void HandlePlayPause(PlayerState& state) {
    if (state.track_list.empty()) {
        spdlog::warn("Play/Pause clicked, but no tracks are loaded.");
        return;
    }
    std::string current_track_name = std::filesystem::path(state.track_list[state.current_track_index]).filename().string();

    if (state.is_playing) {
        spdlog::info("Pause button clicked for: {}", current_track_name);
        if (state.sound_initialized) {
            ma_sound_stop(&state.sound); // Using stop, miniaudio typically resumes from current spot if started again.
                                         // If you want true pause/resume, miniaudio handles this by just calling start again on a stopped sound.
        }
        state.is_playing = false;
        spdlog::info("Playback paused: {}", current_track_name);
    } else { // Not playing or was paused
        spdlog::info("Play button clicked for: {}", current_track_name);
        if (state.sound_initialized && !ma_sound_is_playing(&state.sound)) {
            // Sound is loaded and was paused, so just resume
            ma_sound_start(&state.sound);
            state.is_playing = true;
            spdlog::info("Playback resumed: {}", current_track_name);
        } else {
            // Sound not loaded, or new track selected, or was fully stopped. Re-initialize.
            InitializeAndPlaySound(state, state.current_track_index, true);
        }
    }
}

void HandleNextTrack(PlayerState& state) {
    if (state.track_list.empty()) {
        spdlog::warn("Next button clicked, but no tracks are loaded.");
        return;
    }
    spdlog::info("Next button clicked.");
    int next_track_index = (state.current_track_index + 1) % state.track_list.size();
    bool was_playing = state.is_playing; // Play next track if one was already playing

    InitializeAndPlaySound(state, next_track_index, was_playing);
    if (was_playing && !state.is_playing && state.sound_initialized) {
         // If it was meant to play but InitializeAndPlaySound set is_playing to false due to an issue
         spdlog::warn("Tried to auto-play next track, but an issue occurred.");
    } else if (was_playing && state.is_playing) {
        spdlog::info("Now playing next track: {}", std::filesystem::path(state.track_list[next_track_index]).filename().string());
    } else if (!was_playing) {
        spdlog::info("Selected next track (paused): {}", std::filesystem::path(state.track_list[next_track_index]).filename().string());
    }
}

void HandleVolumeChange(PlayerState& state, float new_volume) {
    // Log the interaction even if volume doesn't change due to no sound
    spdlog::debug("Volume slider interaction. New attempted volume: {:.2f}", new_volume);
    state.volume = new_volume; // Update internal volume state
    if (state.sound_initialized) {
        ma_sound_set_volume(&state.sound, state.volume);
        spdlog::info("Volume set to: {:.2f}", state.volume);
    } else {
        spdlog::debug("Volume changed to {:.2f}, but no sound is currently initialized to apply it to.", state.volume);
    }
}

// --- Main Loop and Rendering ---
void RenderUI(PlayerState& state) {
    ImGui::Begin("Music Player");

    if (ImGui::Button("Refresh Music List")) {
        spdlog::info("'Refresh Music List' button clicked.");
        bool was_playing_before_refresh = state.is_playing;
        std::string playing_song_before_refresh;
        if (was_playing_before_refresh && !state.track_list.empty() && state.current_track_index < state.track_list.size()) {
            playing_song_before_refresh = state.track_list[state.current_track_index];
        }

        StopCurrentSound(state); // Stop before reloading list
        UninitializeCurrentSound(state);
        LoadMusicFiles(state); // Reloads tracks, resets index and playing state

        // Try to re-select and optionally resume the previously playing song if it still exists
        if(was_playing_before_refresh && !playing_song_before_refresh.empty()) {
            auto it = std::ranges::find(state.track_list.begin(), state.track_list.end(), playing_song_before_refresh);
            if (it != state.track_list.end()) {
                int new_index = std::distance(state.track_list.begin(), it);
                spdlog::info("Previously playing song '{}' found after refresh at new index {}.", std::filesystem::path(playing_song_before_refresh).filename().string(), new_index);
                InitializeAndPlaySound(state, new_index, true); // Attempt to resume playing
            } else {
                spdlog::info("Previously playing song '{}' not found after refresh.", std::filesystem::path(playing_song_before_refresh).filename().string());
                // state.is_playing is already false from LoadMusicFiles or StopCurrentSound
            }
        }
    }
    ImGui::Separator();

    if (!state.track_list.empty()) {
        // Boundary check for current_track_index (should be handled by LoadMusicFiles and HandleNextTrack)
        if (state.current_track_index < 0 || state.current_track_index >= static_cast<int>(state.track_list.size())) {
            spdlog::warn("Track index {} is out of bounds (0-{}). Resetting to 0.", state.current_track_index, state.track_list.size() -1 );
            state.current_track_index = 0;
            if(state.is_playing) StopCurrentSound(state); // Stop if it was erroneously playing an out-of-bounds track
            UninitializeCurrentSound(state);
        }

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
    } else {
        ImGui::Text("No tracks found in '%s'", state.music_directory.c_str());
        ImGui::Text("Please add MP3 or WAV files and click 'Refresh Music List'.");
    }
    ImGui::End();
}

void UpdateAutomaticNextTrack(PlayerState& state) {
    if (state.sound_initialized && state.is_playing && !state.track_list.empty()) {
        if (ma_sound_at_end(&state.sound)) {
            std::string ended_track_name = std::filesystem::path(state.track_list[state.current_track_index]).filename().string();
            spdlog::info("Track '{}' ended. Playing next.", ended_track_name);
            HandleNextTrack(state); // This will also start playing if successful
            if (!state.is_playing && state.sound_initialized) { // If HandleNextTrack loaded but didn't start (e.g. last song and no loop)
                 // This case might need more logic if you want specific behavior for end of list
                spdlog::info("Reached end of playlist or failed to auto-play next.");
            } else if (state.is_playing) {
                 spdlog::info("Auto-playing next track: {}", std::filesystem::path(state.track_list[state.current_track_index]).filename().string());
            }
        }
    }
}

// --- Cleanup ---
void Cleanup(GLFWwindow* window, PlayerState& state) {
    spdlog::info("Starting cleanup...");
    UninitializeCurrentSound(state); // Uninit the sound first
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
}


int main() {
    // Initialize spdlog if you have a custom setup, otherwise defaults are fine.
    // spdlog::set_level(spdlog::level::debug); // Example: set log level

    GLFWwindow* window = nullptr;
    ImGuiIO* imgui_io = nullptr; // Pointer to get ImGuiIO after init
    PlayerState playerState;

    if (!InitializeGLFW(window, "Music Player Backend")) return -1;
    if (!InitializeGLEW()) { Cleanup(window, playerState); return -1; }
    if (!InitializeImGui(window, imgui_io)) { Cleanup(window, playerState); return -1; } // Pass address of imgui_io
    if (!InitializeMiniaudio(playerState)) { Cleanup(window, playerState); return -1; }

    LoadMusicFiles(playerState);

    spdlog::info("Main loop starting...");
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        RenderUI(playerState);

        // Rendering backend
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f); // Backend window color (mostly unseen)
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        if (imgui_io && (imgui_io->ConfigFlags & ImGuiConfigFlags_ViewportsEnable)) {
            GLFWwindow* backup_current_context = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup_current_context);
        }
        glfwSwapBuffers(window);

        UpdateAutomaticNextTrack(playerState);
    }

    Cleanup(window, playerState);
    return 0;
}