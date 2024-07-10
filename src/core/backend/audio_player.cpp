#include "core/backend/audio_player.hpp"
#include "core/application.hpp"
#include "core/backend/packet_queue.hpp"

namespace YAVE
{
SwrContext* AudioPlayer::s_Resampler_Context = nullptr;

// Create pointers to store the latest frame and packet.
AVFrame* AudioPlayer::s_LatestFrame = nullptr;
AVPacket* AudioPlayer::s_LatestPacket = nullptr;

StreamMap AudioPlayer::s_StreamList = {};

// Initialize the clock network.
double AudioPlayer::s_MasterClock = 0.0;
double AudioPlayer::s_AudioInternalClock = 0.0;

double AudioPlayer::s_PauseStartTime = 0.0;
double AudioPlayer::s_PauseEndTime = 0.0;

std::unique_ptr<AudioBufferInfo> AudioPlayer::s_AudioBufferInfo =
    std::make_unique<AudioBufferInfo>();

std::unique_ptr<PacketQueue> AudioPlayer::s_AudioPacketQueue = std::make_unique<PacketQueue>();

AudioPlayer::AudioPlayer()
    : m_audio_state(std::make_shared<AudioState>())
    , m_device_info(std::make_unique<AudioDeviceInfo>())
{
}

#pragma region Init Functions

int AudioPlayer::init_swr_ctx(AVFrame* av_frame, SampleRate sample_rate)
{
    static bool is_sw_resample_ctx_initialized = false;

    if (is_sw_resample_ctx_initialized) {
        return 0;
    }

    const int64_t channel_layout = av_get_default_channel_layout(av_frame->channels);

    s_Resampler_Context = swr_alloc();
    if (!s_Resampler_Context) {
        std::cerr << "Failed to allocate memory for the resampler context.\n";
        return -1;
    }

    swr_alloc_set_opts(s_Resampler_Context, channel_layout, AV_SAMPLE_FMT_FLT, sample_rate.first,
        channel_layout, AV_SAMPLE_FMT_FLTP, sample_rate.second, 0, nullptr);

    int init_result = swr_init(s_Resampler_Context);

    if (init_result >= 0) {
        is_sw_resample_ctx_initialized = true;
        return 0;
    }

    std::cout << "Failed to initialize the resampler context: " << av_error_to_string(init_result)
              << "\n";

    swr_free(&s_Resampler_Context);
    return -1;
}

int AudioPlayer::guess_correct_buffer_size(const StreamInfoPtr& stream_info)
{
    if (~stream_info->av_codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE) {
        return stream_info->av_codec_ctx->frame_size;
    }

    return 0;
}

int AudioPlayer::init_sdl_mixer(int num_channels, int nb_samples)
{
    const auto& stream_info = s_StreamList.at("Audio");
    auto& [device_id, spec, wanted_spec] = *m_device_info;

    m_audio_state->flags &= ~AudioFlags::IS_INPUT_CHANGED;
    m_audio_state->flags |= AudioFlags::IS_AUDIO_THREAD_ACTIVE;

    SDL_memset(&wanted_spec, 0, sizeof(wanted_spec));

    wanted_spec.freq = stream_info->av_codec_ctx->sample_rate;
    wanted_spec.format = AUDIO_F32;
    wanted_spec.channels = num_channels;
    wanted_spec.silence = 0;

    const int fixed_buffer_size = guess_correct_buffer_size(stream_info);

    if (fixed_buffer_size <= 0) {
        wanted_spec.samples = nb_samples;
    } else {
        wanted_spec.samples = fixed_buffer_size;
    }

    wanted_spec.callback = &audio_callback;

    m_audio_state->av_codec_ctx = stream_info->av_codec_ctx;
    wanted_spec.userdata = m_audio_state.get();

    device_id = SDL_OpenAudioDevice(nullptr, 0, &wanted_spec, &spec, SDL_AUDIO_ALLOW_FORMAT_CHANGE);

    if (device_id == 0) {
        std::cerr << "Failed to open an audio device: " << SDL_GetError() << "\n";
        return -1;
    }

    SDL_PauseAudioDevice(device_id, 0);

    return 0;
}

#pragma endregion Init Functions

#pragma region Frame Processing

int AudioPlayer::update_audio_stream(AudioState* userdata, Uint8* sdl_stream, int& len)
{
    if (userdata->flags & AudioFlags::IS_MUTED) {
        std::memset(sdl_stream, 0, len);
        len = 0;
        return 0;
    }

    const auto num_samples = s_LatestFrame->nb_samples;
    const auto num_channels = s_LatestFrame->channels;
    const auto out_samples = swr_get_out_samples(s_Resampler_Context, num_samples);

    const AVSampleFormat sample_format = userdata->av_codec_ctx->sample_fmt;

    auto buffer_size =
        av_samples_get_buffer_size(nullptr, num_channels, num_samples, sample_format, 1);

    if (buffer_size < 0) {
        std::array<char, AV_ERROR_MAX_STRING_SIZE> err_buf = { 0 };
        av_strerror(buffer_size, err_buf.data(), err_buf.size());
        std::cerr << "Error obtaining audio buffer size: " << err_buf.data() << "\n";
        return -1;
    }

    if (!av_sample_fmt_is_planar(sample_format)) {
        if (synchronize_audio(userdata, reinterpret_cast<float*>(s_LatestFrame->data[0]),
                num_samples, &buffer_size) != 0) {
            return -1;
        };

        const int nonplanar_sample_size =
            sizeof(decltype(*s_LatestFrame->data)) / AV_NUM_DATA_POINTERS;

        const int sample_per_ch_size = nonplanar_sample_size * userdata->av_codec_ctx->channels;

        const int bytes_per_sec = sample_per_ch_size * userdata->av_codec_ctx->sample_rate;

        s_AudioInternalClock +=
            static_cast<double>(buffer_size) / static_cast<double>(bytes_per_sec);

        userdata->pts = s_AudioInternalClock;

        s_AudioBufferInfo->channel_nb = userdata->av_codec_ctx->channels;
        s_AudioBufferInfo->buffer_size = buffer_size;
        s_AudioBufferInfo->sample_rate = s_LatestFrame->sample_rate;
        s_AudioBufferInfo->buffer_index = 0;

        constexpr static int DOWNSAMPLE_FACTOR = 1024;

        for (int i = 0; i < num_samples; i += DOWNSAMPLE_FACTOR) {
            s_AudioBufferInfo->audio_data.push_back(*s_LatestFrame->data[0]);
        }

        std::memcpy(sdl_stream, s_LatestFrame->data[0], buffer_size);
        len = 0;

        return 0;
    }

    std::vector<float> resampled_audio_buffer(out_samples * num_channels);

    AudioResamplingState resampling_data = { &resampled_audio_buffer, num_samples, num_channels,
        out_samples };

    resample_audio(s_LatestFrame, resampling_data);

    if (synchronize_audio(userdata, reinterpret_cast<float*>(s_LatestFrame->data[0]), num_samples,
            &buffer_size) != 0) {
        return -1;
    };

    static const auto bytes_per_sample = sizeof(decltype(resampled_audio_buffer)::value_type);

    const int sample_per_ch_size = bytes_per_sample * userdata->av_codec_ctx->channels;

    const int bytes_per_sec = sample_per_ch_size * userdata->av_codec_ctx->sample_rate;

    s_AudioInternalClock += static_cast<double>(buffer_size) / static_cast<double>(bytes_per_sec);

    userdata->pts = s_AudioInternalClock;

    s_AudioBufferInfo->channel_nb = userdata->av_codec_ctx->channels;
    s_AudioBufferInfo->buffer_size = buffer_size;
    s_AudioBufferInfo->sample_rate = s_LatestFrame->sample_rate;
    s_AudioBufferInfo->buffer_index = 0;

    constexpr static int DOWNSAMPLE_FACTOR = 1024;

    for (int i = 0; i < resampled_audio_buffer.size(); i += DOWNSAMPLE_FACTOR) {
        s_AudioBufferInfo->audio_data.push_back(resampled_audio_buffer[i]);
    }

    std::memcpy(sdl_stream, resampled_audio_buffer.data(), buffer_size);
    len = 0;

    return 0;
}

#pragma endregion Frame Processing

#pragma region Sample Correction

int AudioPlayer::add_dummy_samples(
    float* samples, int* samples_size, int wanted_size, int max_size, const int total_sample_bytes)
{
    // Check for sufficient buffer size before adding samples
    int additional_samples = (*samples_size - wanted_size);
    const int required_size = *samples_size + additional_samples;

    if (required_size > max_size) {
        std::cerr << "Error: Buffer size exceeds allocated memory" << std::endl;
        return -1;
    }

    // Apply sample correction by replaying the last audio sample
    auto* samples_uint8 = reinterpret_cast<uint8_t*>(samples);

    auto* last_sample_ptr = samples_uint8 + *samples_size - total_sample_bytes;
    auto* next_sample_ptr = last_sample_ptr + total_sample_bytes;

    while (additional_samples > 0) {
        std::memcpy(next_sample_ptr, last_sample_ptr, total_sample_bytes);
        next_sample_ptr += total_sample_bytes;
        additional_samples -= total_sample_bytes;
    }

    return 0;
}

int AudioPlayer::synchronize_audio(
    struct AudioState* audio_state, float* samples, int num_samples, int* samples_size)
{
    const double delta = s_AudioInternalClock - s_MasterClock;
    double avg_diff = 0.0;

