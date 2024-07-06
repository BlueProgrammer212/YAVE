#pragma once

#include "core/backend/video_player.hpp"

namespace YAVE {
struct StreamInfo;

struct Thumbnail {
  std::uint8_t* framebuffer = nullptr;
  AVFormatContext* av_format_context = nullptr;
  StreamInfo stream_info;
  VideoDimension dimension;
};

enum HistogramComparisonResults {
  LAST_HISTOGRAM_BETTER = 0,
  NEW_HISTOGRAM_BETTER  = 1
};

class ThumbnailLoader {
 public:
  ThumbnailLoader();
  ~ThumbnailLoader();

  void allocate_frame_buffer(Thumbnail* data);
  int update_framebuffer(Thumbnail* data);
  int decode_frame(Thumbnail* data);
  int send_packet(Thumbnail* data, int retry_nb = 0);

  int pick_best_thumbnail(Thumbnail* data, bool use_middle_frame = false);

  std::optional<Thumbnail*> load_video_thumbnail(const std::string& path);
  int peek_video_frame_by_timestamp(const int64_t seconds, Thumbnail* data);

  int compare_previous_histogram(const std::vector<int>& new_histogram,
                                 const std::vector<int>& old_histogram);

  std::vector<int> extract_histogram(AVFrame* frame, int num_bins = 256);

 private:
  AVPacket* m_av_packet;
  AVFrame* m_av_frame;
  int64_t m_duration;
};
}  // namespace YAVE