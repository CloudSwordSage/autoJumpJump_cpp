#pragma once

#include <cstdint>
#include <vector>

namespace play_runner {

    struct Size2D {
            int width;
            int height;
    };

    struct Rect {
            int x;
            int y;
            int width;
            int height;
    };

    struct CharacterDetection {
            bool has_body;
            bool has_platform;
            Rect body;
            Rect platform;
            int foot_x;
            int foot_y;
    };

    struct FailMatchResult {
            bool detected;
            int match_x;
            int match_y;
            double match_score;
    };

    class ImageBackend {
        public:
            ImageBackend();

            static Size2D ComputeRoiBounds(
                int image_height,
                int top_margin,
                int bottom_margin,
                int & roi_y_min,
                int & roi_y_max
            );

            static void BuildLabMask(
                const std::uint8_t * bgr,
                int width,
                int height,
                const double target_lab[3],
                const double weights[3],
                double distance_threshold,
                int roi_y_min,
                int roi_y_max,
                int morph_kernel_size,
                std::vector<std::uint8_t> & out_mask
            );

            static void BuildLabMaskFromLab(
                const float * lab,
                int width,
                int height,
                const double target_lab[3],
                const double weights[3],
                double distance_threshold,
                int roi_y_min,
                int roi_y_max,
                int morph_kernel_size,
                std::vector<std::uint8_t> & out_mask
            );

            static CharacterDetection ExtractCharacter(
                const std::vector<std::uint8_t> & mask,
                int width,
                int height,
                int min_area
            );

            static FailMatchResult MatchFailTemplate(
                const std::uint8_t * gray,
                int width,
                int height,
                const std::vector<std::uint8_t> & fail_template,
                int template_width,
                int template_height,
                double match_threshold,
                int search_region_x_parts,
                int search_region_x_start_part,
                int search_region_x_end_part,
                int search_region_y_parts,
                int search_region_y_start_part,
                int search_region_y_end_part
            );

            static FailMatchResult MatchFailTemplateFast(
                const std::uint8_t * gray,
                int width,
                int height,
                const std::vector<std::uint8_t> & fail_template,
                int template_width,
                int template_height,
                double match_threshold,
                int search_region_x_parts,
                int search_region_x_start_part,
                int search_region_x_end_part,
                int search_region_y_parts,
                int search_region_y_start_part,
                int search_region_y_end_part
            );
    };

} // namespace play_runner
