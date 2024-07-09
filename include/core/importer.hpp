#pragma once

#include <filesystem>

#include "application.hpp"
#include "color.hpp"

#include "core/backend/thumbnail_loader.hpp"

namespace YAVE {
struct VideoFile;

constexpr ImVec2 THUMBNAIL_MARGIN = ImVec2(10.0f, 20.0f);
constexpr float THUMBNAIL_VIDEO_TITLE_TOP_PADDING = 5.0f;

using FilePathArray = std::vector<VideoFile>;

struct VideoFile {
  std::string path;
  std::string filename;
  std::string size;
  unsigned int texture_id = 0;
  VideoDimension resolution;
};

struct ImporterUserData {
  FilePathArray file_paths = {};
  std::string current_directory = "";
  int status_code{0};
};

struct ImporterWindowData {
  ImDrawList* draw_list = nullptr;
  ImVec2 thumbnail_size = ImVec2(80.0f, 60.0f);
  float target_zoom_factor = 1.0f;
  float current_zoom_factor = 1.0f;
  int active_index = -1;
};

class Importer {
 public:
  Importer();
  ~Importer();

  void load_entry(const std::filesystem::directory_entry& entry);

  void init();
  void update();
  void render();

  void handle_zooming(float dt);

  [[nodiscard]] ImVec2 maintain_thumbnail_aspect_ratio(
      ImVec2* thumbnail_min, VideoDimension dimensions);

  [[nodiscard]] static std::optional<std::string> get_filename_from_url(
      std::string path);

  [[nodiscard]] static int is_extension_compatible(std::string filename);

  [[nodiscard]] static std::optional<std::string> truncate_filename(
      float max_width, const std::string& filename);

  void handle_video_loading_events(const ImVec2& min, const ImVec2& max,
                                   const VideoFile* file, const int index);

  void request_video_preview(const std::string& video_filename);

  static void request_load_thumbnail(ImporterUserData* data,
                                     const std::string& video_filename,
                                     int file_index);

  static void send_thumbnail_to_main_thread(std::optional<Thumbnail*> thumbnail,
                                            int file_index);

  void init_thumbnail_texture(unsigned int* texture_id);
  void refresh_thumbnail_textures(const Thumbnail thumbnail, int file_index);

  static int load_thumbnail_callback(void* userdata);

  [[nodiscard]] inline std::string get_current_directory() {
    return m_user_data->current_directory;
  }

  inline void open_err_dialog(int errcode) {
    m_user_data->status_code = errcode;
    m_error_message = get_error_message_from_errcode(errcode);
  };

  // TODO: Create a JSON file containing the error messages.
  static inline std::string get_error_message_from_errcode(int errcode) {
    switch (errcode) {
      case -1:
        std::cout << "This file does not contain an audio stream.\n";
        break;
      default:
        std::cout << "Imported sucessfully!.\n";
        break;
    }
  }

  static std::unique_ptr<ThumbnailLoader> s_ThumbnailLoader;

 private:
  void render_files(float avail_width, VideoFile* video_file, const int index,
                    int* max_column_nb_ptr);

  void render_directory();

 private:
  std::unique_ptr<ImporterUserData> m_user_data;
  std::unique_ptr<ImporterWindowData> m_window_data;

  SDL_Thread* m_thumbnail_loader_thread;
  std::string m_error_message;
};
}  // namespace YAVE