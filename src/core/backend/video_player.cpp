#include "core/backend/video_player.hpp"
#include "core/application.hpp"

namespace YAVE
{
std::unique_ptr<PacketQueue> VideoPlayer::s_VideoPacketQueue = std::make_unique<PacketQueue>();
VideoQueue VideoPlayer::s_VideoFileQueue = {};

VideoPlayer::VideoPlayer(SampleRate t_sample_rate)
    : m_video_state(std::make_shared<VideoState>())
{
    m_audio_state->sample_rate = t_sample_rate;
    SDL_RegisterEvents(8);
}

VideoPlayer::~VideoPlayer()
{
    if (m_video_state->flags & VideoFlags::IS_INITIALIZED) {
        stop_threads();
        free_ffmpeg();
        free_sdl_mixer();

        av_freep(&m_video_state->buffer);
    }
}
#pragma region Stream Setup

void VideoPlayer::add_stream(StreamInfoPtr stream_ptr, std::string name)
{
    const char* stream_type = name.c_str();

    if (s_StreamList.find(stream_type) != s_StreamList.end()) {
        s_StreamList.at(stream_type) = std::move(stream_ptr);
        return;
    }

    s_StreamList.insert({ stream_type, std::move(stream_ptr) });
}

int VideoPlayer::process_stream(
    const AVStream* stream, const AVCodec* av_codec, const StreamID stream_index)
{
    auto stream_info = std::make_shared<StreamInfo>();

    stream_info->timebase = stream->time_base;
    stream_info->av_codec = const_cast<AVCodec*>(av_codec);
    stream_info->av_codec_params = stream->codecpar;
    stream_info->stream_index = stream_index;

    if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        stream_info->width = stream->codecpar->width;
        stream_info->height = stream->codecpar->height;
        add_stream(stream_info, "Video");
    }

    if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
        add_stream(stream_info, "Audio");
    }

    return 0;
}

int VideoPlayer::create_context_for_stream(StreamInfoPtr& stream_info)
{
    stream_info->av_codec_ctx = avcodec_alloc_context3(stream_info->av_codec);

    if (!stream_info->av_codec_ctx || !stream_info->av_codec_params) {
        std::cout << "Failed to allocate memory for the codec context.\n";
        return -1;
    }

    if (avcodec_parameters_to_context(stream_info->av_codec_ctx, stream_info->av_codec_params) <
        0) {
        std::cout << "Failed to initialize AVCodecContext.\n";
        return -1;
    }

    if (avcodec_open2(stream_info->av_codec_ctx, stream_info->av_codec, nullptr) < 0) {
        std::cout << "Failed to open the codec using avcodec_open2.\n";
        return -1;
    }

    return 0;
}

#pragma endregion Stream Setup

#pragma region Init Functions
int VideoPlayer::init_sws_scaler_ctx(VideoState* video_state)
{
    if (video_state->flags & VideoFlags::IS_SWS_INITIALIZED) {
        return 0;
    }

    auto& sws_scaler_ctx = video_state->sws_scaler_ctx;

    const auto& stream_info = s_StreamList.at("Video");

    auto& width = video_state->dimensions.x;
    auto& height = video_state->dimensions.y;

    // Convert planar YUV 4:2:0 pixel format to packed RGB 8:8:8 pixel format
    // with bilinear rescaling algorithm.
    sws_scaler_ctx = sws_getContext(width, height, stream_info->av_codec_ctx->pix_fmt, width,
        height, AV_PIX_FMT_RGB0, SWS_BILINEAR, nullptr, nullptr, nullptr);

    if (!sws_scaler_ctx) {
        std::cout << "Failed to initialize the sw scaler.\n";
        return -1;
    }

    video_state->flags |= VideoFlags::IS_SWS_INITIALIZED;

    return 0;
}

int VideoPlayer::allocate_frame_buffer(AVPixelFormat pix_fmt, VideoDimension dimensions)
{
    constexpr int LINESIZE_ALIGNMENT = 32;

    const auto buffer_size =
        av_image_get_buffer_size(AV_PIX_FMT_RGBA, dimensions.x, dimensions.y, LINESIZE_ALIGNMENT);

    const auto total_buffer_size = buffer_size * sizeof(std::uint8_t);

    m_video_state->buffer = static_cast<std::uint8_t*>(av_malloc(total_buffer_size));

    if (!m_video_state->buffer) {
        std::cout << "Failed to allocate memory for the framebuffer.\n";
        return -1;
    }

    return 0;
}

