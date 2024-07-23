#include "core/application.hpp"

#include "core/debugger.hpp"
#include "core/importer.hpp"
#include "core/scene_editor.hpp"
#include "core/timeline.hpp"

namespace YAVE
{
bool Application::is_running = true;
unsigned int Application::s_FrameTexID = 0;
int Application::s_PreferredImageFormat = 0;
AVRational Application::s_Timebase = AVRational{ 1, 60 };

Application::Application()
    : window(nullptr)
    , m_tools(std::make_unique<Tools>())
    , m_style_config(UIStyleConfig(15.f, 1.0f))
    , m_waveform_loader(std::make_unique<WaveformLoader>())
    , m_current_subtitle_gizmo(std::make_unique<SubtitleGizmo>())
    , m_video_loading_thread(nullptr)
{
}

Application::~Application()
{
    ImGui_ImplSDL2_Shutdown();
    ImGui_ImplOpenGL3_Shutdown();

    ImGui::DestroyContext();
    ImPlot::DestroyContext();

    SDL_GL_DeleteContext(m_gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

#pragma region Init Functions

int Application::init_imgui(std::string version)
{
    auto& [font_size, video_zoom_factor, current_zoom_factor, target_zoom_factor, main_font] =
        m_style_config;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    auto& io = ImGui::GetIO();
    (void) io;

    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard |
        ImGuiConfigFlags_ViewportsEnable;

    ImGui::StyleColorsDark();

    {
        ImVec4* colors = ImGui::GetStyle().Colors;
        Color::dark_theme(colors);

        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowPadding = ImVec2(8.00f, 8.00f);
        style.FramePadding = ImVec2(5.00f, 2.00f);
        style.CellPadding = ImVec2(6.00f, 6.00f);
        style.ItemSpacing = ImVec2(6.00f, 6.00f);
        style.ItemInnerSpacing = ImVec2(6.00f, 6.00f);
        style.TouchExtraPadding = ImVec2(0.00f, 0.00f);
        style.IndentSpacing = 25;
        style.ScrollbarSize = 15;
        style.GrabMinSize = 10;
        style.WindowBorderSize = 1;
        style.ChildBorderSize = 1;
        style.PopupBorderSize = 1;
        style.FrameBorderSize = 1;
        style.TabBorderSize = 1;
        style.WindowRounding = 7;
        style.ChildRounding = 4;
        style.FrameRounding = 3;
        style.PopupRounding = 4;
        style.ScrollbarRounding = 9;
        style.GrabRounding = 3;
        style.LogSliderDeadzone = 4;
        style.TabRounding = 4;
    }

    ImGui_ImplSDL2_InitForOpenGL(window, m_gl_context);
    ImGui_ImplOpenGL3_Init(version.c_str());

    io.Fonts->AddFontDefault();
    io.FontGlobalScale = 1.0f;

    main_font = io.Fonts->AddFontFromFileTTF(
        "../../assets/sans-serif.ttf", font_size, NULL, io.Fonts->GetGlyphRangesJapanese());

    IM_ASSERT(main_font != NULL);

    return 0;
}

void Application::init_video_processor()
{
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
    scene_editor->set_video_player(m_video_processor);

    m_video_processor->s_VideoAvailabilityCond = SDL_CreateCond();

    m_video_loading_thread = SDL_CreateThread(
        &Application::file_loading_listener, "Video Loading Thread", &m_video_processor);
}

void Application::init_video_texture()
{
    if (s_FrameTexID != 0) {
        return;
    }

    glGenTextures(1, &s_FrameTexID);
    glBindTexture(GL_TEXTURE_2D, s_FrameTexID);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1 << 0);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glGetInternalformativ(
        GL_TEXTURE_2D, GL_RGB, GL_TEXTURE_IMAGE_FORMAT, 1, &s_PreferredImageFormat);
}

std::string Application::configure_sdl()
{
    // TODO: Load a JSON file that contains these configurations.
    constexpr int ANTIALIASING_FACTOR = 2;

#if defined(__APPLE__)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,
        SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG); // Always required on Mac
#endif

    const char* glsl_version = "#version 430 core";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    // SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);

    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, ANTIALIASING_FACTOR);

    return std::string(glsl_version);
}

