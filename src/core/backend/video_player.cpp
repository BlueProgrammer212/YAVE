#include "core/backend/video_player.hpp"

namespace YAVE
{
std::unique_ptr<PacketQueue> VideoPlayer::s_VideoPacketQueue = std::make_unique<PacketQueue>();

VideoPlayer::VideoPlayer(SampleRate t_sample_rate)
    : m_video_state(std::make_shared<VideoState>())
    , m_hw_device_ctx(nullptr)
{
    m_audio_state->sample_rate = t_sample_rate;
    SDL_RegisterEvents(7);
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

int VideoPlayer::find_streams()
{
    auto& av_format_ctx = m_video_state->av_format_ctx;

    for (std::uint32_t i = 0; i < av_format_ctx->nb_streams; ++i) {
        auto& stream = av_format_ctx->streams[i];

        auto* av_codec_params = stream->codecpar;
        AVCodec* av_codec = avcodec_find_decoder(av_codec_params->codec_id);

        if (!av_codec) {
            continue;
        }

        auto stream_info = std::make_shared<StreamInfo>();

        stream_info->timebase = stream->time_base;
        stream_info->av_codec = av_codec;
        stream_info->av_codec_params = av_codec_params;
        stream_info->stream_index = i;

        if (av_codec_params->codec_type == AVMEDIA_TYPE_VIDEO) {
            stream_info->width = av_codec_params->width;
            stream_info->height = av_codec_params->height;
            add_stream(stream_info, "Video");
            continue;
        }

        if (av_codec_params->codec_type == AVMEDIA_TYPE_AUDIO) {
            add_stream(stream_info, "Audio");
            continue;
        }
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

    // Enable hardware acceleration.
    // init_hwaccel_decoder(stream_info->av_codec_ctx, AV_HWDEVICE_TYPE_OPENCL);

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
        height, AV_PIX_FMT_RGB0, SWS_BICUBIC, nullptr, nullptr, nullptr);

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

int VideoPlayer::init_hwaccel_decoder(AVCodecContext* ctx, const enum AVHWDeviceType type)
{
    if (m_video_state->flags & VideoFlags::IS_HWACCEL_INITIALIZED) {
        return 0;
    }

    ctx->get_format = get_hw_format;

    int err = 0;

    if ((err = av_hwdevice_ctx_create(&m_hw_device_ctx, type, nullptr, nullptr, 0)) < 0) {
        std::cerr << "Failed to create specified HW device.\n";
        return err;
    }

    m_video_state->flags |= VideoFlags::IS_HWACCEL_INITIALIZED;

    ctx->hw_device_ctx = av_buffer_ref(m_hw_device_ctx);
    return err;
}

#pragma endregion Init Functions

#pragma region Hardware Acceleration

AVPixelFormat VideoPlayer::get_hw_format(AVCodecContext* ctx, const AVPixelFormat* pix_fmts)
{

    for (const AVPixelFormat* p = pix_fmts; *p != -1; p++) {
        if (*p != AV_PIX_FMT_DXVA2_VLD) {
            continue;
        }

        return AV_PIX_FMT_DXVA2_VLD;
    }

    std::cerr << "Failed to get DXVA2 hardware surface format.\n";
    return AV_PIX_FMT_NONE;
}

#pragma endregion Hardware Acceleration

#pragma region Video Reader

int VideoPlayer::open_video(const char* filename)
{
    SDL_LockMutex(PacketQueue::mutex);

    auto& flags = m_video_state->flags;
    auto& av_format_ctx = m_video_state->av_format_ctx;

    if (flags & VideoFlags::IS_INITIALIZED) {
        flags |= VideoFlags::IS_INPUT_CHANGED;

        stop_threads();
        reset_video_state();

        // Reset the audio state.
        m_audio_state->pts = 0.0;
        m_audio_state->audio_diff_avg_count = 0;
        m_audio_state->delta_accum = 0;
    }

    m_video_state->av_format_ctx = avformat_alloc_context();

    if (!av_format_ctx) {
        std::cout << "Couldn't allocate memory for the format context.\n";
        return -1;
    }

    if (avformat_open_input(&av_format_ctx, filename, nullptr, nullptr) != 0) {
        std::cout << "Failed to open the file: " << filename << "\n";
        return -1;
    }

    opened_file = filename;
    m_duration = av_format_ctx->duration;
    m_video_state->flags |= VideoFlags::IS_INPUT_ACTIVE;

    find_streams();

    for (auto& stream : s_StreamList) {
        if (create_context_for_stream(stream.second) != 0) {
            std::cerr << "Initialization failed for one or more streams.\n";
            return -1;
        }
    }

    s_LatestFrame = av_frame_alloc();
    s_LatestPacket = av_packet_alloc();

    if (!s_LatestFrame || !s_LatestPacket) {
        std::cout << "Failed to allocate memory for AVFrame and AVPacket.\n";
        return -1;
    }

    m_video_state->flags |= VideoFlags::IS_INITIALIZED;

    SDL_UnlockMutex(PacketQueue::mutex);

    return 0;
}

int VideoPlayer::play_video(AVRational* timebase)
{
    auto& dimensions = m_video_state->dimensions;

    const auto& video_stream_info = s_StreamList.at("Video");
    const auto& audio_stream_info = s_StreamList.at("Audio");

    dimensions.x = video_stream_info->width;
    dimensions.y = video_stream_info->height;

    int ret = this->allocate_frame_buffer(
        video_stream_info->av_codec_ctx->pix_fmt, m_video_state->dimensions);

    if (ret != 0) {
        return -1;
    }

    *timebase = video_stream_info->timebase;

    m_video_state->is_first_frame = true;

    AVPacket* packet_for_first_frame = av_packet_alloc();
    AVFrame* first_frame = av_frame_alloc();

    PacketQueue::mutex = SDL_CreateMutex();
    if (!PacketQueue::mutex) {
        std::cerr << "Failed to create a mutex: " << SDL_GetError() << "\n";
        return -1;
    }

    PacketQueue::cond = SDL_CreateCond();

    if (!PacketQueue::cond) {
        std::cerr << "Failed to create a condition variable: " << SDL_GetError() << "\n";
        SDL_DestroyMutex(PacketQueue::mutex);
        return -1;
    }

    std::optional<AVFrame*> first_audio_frame =
        get_first_audio_frame(packet_for_first_frame, first_frame);

    const auto nb_samples = [&]() -> int {
        if (first_audio_frame.has_value()) {
            return first_audio_frame.value()->nb_samples;
        }

        return DEFAULT_LOW_LATENCY_SAMPLES_BUFFER_SIZE;
    }();

    // After obtaining the first frame, it's important to reset the timestamp.
    this->seek_frame(0.0);

    const int AV_STEREO_CHANNEL_NB = av_get_channel_layout_nb_channels(AV_CH_LAYOUT_STEREO);

    if (init_sdl_mixer(AV_STEREO_CHANNEL_NB, nb_samples) != 0) {
        return -1;
    };

    av_packet_free(&packet_for_first_frame);
    av_frame_free(&first_frame);

    // Start the decoding and video threads.
    m_video_tid = SDL_CreateThread(&video_callback, "Video Thread", m_video_state.get());

    m_decoding_tid = SDL_CreateThread(&enqueue_frames, "Decoding Thread", m_video_state.get());

    return 0;
}

std::optional<AVFrame*> VideoPlayer::get_first_audio_frame(
    AVPacket* dummy_packet, AVFrame* dummy_frame, int retry_count)
{
    constexpr int MAX_NUMBER_OF_ATTEMPTS = 1000;

    int response;

    if (retry_count > MAX_NUMBER_OF_ATTEMPTS) {
        return std::nullopt;
    }

    static const auto& stream_info = s_StreamList.at("Audio");

    response = av_read_frame(m_video_state->av_format_ctx, dummy_packet);

    if (response < 0) {
        goto repeat;
    }

    // Send the packet to the decoder.
    response = avcodec_send_packet(stream_info->av_codec_ctx, dummy_packet);

    if (response < 0) {
        goto repeat;
    }

    response = avcodec_receive_frame(stream_info->av_codec_ctx, dummy_frame);

    if (response < 0) {
        goto repeat;
    }

    return dummy_frame;

repeat:
    av_packet_unref(dummy_packet);
    return get_first_audio_frame(dummy_packet, dummy_frame, ++retry_count);
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

double VideoPlayer::calculateReferenceClock()
{
    double ref_clock = s_AudioInternalClock;

    const auto& [channel_nb, buffer_size, sample_rate, buffer_index, audio_data] =
        *s_AudioBufferInfo;

    int hw_buf_size = buffer_size - buffer_index;
    int sample_bytes = channel_nb * sizeof(float);
    int bytes_per_sec = sample_rate * sample_bytes;

    if (bytes_per_sec > 0) {
        ref_clock -= static_cast<double>(hw_buf_size) / static_cast<double>(bytes_per_sec);
    }

    return ref_clock;
}

double VideoPlayer::calculateActualDelay(VideoState* video_state, double& frame_timer)
{
    double delay = video_state->pts - video_state->last_pts;

    if (delay <= 0 || delay >= 1.0) {
        delay = video_state->last_delay;
    }

    video_state->last_delay = delay;
    video_state->last_pts = video_state->pts;

    const double current_time = av_gettime() / static_cast<double>(AV_TIME_BASE);

    const double ref_clock = calculateReferenceClock();
    const double diff = video_state->pts - ref_clock;

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
    const auto& timebase = s_StreamList.at("Video")->timebase;
    static double actual_delay = 0.0;

    if (video_state->is_first_frame) {
        // When the input is changed, update the frame timer.
        video_state->frame_timer = av_gettime() / static_cast<double>(AV_TIME_BASE);
        video_state->is_first_frame = false;
    }

    // Adjust frame_timer if video is paused
    double pauseElapsedTime = s_PauseEndTime - s_PauseStartTime;
    video_state->frame_timer -= pauseElapsedTime;

    s_PauseStartTime = 0.0;
    s_PauseEndTime = 0.0;

    double frame_delay = av_q2d(timebase);
    frame_delay += s_LatestFrame->repeat_pict * (frame_delay * 0.5);

    if (video_state->pts != 0) {
        s_VideoInternalClock = video_state->pts;
    } else {
        video_state->pts = s_VideoInternalClock;
    }

    s_VideoInternalClock += frame_delay;

    actual_delay = calculateActualDelay(video_state, video_state->frame_timer);
    SDL_Delay(static_cast<Uint32>(actual_delay * 1000 + 0.5));
}

void VideoPlayer::update_pts(VideoState* state, AVPacket* packet)
{
    const auto& time_base = s_StreamList.at("Video")->timebase;
    bool is_dts_avail = packet->dts != AV_NOPTS_VALUE;

    // Get the PTS of the current video frame.
    state->pts = (is_dts_avail ? static_cast<double>(s_LatestFrame->pts) : 0);

    if (is_rational_valid(time_base)) {
        state->pts *= av_q2d(time_base);
    }
}

int VideoPlayer::video_callback(void* data)
{
    auto* video_state = static_cast<VideoState*>(data);

    AVPacket* video_packet = av_packet_alloc();

    while (Application::is_running) {
        if (video_state->flags & VideoFlags::IS_INPUT_CHANGED) {
            break;
        }

        SDL_LockMutex(PacketQueue::mutex);

        bool is_packet_avail = s_VideoPacketQueue->dequeue(video_packet) == 0;

        if (!is_packet_avail) {
            SDL_UnlockMutex(PacketQueue::mutex);
            continue;
        }

        if (video_state->flags & VideoFlags::IS_PAUSED) {
            SDL_CondWait(PacketQueue::cond, PacketQueue::mutex);
        }

        video_state->pts = 0;

        if (decode_video_frame(video_state, video_packet) != 0) {
            SDL_UnlockMutex(PacketQueue::mutex);
            continue;
        };

        av_packet_unref(video_packet);

        SDL_UnlockMutex(PacketQueue::mutex);

        update_pts(video_state, s_LatestPacket);
        synchronize_video(video_state);
    }

    return 0;
}

int VideoPlayer::decode_video_frame(VideoState* video_state, AVPacket* video_packet)
{
    const auto& video_stream_info = s_StreamList.at("Video");

    auto& width = video_stream_info->width;
    auto& height = video_stream_info->height;

    if (!video_packet) {
        return -1;
    }

    // Send the packet to the decoder
    int send_pkt_errcode = avcodec_send_packet(video_stream_info->av_codec_ctx, video_packet);

    if (send_pkt_errcode < 0) {
        return -1;
    }

    // After sending the packet, receive the frame data from the decoder
    int receive_frame_errcode =
        avcodec_receive_frame(video_stream_info->av_codec_ctx, s_LatestFrame);

    if (receive_frame_errcode < 0) {
        return -1;
    }

    VideoPlayer::update_framebuffer(0, video_state);

    return 0;
}

int VideoPlayer::enqueue_frames(void* data)
{
    auto* video_state = static_cast<VideoState*>(data);

    const auto& video_stream_info = s_StreamList.at("Video");
    const auto& audio_stream_info = s_StreamList.at("Audio");

    auto& sws_scaler_ctx = video_state->sws_scaler_ctx;
    auto& av_format_ctx = video_state->av_format_ctx;

    while (Application::is_running) {
        if (video_state->flags & VideoFlags::IS_INPUT_CHANGED) {
            break;
        }

        const int& response = av_read_frame(av_format_ctx, s_LatestPacket);

        if (response == AVERROR_EOF) {
            continue;
        }

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

int VideoPlayer::seek_frame(float seconds)
{
    if (seconds == -1.0f || seconds > (m_duration / AV_TIME_BASE)) {
        return -1;
    }

    SDL_LockMutex(PacketQueue::mutex);

    auto& sws_scaler = m_video_state->sws_scaler_ctx;
    auto& av_format_ctx = m_video_state->av_format_ctx;

    // Perform the seeking operation on the audio stream first before the video
    // stream.
    static const std::array<std::string, 2> stream_ids = { "Audio", "Video" };

    for (const std::string& key : stream_ids) {
        const auto& stream_info = s_StreamList.at(key);

        if (!is_rational_valid(stream_info->timebase)) {
            SDL_UnlockMutex(PacketQueue::mutex);
            return -1;
        }

        const auto target_second = static_cast<int64_t>(seconds * AV_TIME_BASE);

        const auto target_timestamp =
            av_rescale_q(target_second, AVRational{ 1, AV_TIME_BASE }, stream_info->timebase);

        int response = av_seek_frame(
            av_format_ctx, stream_info->stream_index, target_timestamp, AVSEEK_FLAG_BACKWARD);

        if (response < 0) {
            SDL_UnlockMutex(PacketQueue::mutex);
            return -1;
        }

        avcodec_flush_buffers(stream_info->av_codec_ctx);
    }

    s_VideoInternalClock = seconds;
    s_AudioInternalClock = s_VideoInternalClock;

    m_audio_state->audio_diff_avg_count = 0;
    m_audio_state->delta_accum = 0.0;

    s_VideoPacketQueue->clear();
    s_AudioPacketQueue->clear();

    SDL_UnlockMutex(PacketQueue::mutex);

    return 0;
}

#pragma endregion Seek Operation

void VideoPlayer::pause_video()
{
    m_video_state->flags ^= VideoFlags::IS_PAUSED;
    pause_audio();

    if (PacketQueue::cond) {
        SDL_CondSignal(PacketQueue::cond);
    }
}

#pragma endregion Frame Reader

#pragma region Deallocation
void VideoPlayer::free_ffmpeg()
{
    sws_freeContext(m_video_state->sws_scaler_ctx);
    free_resampler_ctx();

    if (m_video_state->flags & VideoFlags::IS_INPUT_ACTIVE) {
        avformat_close_input(&m_video_state->av_format_ctx);
    }

    avformat_free_context(m_video_state->av_format_ctx);

    av_frame_free(&s_LatestFrame);
    av_packet_free(&s_LatestPacket);
    av_packet_free(&s_LatestAudioPacket);

    if (m_hw_device_ctx) {
        // Unreference the hardware device context
        av_buffer_unref(&m_hw_device_ctx);
    }

    for (const auto& pair : s_StreamList) {
        avcodec_free_context(&pair.second->av_codec_ctx);
    }
}

void VideoPlayer::stop_threads()
{
    if (m_video_tid) {
        SDL_WaitThread(m_video_tid, nullptr);
        m_video_tid = nullptr;
    }

    if (m_decoding_tid) {
        SDL_WaitThread(m_decoding_tid, nullptr);
        m_decoding_tid = nullptr;
    }
}

void VideoPlayer::reset_video_state()
{
    if (m_video_state->buffer) {
        av_freep(&m_video_state->buffer);
        m_video_state->buffer = nullptr;
    }

    m_video_state->flags = VideoFlags::IS_INITIALIZED;

    av_frame_free(&s_LatestFrame);
    av_packet_free(&s_LatestPacket);

    for (const auto& pair : s_StreamList) {
        if (pair.second->av_codec_ctx) {
            avcodec_free_context(&pair.second->av_codec_ctx);
        }
    }

    if (m_video_state->av_format_ctx) {
        avformat_close_input(&m_video_state->av_format_ctx);
        avformat_free_context(m_video_state->av_format_ctx);
        m_video_state->av_format_ctx = nullptr;
    }

    s_VideoPacketQueue->clear();
    s_AudioPacketQueue->clear();

    // Reset the clock network.
    s_VideoInternalClock = 0.0;
    s_AudioInternalClock = 0.0;
    m_video_state->frame_timer = 0.0;
    m_video_state->is_first_frame = true;

    // Reset the timestamps
    m_video_state->pts = 0.0;
    m_video_state->last_delay = 40e-3;
    m_video_state->last_pts = 0.0;

    // Reset audio buffer information
    s_AudioBufferInfo->buffer_index = 0;
    s_AudioBufferInfo->buffer_size = 0;
    s_AudioBufferInfo->channel_nb = 2;
    s_AudioBufferInfo->sample_rate = 44100;

    // Reset the dimensions of the video.
    m_video_state->dimensions = VideoDimension(640, 360);

    std::cout << "Video state has been reset.\n";

    SDL_DestroyMutex(PacketQueue::mutex);
    SDL_DestroyCond(PacketQueue::cond);
}

#pragma endregion Deallocation

} // namespace YAVE