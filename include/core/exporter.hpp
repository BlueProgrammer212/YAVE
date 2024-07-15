#pragma once

#include "core/application.hpp"
#include <iostream>

namespace YAVE
{
class Exporter
{
public:
    Exporter() = default;
    ~Exporter();

    void init();
    void update();
    void render();  
private:
};
} // namespace YAVE