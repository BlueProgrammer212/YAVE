#pragma once

#include <algorithm>
#include <array>
#include <functional>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <vector>

#ifdef __cplusplus
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#endif

namespace YAVE
{
struct StreamUnits {
    AVFrame* av_frame = nullptr;
    AVPacket* av_packet = nullptr;
};

struct Codec {
    virtual ~Codec() = default;

    AVCodec* av_codec = nullptr;
    AVCodecParameters* av_codec_params = nullptr;
    AVCodecContext* av_codec_ctx = nullptr;
};

struct StreamInfo : public Codec {
    AVRational timebase = AVRational{ 0, 0 };
    int stream_index = -1;
    int width = -1;
    int height = -1;
};

using StreamInfoPtr = std::shared_ptr<StreamInfo>;
using StreamMap = std::unordered_map<std::string, StreamInfoPtr>;
using StreamID = unsigned int;

using FindStreamCallback = std::function<int(const AVStream* stream, const AVCodec*, const StreamID)>;

/**
 * @brief Converts FFmpeg error codes to a string.
 * @param errnum The FFmpeg error code.
 * @return The error string.
 */
[[nodiscard]] inline static std::string av_error_to_string(int errnum)
{
    std::array<char, AV_ERROR_MAX_STRING_SIZE> buffer;
    av_strerror(errnum, buffer.data(), AV_ERROR_MAX_STRING_SIZE);
    return std::string(buffer.data());
}

class VideoLoader
{
public:
    VideoLoader() = default;
    ~VideoLoader();

    bool allocate_format_context(AVFormatContext** format_context, const std::string& path);

    int find_available_codecs(AVFormatContext** format_context, FindStreamCallback callback);

private:
};
} // namespace YAVE