int VideoPlayer::init_mutex()
{
    static bool is_initialized = false;

    if (is_initialized) {
        return 0;
    }

    is_initialized = true;

    PacketQueue::s_GlobalMutex = SDL_CreateMutex();
    if (!PacketQueue::s_GlobalMutex) {
        std::cerr << "Failed to create a mutex: " << SDL_GetError() << "\n";
        return -1;
    }

    s_VideoPausedCond = SDL_CreateCond();
    s_FrameAvailabilityCond = SDL_CreateCond();
    PacketQueue::s_PacketAvailabilityCond = SDL_CreateCond();

    if (!s_VideoPausedCond || !PacketQueue::s_PacketAvailabilityCond || !s_FrameAvailabilityCond) {
        std::cerr << "Failed to create a condition variable: " << SDL_GetError() << "\n";
        SDL_DestroyMutex(PacketQueue::s_GlobalMutex);
        SDL_DestroyCond(s_VideoPausedCond);
        SDL_DestroyCond(s_FrameAvailabilityCond);
        SDL_DestroyCond(PacketQueue::s_PacketAvailabilityCond);

        return -1;
    }

    return 0;
}

#pragma endregion Init Functions

#pragma region Helper Functions
[[nodiscard]] int VideoPlayer::nb_samples_per_frame(AVPacket* packet, AVFrame* frame)
{
    std::optional<AVFrame*> first_audio_frame =
        get_first_audio_frame(m_video_state->av_format_ctx, packet, frame);

    seek_frame(0.0);

    if (first_audio_frame.has_value()) {
        return first_audio_frame.value()->nb_samples;
    }

    return DEFAULT_SAMPLES_BUFFER_SIZE;
}

#pragma region Video Reader

int VideoPlayer::init_codecs()
{
    auto& av_format_ctx = m_video_state->av_format_ctx;
    m_loader->find_available_codecs(&m_video_state->av_format_ctx, &VideoPlayer::process_stream);

    for (auto& stream : s_StreamList) {
        if (create_context_for_stream(stream.second) != 0) {
            std::cerr << "Initialization failed for one or more streams.\n";
            return -1;
        }
    }

    return 0;
}

int VideoPlayer::allocate_video(const char* filename)
{
    SDL_LockMutex(PacketQueue::s_GlobalMutex);

    auto& av_format_ctx = m_video_state->av_format_ctx;

    if (m_video_state->flags & VideoFlags::IS_INITIALIZED) {
        SDL_UnlockMutex(PacketQueue::s_GlobalMutex);
        return 0;
    }

    // Allocate only one format context for the video player.
    static bool is_format_context_allocated = false;

    if (!is_format_context_allocated) {
        av_format_ctx = avformat_alloc_context();

        if (!av_format_ctx) {
            std::cout << "Failed to allocate memory for the format context.\n";
            return -1;
        };

        is_format_context_allocated = true;
    }

    if (avformat_open_input(&av_format_ctx, filename, nullptr, nullptr) < 0) {
        std::cout << "[Video Player]: Failed to open the specified input.\n";
        return -1;
    };

    m_opened_file = filename;
    m_duration = av_format_ctx->duration;
    m_video_state->flags |= VideoFlags::IS_INPUT_ACTIVE;

    if (init_codecs() < 0) {
        std::cout << "[Video Player]: Failed to find a valid codec.\n";
        return -1;
    };

    if (!s_LatestFrame || !s_LatestPacket) {
        s_LatestFrame = av_frame_alloc();
        s_LatestPacket = av_packet_alloc();
    }

    m_video_state->flags |= VideoFlags::IS_INITIALIZED;

    SDL_UnlockMutex(PacketQueue::s_GlobalMutex);
    return 0;
}

void VideoPlayer::update_video_dimensions()
{
    auto& dimensions = m_video_state->dimensions;

    const auto& video_stream_info = s_StreamList.at("Video");
    const auto& audio_stream_info = s_StreamList.at("Audio");

    dimensions.x = video_stream_info->width;
    dimensions.y = video_stream_info->height;
}