int Application::init()
{
    if (SDL_Init(SDL_INIT_EVERYTHING) != 0) {
        std::cout << "Failed to initialize SDL: " << SDL_GetError() << "\n";
        return -1;
    }

    std::string glsl_version = configure_sdl();

    auto window_flags =
        static_cast<SDL_WindowFlags>(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN);

    window = SDL_CreateWindow("YAVE (Yet Another Video Editor)", SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED, 1280, 720, window_flags);

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

    init_imgui(glsl_version);
    init_video_processor();

    // av_log_set_level(AV_LOG_DEBUG);

    return 0;
}

#pragma endregion Init Functions

void Application::update()
{
    static float last_time = 0.0f;
    const float time = static_cast<float>(SDL_GetTicks());

    static float delta_time = time - last_time;

    {
        const auto& [timeline, importer, scene_editor, debugger] = *m_tools;

        timeline->update(delta_time);
        importer->update();
        scene_editor->update();
        debugger->update();

        debugger->time_base = av_q2d(s_Timebase);
    }

    last_time = time;
}

#pragma region Event Callbacks

void Application::refresh_thumbnails()
{
    auto* thumbnail_data = static_cast<Thumbnail*>(m_event.user.data1);
    auto* url = static_cast<std::string*>(m_event.user.data2);

    // Update the OpenGL texture after obtaining the thumbnail data.
    m_tools->importer->refresh_thumbnail_textures(*thumbnail_data, *url);

    av_free(thumbnail_data->framebuffer);
    delete thumbnail_data;
    delete url;
}

void Application::seek_to_requested_timestamp()
{
    const auto* requested_timestamp = static_cast<float*>(m_event.user.data1);
    bool result = m_video_processor->seek_frame(static_cast<float>(*requested_timestamp),
        m_video_processor->get_flags() & VideoFlags::IS_PAUSED);

    if (result != 0) {
        std::cerr << "Failed to jump to timestamp: " << *requested_timestamp << "\n";
    }

    delete requested_timestamp;
}

void Application::refresh_timeline_waveform()
{
    auto* waveform_data = static_cast<Waveform*>(m_event.user.data1);
    auto* dest_segment_index = static_cast<int*>(m_event.user.data2);

    m_tools->timeline->update_segment_waveform(waveform_data->audio_data, *dest_segment_index);
    m_waveform_loader->free_waveform(waveform_data);

    delete dest_segment_index;
}

void Application::update_texture()
{
    static int last_width = m_video_size.width;
    static int last_height = m_video_size.height;

    auto& framebuffer = m_video_processor->get_framebuffer();

    if (!framebuffer) {
        return;
    }

    auto video_state = m_video_processor->get_videostate();
    m_video_size.width = video_state->dimensions.x;
    m_video_size.height = video_state->dimensions.y;

    if (m_video_size.width != last_width || m_video_size.height != last_height) {
        glTexImage2D(GL_TEXTURE_2D, 0, s_PreferredImageFormat, m_video_size.width,
            m_video_size.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, framebuffer);

        last_width = m_video_size.width;
        last_height = m_video_size.height;

        return;
    }

    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_video_size.width, m_video_size.height, GL_RGBA,
        GL_UNSIGNED_BYTE, framebuffer);
}

#pragma endregion Event Callbacks

#pragma region Video Player

void Application::enqueue_video_request(const std::string& filename, const float timestamp)
{
    auto* video_preview_request = new VideoPreviewRequest();
    video_preview_request->path = filename.c_str();
    video_preview_request->presentation_timestamp = timestamp;
    VideoPlayer::s_VideoFileQueue.push_back(video_preview_request);
    SDL_CondSignal(m_video_processor->s_VideoAvailabilityCond);
}

[[nodiscard]] const std::int64_t Application::get_file_duration(const std::string& filename) const
{
    // Get the duration by allocating a dummy format context.
    AVFormatContext* dummy_format_context = avformat_alloc_context();

    if (!dummy_format_context) {
        std::cout << "[Video Preview Request]: Failed to allocate a format context.\n";
        return -1;
    }

    if (avformat_open_input(&dummy_format_context, filename.c_str(), nullptr, nullptr) != 0) {
        std::cout << "[Video Preview Request]: Failed to open the input: " << filename << "\n";
        return -1;
    }

    // Copy the duration first before deallocating the format context.
    const std::int64_t duration = dummy_format_context->duration;

    avformat_close_input(&dummy_format_context);
    avformat_free_context(dummy_format_context);

    return duration;
}

