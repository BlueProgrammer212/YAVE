#pragma once

#include "core/application.hpp"
#include "srt_parser.h"

namespace YAVE
{
class SubtitlePlayer
{
public:
    SubtitlePlayer();
    SubtitlePlayer(const std::string& input_file_path);
    ~SubtitlePlayer();

    static int callback(void* userdata);

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