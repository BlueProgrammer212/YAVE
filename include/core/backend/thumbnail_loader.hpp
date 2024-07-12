#pragma once

#include "core/backend/loader.hpp"
#include "core/backend/video_player.hpp"

namespace YAVE
{
struct StreamInfo;

struct Thumbnail {
    std::uint8_t* framebuffer = nullptr;
    AVFormatContext* av_format_context = nullptr;
    StreamInfo stream_info;
    VideoDimension dimension;
};

enum HistogramComparisonResults { LAST_HISTOGRAM_BETTER = 0, NEW_HISTOGRAM_BETTER = 1 };

using Histogram = std::vector<int>;

class ThumbnailLoader : public Loader<Thumbnail>
{
public:
    ThumbnailLoader();
    ~ThumbnailLoader() override;

    int open_file(std::string filename, Thumbnail* userdata) override;
    int find_streams(AVFormatContext* av_format_context, Thumbnail* userdata) override;
    void allocate_frame_buffer(Thumbnail* data) override;

    int update_framebuffer(Thumbnail* data);
    int decode_frame(Thumbnail* data);
    int send_packet(Thumbnail* data, int retry_nb = 0);

    inline void free_histogram(std::vector<int>* histogram)
    {
        histogram->clear();
        histogram->shrink_to_fit();
    }

    int pick_best_thumbnail(Thumbnail* data, bool use_middle_frame = false);

    std::optional<Thumbnail*> load_video_thumbnail(const std::string& path);
    int peek_video_frame_by_timestamp(const int64_t seconds, Thumbnail* data);

    int compare_previous_histogram(const Histogram& new_histogram, const Histogram& old_histogram);

    std::unique_ptr<std::vector<int>> extract_histogram(AVFrame* frame, int num_bins = 256);

private:
    AVPacket* m_av_packet;
    AVFrame* m_av_frame;
    int64_t m_duration;
};
} // namespace YAVE