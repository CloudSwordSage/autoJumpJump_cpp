#include "play_runner/config.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include "play_runner/logging.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace play_runner {

    namespace {

        using nlohmann::json;

        template <typename T>
        void AssignIfPresent(const json & j, const char * key, T & out) {
            auto it = j.find(key);
            if (it == j.end() || it->is_null()) {
                return;
            }
            try {
                out = it->get<T>();
            } catch (...) {
            }
        }

        JumpParamsConfig BuildDefaultJumpParams() {
            JumpParamsConfig params{};
            params.best_split = 0;
            params.segments = {};
            params.history_size = 1000;
            params.fail_discard_count = 3;
            params.max_segments = 5;
            params.min_len = 2;
            return params;
        }

        void SanitizeJumpParams(JumpParamsConfig & params) {
            if (params.best_split < 0) {
                params.best_split = 0;
            }
            if (params.history_size <= 0) {
                params.history_size = 1000;
            }
            if (params.fail_discard_count < 0) {
                params.fail_discard_count = 0;
            }
            if (params.max_segments <= 0) {
                params.max_segments = 5;
            }
            if (params.min_len < 2) {
                params.min_len = 2;
            }

            for (auto & seg : params.segments) {
                if (seg.x_start < 0.0) {
                    seg.x_start = 0.0;
                }
                if (seg.x_end < 0.0 && seg.x_end != -1.0) {
                    seg.x_end = 0.0;
                }
            }

            std::sort(
                params.segments.begin(),
                params.segments.end(),
                [](const JumpSegment & left, const JumpSegment & right) {
                    return left.x_start < right.x_start;
                }
            );

            if (params.segments.empty()) {
                params.best_split = 0;
            } else {
                if (params.best_split <= 0) {
                    params.best_split = 1;
                }
                if (params.best_split >
                    static_cast<int>(params.segments.size())) {
                    params.best_split =
                        static_cast<int>(params.segments.size());
                }
            }
        }

        void AssignIfPresent(
            const json & j,
            const char * key,
            JumpParamsConfig & out
        ) {
            auto it = j.find(key);
            if (it == j.end() || it->is_null() || !it->is_object()) {
                return;
            }

            const json & obj = *it;
            AssignIfPresent(obj, "best_split", out.best_split);
            AssignIfPresent(obj, "history_size", out.history_size);
            AssignIfPresent(obj, "fail_discard_count", out.fail_discard_count);
            AssignIfPresent(obj, "max_segments", out.max_segments);
            AssignIfPresent(obj, "min_len", out.min_len);

            auto seg_it = obj.find("segments");
            if (seg_it != obj.end() && seg_it->is_array()) {
                std::vector<JumpSegment> segs;
                segs.reserve(seg_it->size());
                for (const auto & item : *seg_it) {
                    if (!item.is_object()) {
                        continue;
                    }
                    JumpSegment seg{};
                    seg.a = 0.0;
                    seg.b = 0.0;
                    seg.x_start = 0.0;
                    seg.x_end = -1.0;
                    AssignIfPresent(item, "a", seg.a);
                    AssignIfPresent(item, "b", seg.b);
                    AssignIfPresent(item, "x_start", seg.x_start);
                    AssignIfPresent(item, "x_end", seg.x_end);
                    segs.push_back(seg);
                }
                if (!segs.empty()) {
                    out.segments = std::move(segs);
                }
            }
        }

        std::filesystem::path GetExecutableDirectory() {
#ifdef _WIN32
            wchar_t buffer[MAX_PATH];
            DWORD len = ::GetModuleFileNameW(nullptr, buffer, MAX_PATH);
            if (len == 0 || len == MAX_PATH) {
                return std::filesystem::current_path();
            }
            return std::filesystem::path(buffer).parent_path();
#else
            return std::filesystem::current_path();
#endif
        }

        AppConfig BuildDefaultConfig() {
            AppConfig config{};

            config.capture.process_name = u8"跳一跳";
            config.capture.window_title = "Real-time Monitor";
            config.capture.crop_top = 10;
            config.capture.crop_bottom = 10;
            config.capture.crop_left = 10;
            config.capture.crop_right = 10;
            config.capture.roi_top_margin = 250;
            config.capture.roi_bottom_margin = 100;

            config.lab.target_l = 63;
            config.lab.target_a = 141;
            config.lab.target_b = 109;
            config.lab.distance_threshold = 15;
            config.lab.weight_l = 0.3;
            config.lab.weight_a = 1.0;
            config.lab.weight_b = 1.0;

            config.jump.jump_alpha = 2.19;
            config.jump.jump_beta = 0.0;
            config.jump.params = BuildDefaultJumpParams();
            config.jump.foot_center_offset_x = 0;
            config.jump.foot_center_offset_y = 0;
            config.jump.stable_min_frames = 3;
            config.jump.stable_pos_eps = 2;
            config.jump.enable_adaptive_adjustment = false;
            config.jump.press_duration_mode = 0;

            config.onnx.model_path = "models/yolo11n_last.onnx";
            config.onnx.input_width = 640;
            config.onnx.input_height = 640;
            config.onnx.score_threshold = 0.25f;
            config.onnx.nms_iou_threshold = 0.45f;

            config.fail_template.template_path = "fail";
            config.fail_template.template_names = {"fail_1.png", "fail_2.png"};
            config.fail_template.match_threshold = 0.8;
            config.fail_template.search_region_x_parts = 3;
            config.fail_template.search_region_x_start_part = 1;
            config.fail_template.search_region_x_end_part = 3;
            config.fail_template.search_region_y_parts = 3;
            config.fail_template.search_region_y_start_part = 2;
            config.fail_template.search_region_y_end_part = 3;
            config.fail_template.fast_miss_fallback_threshold = 10;

            config.display.enable_monitor_window = true;

            config.logging.level = LogLevel::Info;
            config.logging.file_path = "logs/cpp_play_runner.log";

            config.auto_restart = false;
            config.debug = false;

            return config;
        }

        LogLevel ParseLogLevelJson(
            const json & j,
            const char * key,
            LogLevel def
        ) {
            auto it = j.find(key);
            if (it == j.end() || !it->is_string()) {
                return def;
            }
            std::string raw = it->get<std::string>();
            if (raw == "debug" || raw == "DEBUG") {
                return LogLevel::Debug;
            }
            if (raw == "info" || raw == "INFO") {
                return LogLevel::Info;
            }
            if (raw == "warn" || raw == "WARN" || raw == "warning" ||
                raw == "WARNING") {
                return LogLevel::Warn;
            }
            if (raw == "error" || raw == "ERROR") {
                return LogLevel::Error;
            }
            return def;
        }

        std::string LogLevelToJsonString(LogLevel level) {
            switch (level) {
                case LogLevel::Debug:
                    return "debug";
                case LogLevel::Info:
                    return "info";
                case LogLevel::Warn:
                    return "warn";
                case LogLevel::Error:
                    return "error";
            }
            return "info";
        }

        std::string MakePathRelativeIfUnderBase(
            const std::filesystem::path & base,
            const std::string & raw_path
        ) {
            if (raw_path.empty()) {
                return raw_path;
            }
            std::filesystem::path p(raw_path);
            try {
                if (!p.is_absolute()) {
                    return raw_path;
                }
                std::filesystem::path relative =
                    std::filesystem::relative(p, base);
                if (relative.empty() || relative.is_absolute()) {
                    return raw_path;
                }
                return relative.generic_string();
            } catch (...) {
                return raw_path;
            }
        }

    } // namespace

    std::string GetDefaultConfigPath() {
        std::filesystem::path base = GetExecutableDirectory();
        std::filesystem::path config_path =
            base / "config" / "play_runner.json";
        return config_path.string();
    }

    AppConfig LoadConfigOrDefault(const std::string & path) {
        AppConfig config = BuildDefaultConfig();

        std::filesystem::path p(path);
        if (!p.is_absolute()) {
            p = GetExecutableDirectory() / p;
        }
        if (!std::filesystem::exists(p)) {
            Logger::Instance().Warn(
                "Config file not found, using defaults: " + p.string()
            );
            return config;
        }

        std::ifstream in(p);
        if (!in.is_open()) {
            Logger::Instance().Error(
                "Failed to open config file, using defaults: " + p.string()
            );
            return config;
        }

        json j;
        try {
            in >> j;
        } catch (const std::exception & ex) {
            Logger::Instance().Error(
                std::string("Failed to parse config file, using defaults: ") +
                ex.what()
            );
            return config;
        }

        AssignIfPresent(j, "process_name", config.capture.process_name);
        AssignIfPresent(j, "window_title", config.capture.window_title);
        AssignIfPresent(j, "crop_top", config.capture.crop_top);
        AssignIfPresent(j, "crop_bottom", config.capture.crop_bottom);
        AssignIfPresent(j, "crop_left", config.capture.crop_left);
        AssignIfPresent(j, "crop_right", config.capture.crop_right);
        AssignIfPresent(j, "roi_top_margin", config.capture.roi_top_margin);
        AssignIfPresent(
            j,
            "roi_bottom_margin",
            config.capture.roi_bottom_margin
        );

        AssignIfPresent(j, "lab_l", config.lab.target_l);
        AssignIfPresent(j, "lab_a", config.lab.target_a);
        AssignIfPresent(j, "lab_b", config.lab.target_b);
        AssignIfPresent(
            j,
            "lab_distance_threshold",
            config.lab.distance_threshold
        );
        AssignIfPresent(j, "lab_weight_l", config.lab.weight_l);
        AssignIfPresent(j, "lab_weight_a", config.lab.weight_a);
        AssignIfPresent(j, "lab_weight_b", config.lab.weight_b);

        bool has_jump_params = false;
        auto jump_params_it = j.find("jump_params");
        if (jump_params_it != j.end() && jump_params_it->is_object()) {
            has_jump_params = true;
            AssignIfPresent(j, "jump_params", config.jump.params);
        }

        bool has_jump_alpha = false;
        bool has_jump_beta = false;
        auto jump_alpha_it = j.find("jump_alpha");
        if (jump_alpha_it != j.end() && !jump_alpha_it->is_null()) {
            has_jump_alpha = true;
            AssignIfPresent(j, "jump_alpha", config.jump.jump_alpha);
        }
        auto jump_beta_it = j.find("jump_beta");
        if (jump_beta_it != j.end() && !jump_beta_it->is_null()) {
            has_jump_beta = true;
            AssignIfPresent(j, "jump_beta", config.jump.jump_beta);
        }
        if (has_jump_params && (!has_jump_alpha || !has_jump_beta)) {
            const json & jump_params_obj = *jump_params_it;
            if (!has_jump_alpha) {
                AssignIfPresent(
                    jump_params_obj,
                    "jump_alpha",
                    config.jump.jump_alpha
                );
            }
            if (!has_jump_beta) {
                AssignIfPresent(
                    jump_params_obj,
                    "jump_beta",
                    config.jump.jump_beta
                );
            }
        }

        AssignIfPresent(
            j,
            "foot_center_offset_x",
            config.jump.foot_center_offset_x
        );
        AssignIfPresent(
            j,
            "foot_center_offset_y",
            config.jump.foot_center_offset_y
        );

        (void)has_jump_params;
        SanitizeJumpParams(config.jump.params);
        AssignIfPresent(j, "stable_min_frames", config.jump.stable_min_frames);
        AssignIfPresent(j, "stable_pos_eps", config.jump.stable_pos_eps);
        AssignIfPresent(
            j,
            "enable_adaptive_adjustment",
            config.jump.enable_adaptive_adjustment
        );
        AssignIfPresent(
            j,
            "press_duration_mode",
            config.jump.press_duration_mode
        );
        if (config.jump.press_duration_mode != 0 &&
            config.jump.press_duration_mode != 1) {
            config.jump.press_duration_mode = 0;
        }
        AssignIfPresent(j, "onnx_score_threshold", config.onnx.score_threshold);
        AssignIfPresent(
            j,
            "onnx_nms_iou_threshold",
            config.onnx.nms_iou_threshold
        );

        AssignIfPresent(
            j,
            "enable_monitor_window",
            config.display.enable_monitor_window
        );

        AssignIfPresent(
            j,
            "fail_template_path",
            config.fail_template.template_path
        );
        AssignIfPresent(
            j,
            "fail_template_name",
            config.fail_template.template_names
        );
        AssignIfPresent(
            j,
            "fail_match_threshold",
            config.fail_template.match_threshold
        );
        AssignIfPresent(
            j,
            "fail_search_region_x_parts",
            config.fail_template.search_region_x_parts
        );
        AssignIfPresent(
            j,
            "fail_search_region_x_start_part",
            config.fail_template.search_region_x_start_part
        );
        AssignIfPresent(
            j,
            "fail_search_region_x_end_part",
            config.fail_template.search_region_x_end_part
        );
        AssignIfPresent(
            j,
            "fail_search_region_y_parts",
            config.fail_template.search_region_y_parts
        );
        AssignIfPresent(
            j,
            "fail_search_region_y_start_part",
            config.fail_template.search_region_y_start_part
        );
        AssignIfPresent(
            j,
            "fail_search_region_y_end_part",
            config.fail_template.search_region_y_end_part
        );
        AssignIfPresent(
            j,
            "fail_fast_miss_fallback_threshold",
            config.fail_template.fast_miss_fallback_threshold
        );

        config.logging.level =
            ParseLogLevelJson(j, "log_level", config.logging.level);
        AssignIfPresent(j, "log_file_path", config.logging.file_path);

        AssignIfPresent(j, "auto_restart", config.auto_restart);
        AssignIfPresent(j, "debug", config.debug);

        std::filesystem::path base = GetExecutableDirectory();
        std::filesystem::path log_path(config.logging.file_path);
        if (!log_path.is_absolute()) {
            log_path = base / log_path;
        }
        config.logging.file_path = log_path.string();

        std::filesystem::path model_path(config.onnx.model_path);
        if (!model_path.is_absolute()) {
            model_path = base / model_path;
        }
        config.onnx.model_path = model_path.string();

        std::filesystem::path fail_template_path(
            config.fail_template.template_path
        );
        if (!fail_template_path.is_absolute()) {
            fail_template_path = base / fail_template_path;
        }
        config.fail_template.template_path = fail_template_path.string();

        return config;
    }

    bool SaveConfig(const std::string & path, const AppConfig & config) {
        std::filesystem::path p(path);
        if (!p.is_absolute()) {
            p = GetExecutableDirectory() / p;
        }

        try {
            if (p.has_parent_path()) {
                std::filesystem::create_directories(p.parent_path());
            }
        } catch (...) {
        }

        json j;

        j["process_name"] = config.capture.process_name;
        j["debug"] = config.debug;
        j["window_title"] = config.capture.window_title;
        j["crop_top"] = config.capture.crop_top;
        j["crop_bottom"] = config.capture.crop_bottom;
        j["crop_left"] = config.capture.crop_left;
        j["crop_right"] = config.capture.crop_right;
        j["roi_top_margin"] = config.capture.roi_top_margin;
        j["roi_bottom_margin"] = config.capture.roi_bottom_margin;

        j["lab_l"] = config.lab.target_l;
        j["lab_a"] = config.lab.target_a;
        j["lab_b"] = config.lab.target_b;
        j["lab_distance_threshold"] = config.lab.distance_threshold;
        j["lab_weight_l"] = config.lab.weight_l;
        j["lab_weight_a"] = config.lab.weight_a;
        j["lab_weight_b"] = config.lab.weight_b;

        JumpParamsConfig jump_params = config.jump.params;
        SanitizeJumpParams(jump_params);
        json jump_params_json;
        jump_params_json["best_split"] = jump_params.best_split;
        jump_params_json["history_size"] = jump_params.history_size;
        jump_params_json["fail_discard_count"] = jump_params.fail_discard_count;
        jump_params_json["max_segments"] = jump_params.max_segments;
        jump_params_json["min_len"] = jump_params.min_len;
        jump_params_json["jump_alpha"] = config.jump.jump_alpha;
        jump_params_json["jump_beta"] = config.jump.jump_beta;
        jump_params_json["segments"] = json::array();
        for (const auto & seg : jump_params.segments) {
            json seg_json;
            seg_json["a"] = seg.a;
            seg_json["b"] = seg.b;
            seg_json["x_start"] = seg.x_start;
            seg_json["x_end"] = seg.x_end;
            jump_params_json["segments"].push_back(seg_json);
        }
        j["jump_params"] = jump_params_json;
        j["jump_alpha"] = config.jump.jump_alpha;
        j["jump_beta"] = config.jump.jump_beta;
        j["foot_center_offset_x"] = config.jump.foot_center_offset_x;
        j["foot_center_offset_y"] = config.jump.foot_center_offset_y;
        j["stable_min_frames"] = config.jump.stable_min_frames;
        j["stable_pos_eps"] = config.jump.stable_pos_eps;
        j["enable_adaptive_adjustment"] =
            config.jump.enable_adaptive_adjustment;
        j["press_duration_mode"] = config.jump.press_duration_mode;

        std::filesystem::path base = GetExecutableDirectory();
        j["onnx_model_path"] =
            MakePathRelativeIfUnderBase(base, config.onnx.model_path);
        j["onnx_input_width"] = config.onnx.input_width;
        j["onnx_input_height"] = config.onnx.input_height;
        j["onnx_score_threshold"] = config.onnx.score_threshold;
        j["onnx_nms_iou_threshold"] = config.onnx.nms_iou_threshold;

        j["enable_monitor_window"] = config.display.enable_monitor_window;

        j["fail_template_path"] = MakePathRelativeIfUnderBase(
            base,
            config.fail_template.template_path
        );
        j["fail_template_name"] = config.fail_template.template_names;
        j["fail_match_threshold"] = config.fail_template.match_threshold;
        j["fail_search_region_x_parts"] =
            config.fail_template.search_region_x_parts;
        j["fail_search_region_x_start_part"] =
            config.fail_template.search_region_x_start_part;
        j["fail_search_region_x_end_part"] =
            config.fail_template.search_region_x_end_part;
        j["fail_search_region_y_parts"] =
            config.fail_template.search_region_y_parts;
        j["fail_search_region_y_start_part"] =
            config.fail_template.search_region_y_start_part;
        j["fail_search_region_y_end_part"] =
            config.fail_template.search_region_y_end_part;
        j["fail_fast_miss_fallback_threshold"] =
            config.fail_template.fast_miss_fallback_threshold;

        j["log_level"] = LogLevelToJsonString(config.logging.level);
        j["log_file_path"] =
            MakePathRelativeIfUnderBase(base, config.logging.file_path);

        j["auto_restart"] = config.auto_restart;
        try {
            std::ofstream out(p);
            if (!out.is_open()) {
                return false;
            }
            out << j.dump(2);
            out << '\n';
            return true;
        } catch (...) {
            return false;
        }
    }

} // namespace play_runner
