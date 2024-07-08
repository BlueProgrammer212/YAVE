#include "core/application.hpp"

#include "core/debugger.hpp"
#include "core/importer.hpp"
#include "core/scene_editor.hpp"
#include "core/timeline.hpp"

namespace YAVE {
bool Application::is_running = true;

Application::Application()
    : window(nullptr),
      m_tools(std::make_unique<Tools>()),
      m_style_config(UIStyleConfig(15.f, 1.0f)),
      m_waveform_loader(std::make_unique<WaveformLoader>()) {}

Application::~Application() {
  ImGui_ImplSDL2_Shutdown();
  ImGui_ImplOpenGL3_Shutdown();

  ImGui::DestroyContext();
  ImPlot::DestroyContext();

  SDL_GL_DeleteContext(m_gl_context);
  SDL_DestroyWindow(window);
  SDL_Quit();
}

#pragma region Init Functions

int Application::initImGui(std::string version) {
  auto& [font_size, video_zoom_factor, current_zoom_factor, target_zoom_factor,
         main_font] = m_style_config;

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImPlot::CreateContext();

  auto& io = ImGui::GetIO();
  (void)io;

  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable |
                    ImGuiConfigFlags_NavEnableKeyboard |
                    ImGuiConfigFlags_ViewportsEnable;

  ImGui::StyleColorsDark();

  {
    ImGuiStyle* style = &ImGui::GetStyle();
    style->WindowRounding = 10.0f;
    style->WindowBorderSize = 2.5f;
    style->TabRounding = 5.0f;
    style->WindowPadding = ImVec2(15.0f, 15.0f);
  }

  ImGui_ImplSDL2_InitForOpenGL(window, m_gl_context);
  ImGui_ImplOpenGL3_Init(version.c_str());

  io.Fonts->AddFontDefault();
  io.FontGlobalScale = 1.0f;

  main_font =
      io.Fonts->AddFontFromFileTTF("../../assets/sans-serif.ttf", font_size,
                                   NULL, io.Fonts->GetGlyphRangesJapanese());

  IM_ASSERT(main_font != NULL);

  return 0;
}

void Application::initVideoProcessor() {
  const SampleRate sample_rate = std::make_pair<int, int>(44100, 44100);
  m_video_processor = std::make_shared<VideoPlayer>(sample_rate);

  auto& [timeline, importer, scene_editor, debugger] = *m_tools;

  timeline = std::make_unique<Timeline>();
  importer = std::make_unique<Importer>();
  scene_editor = std::make_unique<SceneEditor>();
  debugger = std::make_unique<Debugger>();

  debugger->video_state = m_video_processor->get_videostate();

  timeline->init();
  importer->init();
  scene_editor->init();

  timeline->video_processor = m_video_processor;
}

void Application::initVideoTexture() {
  if (m_frame_tex_id != 0) {
    return;
  }

  glGenTextures(1, &m_frame_tex_id);
  glBindTexture(GL_TEXTURE_2D, m_frame_tex_id);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1 << 0);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

std::string Application::configure_sdl() {
  // TODO: Load a JSON file that contains these configurations.
  constexpr int ANTIALIASING_FACTOR = 2;

  // Decide GL+GLSL versions
#if defined(IMGUI_IMPL_OPENGL_ES2)
  // GL ES 2.0 + GLSL 100
  const char* glsl_version = "#version 100";
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#elif defined(__APPLE__)
  // GL 3.2 Core + GLSL 150
  const char* glsl_version = "#version 150";
  SDL_GL_SetAttribute(
      SDL_GL_CONTEXT_FLAGS,
      SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);  // Always required on Mac
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
  // GL 4.3 + GLSL 430
  const char* glsl_version = "#version 430 core";
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#endif

  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

  // SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);

  SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
  SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, ANTIALIASING_FACTOR);

  return std::string(glsl_version);
}

int Application::init() {
  if (SDL_Init(SDL_INIT_EVERYTHING) != 0) {
    std::cout << "Failed to initialize SDL: " << SDL_GetError() << "\n";
    return -1;
  }

  std::string glsl_version = configure_sdl();

  auto window_flags = static_cast<SDL_WindowFlags>(
      SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);

  window = SDL_CreateWindow("YAVE (Yet Another Video Editor)",
                            SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                            1280, 720, window_flags);

  if (!window) {
    std::cerr << "Failed to create a window.\n";
    return -2;
  }

  SDL_Surface* icon_surface = IMG_Load("../../assets/logo.png");
  SDL_SetWindowIcon(window, icon_surface);
  SDL_FreeSurface(icon_surface);

  m_gl_context = SDL_GL_CreateContext(window);
  SDL_GL_MakeCurrent(window, m_gl_context);
  SDL_GL_SetSwapInterval(1);

  glewExperimental = GL_TRUE;

  if (glewInit() != GLEW_OK) {
    std::cerr << "Failed to initialize GLEW.\n";
    return -3;
  }

  glEnable(GL_TEXTURE_2D);
  glEnable(GL_DEPTH_TEST);

  initImGui(glsl_version);
  initVideoProcessor();

  return 0;
}

