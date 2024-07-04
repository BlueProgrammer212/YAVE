#include "core/importer.hpp"

namespace YAVE {
std::unique_ptr<ThumbnailLoader> Importer::s_ThumbnailLoader =
    std::make_unique<ThumbnailLoader>();

Importer::Importer()
    : m_user_data(std::make_unique<ImporterUserData>()),
      m_window_data(std::make_unique<ImporterWindowData>()) {}

Importer::~Importer() {}

std::optional<std::string> Importer::get_filename_from_url(std::string path) {
  std::size_t last_slash_pos = path.find_last_of('/');

  if (last_slash_pos != std::string::npos) {
    return path.substr(last_slash_pos + 1);
  }

  return std::nullopt;
}

int Importer::is_extension_compatible(std::string filename) {
  AVOutputFormat* output_format =
      av_guess_format(nullptr, filename.c_str(), nullptr);

  if (!output_format) {
    return -1;
  }

  return 0;
}

void Importer::load_entry(const std::filesystem::directory_entry& entry) {
  if (!std::filesystem::is_regular_file(entry.status()) ||
      !entry.path().has_filename() || !entry.path().has_extension()) {
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

  const auto readable_size =
      std::ceil(static_cast<double>(mantissa) * 10.) / 10.;

  video_file_data.size =
      std::to_string(readable_size) + "BKMGTPE"[unit_index] + "B";

  m_user_data->file_paths.push_back(video_file_data);
}

void Importer::init() {
  auto& current_directory = m_user_data->current_directory;
  current_directory = "../../assets/";

  std::filesystem::path directory_path(current_directory);

  try {
    for (const auto& entry :
         std::filesystem::directory_iterator(directory_path)) {
      load_entry(entry);
    }
  } catch (const std::filesystem::filesystem_error& err) {
    std::cerr << "Error: " << err.what() << std::endl;
  }

  m_thumbnail_loader_thread =
      SDL_CreateThread(&Importer::load_thumbnail_callback,
                       "Thumbnail Loader Thread", m_user_data.get());
}

void Importer::update() {}

void Importer::truncate_filename(float max_width, std::string* filename) {
  float text_width = ImGui::CalcTextSize(filename->c_str()).x;

  if (text_width <= max_width) {
    return;
  }

  int left = 0;
  int right = static_cast<int>(filename->size());

  while (left < right) {
    const int middle = (left + right) / 2;

    std::string trimmed_str = filename->substr(0, middle);
    text_width = ImGui::CalcTextSize(trimmed_str.c_str()).x;

    if (text_width > max_width) {
      right = middle;
    } else {
      left = middle + 1;
    }
  }

  *filename = filename->substr(0, right - 1);
}

void Importer::handle_zooming(float dt) {
  static ImGuiIO* io = &ImGui::GetIO();
  auto& current_zoom_factor = m_window_data->current_zoom_factor;

  current_zoom_factor = 1.0f;

  if (!ImGui::IsKeyDown(ImGuiKey_LeftCtrl)) {
    return;
  }

  if (ImGui::IsWindowHovered()) {
    current_zoom_factor += io->MouseWheel * 0.1f;
  }
}

ImVec2 Importer::maintain_thumbnail_aspect_ratio(ImVec2* thumbnail_min,
                                                 VideoDimension dimensions) {
  if (dimensions.x <= 0 || dimensions.y <= 0) {
    return ImVec2(0, 0);
  }

  auto& thumbnail_size = m_window_data->thumbnail_size;

  const float texture_aspect_ratio =
      static_cast<float>(dimensions.x) / static_cast<float>(dimensions.y);

  const float thumbnail_aspect_ratio = thumbnail_size.x / thumbnail_size.y;
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

void Importer::handle_video_loading_events(const ImVec2& min, const ImVec2& max,
                                           std::string* filename,
                                           const int index) {
  if (!ImGui::IsMouseHoveringRect(min, max)) {
    return;
  }

  if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
    request_video_preview(filename);
    m_window_data->active_index = -1;
    return;
  }

  if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    m_window_data->active_index = index;
  }
}

void Importer::render_files(float avail_width, VideoFile* video_file,
                            const int index, int* max_column_nb_ptr) {
  auto& [path, filename, size, texture_id, dimension] = *video_file;

  auto& draw_list = m_window_data->draw_list;
  auto& thumbnail_size = m_window_data->thumbnail_size;

  *max_column_nb_ptr =
      static_cast<int>((avail_width + THUMBNAIL_MARGIN.x) /
                       (thumbnail_size.x + THUMBNAIL_MARGIN.x));
  *max_column_nb_ptr = std::max(*max_column_nb_ptr, 1);

  ImVec2 min = ImGui::GetCursorScreenPos();
  min.x +=
      (thumbnail_size.x + THUMBNAIL_MARGIN.x) * (index % *max_column_nb_ptr);
  min.y +=
      (thumbnail_size.y + THUMBNAIL_MARGIN.y) * (index / *max_column_nb_ptr);

  ImVec2 max = min + thumbnail_size;

  handle_video_loading_events(min, max, &filename, index);

  // Draw the button
  m_window_data->draw_list->AddRectFilled(min, max, Color::VID_FILE_BTN_COLOR,
                                          0.5f);

  if (video_file->texture_id != 0) {
    ImVec2 thumbnail_min = min;

    ImVec2 thumbnail_max =
        maintain_thumbnail_aspect_ratio(&thumbnail_min, video_file->resolution);

    draw_list->AddImage(reinterpret_cast<void*>(video_file->texture_id),
                        thumbnail_min, thumbnail_min + thumbnail_max);
  }

  if (m_window_data->active_index == index) {
    draw_list->AddRectFilled(min, max, IM_COL32(0, 182, 227, 100), 0.5f);
  }

  std::string filename_cpy = filename;
  truncate_filename(thumbnail_size.x, &filename_cpy);

  float text_width = ImGui::CalcTextSize(filename.c_str()).x;
  ImVec2 text_pos = ImVec2(min.x + (thumbnail_size.x - text_width) / 2,
                           max.y + THUMBNAIL_VIDEO_TITLE_TOP_PADDING);

  draw_list->AddText(text_pos, IM_COL32_WHITE, filename_cpy.c_str());
}

void Importer::refresh_thumbnail_textures(const Thumbnail thumbnail,
                                          int file_index) {
  VideoFile& video_file = m_user_data->file_paths[file_index];
  auto& texture_id = video_file.texture_id;

  if (texture_id == 0) {
    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glBindTexture(GL_TEXTURE_2D, 0);
  }

  glBindTexture(GL_TEXTURE_2D, texture_id);

  video_file.resolution = thumbnail.dimension;

  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, thumbnail.dimension.x,
               thumbnail.dimension.y, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               thumbnail.framebuffer);

