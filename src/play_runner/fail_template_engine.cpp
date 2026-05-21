#include "play_runner/internal/fail_template_engine.h"

#include "play_runner/logging.h"

#include <cmath>
#include <filesystem>
#include <iomanip>
#include <sstream>

#include <opencv2/opencv.hpp>

namespace play_runner {

    namespace {

        constexpr double kFailScoreLogPrecision = 0.001;

    } // namespace

    FailTemplateEngine::FailTemplateEngine(const FailTemplateConfig & config)
        : config_(config) {
    }

    void FailTemplateEngine::Load() {
        templates_.clear();

        std::filesystem::path base_path(config_.template_path);
        if (!config_.template_names.empty()) {
            for (const std::string & name : config_.template_names) {
                std::filesystem::path template_file = base_path / name;
                cv::Mat templ =
                    cv::imread(template_file.string(), cv::IMREAD_GRAYSCALE);
                if (templ.empty()) {
                    Logger::Instance().Warn(
                        "Fail template file not found: " + template_file.string()
                    );
                    continue;
                }

                TemplateImage loaded{};
                loaded.width = templ.cols;
                loaded.height = templ.rows;
                loaded.pixels.assign(
                    templ.data,
                    templ.data + templ.total() * templ.elemSize()
                );
                templates_.push_back(std::move(loaded));

                Logger::Instance().Info(
                    "Fail template loaded: " + template_file.string()
                );
            }
            return;
        }

        cv::Mat templ = cv::imread(base_path.string(), cv::IMREAD_GRAYSCALE);
        if (templ.empty()) {
            Logger::Instance().Warn(
                "Fail template file not found: " + base_path.string()
            );
            return;
        }

        TemplateImage loaded{};
        loaded.width = templ.cols;
        loaded.height = templ.rows;
        loaded.pixels.assign(
            templ.data,
            templ.data + templ.total() * templ.elemSize()
        );
        templates_.push_back(std::move(loaded));

        Logger::Instance().Info("Fail template loaded: " + base_path.string());
    }

    bool FailTemplateEngine::HasTemplates() const {
        return !templates_.empty();
    }

    FailTemplateDetection FailTemplateEngine::Detect(
        const std::uint8_t * gray,
        int width,
        int height
    ) {
        FailTemplateDetection out{};

        if (templates_.empty()) {
            has_last_fail_log_ = false;
            return out;
        }

        out.match = FailMatchResult{false, -1, -1, 0.0};
        out.template_width = 0;
        out.template_height = 0;

        int fast_fallback_threshold = config_.fast_miss_fallback_threshold;
        bool should_run_slow = (fast_fallback_threshold <= 0);

        for (const TemplateImage & templ : templates_) {
            if (templ.pixels.empty() || templ.width <= 0 || templ.height <= 0) {
                continue;
            }
            out.match = ImageBackend::MatchFailTemplateFast(
                gray,
                width,
                height,
                templ.pixels,
                templ.width,
                templ.height,
                config_.match_threshold,
                config_.search_region_x_parts,
                config_.search_region_x_start_part,
                config_.search_region_x_end_part,
                config_.search_region_y_parts,
                config_.search_region_y_start_part,
                config_.search_region_y_end_part
            );
            if (out.match.detected) {
                out.template_width = templ.width;
                out.template_height = templ.height;
                fast_miss_count_ = 0;
                break;
            }
        }

        if (!out.match.detected && !should_run_slow) {
            fast_miss_count_ += 1;
            if (fast_miss_count_ >= fast_fallback_threshold) {
                should_run_slow = true;
                fast_miss_count_ = 0;
            }
        }

        if (!out.match.detected && should_run_slow) {
            for (const TemplateImage & templ : templates_) {
                if (templ.pixels.empty() || templ.width <= 0 ||
                    templ.height <= 0) {
                    continue;
                }
                out.match = ImageBackend::MatchFailTemplate(
                    gray,
                    width,
                    height,
                    templ.pixels,
                    templ.width,
                    templ.height,
                    config_.match_threshold,
                    config_.search_region_x_parts,
                    config_.search_region_x_start_part,
                    config_.search_region_x_end_part,
                    config_.search_region_y_parts,
                    config_.search_region_y_start_part,
                    config_.search_region_y_end_part
                );
                if (out.match.detected) {
                    out.template_width = templ.width;
                    out.template_height = templ.height;
                    break;
                }
            }
        }

        out.detected = out.match.detected;
        if (out.detected) {
            double rounded_score =
                std::round(out.match.match_score / kFailScoreLogPrecision) *
                kFailScoreLogPrecision;
            bool should_log = !has_last_fail_log_ ||
                              last_fail_log_x_ != out.match.match_x ||
                              last_fail_log_y_ != out.match.match_y ||
                              std::abs(last_fail_log_score_ - rounded_score) >
                                  (kFailScoreLogPrecision / 2.0);
            if (should_log) {
                std::ostringstream score_oss;
                score_oss << std::fixed << std::setprecision(3) << rounded_score;
                Logger::Instance().Warn(
                    "Fail detected! Match score: " + score_oss.str() + " at (" +
                    std::to_string(out.match.match_x) + ", " +
                    std::to_string(out.match.match_y) + ")"
                );
                has_last_fail_log_ = true;
                last_fail_log_x_ = out.match.match_x;
                last_fail_log_y_ = out.match.match_y;
                last_fail_log_score_ = rounded_score;
            }
        } else {
            has_last_fail_log_ = false;
        }

        return out;
    }

    void FailTemplateEngine::ResetRuntimeState() {
        fast_miss_count_ = 0;
        has_last_fail_log_ = false;
        last_fail_log_x_ = -1;
        last_fail_log_y_ = -1;
        last_fail_log_score_ = 0.0;
    }

} // namespace play_runner

