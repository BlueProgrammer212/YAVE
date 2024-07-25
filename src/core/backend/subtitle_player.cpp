#include "core/backend/subtitle_player.hpp"

namespace YAVE
{
std::shared_ptr<VideoPlayer> SubtitlePlayer::video_processor = nullptr;
decltype(SubtitlePlayer::s_SubtitleGizmos) SubtitlePlayer::s_SubtitleGizmos = {};
SDL_cond* SubtitlePlayer::s_SubtitleAvailabilityCond = nullptr;

SubtitlePlayer::SubtitlePlayer()
    : m_decoding_thread(nullptr)
    , m_is_thread_active(false)
{
    s_SubtitleAvailabilityCond = SDL_CreateCond();

    if (!s_SubtitleAvailabilityCond) {
        std::cerr << "[Subtitle Player]: Failed to create a conditional variable.\n";
    }
}

SubtitlePlayer::SubtitlePlayer(const std::string& input_file_path)
    : m_parser_factory(std::make_unique<SubtitleParserFactory>(input_file_path.c_str()))
    , m_decoding_thread(nullptr)
    , m_is_thread_active(false)
{
    open_srt_file(input_file_path);

    s_SubtitleAvailabilityCond = SDL_CreateCond();

    if (!s_SubtitleAvailabilityCond) {
        std::cerr << "[Subtitle Player]: Failed to create a conditional variable.\n";
    }
}

SubtitlePlayer::~SubtitlePlayer()
{
    SDL_DestroyCond(s_SubtitleAvailabilityCond);
};

void SubtitlePlayer::update_subtitles(const std::string& input_file_path)
{
    auto subtitle_parser_factory = std::make_unique<SubtitleParserFactory>(input_file_path.c_str());
    auto parser = subtitle_parser_factory->getParser();
    m_subtitles = parser->getSubtitles();

    auto subtitle_editor_ptr = std::make_unique<SubtitleEditor>();
    subtitle_editor_ptr->content = parser->getFileData();

    subtitle_editor_ptr->number_of_words = std::accumulate(m_subtitles.begin(), m_subtitles.end(),
        0, [&](int sum, SubtitleItem* current_subtitle) mutable {
            return sum + current_subtitle->getWordCount();
        });

    subtitle_editor_ptr->total_dialogue_nb = static_cast<unsigned int>(m_subtitles.size());

    request_srt_editor_load(subtitle_editor_ptr.release());

    s_SubtitleGizmos.clear();
    s_SubtitleGizmos.reserve(m_subtitles.size());

    std::for_each(m_subtitles.begin(), m_subtitles.end(), [&](const auto& current_subtitle) {
        auto gizmo = std::make_shared<SubtitleGizmo>();
        gizmo->content = current_subtitle->getDialogue();
        gizmo->pts = static_cast<float>(current_subtitle->getStartTime());
        gizmo->duration = current_subtitle->getEndTime() - gizmo->pts;

        s_SubtitleGizmos.push_back(gizmo);
    });

    SDL_CondBroadcast(s_SubtitleAvailabilityCond);
}

void SubtitlePlayer::open_srt_file(const std::string& input_file_path)
{
    update_subtitles(input_file_path);

    // Check if the decoding thread is already up and running.
    if (!m_is_thread_active) {
        m_decoding_thread =
            SDL_CreateThread(&SubtitlePlayer::callback, "Subtitle Decoding Thread", &m_subtitles);

        m_is_thread_active = true;
    }
}

void SubtitlePlayer::request_srt_editor_load(SubtitleEditor* data)
{
    SDL_Event srt_load_event;
    auto userdata = std::make_unique<decltype(data)>(data);
    srt_load_event.type = CustomVideoEvents::FF_LOAD_SRT_FILE_EVENT;
    srt_load_event.user.data1 = data;
    SDL_PushEvent(&srt_load_event);
}

void SubtitlePlayer::request_subtitle_gizmo_refresh(
    decltype(s_SubtitleGizmos)::value_type subtitle_gizmo)
{
    SDL_Event refresh_event;
    refresh_event.type = CustomVideoEvents::FF_REFRESH_SUBTITLES;
    refresh_event.user.data1 = subtitle_gizmo.get();
    SDL_PushEvent(&refresh_event);
}

int SubtitlePlayer::callback(void* userdata)
{
    auto* subtitles = static_cast<std::vector<SubtitleItem*>*>(userdata);
    static auto empty_subtitles = std::make_shared<SubtitleGizmo>();

    // Synchronize the video and the subtitles.
    for (int n = 0; Application::s_IsRunning;) {
        SDL_LockMutex(PacketQueue::s_GlobalMutex);

        if (s_SubtitleGizmos.empty()) {
            SDL_CondWait(s_SubtitleAvailabilityCond, PacketQueue::s_GlobalMutex);
            SDL_UnlockMutex(PacketQueue::s_GlobalMutex);
            continue;
        }

        SDL_UnlockMutex(PacketQueue::s_GlobalMutex);

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

        // TODO: Calculate the actual delay by using the average video framerate.
        SDL_Delay(100);
    }

    return 0;
}
} // namespace YAVE