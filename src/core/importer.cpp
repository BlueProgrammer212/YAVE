#include "core/importer.hpp"

namespace YAVE
{
std::unique_ptr<ThumbnailLoader> Importer::s_ThumbnailLoader = std::make_unique<ThumbnailLoader>();

Importer::Importer()
    : m_user_data(std::make_unique<ImporterUserData>())
    , m_window_data(std::make_unique<ImporterWindowData>())
{
}

Importer::~Importer() {}

std::optional<std::string> Importer::get_filename_from_url(std::string path)
{
    const std::size_t last_slash_pos = path.find_last_of('/');

    if (last_slash_pos != std::string::npos) {
        return path.substr(last_slash_pos + 1);
    }

    return std::nullopt;
}

void Importer::load_entry(const std::filesystem::directory_entry& entry)
{
    if (!std::filesystem::is_regular_file(entry.status()) || !entry.path().has_filename() ||
        !entry.path().has_extension()) {
        return;
    }

    VideoFile video_file_data;
    video_file_data.path = entry.path().string();
    video_file_data.filename = entry.path().filename().string();
    video_file_data.texture_id = 0;

    // Check if the file extension is compatible.
    if (is_extension_compatible(video_file_data.filename) != 0) {
        return;
    }

    std::uintmax_t mantissa = entry.file_size();
    int unit_index = 0;

    for (; mantissa >= 1024; mantissa /= 1024, ++unit_index)
        ;

    auto readable_size = std::ceil(static_cast<double>(mantissa) * 10.) / 10.;

    video_file_data.size =
        std::to_string(static_cast<int>(readable_size)) + "BKMGTPE"[unit_index] + "B";

    m_user_data->file_paths.push_back(video_file_data);
}

void Importer::init()
{
    auto& current_directory = m_user_data->current_directory;
    current_directory = "../../assets/";

    std::filesystem::path directory_path(current_directory);

    try {
        for (const auto& entry : std::filesystem::directory_iterator(directory_path)) {
            load_entry(entry);
        }
    } catch (const std::filesystem::filesystem_error& err) {
        std::cerr << "Error: " << err.what() << std::endl;
    }

    m_thumbnail_loader_thread = SDL_CreateThread(
        &Importer::load_thumbnail_callback, "Thumbnail Loader Thread", m_user_data.get());
}

void Importer::update() {}

std::optional<std::string> Importer::truncate_filename(float max_width, const std::string& filename)
{
    float text_width = ImGui::CalcTextSize(filename.c_str()).x;

    if (text_width <= max_width) {
        return std::nullopt;
    }

    int left = 0;
    int right = static_cast<int>(filename.size());

    while (left < right) {
        const int middle = (left + right) / 2;

        std::string trimmed_str = filename.substr(0, middle);
        text_width = ImGui::CalcTextSize(trimmed_str.c_str()).x;

        if (text_width > max_width) {
            right = middle;
        } else {
            left = middle + 1;
        }
    }

    return filename.substr(0, right - 1);
}

void Importer::handle_zooming(float dt)
{
    static ImGuiIO* io = &ImGui::GetIO();
    auto& current_zoom_factor = m_window_data->current_zoom_factor;

    current_zoom_factor = 1.0f;

    if (ImGui::IsWindowHovered() && ImGui::IsKeyDown(ImGuiKey_LeftCtrl)) {
        current_zoom_factor += io->MouseWheel * 0.1f;
    }
}

ImVec2 Importer::maintain_thumbnail_aspect_ratio(ImVec2* thumbnail_min, VideoDimension dimensions)
{
    if (dimensions.x <= 0 || dimensions.y <= 0) {
        return ImVec2(0, 0);
    }

    auto& thumbnail_size = m_window_data->thumbnail_size;

    float texture_aspect_ratio =
        static_cast<float>(dimensions.x) / static_cast<float>(dimensions.y);

    float thumbnail_aspect_ratio = thumbnail_size.x / thumbnail_size.y;

    ImVec2 display_size;

    if (texture_aspect_ratio > 1.0f) {
        // Texture is wider than it is tall
        display_size.x = thumbnail_size.x;
        display_size.y = thumbnail_size.x / texture_aspect_ratio;
    } else {
        // Texture is taller than it is wide
        display_size.x = thumbnail_size.y * texture_aspect_ratio;
        display_size.y = thumbnail_size.y;
    }

    thumbnail_min->x += (thumbnail_size.x - display_size.x) * 0.5f;
    thumbnail_min->y += (thumbnail_size.y - display_size.y) * 0.5f;

    return display_size;
}

void Importer::hover_video_file_callback(
    const ImVec2& min, const ImVec2& max, const VideoFile* file, const int index)
{
    std::string tooltip_content =
        "Filename: " + file->filename + "\n" + "Size: " + file->size + "\n";

    ImGui::BeginTooltip();
    ImGui::Text(tooltip_content.c_str());
    ImGui::EndTooltip();

    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        request_video_preview(file->filename);
        m_window_data->active_index = -1;
        return;
    }

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        m_window_data->active_index = index;
    }
}

