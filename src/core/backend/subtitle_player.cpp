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
    m_parser = m_parser_factory->getParser();
    auto* subtitles = new std::vector<SubtitleItem*>(m_parser->getSubtitles());

    m_decoding_thread =
        SDL_CreateThread(&SubtitlePlayer::callback, "Subtitle Decoding Thread", subtitles);
}

SubtitlePlayer::~SubtitlePlayer()
{
    if (m_parser != nullptr) {
        delete m_parser;
    }
};

[[nodiscard]] const std::unique_ptr<SubtitleParserFactory> SubtitlePlayer::open_srt_file(
    const std::string& input_file_path)
{
    return std::make_unique<SubtitleParserFactory>(input_file_path);
}

int SubtitlePlayer::callback(void* userdata)
{
    auto* subtitles = static_cast<std::vector<SubtitleItem*>*>(userdata);

    std::for_each(subtitles->begin(), subtitles->end(),
        [&](const auto& sub) { std::cout << sub->getDialogue() << "\n"; });

    return 0;
}
} // namespace YAVE