#include "play_runner/config.h"

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
            config.jump.stable_min_frames = 3;
            config.jump.stable_pos_eps = 2;

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

            config.debug = false;

            return config;
        }

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

        AssignIfPresent(j, "jump_alpha", config.jump.jump_alpha);
        AssignIfPresent(j, "jump_beta", config.jump.jump_beta);
        AssignIfPresent(j, "stable_min_frames", config.jump.stable_min_frames);
        AssignIfPresent(j, "stable_pos_eps", config.jump.stable_pos_eps);

        AssignIfPresent(j, "onnx_model_path", config.onnx.model_path);
        AssignIfPresent(j, "onnx_input_width", config.onnx.input_width);
        AssignIfPresent(j, "onnx_input_height", config.onnx.input_height);
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

        j["jump_alpha"] = config.jump.jump_alpha;
        j["jump_beta"] = config.jump.jump_beta;
        j["stable_min_frames"] = config.jump.stable_min_frames;
        j["stable_pos_eps"] = config.jump.stable_pos_eps;

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
