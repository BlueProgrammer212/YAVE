#pragma once

#include "application.hpp"
#include "color.hpp"
#include "core/backend/subtitle_player.hpp"

namespace YAVE
{
constexpr std::size_t SUBTITLES_BUFFER_SIZE = 16384;

struct Transition;

using TransitionCache = std::unordered_map<std::string, Transition>;
using SubtitleBuffer = std::array<char, SUBTITLES_BUFFER_SIZE>;

struct Transition {
    double start_timestamp;
    double end_timestamp;
};

class SceneEditor
{
public:
    SceneEditor();
    ~SceneEditor(){};

    void init();
    void update();

    void render();

private:
    void render_subtitles_window();
    void render_transition_window();
    void render_settings_window();
    void render_scene_properties_window();

    TransitionCache m_transition_map;
    SubtitleBuffer m_subtitle_input_buffer;
    std::unique_ptr<SubtitlePlayer> m_subtitle_player;
};
} // namespace YAVE