#pragma endregion Init Functions

void Application::update() {
  static float last_time = 0.0f;
  const float time = static_cast<float>(SDL_GetTicks());

  static float delta_time = time - last_time;

  {
    const auto& [timeline, importer, scene_editor, debugger] = *m_tools;

    timeline->update(delta_time);
    importer->update();
    scene_editor->update();
    debugger->update();

    debugger->time_base = av_q2d(m_time_base);
  }

  last_time = time;
}

#pragma region Event Callbacks

void Application::refresh_thumbnails() {
  auto* thumbnail_data = static_cast<Thumbnail*>(m_event.user.data1);
  int* file_index = static_cast<int*>(m_event.user.data2);

  // Update the OpenGL texture after obtaining the thumbnail data.
  m_tools->importer->refresh_thumbnail_textures(*thumbnail_data, *file_index);

  av_free(thumbnail_data->framebuffer);
  delete (thumbnail_data, file_index);
}

void Application::seek_to_requested_timestamp() {
  const auto* requested_timestamp = static_cast<float*>(m_event.user.data1);

  bool result =
      m_video_processor->seek_frame(static_cast<float>(*requested_timestamp));

  if (result != 0) {
    std::cerr << "Failed to jump to timestamp: " << *requested_timestamp
              << "\n";
  }

  delete requested_timestamp;
}

void Application::refresh_timeline_waveform() {
  auto* waveform_data = static_cast<Waveform*>(m_event.user.data1);
  auto* dest_segment_index = static_cast<int*>(m_event.user.data2);

  m_tools->timeline->update_segment_waveform(waveform_data->audio_data,
                                             *dest_segment_index);

  m_waveform_loader->free_waveform(waveform_data);
  delete dest_segment_index;
}

void Application::update_texture() {
  static int last_width = m_video_size.width;
  static int last_height = m_video_size.height;

  auto video_state = m_video_processor->get_videostate();
  m_video_size.width = video_state->dimensions.x;
  m_video_size.height = video_state->dimensions.y;

  // If the frame needs to be resized, use glTexImage2D instead.
  if (m_video_size.width != last_width || m_video_size.height != last_height) {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, m_video_size.width,
                 m_video_size.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    last_width = m_video_size.width;
    last_height = m_video_size.height;

    return;
  }

  // Using glTexSubImage2D when the color format is not changed.
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_video_size.width,
                  m_video_size.height, GL_RGBA, GL_UNSIGNED_BYTE,
                  m_video_processor->get_framebuffer());

  last_width = m_video_size.width;
  last_height = m_video_size.height;
}

#pragma endregion Event Callbacks

#pragma region Video Player

void Application::preview_video(const char* filename) {
  static float accumulated_duration = 0.0f;

  if (m_video_processor->open_video(filename) != 0) {
    std::cout << "Failed to open the video.\n";
    return;
  };

  if (m_video_processor->play_video(&m_time_base) != 0) {
    std::cerr << "Failed to play the video.\n";
    return;
  };

  auto video_state = m_video_processor->get_videostate();

  m_video_size.width = video_state->dimensions.x;
  m_video_size.height = video_state->dimensions.y;

  initVideoTexture();

  // Allocate memory for the new video dimensions.
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, m_video_size.width,
               m_video_size.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               m_video_processor->get_framebuffer());

  const std::int64_t duration = m_video_processor->getDuration();

  glBindTexture(GL_TEXTURE_2D, 0);

  if (!AudioPlayer::isValidRational(m_time_base)) {
    return;
  }

  const auto duration_in_seconds = static_cast<float>(duration) / AV_TIME_BASE;

  const std::optional<std::string> current_filename =
      Importer::get_filename_from_url(std::string(filename));

  if (!current_filename.has_value()) {
    return;
  }

  const auto end_timestamp = accumulated_duration + duration_in_seconds;

  const auto new_segment = Segment{
      1, current_filename.value(), accumulated_duration, end_timestamp, {}};

  m_tools->timeline->add_segment(new_segment);
  accumulated_duration += duration_in_seconds;

  // Enqueue this file to the waveform loader.
  m_waveform_loader->request_audio_waveform(filename);
}

void Application::handle_zooming(float delta_time) {
  static ImGuiIO* io = &ImGui::GetIO();

  if (ImGui::IsWindowHovered()) {
    m_style_config.target_zoom_factor += io->MouseWheel * 0.1f;
  }

  m_style_config.target_zoom_factor =
      std::clamp(m_style_config.target_zoom_factor, 0.2f, 3.0f);

  // Smooth zooming using linear interpolation
  static constexpr float interpolation_speed = 5.0f;

  m_style_config.current_zoom_factor +=
      (m_style_config.target_zoom_factor - m_style_config.current_zoom_factor) *
      interpolation_speed * delta_time;

  // Ensure we do not overshoot
  if (std::abs(m_style_config.current_zoom_factor -
               m_style_config.target_zoom_factor) < 0.001f) {
    m_style_config.current_zoom_factor = m_style_config.target_zoom_factor;
  }
}

