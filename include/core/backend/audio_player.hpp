#pragma once

#include <SDL.h>
#include <optional>
#include <thread>

#include "core/backend/video_loader.hpp"

#include <SDL_mixer.h>
#include <SDL_mutex.h>
#include <SDL_thread.h>

namespace YAVE
{
struct StreamInfo;
class PacketQueue;

constexpr int DEFAULT_SAMPLES_BUFFER_SIZE = 1024;

// A/V Synchronization Constants
constexpr int SAMPLE_CORRECTION_PERCENT_MAX = 10;
constexpr double AV_DIFFERENCE_AVG_COEF = 0.99;
constexpr double AV_DIFFERENCE_COUNT = 20.0;
constexpr double SYNC_THRESHOLD = 0.045;
constexpr double NOSYNC_THRESHOLD = 1.0;

/**
 * @typedef SampleRate
 * @brief The first element is for the input sample rate. While
 * the second element represents the output sample rate.
 */
using SampleRate = std::pair<int, int>;

#pragma region Audio Flags

// clang-format off
/**
 * @enum AudioFlags
 * @brief Flags representing various states of the audio system.
 */
enum class AudioFlags : unsigned int {
  NONE                     = 0, 
  IS_MUTED                 = 1 << 0,  ///< Flag indicating audio is muted.
  IS_PAUSED                = 1 << 1,  ///< Flag indicating audio is paused.
  IS_AUDIO_THREAD_ACTIVE   = 1 << 2,  ///< Flag indicating audio thread is running.
  IS_INPUT_CHANGED         = 1 << 3   ///< Flag indicating that the input was changed.
};
// clang-format on

/**
 * @typedef AudioFlagType
 * @brief Alias for the underlying type of AudioFlags.
 */
using AudioFlagType = std::underlying_type<AudioFlags>::type;

[[nodiscard]] inline AudioFlags operator|(AudioFlags lhs, AudioFlags rhs)
{
    return static_cast<AudioFlags>(
        static_cast<AudioFlagType>(lhs) | static_cast<AudioFlagType>(rhs));
}

inline AudioFlags& operator|=(AudioFlags& lhs, AudioFlags rhs)
{
    lhs = lhs | rhs;
    return lhs;
}

[[nodiscard]] inline bool operator&(AudioFlags lhs, AudioFlags rhs)
{
    return static_cast<bool>(static_cast<AudioFlagType>(lhs) & static_cast<AudioFlagType>(rhs));
}

inline void operator&=(AudioFlags& lhs, AudioFlags rhs)
{
    lhs =
        static_cast<AudioFlags>(static_cast<AudioFlagType>(lhs) & static_cast<AudioFlagType>(rhs));
}

[[nodiscard]] inline AudioFlags operator^(AudioFlags lhs, AudioFlags rhs)
{
    return static_cast<AudioFlags>(
        static_cast<AudioFlagType>(lhs) ^ static_cast<AudioFlagType>(rhs));
}

inline AudioFlags& operator^=(AudioFlags& lhs, AudioFlags rhs)
{
    lhs = lhs ^ rhs;
    return lhs;
}

inline AudioFlags operator~(AudioFlags lhs)
{
    return static_cast<AudioFlags>(~static_cast<std::uint32_t>(lhs));
}

#pragma endregion Audio Flags

struct AudioResamplingState {
    std::vector<float>* audio_buffer = nullptr;
    int num_samples = 0;
    int num_channels = 0;
    int out_samples = 0;
};

struct AudioBufferInfo {
    int channel_nb = 2;
    int buffer_size = 0;
    int sample_rate = 44100;
    int buffer_index = 0;
};

/**
 * @struct AudioDeviceInfo
 * @brief Contains information about the audio device.
 */
struct AudioDeviceInfo {
    SDL_AudioDeviceID device_id;
    SDL_AudioSpec spec;
    SDL_AudioSpec wanted_spec;
};

struct ClockNetwork {
    double video_internal_clock = 0.0;
    double audio_internal_clock = 0.0;
    double pause_start_time = 0.0;
    double pause_end_time = 0.0;
};

#pragma region Audio Player

class AudioPlayer
{
public:
    virtual ~AudioPlayer() {};
    AudioPlayer();

    static StreamMap s_StreamList;

    /**
     * @struct AudioState
     * @brief The audio player's data that will be passed to the audio callback.
     */
    struct AudioState {
        AVCodecContext* av_codec_ctx = nullptr;
        AVPacket* latest_audio_packet = nullptr;
        AudioFlags flags = AudioFlags::NONE;
        SampleRate sample_rate;
        double pts = 0;
        double delta_accum = 0;
        double audio_diff_avg_count = 0;
    };

    static int add_dummy_samples(float* samples, int* samples_size, int wanted_size, int max_size,
        const int total_sample_bytes);

    /**
     * @brief Calculates the ideal number of samples for A/V synchronization.
     * @param audio_state Contains relevant information about the audio.
     * @param samples This is the audio buffer data
     * @param num_samples The number of samples avaiable inside the frame.
     * @param[in, out] initial_buffer_size The initial size of buffer before synchronization.
     * @return 0 <= for sucess, and a negative integer if there is an error.
     */
    static int synchronize_audio(
        AudioState* audio_state, float* samples, int num_samples, int* initial_buffer_size);