  glBindTexture(GL_TEXTURE_2D, 0);
}

void Importer::render() {
  auto& file_paths = m_user_data->file_paths;
  auto& current_directory = m_user_data->current_directory;
  auto& thumbnail_size = m_window_data->thumbnail_size;

  ImGui::Begin("Importer", nullptr);

  ImGui::Text("Current Directory: ");

  ImGui::SameLine();

  ImGui::InputText("##label", current_directory.data(),
                   current_directory.size() + 1);

  int max_column_nb = 1;

  ImGui::BeginChild("#file_explorer", ImGui::GetContentRegionAvail(), true);

  const float avail_region_width = ImGui::GetContentRegionAvail().x;

  m_window_data->draw_list = ImGui::GetWindowDrawList();

  handle_zooming(ImGui::GetIO().DeltaTime);
  m_window_data->thumbnail_size *= m_window_data->current_zoom_factor;

  for (int i = 0; i < file_paths.size(); i++) {
    render_files(avail_region_width, &file_paths[i], i, &max_column_nb);
  }

  ImGui::Dummy(ImVec2(
      0, (file_paths.size() * (thumbnail_size.y + 25.0f)) / max_column_nb));

  ImGui::EndChild();

  ImGui::End();

  if (m_user_data->status_code < 0) {
    ImGui::Begin("Error", nullptr);
    ImGui::Text(m_error_message.c_str());
    ImGui::End();
  }
}

int Importer::load_thumbnail_callback(void* userdata) {
  auto* importer_user_data = static_cast<ImporterUserData*>(userdata);

  for (int i = 0; i < importer_user_data->file_paths.size(); ++i) {
    if (!Application::is_running) {
      break;
    }

    request_load_thumbnail(importer_user_data,
                           importer_user_data->file_paths[i].filename, i);
  }

  return 0;
}

void Importer::request_video_preview(std::string* video_filename) {
  SDL_Event load_video_event;
  load_video_event.type = CustomVideoEvents::FF_LOAD_NEW_VIDEO_EVENT;
  load_video_event.user.data1 = reinterpret_cast<void*>(video_filename);

  SDL_PushEvent(&load_video_event);
}

void Importer::send_thumbnail_to_main_thread(
    std::optional<Thumbnail*> thumbnail, int file_index) {
  if (!thumbnail.has_value()) {
    return;
  }

  int* file_index_ptr = new int(file_index);

  SDL_Event event;
  event.type = CustomVideoEvents::FF_REFRESH_THUMBNAIL;
  event.user.data1 = thumbnail.value();
  event.user.data2 = file_index_ptr;

  SDL_PushEvent(&event);
}

void Importer::request_load_thumbnail(ImporterUserData* data,
                                      std::string video_filename,
                                      int file_index) {
  const std::string url = data->current_directory + video_filename;
  auto thumbnail = s_ThumbnailLoader->load_video_thumbnail(url);
  send_thumbnail_to_main_thread(thumbnail, file_index);
}

}  // namespace YAVE