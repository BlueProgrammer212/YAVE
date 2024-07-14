#pragma once

#include <deque>
#include <iostream>

#include "core/application.hpp"
#include "core/backend/video_loader.hpp"

namespace YAVE
{
struct WaveformState;
struct Waveform;
struct StreamInfo;

constexpr int MAX_FILE_NUMBER = 3;

using WaveformCache = std::unordered_map<std::string, Waveform*>;

struct WaveformState {
    AVFormatContext* av_format_context = nullptr;
    std::shared_ptr<StreamInfo> stream_info = nullptr;
    AVFrame* av_frame = nullptr;
    AVPacket* av_packet = nullptr;
};

struct Waveform {
    WaveformState* state = nullptr;
    int sample_rate = 44100;
    std::int64_t duration = 0;
    int segment_index = -1;
    std::vector<float> audio_data = {};
};

class WaveformLoader
{
public:
    WaveformLoader();
    ~WaveformLoader();

    void free_waveform(Waveform* waveform);
    int request_audio_waveform(const char* filename);

    static void normalize_audio_data(
        const std::vector<float>& audio_data, std::vector<float>* out, const float factor = 1.0f);

    static SDL_mutex* mutex;
    static SDL_cond* cond;

private:
    static int init_swr_resampler_context(Waveform* waveform);

    static int open_file(std::string filename, Waveform* waveform_out, int* stream_index_ptr);

    static int start(void* data);
    static int get_audio_frames_from_packet(Waveform* waveform, int stream_index);
    static int populate_audio_data(Waveform* waveform);
    static int send_waveform_to_main_thread(Waveform* waveform, int segment_index);

    struct FileQueue {
        std::deque<std::string> queue;
        int nb_files;

        [[nodiscard]] bool isFull()
        {
            return nb_files >= MAX_FILE_NUMBER;
        }
    };

    static WaveformCache s_LoadedWaveforms;
    static SwrContext* s_ResamplerContext;

    static std::unique_ptr<VideoLoader> s_VideoLoader;
    FileQueue* m_file_queue;
    SDL_Thread* m_waveform_loader_thread;
};
} // namespace YAVE