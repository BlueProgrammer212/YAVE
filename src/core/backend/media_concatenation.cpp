#include "core/backend/media_concatenation.hpp"

namespace YAVE
{
MediaConcatenation::~MediaConcatenation() {}

int MediaConcatenation::concat_video(
    AVFormatContext* out_format_context, const std::string& input_url)
{
    AVFormatContext* input_format_context = avformat_alloc_context();

    if (avformat_open_input(&input_format_context, input_url.c_str(), nullptr, nullptr) < 0) {
        std::cerr << "[Media Concatenation]: Failed to open the input file.\n";
        return -1;
    }

    return 0;
}
} // namespace YAVE