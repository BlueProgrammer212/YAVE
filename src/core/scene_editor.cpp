#include "core/scene_editor.hpp"

namespace YAVE
{
SceneEditor::SceneEditor()
    : subtitle_player(std::make_unique<SubtitlePlayer>())
{
    m_subtitle_editor_user_data.input_buffer.resize(SUBTITLES_BUFFER_SIZE);
    m_subtitle_editor_user_data.subtitle_editor = std::make_unique<SubtitleEditor>();
};

void SceneEditor::init() {}

void SceneEditor::update() {}

void SceneEditor::render_scene_properties_window()
{
    ImGui::Begin("Scene Properties Editor");
    ImGui::End();
}

bool SceneEditor::add_image_button(const std::string& src, unsigned int* tex_id_ptr, ImVec2 size)
{
    ImVec2 min = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    if (*tex_id_ptr == 0) {
        glGenTextures(1, tex_id_ptr);
        glBindTexture(GL_TEXTURE_2D, *tex_id_ptr);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        int width = 0;
        int height = 0;
        int num_of_channels = 0;

        stbi_set_flip_vertically_on_load(true);
        unsigned char* image =
            stbi_load(src.c_str(), &width, &height, &num_of_channels, STBI_rgb_alpha);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    {
        const ImVec4& button_bg_color = ImGui::GetStyleColorVec4(ImGuiCol_Button);
        const ImVec4& button_bg_hovered = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
        const ImVec4& button_bg_active = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);

        const auto default_button_padding = ImVec2(4.0f, 4.0f);
        constexpr float default_button_border_radius = 2.0f;

        draw_list->AddRectFilled(min, min + size + default_button_padding,
            ImGui::ColorConvertFloat4ToU32(button_bg_color), default_button_border_radius);

        glBindTexture(GL_TEXTURE_2D, *tex_id_ptr);

        const auto tex_id_converted = static_cast<std::uintptr_t>(*tex_id_ptr);

        draw_list->AddImage(reinterpret_cast<ImTextureID>(tex_id_converted),
            min + default_button_padding, min + size);

        ImGui::Dummy(size + default_button_padding);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    if (!ImGui::IsMouseHoveringRect(min, min + size)) {
        return false;
    }

    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    return ImGui::IsMouseClicked(ImGuiMouseButton_Left);
}

void SceneEditor::modify_srt_file(const std::string& new_file_data)
{
    std::ofstream srt_output_filestream;
    srt_output_filestream.open(m_active_srt_file);

    if (!srt_output_filestream.is_open()) {
        std::cerr << "Failed to open file: " << m_active_srt_file << std::endl;
        return;
    }

    // Write the new data to the file
    srt_output_filestream << new_file_data;

    // Close the file
    srt_output_filestream.close();

    subtitle_player->update_subtitles(m_active_srt_file);
}

void SceneEditor::render_subtitles_window()
{
    ImGui::Begin("Subtitles");

    ImGui::SameLine();

    static bool is_subtitle_open = false;

    const ImVec2 image_button_size = ImVec2(24, 24);

    static unsigned int refresh_button_tex_id = 0;
    static unsigned int add_timestamp_button_tex_id = 0;
    static unsigned int add_file_tex_id = 0;

    static bool needs_buffer_update = true;

    bool add_file_button_clicked =
        add_image_button("../../assets/open_file_icon.png", &add_file_tex_id, image_button_size);

    ImGui::SameLine();

    bool insert_timestamp_button_clicked =
        add_image_button("../../assets/plus.png", &add_timestamp_button_tex_id, image_button_size);

    ImGui::SameLine();

    bool is_refresh_clicked = add_image_button(
        "../../assets/reload_button.png", &refresh_button_tex_id, image_button_size);

    if (add_file_button_clicked) {
        m_active_srt_file = FileExplorer::launch("C:\\Users\\yayma\\", { "*.srt" });

        if (!m_active_srt_file.empty()) {
            subtitle_player->open_srt_file(m_active_srt_file);
            needs_buffer_update = true;
            is_subtitle_open = true;
        }
    }

    if (is_refresh_clicked && is_subtitle_open) {
        subtitle_player->update_subtitles(m_active_srt_file);
        needs_buffer_update = true;
    }

    const auto& window_size = ImGui::GetWindowSize();
    const auto input_box_size = ImVec2(window_size.x * 0.80f, window_size.y * 0.75f);

    ImGui::PushStyleColor(
        ImGuiCol_FrameBg, ImGui::ColorConvertU32ToFloat4(Color::SUBTITLE_BACKGROUND_COLOR));

    auto& [subtitle_editor, input_buffer, needs_update] = m_subtitle_editor_user_data;

    if (needs_buffer_update) {
        // The input buffer needs to be cleared to ensure that null-termination characters are not
        // appended to random indexes.
        std::fill(input_buffer.begin(), input_buffer.end(), '\0');
        const auto& content = m_subtitle_editor_user_data.subtitle_editor->content;
        std::copy(content.begin(), content.end(), input_buffer.begin());
        needs_buffer_update = false;
    }

    bool is_updated = ImGui::InputTextMultiline("##subtitle_editor", input_buffer.data(),
        input_buffer.size(), input_box_size, SUBTITLE_EDITOR_INPUT_FLAGS);

    if (is_updated) {
        std::size_t len = strnlen(input_buffer.data(), input_buffer.size());
        subtitle_editor->content = std::string(input_buffer.data(), len);
        needs_update = true;
    }

    if (insert_timestamp_button_clicked) {
        const std::string current_timestamp = VideoPlayer::get_current_timestamp_str();
        m_subtitle_editor_user_data.subtitle_editor->content += current_timestamp;
        needs_buffer_update = true;
    }

    ImGui::PopStyleColor();

    std::string word_count_display_content =
        "Words: " + std::to_string(m_subtitle_editor_user_data.subtitle_editor->number_of_words) +
        ", ";
    std::string number_of_dialogues = "Dialogues: " +
        std::to_string(m_subtitle_editor_user_data.subtitle_editor->total_dialogue_nb);

    ImGui::Text(word_count_display_content.c_str());
    ImGui::SameLine();
    ImGui::Text(number_of_dialogues.c_str());

    if (ImGui::IsKeyPressed(ImGuiKey_S) && ImGui::IsKeyDown(ImGuiKey_LeftCtrl)) {
        modify_srt_file(m_subtitle_editor_user_data.subtitle_editor->content);
    }

    ImGui::End();
}

void SceneEditor::render_transition_window()
{
    ImGui::Begin("Transitions");

    ImGui::End();
}

void SceneEditor::render_settings_window()
{
    ImGui::Begin("Settings");

    ImGui::End();
}

void SceneEditor::render()
{
    render_scene_properties_window();
    render_subtitles_window();
    render_transition_window();
    render_settings_window();
}
} // namespace YAVE