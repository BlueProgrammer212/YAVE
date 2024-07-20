#pragma once

#include "application.hpp"
#include "color.hpp"
#include "core/backend/subtitle_player.hpp"
#include "core/file_explorer.hpp"
#include "stb_image.h"

namespace YAVE
{
class SubtitlePlayer;
struct SubtitleEditor;

constexpr std::size_t SUBTITLES_BUFFER_SIZE = 32768;
constexpr auto SUBTITLE_EDITOR_INPUT_FLAGS = ImGuiInputTextFlags_AllowTabInput;

struct Transition;

using TransitionCache = std::unordered_map<std::string, Transition>;

struct Transition {
    double start_timestamp;
    double end_timestamp;
};

struct SubtitleEditorUserData {
    std::unique_ptr<SubtitleEditor> subtitle_editor = nullptr;
    std::vector<char> input_buffer = {};
    bool needs_update = false;
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
        auto& subtitle = m_subtitle_editor_user_data.subtitle_editor;
        subtitle->content = input->content;
        subtitle->number_of_words = input->number_of_words;
        subtitle->total_dialogue_nb = input->total_dialogue_nb;
    }

    std::unique_ptr<SubtitlePlayer> subtitle_player;

private:
    void render_subtitles_window();
    void render_transition_window();
    void render_settings_window();
    void render_scene_properties_window();
    void modify_srt_file(const std::string& new_file_data);

    TransitionCache m_transition_map;
    std::string m_active_srt_file;

private:
    SubtitleEditorUserData m_subtitle_editor_user_data;
};
} // namespace YAVE