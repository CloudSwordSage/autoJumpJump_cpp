#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

#include "play_runner/config.h"
#include "play_runner/screen_capture.h"

namespace play_runner {

    struct UiFrameSnapshot {
            std::uint64_t seq = 0;
            int width = 0;
            int height = 0;
            float dpi_scale_x = 1.0f;
            float dpi_scale_y = 1.0f;
            std::vector<std::uint8_t> bgra;
    };

    class ConfigUiState {
        public:
            explicit ConfigUiState(const AppConfig & initial_config);

            AppConfig GetConfigSnapshot() const;
            void ApplyConfig(const AppConfig & config);

            UiFrameSnapshot GetFrameSnapshot() const;
            void UpdateFrame(const CapturedFrame & frame);

            std::atomic<bool> exit_requested;

        private:
            mutable std::mutex config_mutex_;
            AppConfig config_;

            mutable std::mutex frame_mutex_;
            UiFrameSnapshot frame_;
    };

} // namespace play_runner