int VideoPlayer::init_threads(AVRational* timebase)
{
    const auto& video_stream_info = s_StreamList.at("Video");

    update_video_dimensions();

    int ret = this->allocate_frame_buffer(
        video_stream_info->av_codec_ctx->pix_fmt, m_video_state->dimensions);

    if (ret != 0) {
        return -1;
    }

    if (timebase != nullptr) {
        *timebase = video_stream_info->timebase;
    }

    m_video_state->is_first_frame = true;

    if (init_mutex() < 0 || restart_audio_thread() < 0) {
        return -1;
    };

    SDL_CondBroadcast(s_FrameAvailabilityCond);

    // Start the decoding and video threads.
    if (m_video_state->flags & VideoFlags::IS_DECODING_THREAD_ACTIVE) {
        return 0;
    }

    m_video_tid = SDL_CreateThread(&video_callback, "Video Thread", m_video_state.get());
    m_decoding_tid = SDL_CreateThread(&enqueue_packets, "Decoding Thread", m_video_state.get());
    m_video_state->flags |= VideoFlags::IS_DECODING_THREAD_ACTIVE;

    return 0;
}

#pragma endregion Video Reader

#pragma region Frame Processing

int VideoPlayer::refresh_texture()
{
    SDL_Event event;
    event.type = static_cast<std::uint32_t>(CustomVideoEvents::FF_REFRESH_VIDEO_EVENT);

    if (SDL_PushEvent(&event) == 0) {
        return -1;
    };

    return 0;
}

void VideoPlayer::apply_filters(VideoState* video_state, AVFrame* av_frame)
{
    for (int y = 0; y < video_state->dimensions.y; ++y) {
        for (int x = 0; x < video_state->dimensions.x; ++x) {
            av_frame->data[0][y * av_frame->linesize[0] + x] = 0x0;
        }
    }

    for (int y = 0; y < video_state->dimensions.y / 2; ++y) {
        for (int x = 0; x < video_state->dimensions.x / 2; ++x) {
            av_frame->data[1][y * av_frame->linesize[1] + x] = 0x0;
            av_frame->data[2][y * av_frame->linesize[2] + x] = 0x0;
        }
    }
}

unsigned int VideoPlayer::update_framebuffer(Uint32 interval, void* user_data)
{
    auto* data = static_cast<VideoState*>(user_data);
    auto& sws_scaler_ctx = data->sws_scaler_ctx;

    if (init_sws_scaler_ctx(data) < 0) {
        return -1;
    }

    std::array<std::uint8_t*, COLOR_CHANNELS_NB> dest = { data->buffer, nullptr, nullptr, nullptr };

    std::array<int, COLOR_CHANNELS_NB> dest_linesize = { 0, 0, 0, 0 };
    dest_linesize[0] = data->dimensions.x * COLOR_CHANNELS_NB;

    sws_scale(sws_scaler_ctx, s_LatestFrame->data, s_LatestFrame->linesize, 0, data->dimensions.y,
        dest.data(), dest_linesize.data());

    refresh_texture();

    return 0;
}

#pragma endregion Frame Processing

#pragma region Video Synchronization

double VideoPlayer::calculate_reference_clock()
{
    double ref_clock = s_ClockNetwork->audio_internal_clock;

    const auto& [channel_nb, buffer_size, sample_rate, buffer_index] = *s_AudioBufferInfo;

    int hw_buf_size = buffer_size - buffer_index;
    int sample_bytes = channel_nb * sizeof(float);
    int bytes_per_sec = sample_rate * sample_bytes;

    if (bytes_per_sec > 0) {
        ref_clock -= static_cast<double>(hw_buf_size) / static_cast<double>(bytes_per_sec);
    }

    return ref_clock;
}

double VideoPlayer::calculate_actual_delay(VideoState* video_state, double& frame_timer)
{
    double delay = video_state->current_pts - video_state->previous_pts;

    if (delay <= 0 || delay >= 1.0) {
        delay = video_state->previous_delay;
    }

    video_state->previous_delay = delay;
    video_state->previous_pts = video_state->current_pts;

    const double current_time = av_gettime() / static_cast<double>(AV_TIME_BASE);

    const double ref_clock = calculate_reference_clock();
    const double diff = video_state->current_pts - ref_clock;

    const double sync_threshold = std::max(delay, SYNC_THRESHOLD);

    if (std::abs(diff) < NOSYNC_THRESHOLD) {
        if (diff <= -sync_threshold) {
            delay = 0;
        } else if (diff >= sync_threshold) {
            delay *= 2;
        }
    }

    frame_timer += delay;

    return std::max(frame_timer - current_time, 0.010);
}

