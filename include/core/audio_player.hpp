#pragma once

#include <SDL.h>
#include <algorithm>
#include <array>
#include <functional>
#include <iostream>
#include <memory>
#include <thread>
#include <unordered_map>
#include <vector>

#ifdef __cplusplus
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#endif

#include <SDL_mixer.h>
#include <SDL_mutex.h>
#include <SDL_thread.h>

namespace YAVE {
struct StreamInfo;
class PacketQueue;

constexpr int DEFAULT_LOW_LATENCY_SAMPLES_BUFFER_SIZE = 1024;
constexpr std::size_t NUM_OF_STREAMS = 2;

// A/V Synchronization Constants
constexpr int SAMPLE_CORRECTION_PERCENT_MAX = 10;
constexpr double AUDIO_DIFF_AVG_NB = 20.0;

constexpr double SYNC_THRESHOLD = 0.045;
constexpr double NOSYNC_THRESHOLD = 1.0;

constexpr double AUDIO_DIFF_AVG_COEF = 0.95;

/**
 * @typedef SampleRate
 * @brief The first element is for the input samples. While
 * the second element represents the output samples.
 */
using SampleRate = std::pair<int, int>;

/**
 * @typedef StreamInfoPtr
 * @brief An alias for std::shared_ptr<StreamInfo>
 */
using StreamInfoPtr = std::shared_ptr<StreamInfo>;

/**
 * @typedef StreamMap
 * @brief A data type for the stream info cache.
 */
using StreamMap = std::unordered_map<std::string, StreamInfoPtr>;

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

[[nodiscard]] inline AudioFlags operator|(AudioFlags lhs, AudioFlags rhs) {
  return static_cast<AudioFlags>(static_cast<AudioFlagType>(lhs) |
                                 static_cast<AudioFlagType>(rhs));
}

inline AudioFlags& operator|=(AudioFlags& lhs, AudioFlags rhs) {
  lhs = lhs | rhs;
  return lhs;
}

[[nodiscard]] inline bool operator&(AudioFlags lhs, AudioFlags rhs) {
  return static_cast<bool>(static_cast<AudioFlagType>(lhs) &
                           static_cast<AudioFlagType>(rhs));
}

inline void operator&=(AudioFlags& lhs, AudioFlags rhs) {
  lhs = static_cast<AudioFlags>(static_cast<AudioFlagType>(lhs) &
                                static_cast<AudioFlagType>(rhs));
}

[[nodiscard]] inline AudioFlags operator^(AudioFlags lhs, AudioFlags rhs) {
  return static_cast<AudioFlags>(static_cast<AudioFlagType>(lhs) ^
                                 static_cast<AudioFlagType>(rhs));
}

inline AudioFlags& operator^=(AudioFlags& lhs, AudioFlags rhs) {
  lhs = lhs ^ rhs;
  return lhs;
}

inline AudioFlags operator~(AudioFlags lhs) {
  return static_cast<AudioFlags>(~static_cast<std::uint32_t>(lhs));
}

#pragma endregion Audio Flags

#pragma region Stream

/**
 * @struct Codec 
 * @brief A data type that includes the codec's context, parameters, and the codec itself.
 */
struct Codec {
  virtual ~Codec() = default;

  AVCodec* av_codec = nullptr;
  AVCodecParameters* av_codec_params = nullptr;
  AVCodecContext* av_codec_ctx = nullptr;
};

/**
 * @struct StreamInfo
 * @brief A data type that contains all the relevant information of a stream.
 */
struct StreamInfo : public Codec {
  AVRational timebase = AVRational{0, 0};
  int stream_index = -1;
  int width = -1;
  int height = -1;
};

#pragma endregion Stream

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

#pragma region Error Handlers
/**
 * @brief Converts FFmpeg error codes to a string.
 * @param errnum The FFmpeg error code.
 * @return The error string.
 */
[[nodiscard]] inline static std::string av_error_to_string(int errnum) {
  std::array<char, AV_ERROR_MAX_STRING_SIZE> buffer;
  av_strerror(errnum, buffer.data(), AV_ERROR_MAX_STRING_SIZE);
  return std::string(buffer.data());
}

#pragma endregion Error Handlers

#pragma region Audio Player

class AudioPlayer {
 public:
  virtual ~AudioPlayer(){};
  AudioPlayer();

