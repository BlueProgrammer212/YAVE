#pragma once

#include "core/application.hpp"
#include "srt_parser.h"
#include <algorithm>
#include <numeric>

namespace YAVE
{
struct SubtitleGizmo {
    std::string content = "";
    ImVec2 start_position = ImVec2(-1.0f, -1.0f);
    ImVec2 end_position = ImVec2(-1.0f, -1.0f);
    float pts = 0.0f;
    float duration = 5.0f;
    bool is_empty = true;
};

struct SubtitleEditor {
    std::string content = "";
    int number_of_words = 0;
    unsigned int total_dialogue_nb = 0;
};

class SubtitlePlayer
{
public:
    SubtitlePlayer(const std::string& input_file_path);
    SubtitlePlayer();
    ~SubtitlePlayer();

    static std::shared_ptr<VideoPlayer> video_processor;
    static std::vector<std::shared_ptr<SubtitleGizmo>> s_SubtitleGizmos;
    static int callback(void* userdata);

    void update_subtitles(const std::string& input_file_path);
    void open_srt_file(const std::string& input_file_path);

    /**
     * @brief Adds a .srt file in the project directory.
     * @param out_srt_filename
     */
    static void new_srt_file(const std::string& out_srt_filename);

    inline void set_video_player_context(std::shared_ptr<VideoPlayer> video_processor)
    {
        SubtitlePlayer::video_processor = video_processor;
    }

    static void request_subtitle_gizmo_refresh(
        decltype(s_SubtitleGizmos)::value_type subtitle_gizmo);

    static void request_srt_editor_load(SubtitleEditor* data);

    void srt_refresh();

private:
    static SDL_cond* s_SubtitleAvailabilityCond;
    std::unique_ptr<SubtitleParserFactory> m_parser_factory;
    std::vector<SubtitleItem*> m_subtitles;
    bool m_is_thread_active;

private:
    SDL_Thread* m_decoding_thread;
};
} // namespace YAVE