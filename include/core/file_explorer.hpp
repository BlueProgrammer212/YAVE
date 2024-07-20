#pragma once

#include <array>
#include <vector>
#include <iostream>

#include "tinyfiledialogs.h"

namespace YAVE
{
class FileExplorer
{
public:
    FileExplorer() = default;
    ~FileExplorer() {}

    [[nodiscard]] static std::string launch(
        const std::string& current_directory, const std::vector<const char*>& filter_patterns);
};
} // namespace YAVE