  static StreamMap s_StreamList;

  /**
   * @struct AudioState
   * @brief The audio player's data that wil be passed to the audio callback.
   */
  struct AudioState {
    AVCodecContext* av_codec_ctx = nullptr;

    // Audio flags
    AudioFlags flags = AudioFlags::NONE;

    // Output and input sample rate (44.1kHz by default)
    SampleRate sample_rate;

    double pts = 0;

    // Sample Correction
    double delta_accum = 0;
    double audio_diff_avg_count = 0;
  };

  static int add_dummy_samples(float* samples, int* samples_size,
                               int wanted_size, int max_size,
                               const int total_sample_bytes);

  /**
   * @brief Calculates the ideal number of samples for A/V synchronization.
   * @param audio_state Contains relevant information about the audio.
   * @param samples This is the audio buffer data
   * @param num_samples The number of samples avaiable inside the frame.
   * @param[in, out] initial_buffer_size The initial size of buffer before synchronization.
   * @return 0 <= for sucess, and a negative integer if there is an error.
   */
  static int synchronize_audio(AudioState* audio_state, float* samples,
                               int num_samples, int* initial_buffer_size);

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
  static int update_audio_stream(AudioState* userdata, Uint8* sdl_stream,
                                 int& len);

  /**
   * @brief Loads the audio waveform of the current input.
   * @return 0 <= for success, a negative integer for error.
   */
  virtual int load_audio_waveform() { return 0; };

#pragma region Helper Functions
  /**
   * @brief Converts planar audio data to an interleaved audio data.
   * @param audio_data See \ref AudioResamplingState for the structure definition.
   */
  static void resample_audio(AVFrame* latest_frame,
                             struct AudioResamplingState audio_data);

  /**
   * @brief Toggles the flag \ref AudioFlags::IS_MUTED
   */
  void toggle_audio();

  [[nodiscard]] inline bool isMuted() const {
    return m_audio_state->flags & AudioFlags::IS_MUTED;
  }

  [[nodiscard]] inline void pause_audio() {
    m_audio_state->flags ^= AudioFlags::IS_PAUSED;
    bool should_resume = m_audio_state->flags & AudioFlags::IS_PAUSED;

    SDL_PauseAudioDevice(m_device_info->device_id, should_resume);

    if (should_resume) {
      s_PauseEndTime = av_gettime() / static_cast<double>(AV_TIME_BASE);
      return;
    }

    s_PauseStartTime = av_gettime() / static_cast<double>(AV_TIME_BASE);
  }

  [[nodiscard]] inline auto& get_packet_queue() { return s_AudioPacketQueue; }

  [[nodiscard]] static inline const double& get_master_clock() {
    return s_MasterClock;
  }

  [[nodiscard]] static inline const double& get_audio_internal_clock() {
    return s_AudioInternalClock;
  }

  [[nodiscard]] int guess_correct_buffer_size(const StreamInfoPtr& stream_info);

  /**
   * @brief Checks if the denominator and numerator is a non-zero integer.
   * @param av_rational
   * @return true if the rational is valid. 
   */
  [[nodiscard]] static inline bool isValidRational(AVRational av_rational) {
    return av_rational.den > 0 && av_rational.num > 0;
  }
#pragma endregion Helper Functions

  static std::unique_ptr<AudioBufferInfo> s_AudioBufferInfo;
  static std::unique_ptr<PacketQueue> s_AudioPacketQueue;

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
  inline void free_resampler_ctx() { swr_free(&s_Resampler_Context); };

  void free_sdl_mixer();

 protected:
  static AVFrame* s_LatestFrame;
  static AVPacket* s_LatestPacket;

 protected:
  static double s_PauseStartTime;
  static double s_PauseEndTime;

  static double s_MasterClock;
  static double s_AudioInternalClock;

  std::unique_ptr<AudioDeviceInfo> m_device_info;
  std::shared_ptr<AudioState> m_audio_state;

 private:
  static SwrContext* s_Resampler_Context;

  [[nodiscard]] static inline int calculate_bounds(int size, bool is_max) {
    auto sample_correction = (SAMPLE_CORRECTION_PERCENT_MAX / 100.0);
    sample_correction *= -1 * !is_max;

    return static_cast<int>(size * (1.0 + sample_correction));
  };
};

#pragma endregion Audio Player
}  // namespace YAVE