#pragma once

#include "application.hpp"

namespace YAVE
{
class SceneEditor
{
public:
    SceneEditor() = default;
    ~SceneEditor(){};

    void init();
    void update();
    void render();

private:
};
} // namespace YAVE