#pragma once

#include <iomanip>
#include <map>
#include <sstream>

#define NO_SDL_GLEXT
#define GLEW_STATIC

#include <GL/glew.h>
#include <SDL.h>
#include <SDL_opengl.h>

#include "core/application.hpp"
#include "core/backend/audio_player.hpp"
#include "core/backend/packet_queue.hpp"

namespace YAVE
{
struct VideoPreviewRequest;
class AudioPlayer;

using VideoQueue = std::deque<VideoPreviewRequest*>;

enum CustomVideoEvents : std::uint32_t {
    FF_REFRESH_VIDEO_EVENT = SDL_USEREVENT,
    FF_LOAD_NEW_VIDEO_EVENT,
    FF_LOAD_SRT_FILE_EVENT,
    FF_TOGGLE_PAUSE_EVENT,
    FF_MUTE_AUDIO_EVENT,
    FF_SEEK_TO_TIMESTAMP_EVENT,
    FF_REFRESH_THUMBNAIL,
    FF_REFRESH_WAVEFORM,
    FF_REFRESH_SUBTITLES
};

constexpr int COLOR_CHANNELS_NB = 4;

#pragma region Video Flags

// clang-format off
enum class VideoFlags : std::uint32_t {
  NONE                       = 0,     
  IS_INITIALIZED             = 1 << 0,
  IS_PAUSED                  = 1 << 1,
  IS_SWS_INITIALIZED         = 1 << 2,
  IS_INPUT_ACTIVE            = 1 << 3,
  IS_DECODING_THREAD_ACTIVE  = 1 << 4,
};
// clang-format on

using VideoFlagType = std::underlying_type<VideoFlags>::type;

[[nodiscard]] inline VideoFlags operator|(VideoFlags lhs, VideoFlags rhs)
{
    return static_cast<VideoFlags>(
        static_cast<VideoFlagType>(lhs) | static_cast<VideoFlagType>(rhs));
}

inline VideoFlags& operator|=(VideoFlags& lhs, VideoFlags rhs)
{
    lhs = lhs | rhs;
    return lhs;
}

[[nodiscard]] inline bool operator&(VideoFlags lhs, VideoFlags rhs)
{
    return static_cast<bool>(static_cast<VideoFlagType>(lhs) & static_cast<VideoFlagType>(rhs));
}

inline VideoFlags operator&=(VideoFlags& lhs, VideoFlags rhs)
{
    lhs =
        static_cast<VideoFlags>(static_cast<VideoFlagType>(lhs) & static_cast<VideoFlagType>(rhs));
    return lhs;
}

[[nodiscard]] inline VideoFlags operator~(VideoFlags lhs)
{
    return static_cast<VideoFlags>(~static_cast<VideoFlagType>(lhs));
}

[[nodiscard]] inline VideoFlags operator^(VideoFlags lhs, VideoFlags rhs)
{
    return static_cast<VideoFlags>(
        static_cast<VideoFlagType>(lhs) ^ static_cast<VideoFlagType>(rhs));
}

inline VideoFlags operator^=(VideoFlags& lhs, VideoFlags rhs)
{
    lhs = lhs ^ rhs;
    return lhs;
}

#pragma endregion Video Flags

struct VideoDimension {
    int x = 640;
    int y = 360;
};

struct VideoState {
    SwsContext* sws_scaler_ctx = nullptr;
    AVFormatContext* av_format_ctx = nullptr;
    std::uint8_t* buffer = nullptr;

    double current_pts = 0.0;
    double previous_pts = 0.0;
    double previous_delay = 40e-3;

    VideoFlags flags = VideoFlags::NONE;
    VideoDimension dimensions;

    double frame_timer = 0.0;
    bool is_first_frame = false;
};

struct VideoPreviewRequest {
    std::string path = "";
    float presentation_timestamp = 0.0f;
    float duration = 0.0f;
    bool is_active = false;
};

class VideoPlayer : public AudioPlayer
{
public:
    VideoPlayer(SampleRate t_sample_rate);
    ~VideoPlayer() override;

    /**
     * @brief Opens a video and initializes the format context and the codecs.
     * @param path Specifies the path of the video file.
     * @return 0 <= for success, a negative integer for error.
     */
    int allocate_video(const char* path);

    /**
     * @brief Plays the active input file.
     * @param timebase
     * @return 0 for success, a negative integer for error.
     */
    int init_threads(AVRational* timebase);

    /**
     * @brief Allocates memory for the frame buffer.
     * @param width The width of the frame.
     * @param height The height of the frame.
     * @return 0 <= for success, a negative integer for error.
     */
    int allocate_frame_buffer(AVPixelFormat pix_fmt, VideoDimension dimensions);

