#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "play_runner/config.h"
#include "play_runner/logging.h"

namespace play_runner {

    class ConfigUiState;

    class ConfigUi {
        public:
            struct FontCandidate {
                    std::string label;
                    std::string path;
                    int font_no;
            };

            ConfigUi(
                std::shared_ptr<ConfigUiState> ui_state,
                std::string config_path
            );

            int Run();

        private:
            enum class Category {
                Global,
                RoleLab,
                Jump,
                Model,
                FailDetect,
                Log,
            };

            std::shared_ptr<ConfigUiState> ui_state_;
            std::string config_path_;

            Category category_;
            AppConfig working_config_;

            std::string status_text_;

            std::uint64_t last_frame_seq_;
            std::uint64_t last_log_seq_;

            unsigned int frame_texture_;
            int frame_texture_w_;
            int frame_texture_h_;
            float right_preview_column_width_;
            std::vector<std::uint8_t> frame_rgba_;
            std::vector<Logger::Entry> log_entries_;

            bool auto_scroll_logs_;
            std::vector<FontCandidate> font_candidates_;
            int selected_font_index_;
            float font_size_px_;
            bool font_dirty_;
            bool open_success_popup_;
            std::string success_popup_message_;
            bool open_calibration_popup_;
            std::uint64_t last_calibration_result_id_;
            std::uint64_t pending_calibration_request_id_;

            void DrawUi();
            void DrawLeftCategory();
            void DrawCenterEditor();
            void DrawRightPreview();
            void DrawBottomLogs(float height);

            void ReloadFromFile();
            void SaveToFile();
            void SaveToFileAndApply();

            void UpdateFrameTexture();

            void RebuildFonts();
    };

} // namespace play_runner
