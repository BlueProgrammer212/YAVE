#include "core/backend/subtitle_player.hpp"

namespace YAVE
{
SubtitlePlayer::SubtitlePlayer() {}

SubtitlePlayer::SubtitlePlayer(const std::string& input_file_path)
    : m_parser_factory(std::make_unique<SubtitleParserFactory>(input_file_path.c_str()))
    , m_av_packet(nullptr)
    , m_av_frame(nullptr)
    , m_decoding_thread(nullptr)
{
    // The parser is automatically deleleted once the subtitles are obtained.
    m_parser = m_parser_factory->getParser();
    auto* subtitles = new std::vector<SubtitleItem*>(m_parser->getSubtitles());

    m_decoding_thread =
        SDL_CreateThread(&SubtitlePlayer::callback, "Subtitle Decoding Thread", subtitles);
}

SubtitlePlayer::~SubtitlePlayer()
{
    delete m_parser;
};

int SubtitlePlayer::callback(void* userdata)
{
    auto* subtitles = static_cast<std::vector<SubtitleItem*>*>(userdata);

    for (auto& sub : *subtitles) {
        std::cout << sub->getDialogue() << "\n";
    }

    return 0;
}
} // namespace YAVE