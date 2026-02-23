#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace play_runner {

    struct CharacterDetection;
    struct BlockCandidate;
    struct TargetBlock;

    void InitDebugWindow(
        const std::string & title,
        bool enabled,
        double & out_scale
    );

    void SetOverlayVisible(bool visible);

    void RenderDebugFrame(
        bool enabled,
        const std::string & title,
        double monitor_scale,
        const std::vector<std::uint8_t> & cropped_bgr,
        int cropped_w,
        int cropped_h,
        int overlay_x,
        int overlay_y,
        int roi_y_min,
        int roi_y_max,
        const CharacterDetection & character,
        const std::vector<BlockCandidate> & candidates,
        const TargetBlock & target,
        double fps,
        const std::string & capture_method,
        const std::string & status_text
    );

} // namespace play_runner
