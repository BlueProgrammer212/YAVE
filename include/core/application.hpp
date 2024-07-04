#pragma once

#include <iostream>
#include <memory>

#define IMGUI_DEFINE_MATH_OPERATORS

#include <imgui/imgui.h>
#include <imgui/imgui_impl_opengl3.h>
#include <imgui/imgui_impl_sdl2.h>

#include <implot.h>
#include <implot_internal.h>

#include "core/audio_player.hpp"
#include "core/video_player.hpp"

#include <SDL_image.h>

namespace YAVE {
class Debugger;
class Importer;
class SceneEditor;
class Timeline;
class VideoPlayer;
class ThumbnailLoader;

struct UIStyleConfig {
  UIStyleConfig(float t_font_size, float default_video_zoom)
      : font_size(t_font_size),
        main_font(nullptr),
        video_zoom_factor(default_video_zoom) {}

  float font_size;
  float video_zoom_factor;
  float current_zoom_factor = 1.0f;
  float target_zoom_factor = 1.0f;
  ImFont* main_font;
};

struct Tools {
  std::unique_ptr<Timeline> timeline{};
  std::unique_ptr<Importer> importer{};
  std::unique_ptr<SceneEditor> scene_editor{};
  std::unique_ptr<Debugger> debugger{};
};

struct VideoResolution {
  int width;
  int height;
};

class Application {
 public:
  Application();
  ~Application();

  void preview_video(const char* filename);

  int init();
  int initImGui();

  void update();

  void render();
  void render_video_preview();

  void handle_events();
  void handle_keyup_events();
  bool handle_custom_events();

  const ImVec2 maintain_video_aspect_ratio();
  void handle_zooming(float delta_time);

  void update_texture();

  SDL_Window* window;
  SDL_GLContext m_gl_context;

  static bool is_running;

 private:
  unsigned int m_frame_tex_id = 0;

  [[nodiscard]] std::string getRequestedURL(void* userdata);

  std::unique_ptr<Tools> m_tools;
  std::shared_ptr<VideoPlayer> m_video_processor;
  std::unique_ptr<ThumbnailLoader> m_thumbnail_loader;

  UIStyleConfig m_style_config;

  SDL_Event m_event;

  VideoResolution m_video_size;
  AVRational m_time_base;
};
}  // namespace YAVE