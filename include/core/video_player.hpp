#pragma once

#include <optional>

#define GLEW_STATIC
#include <GL/glew.h>

#include <SDL_opengl.h>

#include "core/application.hpp"
#include "core/audio_player.hpp"
#include "core/backend/packet_queue.hpp"

namespace YAVE {
enum CustomVideoEvents : std::uint32_t {
  FF_REFRESH_EVENT = SDL_USEREVENT,
  FF_LOAD_NEW_VIDEO_EVENT,
  FF_TOGGLE_PAUSE_EVENT,
  FF_MUTE_AUDIO_EVENT,
  FF_SEEK_TO_TIMESTAMP_EVENT,
  FF_REFRESH_THUMBNAIL
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
  IS_HWACCEL_INITIALIZED     = 1 << 5,
  IS_INPUT_CHANGED           = 1 << 6
};
// clang-format on

using VideoFlagType = std::underlying_type<VideoFlags>::type;

[[nodiscard]] inline VideoFlags operator|(VideoFlags lhs, VideoFlags rhs) {
  return static_cast<VideoFlags>(static_cast<VideoFlagType>(lhs) |
                                 static_cast<VideoFlagType>(rhs));
}

inline VideoFlags& operator|=(VideoFlags& lhs, VideoFlags rhs) {
  lhs = lhs | rhs;
  return lhs;
}

[[nodiscard]] inline bool operator&(VideoFlags lhs, VideoFlags rhs) {
  return static_cast<bool>(static_cast<VideoFlagType>(lhs) &
                           static_cast<VideoFlagType>(rhs));
}

inline VideoFlags operator&=(VideoFlags& lhs, VideoFlags rhs) {
  lhs = static_cast<VideoFlags>(static_cast<VideoFlagType>(lhs) &
                                static_cast<VideoFlagType>(rhs));
  return lhs;
}

[[nodiscard]] inline VideoFlags operator~(VideoFlags lhs) {
  return static_cast<VideoFlags>(~static_cast<VideoFlagType>(lhs));
}

[[nodiscard]] inline VideoFlags operator^(VideoFlags lhs, VideoFlags rhs) {
  return static_cast<VideoFlags>(static_cast<VideoFlagType>(lhs) ^
                                 static_cast<VideoFlagType>(rhs));
}

inline VideoFlags operator^=(VideoFlags& lhs, VideoFlags rhs) {
  lhs = lhs ^ rhs;
  return lhs;
}

#pragma endregion Video Flags

struct VideoDimension {
  int x = 640;
  int y = 360;
};

/**
 * @struct VideoState
 * @brief Contains the SWS scaler context and the AVFormat context.
 */
struct VideoState {
  SwsContext* sws_scaler_ctx = nullptr;
  AVFormatContext* av_format_ctx = nullptr;
  std::uint8_t* buffer = nullptr;

  double pts = 0.0;
  double last_pts = 0.0;
  double last_delay = 40e-3;

  VideoFlags flags = VideoFlags::NONE;
  VideoDimension dimensions;

  double frame_timer = 0.0;
  bool is_first_frame = false;
};

class VideoPlayer : public AudioPlayer {
 public:
  VideoPlayer(SampleRate t_sample_rate);
  ~VideoPlayer() override;

  /**
   * @brief Opens a video and initializes the format context and the codecs.
   * @param path Specifies the path of the video file.
   * @return 0 <= for success, a negative integer for error.
   */
  int open_video(const char* path);

  /**
   * @brief Loads the audio waveform of the current input.
   * @return 0 <= for success, a negative integer for error.
   */
  int load_audio_waveform() override;

  /**
   * @brief Plays the active input file.
   * @param timebase
   * @return 0 for success, a negative integer for error. 
   */
  int play_video(AVRational* timebase);

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
   * @brief Recurses until we get a valid video frame.
   * @param packet
   * @param frame
   * @param retry_count The number of trials to get a valid video frame. (0 by default)
   * @return A pointer to an AVFrame if it's sucessful, and std::nullopt for failure. 
   */
  [[nodiscard]] std::optional<AVFrame*> get_first_audio_frame(
      AVPacket* dummy_packet, AVFrame* dummy_frame, int retry_count = 0);

  /**
   * @brief Sends the video packets to the decoder and then recieves the frame from the codec.
   * @return 0 <= for success, a negative integer for error. 
   */
  static int decode_video_frame(VideoState* video_state,
                                AVPacket* video_packet);

  /**
   * @brief Synchronize the video with the audio using PTS and DTS.
   * @param video_state
   */
  static void synchronize_video(VideoState* video_state);

  /**
   * @brief Finds a video frame.
   * 
   * @param data The video state
   */
  static int enqueue_frames(void* data);

  /**
   * @brief Jump to specific timestamp.
   * @param seconds The timestamp in seconds.
   * @return 0 <= for success, a negative integer for error.
   */
  int seek_frame(float seconds);

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
   * @return std::int64_t 
   */
  [[nodiscard]] inline std::int64_t getDuration() const { return m_duration; }

  /**
   * @brief Access the framebuffer of the current video.
   * @return std::uint8_t*& 
   */
  [[nodiscard]] inline std::uint8_t*& get_framebuffer() noexcept {
    return m_video_state->buffer;
  }

  /**
   * @brief Get the presentation timestamp of the latest video frame.
   * @return double 
   */
  [[nodiscard]] inline double getPTS() const {
    return static_cast<double>(m_video_state->pts);
  }

  /**
   * @brief Access the video flags.
   * @return VideoFlags& 
   */
  [[nodiscard]] inline VideoFlags& getFlags() noexcept {
    return m_video_state->flags;
  }

  /**
   * @brief Get the video state object.
   * @return VideoState& 
   */
  [[nodiscard]] inline VideoState* get_videostate() noexcept {
    return m_video_state;
  }

  [[nodiscard]] inline std::string getFilename() noexcept {
    return opened_file;
  }

  [[nodiscard]] static double calculateReferenceClock();

  [[nodiscard]] static double calculateActualDelay(VideoState* video_state,
                                                   double& frame_timer);

  void pause_video();

  /**
   * @brief Add a stream to the stream list.
   * @param[in] stream_ptr
   * @param name 
   */
  void add_stream(StreamInfoPtr stream_ptr, std::string name);

  /**
   * @brief Waits for the threads to finish.
   */
  void stop_threads();

  /**
   * @brief Resets the video state. This is particularly useful to load other
   *        video files.
   */
  void reset_video_state();

#pragma endregion Helper Functions

  static std::unique_ptr<PacketQueue> s_VideoPacketQueue;

 protected:
  VideoState* m_video_state;

  std::int64_t m_duration;

  SDL_Thread* m_decoding_tid;
  SDL_Thread* m_video_tid;

 private:
  static int init_sws_scaler_ctx(VideoState* video_state);

  int init_hwaccel_decoder(AVCodecContext* ctx, const enum AVHWDeviceType type);

  static AVPixelFormat get_hw_format(AVCodecContext* ctx,
                                     const AVPixelFormat* pix_fmts);

  static void update_pts(VideoState* video_state, AVPacket* video_packet);

  void free_ffmpeg();
  int find_streams();

  int create_context_for_stream(StreamInfoPtr& stream_info);

  std::string opened_file{""};
  bool m_is_input_open{false};

  AVBufferRef* m_hw_device_ctx;
};
#pragma endregion Video Player

}  // namespace YAVE