    /**
     * @brief Callback that feeds the audio device with samples.
     */
    static void SDLCALL audio_callback(void* userdata, Uint8* stream, int len);

    /**
     * @brief Sends the packets from the packet queue to the decoder
     *        and recieves the audio frame. This function is called by
     *        the audio callback.
     *
     * @param av_packet The audio packet that will be decoded.
     *
     * @return 0 <= on success. Otherwise, it returns a negative integer
     *         if there is an error.
     */
    static int decode_audio_packet(AudioState* userdata, AVPacket* audio_packet);

    /**
     * @brief Resamples the audio buffer and writes it to the SDL stream.
     */
    static int update_audio_stream(AudioState* userdata, Uint8* sdl_stream, int& len);

    /**
     * @brief Recurses until we get a valid audio frame.
     * @param packet
     * @param frame
     * @return A pointer to an AVFrame if it's sucessful, and std::nullopt for failure.
     */
    [[nodiscard]] std::optional<AVFrame*> get_first_audio_frame(
        AVFormatContext* av_format_context, AVPacket* dummy_packet, AVFrame* dummy_frame);

#pragma region Helper Functions
    /**
     * @brief Converts planar audio data to an interleaved audio data.
     * @param audio_data See \ref AudioResamplingState for the structure definition.
     */
    static void resample_audio(AVFrame* latest_frame, struct AudioResamplingState audio_data);

    /**
     * @brief Toggles the flag \ref AudioFlags::IS_MUTED
     */
    void toggle_audio();

    [[nodiscard]] inline bool is_muted() const
    {
        return m_audio_state->flags & AudioFlags::IS_MUTED;
    }

    [[nodiscard]] inline void pause_audio()
    {
        m_audio_state->flags ^= AudioFlags::IS_PAUSED;
        bool should_resume = m_audio_state->flags & AudioFlags::IS_PAUSED;

        SDL_PauseAudioDevice(m_device_info->device_id, should_resume);

        if (should_resume) {
            s_ClockNetwork->pause_end_time = av_gettime() / static_cast<double>(AV_TIME_BASE);
            return;
        }

        s_ClockNetwork->pause_start_time = av_gettime() / static_cast<double>(AV_TIME_BASE);
    }

    [[nodiscard]] inline auto& get_packet_queue()
    {
        return s_AudioPacketQueue;
    }

    [[nodiscard]] static inline double& get_video_internal_clock()
    {
        return s_ClockNetwork->video_internal_clock;
    }

    [[nodiscard]] static inline const double& get_audio_internal_clock()
    {
        return s_ClockNetwork->audio_internal_clock;
    }

    [[nodiscard]] int guess_correct_buffer_size(const StreamInfoPtr& stream_info);

    /**
     * @brief Checks if the denominator and numerator is a non-zero integer.
     * @param av_rational
     * @return true if the rational is valid.
     */
    [[nodiscard]] static inline bool is_rational_valid(AVRational av_rational)
    {
        return av_rational.den > 0 && av_rational.num > 0;
    }
#pragma endregion Helper Functions

    static std::unique_ptr<AudioBufferInfo> s_AudioBufferInfo;
    static std::unique_ptr<PacketQueue> s_AudioPacketQueue;

    static SDL_cond* s_FrameAvailabilityCond;
    static SDL_cond* s_VideoPausedCond;
    static SDL_cond* s_VideoAvailabilityCond;

protected:
    /**
     * @brief Initializes the resampler library.
     * @param num_channels The number of audio channels.
     * @return 0 <= for success, a negative integer for error.
     */
    static int init_swr_ctx(AVFrame* av_frame, SampleRate sample_rate);

    /**
     * @brief Initializes the SDL mixer library.
     * @param num_channels The number of audio channels. (e.g mono, stereo, or surround)
     * @param nb_samples The number of samples inside a frame.
     * @return 0 <= for success, a negative integer for error.
     */
    int init_sdl_mixer(int num_channels, int nb_samples);

    /**
     * @brief Frees the memory allocated by the resampler context.
     */
    inline void free_resampler_ctx()
    {
        swr_free(&s_Resampler_Context);
    };

    void free_sdl_mixer();

protected:
    static AVFrame* s_LatestFrame;
    static AVPacket* s_LatestPacket;

protected:
    static std::unique_ptr<ClockNetwork> s_ClockNetwork;
    std::unique_ptr<AudioDeviceInfo> m_device_info;
    std::shared_ptr<AudioState> m_audio_state;

    inline void reset_audio_buffer_info()
    {
        // Reset audio buffer information
        s_AudioBufferInfo->buffer_index = 0;
        s_AudioBufferInfo->buffer_size = 0;
        s_AudioBufferInfo->channel_nb = 2;
        s_AudioBufferInfo->sample_rate = 44100;
    }

private:
    static SwrContext* s_Resampler_Context;

    [[nodiscard]] static inline int calculate_bounds(int size, bool is_max)
    {
        auto sample_correction = (SAMPLE_CORRECTION_PERCENT_MAX / 100.0);
        sample_correction *= -1 * !is_max;

        return static_cast<int>(size * (1.0 + sample_correction));
    };
};

#pragma endregion Audio Player
} // namespace YAVE