const ImVec2 Application::maintain_video_aspect_ratio() {
  static ImVec2 image_position;
  auto& [width, height] = m_video_size;

  const ImVec2 content_region = ImGui::GetContentRegionAvail();
  const auto texture_aspect_ratio =
      static_cast<float>(width) / static_cast<float>(height);
  const float content_aspect_ratio = content_region.x / content_region.y;

  ImVec2 display_size = content_region;

  if (content_aspect_ratio > texture_aspect_ratio) {
    display_size.x = content_region.y * texture_aspect_ratio;
    display_size.y = content_region.y;
  } else {
    display_size.x = content_region.x;
    display_size.y = content_region.x / texture_aspect_ratio;
  }

  handle_zooming(ImGui::GetIO().DeltaTime);

  display_size *= m_style_config.current_zoom_factor;

  // Recalculate the image position after zooming.
  image_position = ImGui::GetCursorScreenPos();
  image_position.x += (content_region.x - display_size.x) * 0.5f;
  image_position.y += (content_region.y - display_size.y) * 0.5f;
  ImGui::SetCursorScreenPos(image_position);

  return display_size;
}

void Application::render_video_preview() {
  constexpr auto VIDEO_PREVIEW_FLAGS =
      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

  ImGui::Begin("Video Preview", nullptr, VIDEO_PREVIEW_FLAGS);

  const ImVec2& display_size = maintain_video_aspect_ratio();
  const auto& tex_id_ptr = static_cast<uintptr_t>(m_frame_tex_id);

  ImGui::Image(reinterpret_cast<ImTextureID>(tex_id_ptr), display_size);

  ImGui::End();
}

#pragma endregion Video Player

#pragma region Render Function

void Application::render() {
  const auto& [timeline, importer, scene_editor, debugger] = *m_tools;

  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplSDL2_NewFrame();
  ImGui::NewFrame();
  ImGui::PushFont(m_style_config.main_font);

  const auto& main_viewport = ImGui::GetMainViewport();
  ImGui::DockSpaceOverViewport(ImGui::GetID(main_viewport), main_viewport);

  timeline->render();
  importer->render();
  scene_editor->render();
  debugger->render();

  render_video_preview();

  ImGui::PopFont();
  ImGui::Render();

  ImGuiIO& io = ImGui::GetIO();

  glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

  if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
    SDL_Window* backup_window = SDL_GL_GetCurrentWindow();
    SDL_GLContext backup_gl_context = SDL_GL_GetCurrentContext();
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();
    SDL_GL_MakeCurrent(backup_window, backup_gl_context);
  }

  SDL_GL_SwapWindow(window);
  ImGui::EndFrame();
}

#pragma endregion Render Function

#pragma region Event Handler

[[nodiscard]] std::string Application::getRequestedURL(void* userdata) {
  const auto& filename = static_cast<std::string*>(userdata);
  std::string directory = m_tools->importer->get_current_directory();
  return directory + *filename;
}

bool Application::handle_custom_events() {
  bool is_custom_event = true;

  switch (m_event.type) {
    case CustomVideoEvents::FF_REFRESH_VIDEO_EVENT:
      glBindTexture(GL_TEXTURE_2D, m_frame_tex_id);
      update_texture();
      glBindTexture(GL_TEXTURE_2D, 0);
      break;

    case CustomVideoEvents::FF_LOAD_NEW_VIDEO_EVENT: {
      const std::string& url = getRequestedURL(m_event.user.data1);
      preview_video(url.c_str());
    } break;

    case CustomVideoEvents::FF_REFRESH_THUMBNAIL:
      refresh_thumbnails();
      break;

    case CustomVideoEvents::FF_REFRESH_WAVEFORM:
      refresh_timeline_waveform();
      break;

    case CustomVideoEvents::FF_TOGGLE_PAUSE_EVENT:
      m_video_processor->pause_video();
      break;

    case CustomVideoEvents::FF_MUTE_AUDIO_EVENT:
      m_video_processor->toggle_audio();
      break;

    case CustomVideoEvents::FF_SEEK_TO_TIMESTAMP_EVENT:
      seek_to_requested_timestamp();
      break;

    default:
      is_custom_event = false;
      break;
  }

  return is_custom_event;
}

void Application::handle_keyup_events() {
  switch (m_event.key.keysym.sym) {
    case SDL_KeyCode::SDLK_SPACE: {
      m_video_processor->pause_video();
    } break;
  }
}

void Application::handle_events() {
  int ret = SDL_WaitEvent(&m_event);

  if (handle_custom_events()) {
    return;
  };

  ImGui_ImplSDL2_ProcessEvent(&m_event);

  if (m_event.type == SDL_QUIT) {
    is_running = false;
  } else if (m_event.type == SDL_WINDOWEVENT &&
             m_event.window.event == SDL_WINDOWEVENT_CLOSE &&
             m_event.window.windowID == SDL_GetWindowID(window)) {
    is_running = false;
  } else if (m_event.type == SDL_KEYUP) {
    handle_keyup_events();
  }
}
#pragma endregion Event Handler

}  // namespace YAVE