void Importer::render_files(
    float avail_width, VideoFile* video_file, const int index, int* max_column_nb_ptr)
{
    auto& [path, filename, size, texture_id, dimension] = *video_file;

    auto& draw_list = m_window_data->draw_list;
    auto& thumbnail_size = m_window_data->thumbnail_size;

    const auto thumbnail_final_pos = thumbnail_size + THUMBNAIL_MARGIN;

    *max_column_nb_ptr =
        static_cast<int>((avail_width + THUMBNAIL_MARGIN.x) / thumbnail_final_pos.x);

    *max_column_nb_ptr = std::max(*max_column_nb_ptr, 1);

    const int current_column = index % *max_column_nb_ptr;
    const int current_row = index / *max_column_nb_ptr;

    ImVec2 min = ImGui::GetCursorScreenPos();
    min.x += thumbnail_final_pos.x * current_column;
    min.y += thumbnail_final_pos.y * current_row;

    ImVec2 max = min + thumbnail_size;

    if (ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(min, max)) {
        hover_video_file_callback(min, max, video_file, index);
    }

    // Draw the thumbnail container.
    m_window_data->draw_list->AddRectFilled(min, max, Color::VID_FILE_BTN_COLOR, 0.75f);

    if (video_file->texture_id != 0) {
        ImVec2 thumbnail_min = min;

        ImVec2 thumbnail_max =
            maintain_thumbnail_aspect_ratio(&thumbnail_min, video_file->resolution);

        auto texture_id_as_ptr = static_cast<std::uintptr_t>(video_file->texture_id);
        
        draw_list->AddImage(reinterpret_cast<ImTextureID>(texture_id_as_ptr), thumbnail_min,
            thumbnail_min + thumbnail_max);
    }

    if (m_window_data->active_index == index) {
        draw_list->AddRectFilled(min, max, Color::THUMBNAIL_HOVERED, 0.5f);
    }

    const std::optional<std::string>& truncated_filename =
        truncate_filename(thumbnail_size.x, filename);

    if (!truncated_filename.has_value()) {
        return;
    }

    const float text_width = ImGui::CalcTextSize(filename.c_str()).x;

    const auto text_pos = ImVec2(
        min.x + (thumbnail_size.x - text_width) / 2, max.y + THUMBNAIL_VIDEO_TITLE_TOP_PADDING);

    draw_list->AddText(text_pos, IM_COL32_WHITE, truncated_filename.value().c_str());
}

