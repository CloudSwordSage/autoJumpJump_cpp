#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <windows.h>

namespace play_runner {

    struct WindowInfo {
            std::string title;
            std::string class_name;
            std::string process_name;
            unsigned long process_id;
            RECT rect;
            std::uint64_t handle;
    };

    struct CapturedFrame {
            int width;
            int height;
            double dpi_scale_x;
            double dpi_scale_y;
            RECT window_rect;
            std::vector<std::uint8_t> bgra;
    };

    std::vector<WindowInfo> EnumerateWindows();

    bool FindWindow(
        const std::string & process_name,
        const std::string & window_title,
        WindowInfo & out
    );

    CapturedFrame CaptureWindowFrame(const WindowInfo & window);

    POINT LogicalToPhysicalPoint(
        const POINT & logical,
        double dpi_scale_x,
        double dpi_scale_y
    );

    POINT PhysicalToLogicalPoint(
        const POINT & physical,
        double dpi_scale_x,
        double dpi_scale_y
    );

    const char * GetLastCaptureMethod();

} // namespace play_runner
