#pragma once

#include "core/application.hpp"

namespace YAVE
{
class MediaConcatenation
{
public:
    MediaConcatenation() = default;
    ~MediaConcatenation();

    int concat_video(AVFormatContext* out_format_context, const std::string& input_url);

private:
};
} // namespace YAVE