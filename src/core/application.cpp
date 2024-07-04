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
      m_style_config(UIStyleConfig(15.f, 1.0f)) {}

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

int Application::initImGui() {
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
  ImGui_ImplOpenGL3_Init("#version 330 core");

  io.Fonts->AddFontDefault();
  io.FontGlobalScale = 1.0f;

  main_font =
      io.Fonts->AddFontFromFileTTF("../../assets/sans-serif.ttf", font_size,
                                   NULL, io.Fonts->GetGlyphRangesJapanese());

  IM_ASSERT(main_font != NULL);

  return 0;
}

int Application::init() {
  if (SDL_Init(SDL_INIT_EVERYTHING) != 0) {
    std::cout << "Failed to initialize SDL: " << SDL_GetError() << "\n";
    return -1;
  }

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

  SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
  SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 2);

  window =
      SDL_CreateWindow("YAVE (Yet Another Video Editor)",
                       SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1280,
                       720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);

  if (!window) {
    std::cerr << "Failed to create a window.\n";
    return -2;
  }

  SDL_Surface* icon_surface = IMG_Load("../../assets/logo.png");
  SDL_SetWindowIcon(window, icon_surface);
  SDL_FreeSurface(icon_surface);

  m_gl_context = SDL_GL_CreateContext(window);

  glewExperimental = GL_TRUE;

  if (glewInit() != GLEW_OK) {
    std::cerr << "Failed to initialize GLEW.\n";
    return -3;
  }

  SDL_GL_SetSwapInterval(1);

  glEnable(GL_TEXTURE_2D);

  initImGui();

  const SampleRate sample_rate = std::make_pair<int, int>(44100, 44100);
  m_video_processor = std::make_shared<VideoPlayer>(sample_rate);

  {
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

  return 0;
}

#pragma endregion Init Functions

#pragma region Update Function

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

void Application::update_texture() {
  const VideoState* video_state = m_video_processor->get_videostate();
  m_video_size.width = video_state->dimensions.x;
  m_video_size.height = video_state->dimensions.y;

  glBindTexture(GL_TEXTURE_2D, m_frame_tex_id);

  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, m_video_size.width,
               m_video_size.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               m_video_processor->get_framebuffer());

  glBindTexture(GL_TEXTURE_2D, 0);
}

#pragma endregion Update Function

#pragma region Video Player

void Application::preview_video(const char* filename) {
  if (m_video_processor->open_video(filename) != 0) {
    std::cout << "Failed to open the video.\n";
    return;
  };

  if (m_video_processor->play_video(&m_time_base) != 0) {
    std::cerr << "Failed to play the video.\n";
    return;
  };

  auto* video_state = m_video_processor->get_videostate();

  m_video_size.width = video_state->dimensions.x;
  m_video_size.height = video_state->dimensions.y;

  // Initialize the frame OpenGL texture.
  if (m_frame_tex_id == 0) {
    glGenTextures(1, &m_frame_tex_id);
    glBindTexture(GL_TEXTURE_2D, m_frame_tex_id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
  }

  const std::int64_t duration = m_video_processor->getDuration();

  glBindTexture(GL_TEXTURE_2D, 0);

  if (!AudioPlayer::isValidRational(m_time_base)) {
    return;
  }

  const auto duration_in_seconds = static_cast<float>(duration) / AV_TIME_BASE;

  const std::optional<std::string> current_filename =
      Importer::get_filename_from_url(std::string(filename));

  if (current_filename.has_value()) {
    m_tools->timeline->add_segment(
        Segment{1, current_filename.value(), 0.0f, duration_in_seconds});
  }
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

  ImGui_ImplSDL2_NewFrame();
  ImGui_ImplOpenGL3_NewFrame();
  ImGui::NewFrame();

  ImGui::PushFont(m_style_config.main_font);

  const auto& main_viewport = ImGui::GetMainViewport();
  ImGui::DockSpaceOverViewport(main_viewport);

  timeline->render();
  importer->render();
  scene_editor->render();
  debugger->render();

  render_video_preview();

  ImGui::PopFont();

  ImGui::Render();

  static ImGuiIO& io = ImGui::GetIO();

  int display_w, display_h;
  SDL_GL_GetDrawableSize(window, &display_w, &display_h);

  io.DisplaySize =
      ImVec2(static_cast<float>(display_w), static_cast<float>(display_h));

  glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
  glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
  glClear(GL_COLOR_BUFFER_BIT);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(0.0, display_w, display_h, 0.0, -1.0, 1.0);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

  if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
    SDL_Window* backup_window = SDL_GL_GetCurrentWindow();
    SDL_GLContext backup_gl_context = SDL_GL_GetCurrentContext();
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();
    SDL_GL_MakeCurrent(backup_window, backup_gl_context);
  }

  SDL_GL_SwapWindow(window);
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
    case CustomVideoEvents::FF_REFRESH_EVENT:
      update_texture();
      break;

    case CustomVideoEvents::FF_LOAD_NEW_VIDEO_EVENT: {
      const std::string& url = getRequestedURL(m_event.user.data1);
      preview_video(url.c_str());
    } break;

    case CustomVideoEvents::FF_REFRESH_THUMBNAIL: {
      auto* thumbnail_data = static_cast<Thumbnail*>(m_event.user.data1);
      int* file_index = static_cast<int*>(m_event.user.data2);

      // Update the OpenGL texture after obtaining the thumbnail data.
      m_tools->importer->refresh_thumbnail_textures(*thumbnail_data,
                                                    *file_index);

      av_free(thumbnail_data->framebuffer);
      delete (thumbnail_data, file_index);
    } break;

    case CustomVideoEvents::FF_TOGGLE_PAUSE_EVENT: {
      m_video_processor->pause_video();
    } break;

    case CustomVideoEvents::FF_MUTE_AUDIO_EVENT:
      m_video_processor->toggle_audio();
      break;

    case CustomVideoEvents::FF_SEEK_TO_TIMESTAMP_EVENT: {
      const auto* requested_timestamp = static_cast<float*>(m_event.user.data1);

      bool ret = m_video_processor->seek_frame(
          static_cast<float>(*requested_timestamp));

      if (ret != 0) {
        std::cerr << "Failed to jump to timestamp: " << *requested_timestamp
                  << "\n";
      }

      delete requested_timestamp;
    } break;

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
  } else if (m_event.type == SDL_KEYUP) {
    handle_keyup_events();
  }
}
#pragma endregion Event Handler

}  // namespace YAVE