#include "play_runner/config_ui_state.h"

#include "play_runner/logging.h"

#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#endif

namespace play_runner {

    namespace {

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

        void NormalizePaths(AppConfig & config) {
            std::filesystem::path base = GetExecutableDirectory();

            std::filesystem::path log_path(config.logging.file_path);
            if (!log_path.empty() && !log_path.is_absolute()) {
                log_path = base / log_path;
                config.logging.file_path = log_path.string();
            }

            std::filesystem::path model_path(config.onnx.model_path);
            if (!model_path.empty() && !model_path.is_absolute()) {
                model_path = base / model_path;
                config.onnx.model_path = model_path.string();
            }

            std::filesystem::path fail_path(config.fail_template.template_path);
            if (!fail_path.empty() && !fail_path.is_absolute()) {
                fail_path = base / fail_path;
                config.fail_template.template_path = fail_path.string();
            }
        }

    } // namespace

    ConfigUiState::ConfigUiState(const AppConfig & initial_config)
        : exit_requested(false), config_(initial_config) {
    }

    AppConfig ConfigUiState::GetConfigSnapshot() const {
        std::lock_guard<std::mutex> lock(config_mutex_);
        return config_;
    }

    void ConfigUiState::ApplyConfig(const AppConfig & config) {
        AppConfig normalized = config;
        NormalizePaths(normalized);
        {
            std::lock_guard<std::mutex> lock(config_mutex_);
            config_ = normalized;
        }
        Logger::Instance().SetLevel(normalized.logging.level);
        Logger::Instance().SetFilePath(normalized.logging.file_path);
    }

    UiFrameSnapshot ConfigUiState::GetFrameSnapshot() const {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        return frame_;
    }

    void ConfigUiState::UpdateFrame(const CapturedFrame & frame) {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        frame_.seq += 1;
        frame_.width = frame.width;
        frame_.height = frame.height;
        frame_.dpi_scale_x = frame.dpi_scale_x;
        frame_.dpi_scale_y = frame.dpi_scale_y;
        frame_.bgra = frame.bgra;
    }

} // namespace play_runner