void VideoPlayer::synchronize_video(VideoState* video_state)
{
    static double actual_delay = 0.0;
    const auto& timebase = s_StreamList.at("Video")->timebase;
    double& video_clock = s_ClockNetwork->video_internal_clock;
    double frame_delay = av_q2d(timebase);

    if (video_state->is_first_frame) {
        // When the input is changed, update the frame timer.
        video_state->frame_timer = av_gettime() / static_cast<double>(AV_TIME_BASE);
        video_state->is_first_frame = false;
    }

    // Adjust frame_timer if video is paused
    const double pause_elapsed_time =
        s_ClockNetwork->pause_end_time - s_ClockNetwork->pause_start_time;

    video_state->frame_timer -= pause_elapsed_time;

    s_ClockNetwork->pause_start_time = 0.0;
    s_ClockNetwork->pause_end_time = 0.0;

    frame_delay += s_LatestFrame->repeat_pict * (frame_delay * 0.5);

    if (video_state->current_pts != 0) {
        video_clock = video_state->current_pts;
    } else {
        video_state->current_pts = video_clock;
    }

    s_ClockNetwork->video_internal_clock += frame_delay;

    actual_delay = calculate_actual_delay(video_state, video_state->frame_timer);
    SDL_Delay(static_cast<Uint32>(actual_delay * 1000 + 0.5));
}

void VideoPlayer::update_pts(VideoState* state, AVPacket* packet)
{
    const auto& time_base = s_StreamList.at("Video")->timebase;
    bool is_dts_available = packet->dts != AV_NOPTS_VALUE;

    // Get the PTS of the current video frame.
    state->current_pts = (is_dts_available ? static_cast<double>(s_LatestFrame->pts) : 0);

    if (is_rational_valid(time_base)) {
        state->current_pts *= av_q2d(time_base);
    }
}

[[nodiscard]] std::string VideoPlayer::current_timestamp_str()
{
    const double master_clock = AudioPlayer::get_video_internal_clock();
    const int total_seconds = static_cast<int>(std::floor(master_clock));
    const int milliseconds = static_cast<int>((master_clock - total_seconds) * 1000);

    const int hours = total_seconds / 3600;
    const int minutes = (total_seconds % 3600) / 60;
    const int seconds = total_seconds % 60;

    // Format each component with leading zeros if necessary
    std::ostringstream result;
    result << std::setfill('0') << std::setw(2) << hours << ":";
    result << std::setfill('0') << std::setw(2) << minutes << ":";
    result << std::setfill('0') << std::setw(2) << seconds << ",";
    result << std::setfill('0') << std::setw(3) << milliseconds;

    return result.str();
}

int VideoPlayer::video_callback(void* data)
{
    auto* video_state = static_cast<VideoState*>(data);

    AVPacket video_packet;
    av_init_packet(&video_packet);

    while (Application::s_IsRunning) {
        SDL_LockMutex(PacketQueue::s_GlobalMutex);

        bool is_packet_avail = s_VideoPacketQueue->dequeue(&video_packet) == 0;

        if (!is_packet_avail) {
            SDL_CondWait(PacketQueue::s_PacketAvailabilityCond, PacketQueue::s_GlobalMutex);
            SDL_UnlockMutex(PacketQueue::s_GlobalMutex);
            continue;
        }

        if (video_state->flags & VideoFlags::IS_PAUSED) {
            SDL_CondWait(s_VideoPausedCond, PacketQueue::s_GlobalMutex);
        }

        video_state->current_pts = 0;

        if (decode_video_frame(video_state, &video_packet) != 0) {
            SDL_UnlockMutex(PacketQueue::s_GlobalMutex);
            av_packet_unref(&video_packet);
            continue;
        };

        av_packet_unref(&video_packet);

        SDL_UnlockMutex(PacketQueue::s_GlobalMutex);

        update_pts(video_state, s_LatestPacket);
        synchronize_video(video_state);
    }

    return 0;
}

