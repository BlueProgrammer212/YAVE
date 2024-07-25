#pragma once

#include "core/application.hpp"
#include "core/backend/media_concatenation.hpp"
#include <iostream>

namespace YAVE
{
class Exporter
{
public:
    Exporter();
    ~Exporter();

    [[nodiscard]] AVFormatContext* create_output_format_context(const std::string& filename);
    
    bool copy_streams(
        AVFormatContext* input_format_context, AVFormatContext* output_format_context);

    void init();
    void update();
    void render();

private:
    int m_bitrate;
    int m_max_quality_bitrate;
    int m_selected_fps;
};
} // namespace YAVE