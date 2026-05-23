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

            std::uint64_t RequestFootCalibration();
            bool ConsumeFootCalibrationRequest(std::uint64_t & out_request_id);
            void PublishFootCalibrationResult(
                std::uint64_t request_id,
                int offset_x,
                int offset_y
            );
            bool ConsumeFootCalibrationResult(
                std::uint64_t & inout_last_result_id,
                int & out_offset_x,
                int & out_offset_y
            );

            std::atomic<bool> exit_requested;

        private:
            mutable std::mutex config_mutex_;
            AppConfig config_;

            mutable std::mutex frame_mutex_;
            UiFrameSnapshot frame_;

            std::atomic<std::uint64_t> foot_calib_request_id_{0};
            std::atomic<std::uint64_t> foot_calib_consumed_request_id_{0};
            std::atomic<std::uint64_t> foot_calib_result_id_{0};
            std::atomic<int> foot_calib_offset_x_{0};
            std::atomic<int> foot_calib_offset_y_{0};
    };

} // namespace play_runner
