#pragma once

#include "application.hpp"

namespace YAVE
{
namespace Color
{
    // Background
    constexpr ImU32 PRIMARY = IM_COL32(20, 20, 24, 255);
    constexpr ImU32 SECONDARY = IM_COL32(30, 30, 34, 255);
    constexpr ImVec4 TRANSPARENT = ImVec4(0, 0, 0, 0);

    // Timeline
    constexpr ImU32 VIDEO_SEGMENT_COLOR = IM_COL32(0, 100, 150, 255);
    constexpr ImU32 AUDIO_SEGMENT_COLOR = IM_COL32(0, 150, 136, 255);
    constexpr ImU32 TRACK_COLOR = IM_COL32(40, 40, 45, 255);
    constexpr ImU32 RULER_COLOR = IM_COL32(35, 35, 39, 255);
    constexpr ImU32 CURSOR_COLOR = IM_COL32(200, 20, 20, 255);

    // Importer
    constexpr ImU32 VID_FILE_BTN_COLOR = IM_COL32(40, 40, 40, 255);
    constexpr ImU32 THUMBNAIL_HOVERED = IM_COL32(0, 182, 227, 100);

    // Waveform
    constexpr ImU32 WAVEFORM_VID_COLOR = IM_COL32(0, 200, 250, 255);

    // Subtitle Editor
    constexpr ImU32 SUBTITLE_BACKGROUND_COLOR = IM_COL32(40, 40, 40, 255);

    void dark_theme(ImVec4* colors);
} // namespace Color
} // namespace YAVE