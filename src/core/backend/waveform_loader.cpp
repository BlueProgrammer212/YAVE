#include "core/backend/waveform_loader.hpp"

namespace YAVE {
SDL_mutex* WaveformLoader::mutex = nullptr;
SDL_cond* WaveformLoader::cond = nullptr;
SwrContext* WaveformLoader::s_ResamplerContext = nullptr;

WaveformCache WaveformLoader::s_LoadedWaveforms = {};

int WaveformLoader::send_waveform_to_main_thread(Waveform* waveform,
                                                 int segment_index) {
  SDL_Event event;
  event.type = CustomVideoEvents::FF_REFRESH_WAVEFORM;
  event.user.data1 = reinterpret_cast<void*>(waveform);
  event.user.data2 = new int(segment_index);
  return SDL_PushEvent(&event) == 1;
}

int WaveformLoader::request_audio_waveform(const char* filename) {
  if (m_file_queue->isFull()) {
    std::cout << "[Waveform] Exceeded queue size.\n";
    return -1;
  }

  m_file_queue->queue.push_back(std::string(filename));
  m_file_queue->nb_files = std::min(++m_file_queue->nb_files, MAX_FILE_NUMBER);
  SDL_CondSignal(cond);

  return 0;
}

int WaveformLoader::init_swr_resampler_context(Waveform* waveform) {
  const unsigned int sample_rate = waveform->state->av_frame->sample_rate;
  static bool is_resampler_context_initalized = false;

  if (is_resampler_context_initalized) {
    return 0;
  }

  s_ResamplerContext = swr_alloc();

  if (!s_ResamplerContext) {
    swr_free(&s_ResamplerContext);
    std::cout
        << "[Waveform] Failed to allocate memory for the resampler context.\n";
    return -1;
  }

  const int64_t channel_layout =
      av_get_default_channel_layout(waveform->state->av_frame->channels);

  swr_alloc_set_opts(s_ResamplerContext, channel_layout, AV_SAMPLE_FMT_FLT,
                     sample_rate, channel_layout, AV_SAMPLE_FMT_FLTP,
                     sample_rate, 0, nullptr);

  int init_result = swr_init(s_ResamplerContext);

  if (init_result < 0) {
    return -1;
  }

  is_resampler_context_initalized = true;

  return 0;
}

int WaveformLoader::populate_audio_data(Waveform* waveform) {
  constexpr int DOWNSAMPLE_FACTOR = 512;

  if (init_swr_resampler_context(waveform) < 0) {
    return -1;
  }

  const auto nb_channels = waveform->state->av_frame->channels;
  const auto nb_samples = waveform->state->av_frame->nb_samples;
  const auto sample_format = waveform->state->av_codec_context->sample_fmt;
  bool is_audio_planar = av_sample_fmt_is_planar(sample_format);

  const auto out_samples_nb =
      swr_get_out_samples(s_ResamplerContext, nb_samples);

  // If the audio data is not planar, there's no need to resample.
  if (!is_audio_planar) {
    for (int i = 0; i < nb_samples; i += DOWNSAMPLE_FACTOR) {
      waveform->audio_data.push_back(*waveform->state->av_frame->data[0]);
    }

    return 0;
  }

  std::vector<float> resampled_audio_buffer(nb_channels * nb_samples);
  std::array<float*, 1> out_data = {resampled_audio_buffer.data()};

  auto extended_data_ptr =
      const_cast<const uint8_t**>(waveform->state->av_frame->extended_data);

  int ret = swr_convert(s_ResamplerContext,
                        reinterpret_cast<uint8_t**>(out_data.data()),
                        out_samples_nb, extended_data_ptr, nb_samples);

  if (ret < 0) {
    std::array<char, AV_ERROR_MAX_STRING_SIZE> err_buf = {0};
    av_strerror(ret, err_buf.data(), err_buf.size());

    std::cerr << "Failed to convert the audio data from planar to interleaved: "
              << err_buf.data() << "\n";

    return -1;
  }

  for (int i = 0; i < resampled_audio_buffer.size(); i += DOWNSAMPLE_FACTOR) {
    if (i >= resampled_audio_buffer.size()) {
      break;
    }

    waveform->audio_data.push_back(resampled_audio_buffer[i]);
  }

  return 0;
}

int WaveformLoader::start(void* data) {
  auto userdata = static_cast<FileQueue*>(data);

  while (Application::is_running) {
    SDL_LockMutex(mutex);

    if (userdata->queue.empty()) {
      SDL_CondWait(cond, mutex);
    }

    if (!Application::is_running) {
      break;
    }

    std::string filename = userdata->queue.back();
    userdata->nb_files = std::max(--userdata->nb_files, 0);
    userdata->queue.pop_back();

    static int segment_index = -1;

    auto waveform = new Waveform();
    waveform->state = new WaveformState();
    int stream_index = -1;

    if (open_file(filename, waveform, &stream_index) != 0) {
      continue;
    };

    auto& av_frame = waveform->state->av_frame;
    auto& av_packet = waveform->state->av_packet;
    auto& av_codec_ctx = waveform->state->av_codec_context;

    av_frame = av_frame_alloc();
    av_packet = av_packet_alloc();

    while (av_read_frame(waveform->state->av_format_context, av_packet) >= 0) {
      if (av_packet->stream_index != stream_index) {
        av_packet_unref(av_packet);
        continue;
      }

      int response = avcodec_send_packet(av_codec_ctx, av_packet);

      if (response == AVERROR(EAGAIN)) {
        av_packet_unref(av_packet);
        continue;
      } else if (response < 0) {
        av_packet_unref(av_packet);
        break;
      }

      response = avcodec_receive_frame(av_codec_ctx, av_frame);

      if (response == AVERROR(EAGAIN)) {
        av_packet_unref(av_packet);
        continue;
      } else if (response < 0) {
        av_packet_unref(av_packet);
        break;
      }

      populate_audio_data(waveform);
      av_packet_unref(av_packet);
    }

    // After populating the data, send the data to the main thread.
    send_waveform_to_main_thread(waveform, ++segment_index);

    SDL_UnlockMutex(mutex);
  }

  return 0;
}

WaveformLoader::WaveformLoader() : m_waveform_loader_thread(nullptr) {
  mutex = SDL_CreateMutex();

  if (!mutex) {
    std::cout << "[Waveform] Failed to create a mutex.\n";
  }

  cond = SDL_CreateCond();

  if (!cond) {
    std::cout << "[Waveform] Failed to create a conditional variable.\n";
  }

  m_file_queue = new FileQueue();
  m_file_queue->queue = {};
  m_file_queue->nb_files = 0;

  m_waveform_loader_thread =
      SDL_CreateThread(&WaveformLoader::start, "Waveform Loader Thread",
                       reinterpret_cast<void*>(m_file_queue));
}

void WaveformLoader::free_waveform(Waveform* waveform) {
  avformat_close_input(&waveform->state->av_format_context);
  avformat_free_context(waveform->state->av_format_context);

  avcodec_close(waveform->state->av_codec_context);
  avcodec_free_context(&waveform->state->av_codec_context);

  av_frame_free(&waveform->state->av_frame);
  av_packet_free(&waveform->state->av_packet);

  delete waveform->state;
  delete waveform;
}

WaveformLoader::~WaveformLoader() {
}

int WaveformLoader::open_file(std::string filename, Waveform* waveform,
                              int* stream_index_ptr) {
  if (s_LoadedWaveforms.find(filename) != s_LoadedWaveforms.end()) {
    std::cout << "[Waveform] Loading waveform from the cache.\n";
    return 0;
  }

  auto& [av_format_context, av_codec_context, av_codec_parameters, av_codec,
         av_frame, av_packet] = *waveform->state;

  av_format_context = avformat_alloc_context();

  if (!av_format_context) {
    std::cout
        << "[Waveform] Failed to allocate memory for the format context.\n";
    return -1;
  }

  int open_input_ret = avformat_open_input(&av_format_context, filename.c_str(),
                                           nullptr, nullptr);

  if (open_input_ret != 0) {
    std::cout << "[Waveform] Failed to open the input: "
              << av_error_to_string(open_input_ret) << "\n";
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

  s_LoadedWaveforms.insert({filename, std::move(waveform)});
  return 0;
}
}  // namespace YAVE