#include "core/scene_editor.hpp"

namespace YAVE
{
SceneEditor::SceneEditor()
    : m_subtitle_player(std::make_unique<SubtitlePlayer>())
    , m_subtitle_input_buffer({}){};

void SceneEditor::init() {}

void SceneEditor::update() {}

void SceneEditor::render_scene_properties_window()
{
    ImGui::Begin("Scene Properties Editor");
    ImGui::End();
}

void SceneEditor::render_subtitles_window()
{
    ImGui::Begin("Subtitles");

    if (ImGui::Button("Add Subtitle at Current Timestamp")) {
    }

    ImGui::SameLine();

    if (ImGui::Button("Load a .srt file")) {
    }

    const auto& window_size = ImGui::GetWindowSize();

    ImGui::PushStyleColor(
        ImGuiCol_FrameBg, ImGui::ColorConvertU32ToFloat4(Color::SUBTITLE_BACKGROUND_COLOR));

    const auto input_box_size = ImVec2(window_size.x * 0.80f, window_size.y * 0.75f);

    ImGui::InputTextMultiline("##subtitle_input", m_subtitle_input_buffer.data(),
        SUBTITLES_BUFFER_SIZE, input_box_size, ImGuiInputTextFlags_None);

    ImGui::PopStyleColor();

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