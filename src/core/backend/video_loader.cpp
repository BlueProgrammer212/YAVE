#include "core/backend/video_loader.hpp"

namespace YAVE
{
VideoLoader::~VideoLoader(){};

bool VideoLoader::allocate_format_context(AVFormatContext** format_context, const std::string& path)
{
    *format_context = avformat_alloc_context();

    int open_input_result =
        avformat_open_input(format_context, path.c_str(), nullptr, nullptr) == 0;

    return *format_context && open_input_result;
}

int VideoLoader::find_available_codecs(
    AVFormatContext** format_context, FindStreamCallback callback)
{
    for (StreamID i = 0; i < (*format_context)->nb_streams; ++i) {
        const auto& av_stream = (*format_context)->streams[i];
        const auto* av_codec_params = av_stream->codecpar;
        const AVCodec* av_codec = avcodec_find_decoder(av_codec_params->codec_id);

        if (!av_codec) {
            continue;
        }

        std::cout << "Found stream: " << av_codec->long_name << "\n";
        callback(av_stream, av_codec, i);
    }

    return 0;
}
} // namespace YAVE