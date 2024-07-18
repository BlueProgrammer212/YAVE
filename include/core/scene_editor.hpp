#pragma once

#include "application.hpp"
#include "color.hpp"
#include "core/backend/subtitle_player.hpp"
#include "stb_image.h"

namespace YAVE
{
class SubtitlePlayer;
struct SubtitleEditor;

constexpr std::size_t SUBTITLES_BUFFER_SIZE = 4096;

struct Transition;

using TransitionCache = std::unordered_map<std::string, Transition>;

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

    bool add_image_button(const std::string& src, unsigned int* tex_id_ptr, ImVec2 size);

    inline void set_video_player(std::shared_ptr<VideoPlayer> video_player)
    {
        subtitle_player->set_video_player_context(video_player);
    }

    inline void update_input_buffer(const SubtitleEditor* input)
    {
        *m_subtitle = *input;
    }

    std::unique_ptr<SubtitlePlayer> subtitle_player;

private:
    void render_subtitles_window();
    void render_transition_window();
    void render_settings_window();
    void render_scene_properties_window();
    void modify_srt_file(const std::string& new_file_data);

    TransitionCache m_transition_map;
    std::unique_ptr<SubtitleEditor> m_subtitle;
    std::string m_active_srt_file;
};
} // namespace YAVE