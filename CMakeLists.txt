cmake_minimum_required(VERSION 3.20)

project(auto_jump_jump_cpp_play_runner LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_link_options(-static-libgcc -static-libstdc++)
endif()

include(FetchContent)

find_package(glfw3 CONFIG QUIET)
if(NOT glfw3_FOUND)
    FetchContent_Declare(
        glfw
        URL https://github.com/glfw/glfw/archive/refs/tags/3.4.tar.gz
    )
    FetchContent_MakeAvailable(glfw)
endif()

set(GLFW_TARGET glfw)
if(TARGET glfw3::glfw)
    set(GLFW_TARGET glfw3::glfw)
elseif(TARGET glfw)
    set(GLFW_TARGET glfw)
elseif(TARGET glfw3)
    set(GLFW_TARGET glfw3)
endif()

add_library(dear-imgui STATIC
    imgui/imgui.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
    imgui/misc/cpp/imgui_stdlib.cpp
    imgui/backends/imgui_impl_glfw.cpp
    imgui/backends/imgui_impl_opengl3.cpp
)

target_include_directories(dear-imgui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui
)

target_link_libraries(dear-imgui PUBLIC ${GLFW_TARGET})

add_library(cpp-play-core STATIC
    src/play_runner/play_runner.cpp
    src/play_runner/logging.cpp
    src/play_runner/config.cpp
    src/play_runner/config_ui_state.cpp
    src/play_runner/config_ui.cpp
    src/play_runner/screen_capture.cpp
    src/play_runner/input_control.cpp
    src/play_runner/image_backend.cpp
    src/play_runner/inference.cpp
    src/play_runner/debug_draw.cpp
    src/play_runner/performance_profiler.cpp
    src/play_runner/fail_template_engine.cpp
    src/play_runner/jump_adaptive.cpp
    src/play_runner/play_session.cpp
)

target_include_directories(cpp-play-core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/opencv/include
)

if(WIN32)
    add_executable(cpp-play-runner WIN32
        src/main.cpp
    )
else()
    add_executable(cpp-play-runner
        src/main.cpp
    )
endif()

target_link_libraries(cpp-play-runner PRIVATE cpp-play-core)

target_link_directories(cpp-play-core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime/lib
)

target_link_libraries(cpp-play-core PUBLIC
    dear-imgui
    ${GLFW_TARGET}
    opengl32
    onnxruntime
    onnxruntime_providers_shared
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/opencv/x64/mingw/staticlib/libopencv_highgui4130.a
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/opencv/x64/mingw/staticlib/libopencv_imgcodecs4130.a
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/opencv/x64/mingw/staticlib/libopencv_imgproc4130.a
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/opencv/x64/mingw/staticlib/libopencv_core4130.a
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/opencv/x64/mingw/staticlib/liblibjpeg-turbo.a
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/opencv/x64/mingw/staticlib/liblibwebp.a
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/opencv/x64/mingw/staticlib/liblibpng.a
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/opencv/x64/mingw/staticlib/liblibtiff.a
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/opencv/x64/mingw/staticlib/liblibopenjp2.a
    D:/msys64/ucrt64/lib/libz.dll.a
)


target_include_directories(cpp-play-runner
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set(DIST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/dist)

add_custom_command(TARGET cpp-play-runner POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${DIST_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${DIST_DIR}/config
    COMMAND ${CMAKE_COMMAND} -E make_directory ${DIST_DIR}/models
    COMMAND ${CMAKE_COMMAND} -E make_directory ${DIST_DIR}/fail
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:cpp-play-runner>
        ${DIST_DIR}/$<TARGET_FILE_NAME:cpp-play-runner>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime/lib/onnxruntime.dll
        ${DIST_DIR}/onnxruntime.dll
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime/lib/onnxruntime_providers_shared.dll
        ${DIST_DIR}/onnxruntime_providers_shared.dll
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime/lib/onnxruntime_providers_cuda.dll
        ${DIST_DIR}/onnxruntime_providers_cuda.dll
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/config/play_runner.json
        ${DIST_DIR}/config/play_runner.json
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/models/yolo11n_last.onnx
        ${DIST_DIR}/models/yolo11n_last.onnx
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/fail/fail_1.png
        ${DIST_DIR}/fail/fail_1.png
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/fail/fail_2.png
        ${DIST_DIR}/fail/fail_2.png
)