    const auto sample_rate = audio_state->av_codec_ctx->sample_rate;
    const int num_channels = audio_state->av_codec_ctx->channels;
    const int sample_size = sizeof(decltype(*samples));

    if (delta >= 1.0) {
        audio_state->audio_diff_avg_count = 0;
        audio_state->delta_accum = 0;
        return 0;
    }

    audio_state->delta_accum = delta + AUDIO_DIFF_AVG_COEF * audio_state->delta_accum;

    if (audio_state->audio_diff_avg_count < AUDIO_DIFF_AVG_NB) {
        audio_state->audio_diff_avg_count++;
        return 0;
    }

    avg_diff = audio_state->delta_accum * (1.0 - AUDIO_DIFF_AVG_COEF);

    // If the difference is small, don't adjust the samples.
    if (std::abs(avg_diff) < SYNC_THRESHOLD) {
        return 0;
    }

    // Calculate the ideal buffer size to successfully synchronize the audio.
    const int total_sample_bytes = num_channels * sample_size;

    int wanted_size = *samples_size + (static_cast<int>(delta * sample_rate) * total_sample_bytes);

    const int min_size = calculate_bounds(*samples_size, false);
    const int max_size = calculate_bounds(*samples_size, true);

    wanted_size = std::clamp(wanted_size, min_size, max_size);

