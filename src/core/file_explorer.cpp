#include "core/file_explorer.hpp"

namespace YAVE
{
[[nodiscard]] std::string FileExplorer::launch(
    const std::string& current_directory, const std::vector<const char*>& filter_patterns)
{
    const char* title = "Open a File";
    const char* defaultPath = nullptr;

    const char* selectedFile = tinyfd_openFileDialog(
        title, defaultPath, filter_patterns.size(), filter_patterns.data(), nullptr, 0);

    if (selectedFile) {
        std::cout << "Selected file: " << selectedFile << std::endl;
        return std::string(selectedFile);
    }

    std::cout << "No file selected" << std::endl;
    return "";
}
} // namespace YAVE