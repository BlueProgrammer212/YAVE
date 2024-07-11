#include "core/debugger.hpp"

namespace YAVE
{
Debugger::Debugger()
    : video_state(nullptr)
{
}

void Debugger::init() {}

void Debugger::update() {}

void Debugger::render()
{
    ImGui::Begin("Stats for Nerds");

    // Clock Network Information
    const std::string video_pts = "Current Video PTS: " + std::to_string(video_state->pts) + " sec";

    const std::string video_internal_clock =
        "Video Internal Clock: " + std::to_string(AudioPlayer::get_video_internal_clock()) + " sec";

    const std::string audio_internal_clock =
        "Audio Internal Clock: " + std::to_string(AudioPlayer::get_audio_internal_clock()) + " sec";

    // Calculate the average clock difference.
    static int count_sample = 0;
    static double diff_sum = 0;

    diff_sum += AudioPlayer::get_video_internal_clock() - AudioPlayer::get_audio_internal_clock();

    count_sample++;

    const std::string clock_diff =
        "Average Clock Difference: " + std::to_string(diff_sum / count_sample) + " sec";

    const int kilobytes_per_second = calculate_kilobytes_per_second();

    const std::string kilobytes_per_second_str =
        "Kilobytes Processed per Second: " + std::to_string(kilobytes_per_second) + " kb";

    // Video Information
    const std::string width_str = "Width: " + std::to_string(video_state->dimensions.x) + "px";
    const std::string height_str = "Height: " + std::to_string(video_state->dimensions.y) + "px";

    // Audio Information
    const float sample_rate =
        static_cast<float>(AudioPlayer::s_AudioBufferInfo->sample_rate) / 1000.0f;

    const std::string sample_rate_str = "Sample Rate: " + std::to_string(sample_rate) + " kHz";

    ImGui::Text("Clock Network (For A/V Synchronization)");

    ImGui::Text(video_pts.c_str());
    ImGui::Text(video_internal_clock.c_str());
    ImGui::Text(audio_internal_clock.c_str());
    ImGui::Text(clock_diff.c_str());

    ImGui::Dummy(ImVec2(0, 10));

    ImGui::Text("Video Information");

    ImGui::Text(width_str.c_str());
    ImGui::Text(height_str.c_str());

    ImGui::Dummy(ImVec2(0, 10));

    ImGui::Text("Audio Information");

    ImGui::Text(sample_rate_str.c_str());
    ImGui::Text(kilobytes_per_second_str.c_str());

    ImGui::End();
}
} // namespace YAVE