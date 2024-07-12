#include "core/backend/thumbnail_loader.hpp"

namespace YAVE
{
ThumbnailLoader::ThumbnailLoader(){};

ThumbnailLoader::~ThumbnailLoader(){};

#pragma region Thumbnail Loader
int ThumbnailLoader::decode_frame(Thumbnail* data)
{
    for (int response = 0;;) {
        response = av_read_frame(data->av_format_context, m_av_packet);

        if (m_av_packet->stream_index != data->stream_info.stream_index) {
            av_packet_unref(m_av_packet);
            continue;
        }

        if (response == AVERROR(EAGAIN)) {
            av_packet_unref(m_av_packet);
            continue;
        }

        if (response < 0) {
            av_packet_unref(m_av_packet);
            return -1;
        }

        break;
    };

    return 0;
}

std::unique_ptr<std::vector<int>> ThumbnailLoader::extract_histogram(AVFrame* frame, int num_bins)
{
    auto histogram = std::make_unique<std::vector<int>>(num_bins, 0);

    // The downsample factor is directly related to the video's dimensions.
    constexpr int DOWNSAMPLE_FACTOR = 256;

    int total_pixels = (frame->height / DOWNSAMPLE_FACTOR) * (frame->width / DOWNSAMPLE_FACTOR);

    for (int i = 0; i < total_pixels; ++i) {
        int y = (i / (frame->width / DOWNSAMPLE_FACTOR)) * DOWNSAMPLE_FACTOR;
        int x = (i % (frame->width / DOWNSAMPLE_FACTOR)) * DOWNSAMPLE_FACTOR;
        int pixel_intensity = frame->data[0][y * frame->linesize[0] + x];
        (*histogram)[pixel_intensity]++;
    }

    return histogram;
}

int ThumbnailLoader::compare_previous_histogram(
    const std::vector<int>& new_histogram, const std::vector<int>& old_histogram)
{
    double accumulated_squared_diff = 0.0;

    for (size_t i = 0; i < new_histogram.size(); ++i) {
        double delta_pixel_intensity = new_histogram[i] - old_histogram[i];
        accumulated_squared_diff += std::pow(delta_pixel_intensity, 2.0);
    }

    const double mean = accumulated_squared_diff / new_histogram.size();
    const double RMSE = std::sqrt(mean);

    constexpr double THRESHOLD = 1.75;

    if (RMSE < THRESHOLD) {
        return NEW_HISTOGRAM_BETTER;
    }

    return LAST_HISTOGRAM_BETTER;
}

#pragma region Thumbnail Picker
int ThumbnailLoader::pick_best_thumbnail(Thumbnail* data, bool use_middle_frame)
{
    if (use_middle_frame) {
        auto seconds = m_duration / (2 * AV_TIME_BASE);

        if (peek_video_frame_by_timestamp(seconds, data) < 0) {
            return -1;
        }

        return 0;
    }

    auto seconds = m_duration / (2 * AV_TIME_BASE);

    if (peek_video_frame_by_timestamp(seconds, data) < 0) {
        return -1;
    }

    auto av_codec_ctx = data->stream_info.av_codec_ctx;

    // Using Root Mean Square Error (RMSE) to determine the best frame.
    AVPacket* packet = av_packet_alloc();
    AVFrame* dummy_frame = av_frame_alloc();
    AVFrame* best_frame = av_frame_alloc();

    constexpr int NUM_BINS = 256;
    constexpr int MAX_NB_BEST_FRAMES = 64;
    constexpr int FRAME_SKIP_INTERVAL = 10;

    std::vector<int> last_histogram(NUM_BINS, 0);
    int frame_skip_count = -1;

    static int best_frame_count = 0;

    while (av_read_frame(data->av_format_context, packet) >= 0) {
        if (packet->stream_index != data->stream_info.stream_index) {
            av_packet_unref(packet);
            continue;
        }

        int response = avcodec_send_packet(av_codec_ctx, packet);

        if (response == AVERROR(EAGAIN)) {
            av_packet_unref(packet);
            continue;
        }

        if (response < 0) {
            av_packet_unref(packet);
            return -1;
        }

        response = avcodec_receive_frame(av_codec_ctx, dummy_frame);

        if (response == AVERROR(EAGAIN)) {
            av_packet_unref(packet);
            continue;
        }

        if (response < 0) {
            av_packet_unref(packet);
            return -1;
        }

        auto current_histogram = extract_histogram(dummy_frame, NUM_BINS);

        if (++frame_skip_count == 0) {
            free_histogram(&last_histogram);
            last_histogram = std::move(*current_histogram);
            av_packet_unref(packet);
            continue;
        }

        int comparison_result = compare_previous_histogram(*current_histogram, last_histogram);
        av_packet_unref(packet);

        if (comparison_result != NEW_HISTOGRAM_BETTER) {
            continue;
        }

        free_histogram(&last_histogram);
        last_histogram = std::move(*current_histogram);
        av_frame_ref(best_frame, dummy_frame);

        break;
    }

    free_histogram(&last_histogram);
    av_frame_free(&dummy_frame);
    av_packet_free(&packet);

    if (!best_frame->data[0]) {
        return -1;
    }

    double best_frame_pts_in_sec = best_frame->pts * av_q2d(data->stream_info.timebase);

    best_frame_count = 0;
    av_frame_free(&best_frame);

    if (peek_video_frame_by_timestamp(static_cast<std::int64_t>(best_frame_pts_in_sec), data) !=
        0) {
        return -1;
    };

    return 0;
}

#pragma endregion Thumbnail Picker

int ThumbnailLoader::update_framebuffer(Thumbnail* data)
{
    const int width = data->stream_info.width;
    const int height = data->stream_info.height;

    SwsContext* sws_scaler_ctx =
        sws_getContext(width, height, data->stream_info.av_codec_ctx->pix_fmt, width, height,
            AV_PIX_FMT_RGB0, SWS_BILINEAR, nullptr, nullptr, nullptr);

    if (!sws_scaler_ctx) {
        return -1;
    }

    std::array<std::uint8_t*, COLOR_CHANNELS_NB> dest = { data->framebuffer, nullptr, nullptr,
        nullptr };

    std::array<int, COLOR_CHANNELS_NB> dest_linesize = { 0, 0, 0, 0 };
    dest_linesize[0] = width * COLOR_CHANNELS_NB;

    sws_scale(sws_scaler_ctx, m_av_frame->data, m_av_frame->linesize, 0, height, dest.data(),
        dest_linesize.data());

    data->dimension.x = m_av_frame->width;
    data->dimension.y = m_av_frame->height;

    sws_freeContext(sws_scaler_ctx);

    return 0;
}

int ThumbnailLoader::send_packet(Thumbnail* data, int retry_nb)
{
    if (decode_frame(data) != 0) {
        av_packet_unref(m_av_packet);
        return send_packet(data, ++retry_nb);
    };

    int send_pkt_err = avcodec_send_packet(data->stream_info.av_codec_ctx, m_av_packet);

    if (send_pkt_err == AVERROR_EOF) {
        av_packet_unref(m_av_packet);
        return -1;
    }

    if (send_pkt_err == AVERROR(EAGAIN)) {
        av_packet_unref(m_av_packet);
        return send_packet(data, ++retry_nb);
    }

    if (send_pkt_err < 0) {
        av_packet_unref(m_av_packet);
        return -1;
    }

    int recieve_frame_errcode = avcodec_receive_frame(data->stream_info.av_codec_ctx, m_av_frame);

    if (recieve_frame_errcode == AVERROR_EOF) {
        av_packet_unref(m_av_packet);
        return -1;
    }

    // Recurse until we get a valid video frame.
    if (recieve_frame_errcode == AVERROR(EAGAIN)) {
        av_packet_unref(m_av_packet);
        return send_packet(data, ++retry_nb);
    }

    if (recieve_frame_errcode < 0) {
        av_packet_unref(m_av_packet);
        return -1;
    }

    return 0;
}

void ThumbnailLoader::allocate_frame_buffer(Thumbnail* data)
{
    constexpr int LINESIZE_ALIGNMENT = 32;

    const int width = data->stream_info.width;
    const int height = data->stream_info.height;

    const auto buffer_size =
        av_image_get_buffer_size(AV_PIX_FMT_RGBA, width, height, LINESIZE_ALIGNMENT);

    const auto total_buffer_size = buffer_size * sizeof(std::uint8_t);
    data->framebuffer = static_cast<std::uint8_t*>(av_malloc(total_buffer_size));
}

int ThumbnailLoader::peek_video_frame_by_timestamp(const int64_t seconds, Thumbnail* data)
{
    const auto target_timestamp =
        static_cast<int64_t>(seconds / av_q2d(data->stream_info.timebase));

    int seek_ret = av_seek_frame(data->av_format_context, data->stream_info.stream_index,
        target_timestamp, AVSEEK_FLAG_BACKWARD);

    avcodec_flush_buffers(data->stream_info.av_codec_ctx);

    return seek_ret;
}

int ThumbnailLoader::find_streams(AVFormatContext* av_format_context, Thumbnail* userdata)
{
    for (std::uint32_t i = 0; i < userdata->av_format_context->nb_streams; ++i) {
        auto& stream = userdata->av_format_context->streams[i];
        auto& stream_info = userdata->stream_info;
        AVCodec* av_codec = avcodec_find_decoder(stream->codecpar->codec_id);

        if (!av_codec || stream->codecpar->codec_type != AVMEDIA_TYPE_VIDEO) {
            continue;
        }

        stream_info.av_codec = av_codec;
        stream_info.av_codec_params = stream->codecpar;
        stream_info.stream_index = i;
        stream_info.timebase = stream->time_base;
        stream_info.width = stream->codecpar->width;
        stream_info.height = stream->codecpar->height;

        m_duration = userdata->av_format_context->duration;

        break;
    }

    return 0;
}

int ThumbnailLoader::open_file(std::string filename, Thumbnail* userdata)
{
    // Open the format context and initialize the video stream.
    userdata->av_format_context = avformat_alloc_context();

    int open_ret =
        avformat_open_input(&userdata->av_format_context, filename.c_str(), nullptr, nullptr);

    if (open_ret != 0) {
        return -1;
    }

    if (find_streams(userdata->av_format_context, userdata) != 0) {
        return -1;
    };

    userdata->stream_info.av_codec_ctx = avcodec_alloc_context3(userdata->stream_info.av_codec);

    if (!userdata->stream_info.av_codec_ctx || !userdata->stream_info.av_codec_params) {
        return -1;
    }

    if (avcodec_parameters_to_context(
            userdata->stream_info.av_codec_ctx, userdata->stream_info.av_codec_params) < 0) {
        return -1;
    }

    if (avcodec_open2(
            userdata->stream_info.av_codec_ctx, userdata->stream_info.av_codec, nullptr)) {
        return -1;
    }

    if (userdata->stream_info.width <= 0 || userdata->stream_info.height <= 0) {
        return -1;
    }

    this->allocate_frame_buffer(userdata);

    if (!userdata->framebuffer) {
        return -1;
    }

    return 0;
}

std::optional<Thumbnail*> ThumbnailLoader::load_video_thumbnail(const std::string& path)
{
    auto* data = new Thumbnail();

    if (open_file(path, reinterpret_cast<Thumbnail*>(data)) < 0) {
        return std::nullopt;
    }

    m_av_frame = av_frame_alloc();
    m_av_packet = av_packet_alloc();

    int result = pick_best_thumbnail(data, true);

    if (result < 0 || send_packet(data) != 0) {
        return std::nullopt;
    };

    if (update_framebuffer(data) != 0) {
        return std::nullopt;
    };

    av_frame_free(&m_av_frame);
    av_packet_free(&m_av_packet);

    avformat_close_input(&data->av_format_context);
    avformat_free_context(data->av_format_context);

    avcodec_free_context(&data->stream_info.av_codec_ctx);

    return data;
}
#pragma endregion Thumbnail Loader
} // namespace YAVE