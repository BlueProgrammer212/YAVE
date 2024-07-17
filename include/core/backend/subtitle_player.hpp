#pragma once

#include "core/application.hpp"
#include "srt_parser.h"

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

class SubtitlePlayer
{
public:
    SubtitlePlayer(const std::string& input_file_path);
    SubtitlePlayer();
    ~SubtitlePlayer();

    static std::shared_ptr<VideoPlayer> video_processor;
    static std::vector<SubtitleGizmo*> s_SubtitleGizmos;
    static int callback(void* userdata);

    void open_srt_file(const std::string& input_file_path);

    /**
     * @brief Adds a .srt file in the project directory.
     * @param out_srt_filename
     */
    static void new_srt_file(const std::string& out_srt_filename);

    [[nodiscard]] inline const std::vector<SubtitleItem*> get_subtitle_array()
    {
        return m_parser->getSubtitles();
    }

    inline void set_video_player_context(std::shared_ptr<VideoPlayer> video_processor)
    {
        SubtitlePlayer::video_processor = video_processor;
    }

    static void request_subtitle_gizmo_refresh(SubtitleGizmo* subtitle_gizmo);
    static void request_srt_editor_load(std::string data);

    void srt_refresh();

private:
    std::unique_ptr<SubtitleParserFactory> m_parser_factory;
    SubtitleParser* m_parser;

private:
    SDL_Thread* m_decoding_thread;
};
} // namespace YAVE