int VideoPlayer::decode_video_frame(
    VideoState* video_state, AVPacket* video_packet, AVFrame* dummy_frame)
{
    const auto& video_stream_info = s_StreamList.at("Video");

    if (!video_packet) {
        return -1;
    }

    // Send the packet to the decoder
    int send_pkt_errcode = avcodec_send_packet(video_stream_info->av_codec_ctx, video_packet);

    if (send_pkt_errcode < 0) {
        return -1;
    }

    // After sending the packet, receive the frame data from the decoder
    int receive_frame_errcode = avcodec_receive_frame(
        video_stream_info->av_codec_ctx, !dummy_frame ? s_LatestFrame : dummy_frame);

    if (receive_frame_errcode < 0) {
        return -1;
    }

    VideoPlayer::update_framebuffer(0, video_state);

    return 0;
}

int VideoPlayer::enqueue_packets(void* data)
{
    auto* video_state = static_cast<VideoState*>(data);

    const auto& video_stream_info = s_StreamList.at("Video");
    const auto& audio_stream_info = s_StreamList.at("Audio");

    auto& sws_scaler_ctx = video_state->sws_scaler_ctx;
    auto& av_format_ctx = video_state->av_format_ctx;

    while (Application::s_IsRunning) {
        SDL_LockMutex(PacketQueue::s_GlobalMutex);

        if (video_state->flags & VideoFlags::IS_PAUSED) {
            SDL_CondWait(s_VideoPausedCond, PacketQueue::s_GlobalMutex);
            SDL_UnlockMutex(PacketQueue::s_GlobalMutex);
            continue;
        }

        const int& response = av_read_frame(av_format_ctx, s_LatestPacket);

        if (response == AVERROR_EOF) {
            SDL_CondWait(s_FrameAvailabilityCond, PacketQueue::s_GlobalMutex);
            SDL_UnlockMutex(PacketQueue::s_GlobalMutex);
            continue;
        }

        SDL_UnlockMutex(PacketQueue::s_GlobalMutex);

        if (response < 0) {
            std::cerr << "Failed to decode the frames: " << av_error_to_string(response) << "\n";
            av_packet_unref(s_LatestPacket);
            break;
        }

        const std::uint32_t& packet_index = s_LatestPacket->stream_index;

        if (video_stream_info->stream_index == packet_index) {
            s_VideoPacketQueue->enqueue(s_LatestPacket);
        } else if (audio_stream_info->stream_index == packet_index) {
            s_AudioPacketQueue->enqueue(s_LatestPacket);
        }

        av_packet_unref(s_LatestPacket);
    }

    return 0;
}

#pragma region Seek Operation

int VideoPlayer::seek_frame(float seconds, bool should_update_framebuffer)
{
    if (seconds == -1.0f || seconds * AV_TIME_BASE > m_duration) {
        return -1;
    }

    SDL_LockMutex(PacketQueue::s_GlobalMutex);

    auto& sws_scaler = m_video_state->sws_scaler_ctx;
    auto& av_format_ctx = m_video_state->av_format_ctx;

    for (const std::string& key : { "Audio", "Video" }) {
        const auto& stream_info = s_StreamList.at(key);

        if (!is_rational_valid(stream_info->timebase)) {
            SDL_UnlockMutex(PacketQueue::s_GlobalMutex);
            return -1;
        }

        const auto target_timestamp = static_cast<int64_t>(seconds / av_q2d(stream_info->timebase));

        int response = av_seek_frame(
            av_format_ctx, stream_info->stream_index, target_timestamp, AVSEEK_FLAG_BACKWARD);

        if (response < 0) {
            SDL_UnlockMutex(PacketQueue::s_GlobalMutex);
            return -1;
        }

        avcodec_flush_buffers(stream_info->av_codec_ctx);
    }

    if (m_video_state->flags & VideoFlags::IS_PAUSED) {
        const auto& stream_info = s_StreamList.at("Video");

        while (av_read_frame(m_video_state->av_format_ctx, s_LatestPacket) >= 0) {
            if (s_LatestPacket->stream_index != stream_info->stream_index) {
                av_packet_unref(s_LatestPacket);
                continue;
            }

            int response = avcodec_send_packet(stream_info->av_codec_ctx, s_LatestPacket);

            if (response == AVERROR_EOF) {
                av_packet_unref(s_LatestPacket);
                break;
            }

            if (response == AVERROR(EAGAIN) || response < 0) {
                av_packet_unref(s_LatestPacket);
                continue;
            }

            response = avcodec_receive_frame(stream_info->av_codec_ctx, s_LatestFrame);

            if (response == AVERROR(EAGAIN) || response < 0) {
                av_packet_unref(s_LatestPacket);
                continue;
            }

            VideoPlayer::update_framebuffer(0, m_video_state.get());
            break;
        }
    }

    SDL_CondBroadcast(s_FrameAvailabilityCond);

    s_ClockNetwork->video_internal_clock = seconds;
    s_ClockNetwork->audio_internal_clock = seconds;

    s_VideoPacketQueue->clear();
    s_AudioPacketQueue->clear();

    SDL_UnlockMutex(PacketQueue::s_GlobalMutex);

    return 0;
}