    /**
     * @brief Updates the framebuffer with the next packet when the last frame
     *        has finished rendering.
     * @param data The video state
     * @return 0 <= for success, a negative integer for error.
     */
    static int video_callback(void* data);

    /**
     * @brief Sends the video packets to the decoder and then recieves the frame from the codec.
     * @return 0 <= for success, a negative integer for error.
     */
    static int decode_video_frame(
        VideoState* video_state, AVPacket* video_packet, AVFrame* dummy_frame = nullptr);

    /**
     * @brief Enqueues audio and video packets in a separate thread.
     * @param data The video state
     */
    static int enqueue_packets(void* data);

    /**
     * @brief Open other input files
     *
     * @param av_format_context
     * @param url
     * @return int
     */
    static int switch_input(AVFormatContext** av_format_context, const std::string& url);

    /**
     * @brief Jump to specific timestamp.
     * @param seconds The timestamp in seconds.
     * @return 0 <= for success, a negative integer for error.
     */
    int seek_frame(float seconds, bool update_frame = false);

    /**
     * @brief After decoding the packet will be translated into a framebuffer.
     * @param width The width of the video frame.
     * @param height The height of the video frame.
     * @return 0 <= for success, a negative integer for error.
     */
    static unsigned int update_framebuffer(Uint32 interval, void* data);

    /**
     * @brief Apply the filters to the subsampled RGB frame.
     * @param video_state Contains information about the video
     * @param av_frame The frame that will be edited
     */
    static void apply_filters(VideoState* video_state, AVFrame* av_frame);

    /**
     * @brief This signals the application to refresh the OpenGL texture.
     * @return 0 <= for success, a negative integer for error.
     */
    static int refresh_texture();

#pragma region Helper Functions
    /**
     * @brief Get the total duration of the video.
     * @return std::int64_t&
     */
    [[nodiscard]] inline std::int64_t& get_duration()
    {
        return m_duration;
    }

    /**
     * @brief Access the framebuffer of the current video.
     * @return std::uint8_t*&
     */
    [[nodiscard]] inline std::uint8_t*& get_framebuffer() noexcept
    {
        return m_video_state->buffer;
    }

    /**
     * @brief Get the presentation timestamp of the latest video frame.
     * @return double
     */
    [[nodiscard]] inline double get_pts() const
    {
        return static_cast<double>(m_video_state->current_pts);
    }

    /**
     * @brief Access the video flags.
     * @return VideoFlags&
     */
    [[nodiscard]] inline VideoFlags& get_flags() noexcept
    {
        return m_video_state->flags;
    }

    /**
     * @brief Get the video state object.
     * @return VideoState&
     */
    [[nodiscard]] inline std::shared_ptr<VideoState> video_state() noexcept
    {
        return m_video_state;
    }

    [[nodiscard]] inline std::string& filename() noexcept
    {
        return m_opened_file;
    }

    [[nodiscard]] static std::string current_timestamp_str();

    [[nodiscard]] static double calculate_reference_clock();

    [[nodiscard]] static double calculate_actual_delay(
        VideoState* video_state, double& frame_timer);

    void pause_video();

    static void add_stream(StreamInfoPtr stream_ptr, std::string name);

    static int process_stream(
        const AVStream* stream, const AVCodec* av_codec, const StreamID stream_index);

#pragma endregion Helper Functions

public:
    void stop_threads();
    void update_video_dimensions();

    [[nodiscard]] int nb_samples_per_frame(AVPacket* packet, AVFrame* frame);
    int restart_audio_thread();

    int init_codecs();

    static std::unique_ptr<PacketQueue> s_VideoPacketQueue;
    static VideoQueue s_VideoFileQueue;

protected:
    std::shared_ptr<VideoState> m_video_state;
    std::int64_t m_duration;

    SDL_Thread* m_decoding_tid;
    SDL_Thread* m_video_tid;

    inline static void reset_internal_clocks()
    {
        // Reset the audio and video internal clock.
        s_ClockNetwork->audio_internal_clock = 0.0;
        s_ClockNetwork->video_internal_clock = 0.0;
    }

private:
    std::unique_ptr<VideoLoader> m_loader;

    static int init_sws_scaler_ctx(VideoState* video_state);
    int create_context_for_stream(StreamInfoPtr& stream_info);
    int init_mutex();

private:
    static void update_pts(VideoState* video_state, AVPacket* video_packet);
    static void synchronize_video(VideoState* video_state);

private:
    void free_ffmpeg();

private:
    std::string m_opened_file{ "" };
    bool m_is_input_open{ false };
};
#pragma endregion Video Player

} // namespace YAVE