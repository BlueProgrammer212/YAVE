#include "core/backend/waveform_loader.hpp"

namespace YAVE {
SDL_mutex* WaveformLoader::mutex = nullptr;
SDL_cond* WaveformLoader::cond = nullptr;

std::vector<std::shared_ptr<Waveform>> WaveformLoader::s_LoadedWaveforms = {};

int WaveformLoader::enqueue_file(const char* filename) {
  if (m_file_queue->isFull()) {
    std::cout << "[Waveform] Exceeded queue size.\n";
    return -1;
  }

  m_file_queue->queue.push_back(filename);
  m_file_queue->nb_files = std::max(++m_file_queue->nb_files, MAX_FILE_NUMBER);
  SDL_CondSignal(cond);

  return 0;
}

// TODO: Initialize a resampler context for the waveform loader.
// TODO: Populate the waveform's audio data.
// TODO: Resample planar audio data using swr.
// TODO: Send the audio data to the timeline.
// TODO: Create a custom event named "FF_REFRESH_WAVEFORM" to update the plot.

int WaveformLoader::start(void* data) {
  auto userdata = static_cast<FileQueue*>(data);

  while (Application::is_running) {
    SDL_LockMutex(mutex);

    if (userdata->queue.empty()) {
      SDL_CondWait(cond, mutex);
    }

    const char* filename = userdata->queue.back();
    userdata->nb_files = std::max(--userdata->nb_files, 0);
    userdata->queue.pop_back();

    auto waveform = std::make_shared<Waveform>();
    int stream_index = -1;

    if (open_file(filename, waveform, &stream_index) != 0) {
      continue;
    };

    auto& av_frame = waveform->state.av_frame;
    auto& av_packet = waveform->state.av_packet;
    auto& av_codec_ctx = waveform->state.av_codec_context;

    av_frame = av_frame_alloc();
    av_packet = av_packet_alloc();

    while (av_read_frame(waveform->state.av_format_context, av_packet) >= 0) {
      if (av_packet->stream_index != stream_index) {
        av_packet_unref(av_packet);
        continue;
      }

      int response = avcodec_send_packet(av_codec_ctx, av_packet);
      response = handle_errors<AVPacket>(response, av_packet);

      if (response == -1) {
        continue;
      } else if (response == -2) {
        break;
      }

      response = avcodec_receive_frame(av_codec_ctx, av_frame);
      response = handle_errors<AVFrame>(response, av_frame);

      if (response == -1) {
        continue;
      } else if (response == -2) {
        break;
      }

      av_packet_unref(av_packet);
    }

    SDL_UnlockMutex(mutex);
  }

  return 0;
}

WaveformLoader::WaveformLoader()
    : m_file_queue(nullptr), m_waveform_loader_thread(nullptr) {
  mutex = SDL_CreateMutex();

  if (!mutex) {
    std::cout << "[Waveform] Failed to create a mutex.\n";
  }

  cond = SDL_CreateCond();

  if (!cond) {
    std::cout << "[Waveform] Failed to create a conditional variable.\n";
  }

  m_waveform_loader_thread =
      SDL_CreateThread(&WaveformLoader::start, "Waveform Loader Thread",
                       reinterpret_cast<void*>(m_file_queue));
}

void WaveformLoader::free_waveform(std::shared_ptr<Waveform> waveform) {
  avformat_close_input(&waveform->state.av_format_context);
  avformat_free_context(waveform->state.av_format_context);

  avcodec_close(waveform->state.av_codec_context);
  avcodec_free_context(&waveform->state.av_codec_context);
}

WaveformLoader::~WaveformLoader() {
  for (auto& waveform : s_LoadedWaveforms) {
    free_waveform(waveform);
  }
}

int WaveformLoader::open_file(const char* filename,
                              std::shared_ptr<Waveform>& waveform,
                              int* stream_index_ptr) {
  auto& [av_format_context, av_codec_context, av_codec_parameters, av_codec,
         av_frame, av_packet] = waveform->state;

  av_format_context = avformat_alloc_context();

  if (!av_format_context) {
    std::cout
        << "[Waveform] Failed to allocate memory for the format context.\n";
    return -1;
  }

  if (!avformat_open_input(&av_format_context, filename, nullptr, nullptr)) {
    std::cout << "[Waveform] Failed to open the input.\n";
    return -1;
  }

  int stream_index = -1;

  for (std::uint32_t i = 0; i < av_format_context->nb_streams; ++i) {
    auto& stream = av_format_context->streams[i];
    av_codec = avcodec_find_decoder(stream->codecpar->codec_id);
    av_codec_parameters = stream->codecpar;

    if (!av_codec || stream->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) {
      continue;
    }

    stream_index = i;
    waveform->duration = stream->duration;
    break;
  }

  if (stream_index < 0) {
    std::cout << "[Waveform] Failed to find a valid audio stream.\n";
    return -1;
  }

  *stream_index_ptr = stream_index;

  // Allocate a context for the decoder.
  av_codec_context = avcodec_alloc_context3(av_codec);

  if (!av_codec_context || !av_codec_parameters) {
    std::cout << "[Waveform] Failed to create the codec context.\n";
    return -1;
  }

  const int codec_param_to_ctx_result =
      avcodec_parameters_to_context(av_codec_context, av_codec_parameters);

  if (codec_param_to_ctx_result < 0) {
    std::cout << "[Waveform] Failed to set the parameters of the codec "
                 "context.\n";
    return -1;
  }

  if (avcodec_open2(av_codec_context, av_codec, nullptr) < 0) {
    std::cout << "[Waveform] Failed to open the audio stream.\n";
    return -1;
  }

  s_LoadedWaveforms.push_back(waveform);
}
}  // namespace YAVE