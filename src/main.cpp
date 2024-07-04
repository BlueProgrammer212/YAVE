#include "core/application.hpp"

#undef main

int SDL_main(int argc, char* argv[]) {
  YAVE::Application app;

  if (app.init() != 0) {
    return -1;
  };

  while (app.is_running) {
    app.update();
    app.render();
    app.handle_events();
  }

  return 0;
}