#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "application.hpp"
#include "color.hpp"

namespace YAVE
{
struct Segment;
struct SegmentStyle;
struct TrackStyle;
struct PlayheadProperties;
struct VideoDimension;

constexpr unsigned int NUMBER_OF_TRACKS = 5;
constexpr float SEGMENT_THUMBNAIL_WIDTH = 80.f;

// clang-format off
constexpr ImPlotAxisFlags WAVEFORM_AXIS_FLAGS =
    ImPlotAxisFlags_NoDecorations | 
    ImPlotAxisFlags_NoGridLines   |
    ImPlotAxisFlags_NoTickMarks   | 
    ImPlotAxisFlags_NoTickLabels  |
    ImPlotAxisFlags_NoLabel;
// clang-format on

enum TimelineLayers {
    TRACK_BACKGROUND_LAYER,
    SEGMENT_LAYER,
    WAVEFORM_LAYER,
    RULER_LAYER,
    CURSOR_LAYER,
    TRACK_LAYER,
    TIMESTAMP_LAYER
};

using SegmentArray = std::vector<std::shared_ptr<Segment>>;
using TrackArray = std::vector<std::string>;

struct Segment {
    unsigned int track_position;
    std::string name;
    float start_time;
    float end_time;
    std::vector<float> waveform_data;
    unsigned int thumbnail_texture_id = 0;
    VideoDimension thumbnail_tex_dimensions;
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

class Timeline
{
public:
    Timeline();
    ~Timeline();

    void init();
    void render();
    void update(float delta_time);

    int update_segment_waveform(const std::vector<float>& audio_data, int segment_index);

    inline void add_segment(const Segment& segment)
    {
        m_segment_array.push_back(std::make_unique<Segment>(segment));
    };

    /**
     * @brief Pushes FF_SEEK_TO_TIMESTAMP_EVENT to the event listener.
     * @param[in] mouse_pos A pointer to the requested timestamp.
     * @return 1 for success, 0 for error.
     */
    int request_seek_frame(std::unique_ptr<float> mouse_pos);

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
    void render_segment_thumbnail(
        const ImVec2& min, unsigned int tex_id, const VideoDimension& resolution);

    void maintain_thumbnail_aspect_ratio(
        const VideoDimension& resolution, ImVec2& min, ImVec2& max, const ImVec2& content_region);

    void handle_segments();
    int handle_segment_renaming(
        std::shared_ptr<Segment> segment, const ImVec2& min, const ImVec2& max, const ImVec2& initial_cursor_pos);
    void render_waveform(
        const ImVec2& min, const ImVec2& max, const std::vector<float>& audio_data);

private:
    void render_tracks();
    void render_timestamp();
    void render_ruler(const ImVec2& timestamp_max);
    void render_playhead();

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
} // namespace YAVE