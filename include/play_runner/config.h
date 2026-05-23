#pragma once

#include <string>
#include <vector>

#include "play_runner/logging.h"

namespace play_runner {

    struct JumpSegment {
            double a;
            double b;
            double x_start;
            double x_end;
    };

    struct JumpParamsConfig {
            int best_split;
            std::vector<JumpSegment> segments;
            int history_size;
            int fail_discard_count;
            int max_segments;
            int min_len;
    };

    struct CaptureConfig {
            std::string process_name;
            std::string window_title;
            int crop_top;
            int crop_bottom;
            int crop_left;
            int crop_right;
            int roi_top_margin;
            int roi_bottom_margin;
    };

    struct LabConfig {
            int target_l;
            int target_a;
            int target_b;
            int distance_threshold;
            double weight_l;
            double weight_a;
            double weight_b;
    };

    struct JumpConfig {
            double jump_alpha;
            double jump_beta;
            JumpParamsConfig params;
            int foot_center_offset_x;
            int foot_center_offset_y;
            int stable_min_frames;
            int stable_pos_eps;
            bool enable_adaptive_adjustment;
            int press_duration_mode;
    };

    struct OnnxConfig {
            std::string model_path;
            int input_width;
            int input_height;
            float score_threshold;
            float nms_iou_threshold;
    };

    struct FailTemplateConfig {
            std::string template_path;
            std::vector<std::string> template_names;
            double match_threshold;
            int search_region_x_parts;
            int search_region_x_start_part;
            int search_region_x_end_part;
            int search_region_y_parts;
            int search_region_y_start_part;
            int search_region_y_end_part;
            int fast_miss_fallback_threshold;
    };

    struct DisplayConfig {
            bool enable_monitor_window;
    };

    struct LoggingConfig {
            LogLevel level;
            std::string file_path;
    };

    struct AppConfig {
            CaptureConfig capture;
            LabConfig lab;
            JumpConfig jump;
            OnnxConfig onnx;
            FailTemplateConfig fail_template;
            DisplayConfig display;
            LoggingConfig logging;
            bool auto_restart;
            bool debug;
    };

    std::string GetDefaultConfigPath();

    AppConfig LoadConfigOrDefault(const std::string & path);

    bool SaveConfig(const std::string & path, const AppConfig & config);

} // namespace play_runner
