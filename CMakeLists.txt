cmake_minimum_required(VERSION 3.5)

project(YAVE CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_definitions(__STDC_CONSTANT_MACROS)

set(VENDOR_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor)
set(LOCAL_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

set(GLEW_LIBRARY_DIR ${VENDOR_DIR}/GLEW/lib)
set(FFMPEG_LIB_DIR ${VENDOR_DIR}/FFmpeg/lib)
set(SDL_LIB_DIR ${VENDOR_DIR}/SDL2/lib/x64)
set(SDL_MIXER_LIB_DIR ${VENDOR_DIR}/SDL2_mixer/lib/x64)

include_directories(
    ${VENDOR_DIR}/GLEW/include
    ${VENDOR_DIR}/glm
    ${VENDOR_DIR}/SDL2/include
    ${VENDOR_DIR}/SDL2_mixer/include
    ${VENDOR_DIR}/SDL2_image/include
    ${VENDOR_DIR}/implot/include
    ${VENDOR_DIR}/SRTParser

    ${LOCAL_INCLUDE_DIR}
    ${LOCAL_INCLUDE_DIR}/imgui
)

file (GLOB SOURCES 
      "src/*.cpp"
      "src/imgui/*.cpp" 
      "src/core/*.cpp"
      "src/core/backend/*.cpp"
      "vendor/implot/src/*.cpp")

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE ${VENDOR_DIR}/FFmpeg/include)

target_link_libraries(
    ${PROJECT_NAME} PRIVATE
    
    ${SDL_LIB_DIR}/SDL2main.lib
    ${SDL_LIB_DIR}/SDL2.lib
    ${VENDOR_DIR}/SDL2_image/lib/x64/SDL2_image.lib

    ${GLEW_LIBRARY_DIR}/glew32s.lib
    opengl32.lib

    ${FFMPEG_LIB_DIR}/avcodec.lib
    ${FFMPEG_LIB_DIR}/avdevice.lib
    ${FFMPEG_LIB_DIR}/avfilter.lib
    ${FFMPEG_LIB_DIR}/avformat.lib
    ${FFMPEG_LIB_DIR}/avutil.lib
    ${FFMPEG_LIB_DIR}/swresample.lib
    ${FFMPEG_LIB_DIR}/swscale.lib
    ${FFMPEG_LIB_DIR}/postproc.lib
)

set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)