    // Remove samples if the video is late
    if (wanted_size < *samples_size) {
        *samples_size = wanted_size;
        return 0;
    }

    add_dummy_samples(samples, samples_size, wanted_size, max_size, total_sample_bytes);

    *samples_size = wanted_size;

    return 0;
}

#pragma endregion Sample Correction

#pragma region Audio Callback
void AudioPlayer::audio_callback(void* t_userdata, Uint8* stream, int len)
{
    auto* userdata = static_cast<AudioState*>(t_userdata);

    AVPacket* audio_packet = av_packet_alloc();

    while (len > 0) {
        int result = decode_audio_packet(userdata, audio_packet);

        if (result < 0) {
            std::memset(stream, 0, len);
            len = 0;
            break;
        }

        av_packet_unref(audio_packet);

        init_swr_ctx(s_LatestFrame, userdata->sample_rate);

        if (update_audio_stream(userdata, stream, len) != 0) {
            break;
        }
    }

    av_packet_free(&audio_packet);
}
#pragma endregion Audio Callback

#pragma region Packet Decoder

int AudioPlayer::decode_audio_packet(struct AudioState* userdata, AVPacket* audio_packet)
{
    SDL_LockMutex(PacketQueue::mutex);

    const auto& stream_info = s_StreamList.at("Audio");

    if (s_AudioPacketQueue->dequeue(audio_packet) != 0) {
        SDL_UnlockMutex(PacketQueue::mutex);
        av_packet_unref(audio_packet);
        return -1;
    }

    if (!audio_packet) {
        SDL_UnlockMutex(PacketQueue::mutex);
        return -1;
    }

    int response = avcodec_send_packet(stream_info->av_codec_ctx, audio_packet);

    if (response == AVERROR(EAGAIN)) {
        SDL_UnlockMutex(PacketQueue::mutex);
        return -1;
    }

    if (response < 0 || response == AVERROR_EOF) {
        SDL_UnlockMutex(PacketQueue::mutex);
        return -1;
    }

    if (audio_packet->pts != AV_NOPTS_VALUE) {
        s_AudioInternalClock = av_q2d(stream_info->timebase) * audio_packet->pts;
    }

    response = avcodec_receive_frame(stream_info->av_codec_ctx, s_LatestFrame);

    if (response == AVERROR(EAGAIN)) {
        SDL_UnlockMutex(PacketQueue::mutex);
        return -1;
    }

    if (response < 0 || response == AVERROR_EOF) {
        SDL_UnlockMutex(PacketQueue::mutex);
        return -1;
    }

    SDL_UnlockMutex(PacketQueue::mutex);
    return 0;
}

#pragma endregion Packet Decoder

#pragma region Helper Functions
void AudioPlayer::toggle_audio()
{
    m_audio_state->flags ^= AudioFlags::IS_MUTED;
}

void AudioPlayer::resample_audio(AVFrame* frame, struct AudioResamplingState data)
{
    if (!s_Resampler_Context) {
        std::cerr << "Resampler context is not initialized.\n";
        return;
    }

    std::array<float*, 1> out_data = { data.audio_buffer->data() };
    auto extended_data_ptr = const_cast<const uint8_t**>(frame->extended_data);

    int ret = swr_convert(s_Resampler_Context, reinterpret_cast<uint8_t**>(out_data.data()),
        data.out_samples, extended_data_ptr, data.num_samples);

    if (ret < 0) {
        std::array<char, AV_ERROR_MAX_STRING_SIZE> err_buf = { 0 };
        av_strerror(ret, err_buf.data(), err_buf.size());

        std::cerr << "Failed to convert the audio data from planar to interleaved: "
                  << err_buf.data() << "\n";

        return;
    }
}
#pragma endregion Helper Functions

#pragma region Deallocation
void AudioPlayer::free_sdl_mixer()
{
    SDL_CloseAudioDevice(m_device_info->device_id);
    SDL_DestroyMutex(PacketQueue::mutex);
    SDL_DestroyCond(PacketQueue::cond);
}
#pragma endregion Deallocation

} // namespace YAVE