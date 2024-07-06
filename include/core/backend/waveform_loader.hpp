#pragma once

#include <deque>
#include <iostream>

#include "core/application.hpp"

namespace YAVE {
constexpr int MAX_FILE_NUMBER = 3;

struct WaveformState {
  AVFormatContext* av_format_context = nullptr;
  AVCodecContext* av_codec_context = nullptr;
  AVCodecParameters* av_codec_parameters = nullptr;
  AVCodec* av_codec = nullptr;
  AVFrame* av_frame = nullptr;
  AVPacket* av_packet = nullptr;
};

struct Waveform {
  WaveformState state;
  int sample_rate;
  int duration;
  std::vector<int> audio_data;
};

class WaveformLoader {
 public:
  WaveformLoader();
  ~WaveformLoader();

  void free_waveform(std::shared_ptr<Waveform> waveform);

  int enqueue_file(const char* filename);

  static int open_file(const char* filename,
                       std::shared_ptr<Waveform>& waveform_out,
                       int* stream_index_ptr);

  static int start(void* data);

  static SDL_mutex* mutex;
  static SDL_cond* cond;

 private:
  struct FileQueue {
    std::deque<const char*> queue;
    int nb_files;

    [[nodiscard]] bool isFull() { return nb_files >= MAX_FILE_NUMBER; }
  };

  static std::vector<std::shared_ptr<Waveform>> s_LoadedWaveforms;
  FileQueue* m_file_queue;

  template <typename T>
  static inline int handle_errors(int response,
                                  T* av_packet_or_frame) noexcept {
    if (response >= 0) {
      return 0;
    }

    int status_code = 0;

    if constexpr (std::is_same_v<T, AVPacket>) {
      av_packet_unref(av_packet_or_frame);
    }

    // -1 for continue; and -2 to stop decoding the frames.
    return response == AVERROR(EAGAIN) ? -1 : -2;
  }

  SDL_Thread* m_waveform_loader_thread;
};
}  // namespace YAVE