#include "core/application.hpp"

#undef main

int SDL_main(int argc, char* argv[])
{
    YAVE::Application app;

    if (app.init() != 0) {
        return -1;
    };

    while (app.s_IsRunning) {
        app.handle_events();
        app.update();
        app.render();
    }

    return 0;
}