void Application::add_segment_to_timeline(const std::string& filename)
{
    static float cumulative_timestamp = 0.0f;
    static int video_count = -1;

    std::int64_t duration = get_file_duration(filename);

    if (duration < 0) {
        return;
    }

    const auto duration_in_seconds = static_cast<float>(duration) / AV_TIME_BASE;

    const std::optional<std::string> current_filename =
        Importer::get_filename_from_url(std::string(filename));

    if (!current_filename.has_value()) {
        return;
    }

    const auto end_timestamp = cumulative_timestamp + duration_in_seconds;

    // Obtain the thumbnail texture id.
    const std::int64_t file_index = m_tools->importer->find_file_by_url(filename);
    const auto& importer_user_data = m_tools->importer->get_user_data();
    const auto& current_video_file = importer_user_data->file_paths[file_index];

    constexpr unsigned int DEFAULT_TRACK_POSITION = 1;

    const auto new_segment =
        Segment{ DEFAULT_TRACK_POSITION, current_filename.value(), cumulative_timestamp,
            end_timestamp, {}, current_video_file.texture_id, current_video_file.resolution };

    m_tools->timeline->add_segment(new_segment);

    // When there are more than one video, the video will be concatenated.
    if (++video_count > 0) {
        enqueue_video_request(filename, cumulative_timestamp);
    } else {
        open_first_video(filename, m_video_processor);
    }

    cumulative_timestamp += duration_in_seconds;

    // Enqueue this file to the waveform loader and the video player.
    m_waveform_loader->request_audio_waveform(filename.c_str());

    init_video_texture();
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Application::open_first_video(
    const std::string& filename, std::shared_ptr<VideoPlayer> video_player)
{
    if (video_player->allocate_video(filename.c_str()) != 0) {
        return;
    }

    if (video_player->play_video(&s_Timebase) != 0) {
        return;
    }
}

void Application::handle_zooming(float delta_time)
{
    static ImGuiIO* io = &ImGui::GetIO();

    if (ImGui::IsWindowHovered()) {
        m_style_config.target_zoom_factor += io->MouseWheel * 0.1f;
    }

    m_style_config.target_zoom_factor = std::clamp(m_style_config.target_zoom_factor, 0.2f, 3.0f);

    // Smooth zooming using linear interpolation
    static constexpr float interpolation_speed = 5.0f;

    m_style_config.current_zoom_factor +=
        (m_style_config.target_zoom_factor - m_style_config.current_zoom_factor) *
        interpolation_speed * delta_time;

    // Ensure we do not overshoot
    if (std::abs(m_style_config.current_zoom_factor - m_style_config.target_zoom_factor) < 0.001f) {
        m_style_config.current_zoom_factor = m_style_config.target_zoom_factor;
    }
}

const ImVec2 Application::maintain_video_aspect_ratio(ImVec2* display_min)
{
    static ImVec2 image_position;
    auto& [width, height] = m_video_size;

    const ImVec2 content_region = ImGui::GetContentRegionAvail();
    const auto texture_aspect_ratio = static_cast<float>(width) / static_cast<float>(height);
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
    *display_min = ImGui::GetCursorScreenPos();
    display_min->x += (content_region.x - display_size.x) * 0.5f;
    display_min->y += (content_region.y - display_size.y) * 0.5f;

    return display_size;
}

void Application::render_subtitles(const ImVec2& min, const ImVec2& max)
{
    if (m_current_subtitle_gizmo->is_empty) {
        return;
    }

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    ImGui::SetWindowFontScale(1.8f * m_style_config.current_zoom_factor);
    const std::string sample_subtitle_text = m_current_subtitle_gizmo->content;
    const ImVec2 text_size = ImGui::CalcTextSize(sample_subtitle_text.c_str());

    auto subtitle_display_min = min;
    subtitle_display_min.x += (max.x - text_size.x) * 0.5f;
    subtitle_display_min.y += (max.y - text_size.y) * 0.9f;

    const ImVec2 bg_min = subtitle_display_min - ImVec2(5.0f, 5.0f);
    const ImVec2 bg_max = text_size + ImVec2(5.0f, 5.0f);

    draw_list->AddRectFilled(bg_min, bg_min + bg_max, IM_COL32(0, 0, 0, 200), 1.0f);
    draw_list->AddText(subtitle_display_min, IM_COL32_WHITE, sample_subtitle_text.c_str());
    ImGui::SetWindowFontScale(1.0f);
}

int Application::file_loading_listener(void* userdata)
{
    auto* video_processor = static_cast<std::shared_ptr<VideoPlayer>*>(userdata);

    while (Application::is_running) {
        SDL_LockMutex(PacketQueue::s_GlobalMutex);

        if (VideoPlayer::s_VideoFileQueue.empty()) {
            SDL_CondWait(VideoPlayer::s_VideoAvailabilityCond, PacketQueue::s_GlobalMutex);
            SDL_UnlockMutex(PacketQueue::s_GlobalMutex);
            continue;
        }

        auto* latest_video = VideoPlayer::s_VideoFileQueue.front();
        auto current_video_state = (*video_processor)->get_videostate();

        // If there are no prior videos, preview the video first and create an output format
        // context.


        VideoPlayer::s_VideoFileQueue.pop_front();

        SDL_UnlockMutex(PacketQueue::s_GlobalMutex);
    }

    return 0;
}

void Application::render_video_preview()
{
    constexpr auto VIDEO_PREVIEW_FLAGS =
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::Begin("Video Preview", nullptr, VIDEO_PREVIEW_FLAGS);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    ImVec2 display_min;

    const ImVec2& display_max = maintain_video_aspect_ratio(&display_min);
    const auto& tex_id_ptr = static_cast<uintptr_t>(s_FrameTexID);

    draw_list->AddImage(
        reinterpret_cast<ImTextureID>(tex_id_ptr), display_min, display_min + display_max);
    render_subtitles(display_min, display_max);

    ImGui::End();
}

#pragma endregion Video Player

#pragma region Render Function

void Application::render()
{
    const auto& [timeline, importer, scene_editor, debugger] = *m_tools;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    ImGui::PushFont(m_style_config.main_font);

    ImGuiViewport* main_viewport = ImGui::GetMainViewport();
    ImGui::DockSpaceOverViewport(main_viewport->ID);

    timeline->render();
    importer->render();
    scene_editor->render();
    debugger->render();

    render_video_preview();

    ImGui::PopFont();
    ImGui::Render();

    ImGuiIO& io = ImGui::GetIO();

    glViewport(0, 0, (int) io.DisplaySize.x, (int) io.DisplaySize.y);
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

[[nodiscard]] std::string Application::get_requested_url(void* userdata)
{
    const auto& filename = static_cast<std::string*>(userdata);
    std::string directory = m_tools->importer->get_current_directory();
    return directory + *filename;
}

bool Application::handle_custom_events()
{
    bool is_custom_event = true;

    switch (m_event.type) {
    case CustomVideoEvents::FF_REFRESH_VIDEO_EVENT:
        glBindTexture(GL_TEXTURE_2D, s_FrameTexID);
        update_texture();
        glBindTexture(GL_TEXTURE_2D, 0);
        break;

    case CustomVideoEvents::FF_LOAD_NEW_VIDEO_EVENT: {
        const std::string& url = get_requested_url(m_event.user.data1);
        add_segment_to_timeline(url);
    } break;

    case CustomVideoEvents::FF_LOAD_SRT_FILE_EVENT: {
        const auto* subtitle_editor_data = static_cast<SubtitleEditor*>(m_event.user.data1);
        m_tools->scene_editor->update_input_buffer(subtitle_editor_data);
        delete subtitle_editor_data;
    } break;

    case CustomVideoEvents::FF_REFRESH_THUMBNAIL:
        refresh_thumbnails();
        break;

    case CustomVideoEvents::FF_REFRESH_WAVEFORM:
        refresh_timeline_waveform();
        break;

    case CustomVideoEvents::FF_REFRESH_SUBTITLES: {
        const auto* userdata = static_cast<SubtitleGizmo*>(m_event.user.data1);
        m_current_subtitle_gizmo->content = userdata->content;
        m_current_subtitle_gizmo->is_empty = userdata->is_empty;
    } break;

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

void Application::handle_keyup_events()
{
    switch (m_event.key.keysym.sym) {
    case SDL_KeyCode::SDLK_SPACE: {
        m_video_processor->pause_video();
    } break;
    }
}

void Application::handle_events()
{
    int ret = SDL_WaitEvent(&m_event);

    if (handle_custom_events()) {
        return;
    };

    ImGui_ImplSDL2_ProcessEvent(&m_event);

    if (m_event.type == SDL_QUIT) {
        is_running = false;
    } else if (m_event.type == SDL_WINDOWEVENT && m_event.window.event == SDL_WINDOWEVENT_CLOSE &&
        m_event.window.windowID == SDL_GetWindowID(window)) {
        is_running = false;
    } else if (m_event.type == SDL_KEYUP) {
        // handle_keyup_events();
    }
}
#pragma endregion Event Handler

} // namespace YAVE