#pragma once

#include <iostream>
#include "core/backend/video_player.hpp"

template <typename T>
class Loader {
 public:
  virtual ~Loader(){};

  virtual int open_file(std::string filename, T* userdata) { return 0; }
  
  virtual int find_streams(AVFormatContext* av_format_ctx, T* userdata) {
    return 0;
  }

  virtual void allocate_frame_buffer(T* userdata) {}

 protected:
};