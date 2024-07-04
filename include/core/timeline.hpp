#pragma once

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include <cmath>

#include "application.hpp"
#include "color.hpp"

namespace YAVE {
struct Segment;
struct SegmentStyle;
struct TrackStyle;
struct PlayheadProperties;

constexpr unsigned int NUMBER_OF_TRACKS = 5;

enum TimelineLayers {
  TRACK_BACKGROUND_LAYER,
  SEGMENT_LAYER,
  WAVEFORM_LAYER,
  CURSOR_LAYER,
  TRACK_LAYER,
  RULER_LAYER,
  TIMESTAMP_LAYER
};

using SegmentArray = std::vector<std::shared_ptr<Segment>>;
using TrackArray = std::vector<std::string>;

struct Segment {
  unsigned int track_position;
  std::string name;
  float start_time;
  float end_time;
};

struct SegmentStyle {
  ImU32 color;
  float scale;
  float border_radius;
  ImVec2 label_margin;
};

struct TrackStyle {
  ImVec2 size;
  ImVec2 label_margin;
  float border_radius;
};

struct PlayheadProperties {
  float thickness;
  float current_time;
  ImVec2 highlight_start;
  ImVec2 highlight_end;
};

class Timeline {
 public:
  Timeline();
  ~Timeline();

  void init();
  void render();
  void update(float delta_time);
  void update_timestamp();

  void add_segment(const Segment& segment);

  /**
   * @brief Sends a custom event (FF_SEEK_TO_TIMESTAMP_EVENT) to SDL.
   * @param[in] mouse_pos A pointer to the requested timestamp.
   * @return 1 for success, 0 for error. 
   */
  int request_seek_frame(float* mouse_pos);

  /**
   * @brief Handles the ruler events 
   * @param min The top-left vertex of the ruler.
   * @param max The bottom-right vertex of the ruler. 
   * @return 1 for success, 0 for error.
   */
  int handle_ruler_events(const ImVec2& ruler_min);

  /**
   * @brief Handles the playhead events 
   * @param min The top-left vertex of the playhead hitbox.
   * @param max The bottom-right vertex of the playhead hitbox. 
   * @return 1 for success, 0 for error.
   */
  int handle_playhead_events(const ImVec2& min, const ImVec2& max);

  std::shared_ptr<VideoPlayer> video_processor;

 private:
  void render_segments();
  void render_tracks();
  void render_timestamp();
  void render_ruler(const ImVec2& timestamp_max);
  void render_playhead();
  void render_waveform();

  ImVec2 m_window_size;
  ImVec2 m_child_window_size;

  SegmentStyle m_segment_style;
  TrackStyle m_track_style;
  PlayheadProperties m_playhead_prop;

  std::string m_timestamp;

  SegmentArray m_segment_array;
  TrackArray m_track_array;

  ImDrawList* m_draw_list;
};
}  // namespace YAVE