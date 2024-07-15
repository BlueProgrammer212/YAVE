#include "core/backend/subtitle_player.hpp"

namespace YAVE
{
SubtitlePlayer::SubtitlePlayer()
    : m_av_packet(nullptr)
    , m_av_frame(nullptr)
    , m_decoding_thread(nullptr)
    , m_parser(nullptr)
{
}

SubtitlePlayer::SubtitlePlayer(const std::string& input_file_path)
    : m_parser_factory(std::make_unique<SubtitleParserFactory>(input_file_path.c_str()))
    , m_av_packet(nullptr)
    , m_av_frame(nullptr)
    , m_parser(nullptr)
    , m_decoding_thread(nullptr)
{
    open_srt_file(input_file_path);
}

SubtitlePlayer::~SubtitlePlayer()
{
    if (m_parser != nullptr) {
        delete m_parser;
    }
};

void SubtitlePlayer::open_srt_file(const std::string& input_file_path)
{
    auto subtitle_parser_factory = std::make_unique<SubtitleParserFactory>(input_file_path.c_str());

    m_parser = subtitle_parser_factory->getParser();
    auto subtitles = std::make_unique<std::vector<SubtitleItem*>>(m_parser->getSubtitles());

    m_decoding_thread = SDL_CreateThread(
        &SubtitlePlayer::callback, "Subtitle Decoding Thread", subtitles.release());
}

int SubtitlePlayer::callback(void* userdata)
{
    auto* subtitles = static_cast<std::vector<SubtitleItem*>*>(userdata);

    return 0;
}
} // namespace YAVE