#pragma endregion Seek Operation

#pragma region Switch Input
int VideoPlayer::restart_audio_thread()
{
    AVPacket* packet_for_first_frame = av_packet_alloc();
    AVFrame* first_frame = av_frame_alloc();

    const int samples_per_frame = nb_samples_per_frame(packet_for_first_frame, first_frame);
    const int AV_STEREO_CHANNEL_NB = av_get_channel_layout_nb_channels(AV_CH_LAYOUT_STEREO);

    if (m_video_state->flags & VideoFlags::IS_DECODING_THREAD_ACTIVE) {
        SDL_CloseAudioDevice(m_device_info->device_id);
    }

    av_packet_free(&packet_for_first_frame);
    av_frame_free(&first_frame);

    if (init_sdl_mixer(AV_STEREO_CHANNEL_NB, samples_per_frame) != 0) {
        return -1;
    };

    return 0;
}

int VideoPlayer::switch_input(AVFormatContext** av_format_context, const std::string& url)
{
    avformat_close_input(av_format_context);

    if (avformat_open_input(av_format_context, url.c_str(), nullptr, nullptr) != 0) {
        std::cout << "[Video Preview]: Failed to switch the input.\n";
        return -1;
    }

    reset_internal_clocks();
    reset_audio_buffer_info();

    (s_VideoPacketQueue, s_AudioPacketQueue)->clear();

    // Signal the packet enqueuer thread that a frame might be available.
    SDL_CondBroadcast(s_FrameAvailabilityCond);

    return 0;
}
#pragma endregion Switch Input

void VideoPlayer::pause_video()
{
    m_video_state->flags ^= VideoFlags::IS_PAUSED;
    pause_audio();

    if (s_VideoPausedCond) {
        SDL_CondBroadcast(s_VideoPausedCond);
    }
}

#pragma endregion Frame Reader

#pragma region Deallocation
void VideoPlayer::free_ffmpeg()
{
    sws_freeContext(m_video_state->sws_scaler_ctx);
    free_resampler_ctx();

    avformat_close_input(&m_video_state->av_format_ctx);
    avformat_free_context(m_video_state->av_format_ctx);

    av_frame_free(&s_LatestFrame);
    av_packet_free(&s_LatestPacket);
    av_packet_free(&m_audio_state->latest_audio_packet);

    for (const auto& pair : s_StreamList) {
        avcodec_free_context(&pair.second->av_codec_ctx);
    }
}

void VideoPlayer::stop_threads()
{
    m_video_state->flags &= ~VideoFlags::IS_PAUSED;

    // Signal every conditional variable to stop threads.
    SDL_CondBroadcast(s_VideoPausedCond);
    SDL_CondBroadcast(s_FrameAvailabilityCond);
    SDL_CondBroadcast(PacketQueue::s_PacketAvailabilityCond);

    if (m_video_tid) {
        SDL_WaitThread(m_video_tid, nullptr);
        m_video_tid = nullptr;
    }

    if (m_decoding_tid) {
        SDL_WaitThread(m_decoding_tid, nullptr);
        m_decoding_tid = nullptr;
    }
}
#pragma endregion Deallocation

} // namespace YAVE