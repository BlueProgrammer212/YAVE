#include "core/timeline.hpp"
#include "core/backend/audio_player.hpp"

namespace YAVE {
#pragma region Init Functions

Timeline::Timeline() : m_draw_list(nullptr), m_timestamp("00:00:00") {
  // clang-format off
  m_segment_style = SegmentStyle{
    Color::VIDEO_SEGMENT_COLOR, 1.0f, 7.5f,
    ImVec2(5.0f, 5.0f)
  };

  m_track_style = TrackStyle{
    ImVec2(150.0f, 75.0f), 
    ImVec2(5.0f, 5.0f), 0.0f
  };

  m_playhead_prop = PlayheadProperties{
    1.0f, 0.0f, ImVec2(0.0f, 0.0f), ImVec2(0.0f, 0.0f)
  };
  // clang-format on
}

Timeline::~Timeline() {}

void Timeline::init() {
  // Add test segments & tracks.
  for (unsigned int i = 1; i < NUMBER_OF_TRACKS + 1; ++i) {
    std::string track_label = "Track " + std::to_string(i);
    m_track_array.push_back(track_label);
  }
}

#pragma endregion Init Function

#pragma region Update Function

void Timeline::update_timestamp() {
  const double master_clock = AudioPlayer::get_master_clock();

  const int total_seconds = static_cast<int>(std::floor(master_clock));
  const int milliseconds =
      static_cast<int>((master_clock - total_seconds) * 1000);

  const int hours = total_seconds / 3600;
  const int minutes = (total_seconds % 3600) / 60;
  const int seconds = total_seconds % 60;

  // Format each component with leading zeros if necessary
  std::ostringstream result;
  result << std::setfill('0') << std::setw(2) << hours << ":";
  result << std::setfill('0') << std::setw(2) << minutes << ":";
  result << std::setfill('0') << std::setw(2) << seconds << ":";
  result << std::setfill('0') << std::setw(3) << milliseconds;

  m_timestamp = result.str();
}

void Timeline::update(float delta_time) {
  static const float track_proportion = 0.2f;
  m_track_style.size.y = track_proportion * m_window_size.y;

  m_playhead_prop.current_time =
      static_cast<float>(AudioPlayer::get_master_clock()) *
      m_segment_style.scale;

  update_timestamp();
}

#pragma endregion Update Function

void Timeline::render() {
  ImGui::Begin("Timeline", nullptr, ImGuiWindowFlags_NoScrollbar);

  m_draw_list = ImGui::GetWindowDrawList();

  auto& video_flags = video_processor->getFlags();

  if (ImGui::Button(~video_flags & VideoFlags::IS_PAUSED ? "Pause"
                                                         : "Resume")) {
    SDL_Event pause_event;
    pause_event.type = CustomVideoEvents::FF_TOGGLE_PAUSE_EVENT;
    SDL_PushEvent(&pause_event);
  }

  ImGui::SameLine();

  if (ImGui::Button(video_processor->is_muted() ? "Unmute" : "Mute")) {
    SDL_Event mute_event;
    mute_event.type = CustomVideoEvents::FF_MUTE_AUDIO_EVENT;
    SDL_PushEvent(&mute_event);
  };

  ImGui::SameLine();

  ImGui::Text("Magnify: ");

  ImGui::SameLine();

  ImGui::SetNextItemWidth(200.0f);

  ImGui::SliderFloat("##magnify_label", &m_segment_style.scale, 0.9f, 10.0f,
                     "%.3f", ImGuiSliderFlags_AlwaysClamp);

  m_window_size = ImGui::GetWindowSize();

  // clang-format off
  static const float child_window_proportion = 0.755f;
  const auto& child_window_size = ImVec2(0.0f, m_window_size.y * child_window_proportion);

  ImGui::BeginChild("###scrolling", child_window_size, false,
                    ImGuiWindowFlags_AlwaysVerticalScrollbar |
                    ImGuiWindowFlags_AlwaysHorizontalScrollbar);
  // clang-format on

  m_child_window_size = ImGui::GetWindowSize();

  m_draw_list = ImGui::GetWindowDrawList();
  m_draw_list->ChannelsSplit(7);

  render_timestamp();
  render_tracks();
  render_segments();
  render_playhead();

  m_draw_list->ChannelsMerge();

  ImGui::Dummy(ImVec2(m_window_size.x * m_segment_style.scale,
                      m_track_style.size.y * m_track_array.size()));

  ImGui::EndChild();

  ImGui::End();
}

#pragma region Segments

int Timeline::update_segment_waveform(const std::vector<float>& audio_data,
                                      int segment_index) {
  if (segment_index < 0 && segment_index > m_segment_array.size()) {
    return -1;
  }

  auto destination_segment = m_segment_array[segment_index];

  // Copy the audio data from the waveform loader to the segment.
  destination_segment->waveform_data = audio_data;

  return 0;
}

void Timeline::add_segment(const Segment& segment) {
  auto new_segment = std::make_unique<Segment>();
  new_segment->track_position = segment.track_position;
  new_segment->name = segment.name;
  new_segment->start_time = segment.start_time;
  new_segment->end_time = segment.end_time;
  m_segment_array.push_back(std::move(new_segment));
}

void Timeline::render_segments() {
  static const auto outline_color = IM_COL32(1, 43, 81, 255);
  ImVec2 initial_cursor_pos = ImGui::GetCursorScreenPos();

  m_draw_list->ChannelsSetCurrent(TimelineLayers::SEGMENT_LAYER);

  for (auto& segment : m_segment_array) {
    ImVec2 min = ImGui::GetCursorScreenPos();

    const float delta_time = segment->end_time - segment->start_time;
    const float segment_width = delta_time * m_segment_style.scale;

    min.x += m_segment_style.scale * segment->start_time + m_track_style.size.x;
    min.y += segment->track_position * m_track_style.size.y;

    ImVec2 max = min;

    max.x += segment_width;
    max.y += m_track_style.size.y;

    // Render a separator between the clip and the audio waveform.
    const auto& start_point =
        ImVec2(min.x, min.y + (m_track_style.size.y / 2.0f));
    const auto& end_point = ImVec2(max.x, start_point.y);

    m_draw_list->AddRectFilled(min, max, m_segment_style.color,
                               m_segment_style.border_radius);

    // Render the segment label.
    min += m_segment_style.label_margin;
    m_draw_list->AddText(min, IM_COL32_WHITE, segment->name.c_str());

    render_waveform(start_point, max, segment->waveform_data);
    ImGui::SetCursorScreenPos(initial_cursor_pos);

    m_draw_list->AddLine(start_point, end_point, outline_color, 1.25f);
  }
}

#pragma endregion Segments

#pragma region Tracks

void Timeline::render_tracks() {
  float scroll_x = ImGui::GetScrollX();

  for (std::uint32_t i = 0; i < m_track_array.size(); ++i) {
    m_draw_list->ChannelsSetCurrent(TimelineLayers::TRACK_LAYER);

    ImVec2 min = ImGui::GetCursorScreenPos();
    min.x += scroll_x;
    min.y += i * m_track_style.size.y;

    // Add a little bit of y-offset between tracks.
    static const float bottom_margin = 2.0f;
    min.y += i > 0 ? bottom_margin * i : 0.0f;

    const ImVec2 max = min + m_track_style.size;

    m_draw_list->AddRectFilled(min, max, Color::TRACK_COLOR,
                               m_track_style.border_radius);

    m_draw_list->ChannelsSetCurrent(TimelineLayers::TRACK_BACKGROUND_LAYER);

    // Render the track background.
    const ImVec2 background_min =
        ImVec2(max.x, min.y - (i > 0) * bottom_margin * i);

    const auto background_max =
        min + ImVec2(m_window_size.x + scroll_x, m_track_style.size.y);

    ImU32 bg_color = i % 2 == 0 ? Color::SECONDARY : Color::PRIMARY;

    m_draw_list->AddRectFilled(background_min, background_max, bg_color);

    m_draw_list->ChannelsSetCurrent(TimelineLayers::TRACK_LAYER);

    // Render the track label.
    min += m_track_style.label_margin;
    m_draw_list->AddText(min, IM_COL32_WHITE, m_track_array[i].c_str());
  }
}

#pragma endregion Tracks

#pragma region Timestamp

void Timeline::render_timestamp() {
  m_draw_list->ChannelsSetCurrent(TimelineLayers::TIMESTAMP_LAYER);

  float scroll_x = ImGui::GetScrollX();

  //Render the timestamp.
  ImVec2 timestamp_min = ImGui::GetCursorScreenPos();
  timestamp_min.x += scroll_x;

  const ImVec2 timestamp_max =
      timestamp_min + ImVec2(m_track_style.size.x, 40.0f);

  m_draw_list->AddRectFilled(timestamp_min, timestamp_max, Color::SECONDARY,
                             7.5f);

  m_draw_list->AddText(timestamp_min + m_track_style.label_margin, 0xFFFFFFFF,
                       m_timestamp.c_str());

  render_ruler(timestamp_max);
}

#pragma endregion Timestamp

#pragma region Waveform
void Timeline::render_waveform(const ImVec2& min, const ImVec2& max,
                               const std::vector<float>& audio_data) {
  m_draw_list->ChannelsSetCurrent(TimelineLayers::WAVEFORM_LAYER);

  if (video_processor->s_StreamList.find("Audio") ==
      video_processor->s_StreamList.end()) {
    return;
  }

  static auto audio_stream = video_processor->s_StreamList.at("Audio");

  if (AudioPlayer::s_AudioBufferInfo == nullptr ||
      AudioPlayer::s_AudioBufferInfo->audio_data.empty()) {
    return;
  }

  constexpr static std::array<ImPlotStyleVar_, 4> style_var_set = {
      ImPlotStyleVar_PlotPadding, ImPlotStyleVar_LabelPadding,
      ImPlotStyleVar_LegendPadding, ImPlotStyleVar_FitPadding};

  for (const auto& var : style_var_set) {
    ImPlot::PushStyleVar(var, ImVec2(0, 0));
  }

  ImPlot::PushStyleColor(ImPlotCol_FrameBg, Color::TRANSPARENT);
  ImPlot::PushStyleColor(ImPlotCol_PlotBg, Color::TRANSPARENT);

  ImVec4 waveform_vid_color =
      ImGui::ColorConvertU32ToFloat4(Color::WAVEFORM_VID_COLOR);

  ImPlot::PushStyleColor(ImPlotCol_Line, waveform_vid_color);

  ImGui::SetCursorScreenPos(min);
  const ImVec2& plot_size = max - min;

  if (ImPlot::BeginPlot("##WaveformPlot", plot_size, ImPlotFlags_CanvasOnly)) {
    ImPlot::SetupAxis(ImAxis_X1, NULL, WAVEFORM_AXIS_FLAGS);
    ImPlot::SetupAxis(ImAxis_Y1, NULL, WAVEFORM_AXIS_FLAGS);

    ImPlot::SetupAxisLimits(ImAxis_X1, 0.0f,
                            static_cast<double>(audio_data.size()),
                            ImPlotCond_Always);
    ImPlot::SetupAxisLimits(ImAxis_Y1, -1.0f, 1.0f, ImPlotCond_Always);

    ImPlot::PlotLine("Waveform", audio_data.data(),
                     static_cast<int>(audio_data.size()));

    ImPlot::EndPlot();
  }

  ImPlot::PopStyleVar(static_cast<int>(style_var_set.size()));
  ImPlot::PopStyleColor(3);
}

#pragma endregion Waveform

#pragma region Timeline Ruler

int Timeline::request_seek_frame(std::unique_ptr<float> mouse_pos) {
  SDL_Event seek_event;
  seek_event.type = CustomVideoEvents::FF_SEEK_TO_TIMESTAMP_EVENT;
  seek_event.user.data1 = reinterpret_cast<void*>(mouse_pos.release());

  return SDL_PushEvent(&seek_event) == 1;
}

int Timeline::handle_ruler_events(const ImVec2& ruler_min) {
  bool is_clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
  bool is_dragging = ImGui::IsMouseDragging(ImGuiMouseButton_Left);

  if (!is_clicked && !is_dragging) {
    return 1;
  }

  const float mouse_delta =
      (ImGui::GetMousePos().x - ruler_min.x) / m_segment_style.scale;

  auto mouse_pos = std::make_unique<float>(mouse_delta);
  return request_seek_frame(std::move(mouse_pos));
}

void Timeline::render_ruler(const ImVec2& timestamp_max) {
  static const float RulerHeight = 40.0f;

  m_draw_list->ChannelsSetCurrent(TimelineLayers::RULER_LAYER);

  ImVec2 ruler_min = ImGui::GetCursorScreenPos();

  ruler_min.x += m_track_style.size.x;

  ImVec2 ruler_max =
      ruler_min + ImVec2(m_window_size.x * m_segment_style.scale, RulerHeight);

  ruler_max.x -= m_track_style.size.x;

  if (ImGui::IsMouseHoveringRect(ruler_min, ruler_max, true)) {
    handle_ruler_events(ruler_min);
  }

  m_draw_list->AddRectFilled(ruler_min, ruler_max, Color::RULER_COLOR);

  ImGui::Dummy(ImVec2(m_window_size.x, RulerHeight));

  // Calculate how many markers the ruler can accommodate.
  constexpr float MARKER_LEFT_SPACING = 8.0f;

  const float horizontal_avail_region =
      ImGui::GetWindowWidth() * m_segment_style.scale;

  const auto number_of_lines =
      (horizontal_avail_region + MARKER_LEFT_SPACING) /
      (2.0F + MARKER_LEFT_SPACING * m_segment_style.scale);

  for (unsigned int i = 0; i < static_cast<unsigned int>(number_of_lines);
       ++i) {
    ImVec2 upper_vert = ruler_min;
    upper_vert.x += (2.0f + MARKER_LEFT_SPACING * m_segment_style.scale) * i;

    ImVec2 bottom_vert = upper_vert;
    bottom_vert.y += RulerHeight;

    if (upper_vert.x >= ruler_min.x && upper_vert.x <= ruler_max.x) {
      constexpr int MILESTONE = 5;
      bottom_vert.y -= i % MILESTONE == 0 ? 10.0f : 20.0f;

      m_draw_list->AddLine(upper_vert, bottom_vert,
                           IM_COL32(100, 100, 100, 255), 2.0f);
    }
  }
}

#pragma endregion Timeline Ruler

#pragma region Playhead

int Timeline::handle_playhead_events(const ImVec2& min, const ImVec2& max) {
  if (!ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
    return 1;
  }

  float mouse_delta = ImGui::GetMousePos().x - min.x;

  auto final_mouse_pos =
      std::make_unique<float>(mouse_delta / m_segment_style.scale);

  return request_seek_frame(std::move(final_mouse_pos));
}

void Timeline::render_playhead() {
  m_draw_list->ChannelsSetCurrent(TimelineLayers::CURSOR_LAYER);

  const float horizontal_scroll = ImGui::GetScrollX();

  ImVec2 min = ImGui::GetCursorScreenPos();
  min.x += m_track_style.size.x + m_playhead_prop.current_time;

  if (m_playhead_prop.current_time >=
      horizontal_scroll + (m_window_size.x - m_track_style.size.x)) {
    ImGui::SetScrollX(m_playhead_prop.current_time);
  }

  ImVec2 max = min;
  max.x += m_playhead_prop.thickness;
  max.y += m_window_size.y;

  constexpr float HITBOX_SCALE = 30.0f;
  const auto min_playhead_hitbox = ImVec2(min.x - HITBOX_SCALE, min.y);
  const auto max_playhead_hitbox = ImVec2(max.x + HITBOX_SCALE, max.y);

  if (ImGui::IsMouseHoveringRect(min, max)) {
    handle_playhead_events(min_playhead_hitbox, max_playhead_hitbox);
  }

  m_draw_list->AddRectFilled(min, max, Color::CURSOR_COLOR);
}

#pragma endregion Playhead

}  // namespace YAVE