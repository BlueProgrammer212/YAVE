#include "core/exporter.hpp"

namespace YAVE
{
Exporter::Exporter()
    : m_bitrate(640 * 480 * 60 * 0.15f)
    , m_selected_fps(60)
    , m_max_quality_bitrate(3840 * 2160 * 60 * 0.15f)
{
}

Exporter::~Exporter() {}
[[nodiscard]] AVFormatContext* Exporter::create_output_format_context(const std::string& filename)
{
    AVFormatContext* output_format_context = nullptr;
    avformat_alloc_output_context2(&output_format_context, nullptr, nullptr, filename.c_str());

    if (!output_format_context) {
        std::cout << "[Exporter] Failed to allocate memory for the output format context.\n";
    }

    return output_format_context;
}

bool Exporter::copy_streams(
    AVFormatContext* input_format_context, AVFormatContext* output_format_context)
{
    for (std::size_t i = 0; i < input_format_context->nb_streams; ++i) {
        AVStream* in_stream = input_format_context->streams[i];
        AVStream* out_stream = avformat_new_stream(output_format_context, nullptr);

        if (!out_stream) {
            std::cerr << "Failed to allocate output stream.\n";
            return false;
        }

        if (avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar) < 0) {
            std::cerr << "[Exporter] Failed to copy codec parameters.\n";
            return false;
        }

        out_stream->codecpar->codec_tag = 0;
    }

    return true;
}

void Exporter::init() {}

void Exporter::update() {}

void Exporter::render()
{
    ImGui::Begin("Exporter");

    ImGui::SetWindowFontScale(1.2f);
    ImGui::Text("Transsizing");
    ImGui::SetWindowFontScale(1.0f);

    ImGui::Text("Select a resolution:");

    // clang-format off
    static const char* AVAILABLE_RESOLUTIONS[] = { 
        "144p (Mobile): 256x144",
        "240p (Mobile): 426x240",
        "360p: 640x360",
        "480p (SD): 640x480", 
        "720p (HD): 1280x720",
        "Square Video (Facebook, Instagram): 1080x1080" ,
        "1080p (Full HD): 1920x1080",
        "4k UHD: 3840x2160" };
    // clang-format on

    static const char* current_item = NULL;

    if (ImGui::BeginCombo("##combo", current_item)) {
        for (int n = 0; n < IM_ARRAYSIZE(AVAILABLE_RESOLUTIONS); n++) {
            bool is_selected = (current_item == AVAILABLE_RESOLUTIONS[n]);

            if (ImGui::Selectable(AVAILABLE_RESOLUTIONS[n], is_selected)) {
                current_item = AVAILABLE_RESOLUTIONS[n];
            }

            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
        }

        ImGui::EndCombo();
    }

    ImGui::SetWindowFontScale(1.1f);
    ImGui::Dummy(ImVec2(0, 5));
    ImGui::Text("Transrating");
    ImGui::SetWindowFontScale(1.0f);

    ImGui::Text("Bitrate: ");
    ImGui::SameLine();
    ImGui::InputInt("##bitrate_input", &m_bitrate);
    ImGui::SameLine();
    ImGui::Text("bps");

    ImGui::Button("Export");

    ImGui::End();
}
} // namespace YAVE