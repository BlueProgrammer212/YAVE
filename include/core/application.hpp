#pragma once

#include <iostream>
#include <memory>

#define IMGUI_DEFINE_MATH_OPERATORS

#include <imgui/imgui.h>
#include <imgui/imgui_impl_opengl3.h>
#include <imgui/imgui_impl_sdl2.h>

#include <implot.h>
#include <implot_internal.h>

#include "core/backend/audio_player.hpp"
#include "core/backend/video_player.hpp"
#include "core/backend/waveform_loader.hpp"
#include "core/exporter.hpp"

#include <SDL_image.h>

namespace YAVE
{
class Debugger;
class Importer;
class SceneEditor;
class Timeline;
class VideoPlayer;
class ThumbnailLoader;
class WaveformLoader;
class Exporter;

struct SubtitleGizmo;

struct UIStyleConfig {
    UIStyleConfig(float t_font_size, float default_video_zoom)
        : font_size(t_font_size)
        , main_font(nullptr)
        , video_zoom_factor(default_video_zoom)
    {
    }

    float font_size;
    float video_zoom_factor;
    float current_zoom_factor = 1.0f;
    float target_zoom_factor = 1.0f;
    ImFont* main_font;
};

struct Tools {
    std::unique_ptr<Timeline> timeline{};
    std::unique_ptr<Importer> importer{};
    std::unique_ptr<SceneEditor> scene_editor{};
    std::unique_ptr<Debugger> debugger{};
    std::unique_ptr<Exporter> exporter;
};

struct VideoResolution {
    int width;
    int height;
};

class Application
{
public:
    Application();
    ~Application();

    static void open_first_video(
        const std::string& filename, std::shared_ptr<VideoPlayer> video_player);
    void enqueue_video_request(const std::string& filename, const float timestamp);
    void add_segment_to_timeline(const std::string& filename);
    [[nodiscard]] const std::int64_t get_file_duration(const std::string& filename) const;

    static void on_video_end_callback();

    int init();
    int init_imgui(std::string version);
    void init_video_processor();
    static void init_video_texture();
    std::string configure_sdl();

    void update();

    void render();
    void render_video_preview();
    void render_subtitles(const ImVec2& min, const ImVec2& max);
    static int file_loading_listener(void* userdata);

public:
    void handle_events();
    void handle_keyup_events();
    bool handle_custom_events();
    void handle_zooming(float delta_time);
    const ImVec2 maintain_video_aspect_ratio(ImVec2* display_min);

public:
    void update_texture();
    void refresh_timeline_waveform();
    void seek_to_requested_timestamp();
    void refresh_thumbnails();

    SDL_Window* window;
    SDL_GLContext m_gl_context;

    static bool s_IsRunning;

private:
    static unsigned int s_FrameTexID;
    static int s_PreferredImageFormat;

    [[nodiscard]] std::string get_requested_url(void* userdata);

    std::unique_ptr<Tools> m_tools;
    std::shared_ptr<VideoPlayer> m_video_processor;
    std::unique_ptr<ThumbnailLoader> m_thumbnail_loader;
    std::unique_ptr<WaveformLoader> m_waveform_loader;
    std::unique_ptr<SubtitleGizmo> m_current_subtitle_gizmo;

private:
    bool has_loaded_a_video = true;
    SDL_Thread* m_video_loading_thread;

    UIStyleConfig m_style_config;
    VideoResolution m_video_size;
    SDL_Event m_event;

    static AVRational s_Timebase;
};
} // namespace YAVE