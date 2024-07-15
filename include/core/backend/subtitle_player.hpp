#pragma once

#include "core/application.hpp"
#include "srt_parser.h"

namespace YAVE
{
struct SubtitleGizmo {
    std::string content;
    ImVec2 start_position;
    ImVec2 end_position;
};

class SubtitlePlayer
{
public:
    SubtitlePlayer(const std::string& input_file_path);
    SubtitlePlayer();
    ~SubtitlePlayer();

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

private:
    std::unique_ptr<SubtitleParserFactory> m_parser_factory;
    SubtitleParser* m_parser;

private:
    AVPacket* m_av_packet;
    AVFrame* m_av_frame;
    SDL_Thread* m_decoding_thread;
};
} // namespace YAVE