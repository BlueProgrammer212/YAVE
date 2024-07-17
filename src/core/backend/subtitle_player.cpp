#include "core/backend/subtitle_player.hpp"

namespace YAVE
{
std::shared_ptr<VideoPlayer> SubtitlePlayer::video_processor = nullptr;
decltype(SubtitlePlayer::s_SubtitleGizmos) SubtitlePlayer::s_SubtitleGizmos = {};

SubtitlePlayer::SubtitlePlayer()
    : m_decoding_thread(nullptr)
    , m_parser(nullptr)
{
}

SubtitlePlayer::SubtitlePlayer(const std::string& input_file_path)
    : m_parser_factory(std::make_unique<SubtitleParserFactory>(input_file_path.c_str()))
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

    // Manual deallocation of subtitle gizmos.
    for (auto* gizmo : s_SubtitleGizmos) {
        delete gizmo;
        gizmo = nullptr;
    }
};

void SubtitlePlayer::open_srt_file(const std::string& input_file_path)
{
    auto subtitle_parser_factory = std::make_unique<SubtitleParserFactory>(input_file_path.c_str());
    m_parser = subtitle_parser_factory->getParser();

    auto subtitles = std::make_unique<std::vector<SubtitleItem*>>(m_parser->getSubtitles());

    m_decoding_thread = SDL_CreateThread(
        &SubtitlePlayer::callback, "Subtitle Decoding Thread", subtitles.release());

    request_srt_editor_load(m_parser->getFileData());
}

void SubtitlePlayer::request_srt_editor_load(std::string data)
{
    SDL_Event srt_load_event;
    auto userdata = std::make_unique<decltype(data)>(data);
    srt_load_event.type = CustomVideoEvents::FF_LOAD_SRT_FILE_EVENT;
    srt_load_event.user.data1 = userdata.release();
    SDL_PushEvent(&srt_load_event);
}

void SubtitlePlayer::request_subtitle_gizmo_refresh(SubtitleGizmo* subtitle_gizmo)
{
    SDL_Event refresh_event;
    refresh_event.type = CustomVideoEvents::FF_REFRESH_SUBTITLES;
    refresh_event.user.data1 = subtitle_gizmo;
    SDL_PushEvent(&refresh_event);
}

int SubtitlePlayer::callback(void* userdata)
{
    auto* subtitles = static_cast<std::vector<SubtitleItem*>*>(userdata);
    static auto empty_subtitles = new SubtitleGizmo();

    s_SubtitleGizmos.reserve(subtitles->size());

    std::for_each(subtitles->begin(), subtitles->end(), [&](const auto& current_subtitle) {
        auto gizmo = new SubtitleGizmo();
        gizmo->content = current_subtitle->getDialogue();
        gizmo->pts = static_cast<float>(current_subtitle->getStartTime());
        gizmo->duration = current_subtitle->getEndTime() - gizmo->pts;

        s_SubtitleGizmos.push_back(gizmo);
    });


    // Synchronize the video and the subtitles.
    for (int n = 0; Application::is_running && s_SubtitleGizmos.size() > 0;) {
        const double master_clock = AudioPlayer::get_video_internal_clock();
        bool is_subtitle_present = false;

        auto& current_gizmo = s_SubtitleGizmos.front();

        float end_timestamp = 0.0f;

        std::for_each(s_SubtitleGizmos.begin(), s_SubtitleGizmos.end(),
            [&, pts_in_sec = 0.0f, duration_s = 0.0f](auto& current_gizmo) mutable {
                if (is_subtitle_present) {
                    return;
                }

                // Convert the timestamps from milliseconds to seconds.
                pts_in_sec = current_gizmo->pts / 1000;
                duration_s = current_gizmo->duration / 1000;
                end_timestamp = pts_in_sec + duration_s;

                is_subtitle_present = master_clock >= pts_in_sec && master_clock <= end_timestamp;

                if (is_subtitle_present) {
                    current_gizmo->is_empty = false;
                    request_subtitle_gizmo_refresh(current_gizmo);
                }
            });


        if (!is_subtitle_present) {
            request_subtitle_gizmo_refresh(empty_subtitles);
        }

        const float delta_timestamp = end_timestamp - static_cast<float>(master_clock);
        SDL_Delay(100);
    }

    return 0;
}
} // namespace YAVE