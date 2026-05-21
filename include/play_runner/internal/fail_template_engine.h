#pragma once

#include <cstdint>
#include <vector>

#include "play_runner/config.h"
#include "play_runner/image_backend.h"

namespace play_runner {

    struct FailTemplateDetection {
            bool detected = false;
            FailMatchResult match{false, -1, -1, 0.0};
            int template_width = 0;
            int template_height = 0;
    };

    class FailTemplateEngine {
        public:
            explicit FailTemplateEngine(const FailTemplateConfig & config);

            void Load();

            bool HasTemplates() const;

            FailTemplateDetection Detect(
                const std::uint8_t * gray,
                int width,
                int height
            );

            void ResetRuntimeState();

        private:
            struct TemplateImage {
                    std::vector<std::uint8_t> pixels;
                    int width = 0;
                    int height = 0;
            };

            FailTemplateConfig config_;
            std::vector<TemplateImage> templates_;

            int fast_miss_count_ = 0;

            bool has_last_fail_log_ = false;
            int last_fail_log_x_ = -1;
            int last_fail_log_y_ = -1;
            double last_fail_log_score_ = 0.0;
    };

} // namespace play_runner

