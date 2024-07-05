#pragma once

#include "application.hpp"

namespace YAVE {
// clang-format off
namespace Color {
constexpr ImU32 PRIMARY             = IM_COL32(20,  20,  24,  255);
constexpr ImU32 SECONDARY           = IM_COL32(30,  30,  34,  255);
constexpr ImVec4 TRANSPARENT        = ImVec4  (0,   0,   0,   0);

#pragma region Timeline
constexpr ImU32 VIDEO_SEGMENT_COLOR = IM_COL32(0,   100, 150, 255);
constexpr ImU32 AUDIO_SEGMENT_COLOR = IM_COL32(0,   150, 136, 255);
constexpr ImU32 TRACK_COLOR         = IM_COL32(40,  40,  45,  255);
constexpr ImU32 RULER_COLOR         = IM_COL32(35,  35,  39,  255);
constexpr ImU32 CURSOR_COLOR        = IM_COL32(200, 20,  20,  255);
#pragma endregion Timeline

#pragma region Importer
constexpr ImU32 VID_FILE_BTN_COLOR  = IM_COL32(40,  40,  40,  255);
#pragma endregion Importer
// clang-format on
}  // namespace Color
}  // namespace YAVE