void Importer::init_thumbnail_texture(unsigned int* texture_id)
{
    glGenTextures(1, texture_id);
    glBindTexture(GL_TEXTURE_2D, *texture_id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Importer::refresh_thumbnail_textures(const Thumbnail thumbnail, const std::string& url)
{
    int64_t file_index = find_file_by_url(url);

    if (file_index < 0) {
        std::cout << "Failed to find the file by its url.\n";
        return;
    }

    VideoFile& video_file = m_user_data->file_paths[file_index];
    auto& texture_id = video_file.texture_id;

    if (texture_id == 0) {
        init_thumbnail_texture(&texture_id);
    }

    glBindTexture(GL_TEXTURE_2D, texture_id);

    video_file.resolution = thumbnail.dimension;

    int preferred_format;
    glGetInternalformativ(GL_TEXTURE_2D, GL_RGB, GL_TEXTURE_IMAGE_FORMAT, 1, &preferred_format);

    glTexImage2D(GL_TEXTURE_2D, 0, preferred_format, thumbnail.dimension.x, thumbnail.dimension.y,
        0, GL_RGBA, GL_UNSIGNED_BYTE, thumbnail.framebuffer);

    glBindTexture(GL_TEXTURE_2D, 0);
}

void Importer::render()
{
    auto& file_paths = m_user_data->file_paths;
    auto& current_directory = m_user_data->current_directory;
    auto& thumbnail_size = m_window_data->thumbnail_size;

    ImGui::Begin("Importer", nullptr);

    ImGui::Text("Current Directory: ");

    ImGui::SameLine();

    ImGui::InputText("##label", current_directory.data(), current_directory.size() + 1);

    int max_column_nb = 1;

    ImGui::BeginChild("#file_explorer", ImGui::GetContentRegionAvail(), true);

    float avail_region_width = ImGui::GetContentRegionAvail().x;

    m_window_data->draw_list = ImGui::GetWindowDrawList();

    handle_zooming(ImGui::GetIO().DeltaTime);
    m_window_data->thumbnail_size *= m_window_data->current_zoom_factor;

    for (int i = 0; i < file_paths.size(); i++) {
        render_files(avail_region_width, &file_paths[i], i, &max_column_nb);
    }

    ImGui::Dummy(ImVec2(0, (file_paths.size() * (thumbnail_size.y + 25.0f)) / max_column_nb));

    ImGui::EndChild();

    ImGui::End();

    if (m_user_data->status_code < 0) {
        ImGui::Begin("Error", nullptr);
        const std::string final_error_message =
            "An error occured while trying to import this file: " + m_error_message;
        ImGui::Text(m_error_message.c_str());
        ImGui::End();
    }
}

int Importer::load_thumbnail_callback(void* userdata)
{
    auto* importer_user_data = static_cast<ImporterUserData*>(userdata);

    for (int i = 0; i < importer_user_data->file_paths.size(); ++i) {
        if (!Application::is_running) {
            break;
        }

        request_load_thumbnail(importer_user_data, importer_user_data->file_paths[i].filename);
    }

    return 0;
}

void Importer::request_video_preview(const std::string& video_filename)
{
    SDL_Event load_video_event;
    load_video_event.type = CustomVideoEvents::FF_LOAD_NEW_VIDEO_EVENT;

    auto url_ptr = new std::string(video_filename);
    load_video_event.user.data1 = reinterpret_cast<void*>(url_ptr);

    SDL_PushEvent(&load_video_event);
}

void Importer::send_thumbnail_to_main_thread(std::optional<Thumbnail*> thumbnail, std::string url)
{
    if (!thumbnail.has_value()) {
        return;
    }

    auto file_url_uptr = std::make_unique<std::string>(url);

    SDL_Event event;
    event.type = CustomVideoEvents::FF_REFRESH_THUMBNAIL;
    event.user.data1 = thumbnail.value();
    event.user.data2 = file_url_uptr.release();

    SDL_PushEvent(&event);
}

void Importer::request_load_thumbnail(ImporterUserData* data, const std::string& video_filename)
{
    const std::string url = data->current_directory + video_filename;
    auto thumbnail = s_ThumbnailLoader->load_video_thumbnail(url);
    send_thumbnail_to_main_thread(thumbnail, url);
}

} // namespace YAVE