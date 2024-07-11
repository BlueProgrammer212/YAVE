#pragma once

#include "application.hpp"
#include <string>

namespace YAVE
{
class Debugger
{
public:
    Debugger();
    ~Debugger(){};

    void init();
    void update();
    void render();

    [[nodiscard]] inline int calculate_kilobytes_per_second() const
    {
        const int channels_nb = AudioPlayer::s_AudioBufferInfo->channel_nb;
        const int sample_rate = AudioPlayer::s_AudioBufferInfo->sample_rate;
        const int sample_bytes = channels_nb * sizeof(float);

        return (sample_rate * sample_bytes) / 1000;
    };

    std::shared_ptr<VideoState> video_state;
    double time_base;

private:
};
} // namespace YAVE