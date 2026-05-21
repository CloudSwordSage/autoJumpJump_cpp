#include "play_runner/image_backend.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <cstring>

#include <opencv2/opencv.hpp>

namespace play_runner {

    namespace {

        struct Point {
                int x;
                int y;
        };

        void FloodFill(
            const std::vector<std::uint8_t> & mask,
            int width,
            int height,
            int sx,
            int sy,
            std::vector<int> & out_indices
        ) {
            const int total = width * height;
            std::vector<std::uint8_t> visited(
                static_cast<std::size_t>(total),
                0
            );
            std::vector<Point> stack;
            stack.reserve(1024);
            stack.push_back(Point{sx, sy});

            while (!stack.empty()) {
                Point p = stack.back();
                stack.pop_back();

                int x = p.x;
                int y = p.y;
                if (x < 0 || x >= width || y < 0 || y >= height)
                    continue;

                int idx = y * width + x;
                if (visited[idx])
                    continue;
                visited[idx] = 1;

                if (mask[idx] == 0)
                    continue;

                out_indices.push_back(idx);

                stack.push_back(Point{x + 1, y});
                stack.push_back(Point{x - 1, y});
                stack.push_back(Point{x, y + 1});
                stack.push_back(Point{x, y - 1});
            }
        }

    } // namespace

    ImageBackend::ImageBackend() {
    }

    Size2D ImageBackend::ComputeRoiBounds(
        int image_height,
        int top_margin,
        int bottom_margin,
        int & roi_y_min,
        int & roi_y_max
    ) {
        if (image_height > top_margin + bottom_margin) {
            roi_y_min = top_margin;
            roi_y_max = image_height - bottom_margin;
        } else {
            roi_y_min = 0;
            roi_y_max = image_height;
        }
        Size2D s{};
        s.width = 0;
        s.height = roi_y_max - roi_y_min;
        return s;
    }

    void ImageBackend::BuildLabMask(
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
    ) {
        if (!bgr) {
            throw std::invalid_argument("bgr is null");
        }
        if (width <= 0 || height <= 0) {
            throw std::invalid_argument("invalid width/height");
        }
        if (roi_y_min < 0)
            roi_y_min = 0;
        if (roi_y_max > height)
            roi_y_max = height;
        if (roi_y_min > roi_y_max)
            roi_y_min = roi_y_max;

        out_mask.assign(static_cast<std::size_t>(width * height), 0);

        // 使用 OpenCV 加速 BGR->Lab 转换
        cv::Mat
            bgr_mat(height, width, CV_8UC3, const_cast<std::uint8_t *>(bgr));
        cv::Mat lab_mat;
        cv::cvtColor(bgr_mat, lab_mat, cv::COLOR_BGR2Lab);

        const float t_l = static_cast<float>(target_lab[0]);
        const float t_a = static_cast<float>(target_lab[1]);
        const float t_b = static_cast<float>(target_lab[2]);
        const float w_l = static_cast<float>(weights[0]);
        const float w_a = static_cast<float>(weights[1]);
        const float w_b = static_cast<float>(weights[2]);
        const float dist_thresh = static_cast<float>(distance_threshold);

        // 逐像素计算 Lab 距离
        for (int y = roi_y_min; y < roi_y_max; ++y) {
            const cv::Vec3b * lab_row = lab_mat.ptr<cv::Vec3b>(y);
            std::uint8_t * mask_row = out_mask.data() + y * width;

            for (int x = 0; x < width; ++x) {
                const cv::Vec3b & lab = lab_row[x];
                float d_l = (static_cast<float>(lab[0]) - t_l) * w_l;
                float d_a = (static_cast<float>(lab[1]) - t_a) * w_a;
                float d_b = (static_cast<float>(lab[2]) - t_b) * w_b;
                float dist = std::sqrt(d_l * d_l + d_a * d_a + d_b * d_b);

                mask_row[x] = (dist <= dist_thresh) ? 255 : 0;
            }
        }

        // 使用 OpenCV 加速形态学操作
        if (morph_kernel_size == 3) {
            cv::Mat mask_mat(height, width, CV_8UC1, out_mask.data());
            cv::Mat kernel =
                cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
            cv::morphologyEx(mask_mat, mask_mat, cv::MORPH_OPEN, kernel);
            cv::morphologyEx(mask_mat, mask_mat, cv::MORPH_CLOSE, kernel);
        }
    }

    void ImageBackend::BuildLabMaskFromLab(
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
    ) {
        if (!lab) {
            throw std::invalid_argument("lab is null");
        }
        if (width <= 0 || height <= 0) {
            throw std::invalid_argument("invalid width/height");
        }
        if (roi_y_min < 0)
            roi_y_min = 0;
        if (roi_y_max > height)
            roi_y_max = height;
        if (roi_y_min > roi_y_max)
            roi_y_min = roi_y_max;

        out_mask.assign(static_cast<std::size_t>(width * height), 0);

        const double w_l = weights[0];
        const double w_a = weights[1];
        const double w_b = weights[2];
        const double t0 = target_lab[0];
        const double t1 = target_lab[1];
        const double t2 = target_lab[2];

        const int total = width * height;
        for (int y = 0; y < height; ++y) {
            bool in_roi = (y >= roi_y_min && y < roi_y_max);
            for (int x = 0; x < width; ++x) {
                int idx = y * width + x;
                if (!in_roi) {
                    out_mask[idx] = 0;
                    continue;
                }
                int offset = idx * 3;
                if (offset + 2 >= total * 3) {
                    out_mask[idx] = 0;
                    continue;
                }
                double L = static_cast<double>(lab[offset + 0]);
                double A = static_cast<double>(lab[offset + 1]);
                double B = static_cast<double>(lab[offset + 2]);

                double d_l = (L - t0) * w_l;
                double d_a = (A - t1) * w_a;
                double d_b = (B - t2) * w_b;
                double dist = std::sqrt(d_l * d_l + d_a * d_a + d_b * d_b);

                out_mask[idx] = (dist <= distance_threshold) ? 255 : 0;
            }
        }

        if (morph_kernel_size == 3) {
            cv::Mat mask_mat(height, width, CV_8UC1, out_mask.data());
            cv::Mat kernel =
                cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
            cv::morphologyEx(mask_mat, mask_mat, cv::MORPH_OPEN, kernel);
            cv::morphologyEx(mask_mat, mask_mat, cv::MORPH_CLOSE, kernel);
        }
    }

    CharacterDetection ImageBackend::ExtractCharacter(
        const std::vector<std::uint8_t> & mask,
        int width,
        int height,
        int min_area
    ) {
        if (width <= 0 || height <= 0) {
            throw std::invalid_argument("invalid width/height");
        }
        if (static_cast<int>(mask.size()) != width * height) {
            throw std::invalid_argument("mask size mismatch");
        }

        CharacterDetection result{};
        result.has_body = false;
        result.has_platform = false;
        result.body = Rect{0, 0, 0, 0};
        result.platform = Rect{0, 0, 0, 0};
        result.foot_x = 0;
        result.foot_y = 0;

        const int total = width * height;
        std::vector<std::uint8_t> visited(static_cast<std::size_t>(total), 0);

        struct Region {
                int area;
                Rect rect;
        };
        std::vector<Region> regions;
        regions.reserve(32);

        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                int idx = y * width + x;
                if (visited[idx] || mask[idx] == 0) {
                    continue;
                }

                std::vector<int> indices;
                indices.reserve(256);
                FloodFill(mask, width, height, x, y, indices);
                for (int index : indices) {
                    visited[index] = 1;
                }

                if (indices.empty()) {
                    continue;
                }

                int min_x = width;
                int max_x = 0;
                int min_y = height;
                int max_y = 0;
                for (int index : indices) {
                    int py = index / width;
                    int px = index - py * width;
                    if (px < min_x)
                        min_x = px;
                    if (px > max_x)
                        max_x = px;
                    if (py < min_y)
                        min_y = py;
                    if (py > max_y)
                        max_y = py;
                }

                int w_rect = max_x - min_x + 1;
                int h_rect = max_y - min_y + 1;
                int area = w_rect * h_rect;

                if (area < min_area) {
                    continue;
                }

                Region region{};
                region.area = area;
                region.rect = Rect{min_x, min_y, w_rect, h_rect};
                regions.push_back(region);
            }
        }

        if (regions.empty()) {
            return result;
        }

        std::sort(
            regions.begin(),
            regions.end(),
            [](const Region & a, const Region & b) { return a.area > b.area; }
        );

        if (!regions.empty()) {
            const Region & body_region = regions[0];
            result.has_body = true;
            result.body = body_region.rect;
            result.foot_x = body_region.rect.x + body_region.rect.width / 2;
            result.foot_y = body_region.rect.y + body_region.rect.height;
        }

        if (regions.size() >= 2) {
            const Region & platform_region = regions[1];
            result.has_platform = true;
            result.platform = platform_region.rect;
        }

        return result;
    }

    FailMatchResult ImageBackend::MatchFailTemplate(
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
    ) {
        FailMatchResult result{};
        result.detected = false;
        result.match_x = -1;
        result.match_y = -1;
        result.match_score = 0.0;

        if (!gray || template_width <= 0 || template_height <= 0 ||
            fail_template.empty()) {
            return result;
        }

        cv::Mat
            gray_mat(height, width, CV_8UC1, const_cast<std::uint8_t *>(gray));

        // 加载模板为灰度图
        cv::Mat templ_mat(
            template_height,
            template_width,
            CV_8UC1,
            const_cast<std::uint8_t *>(fail_template.data())
        );

        if (search_region_x_parts <= 0 || search_region_y_parts <= 0) {
            return result;
        }

        int x_start_part = search_region_x_start_part;
        int x_end_part = search_region_x_end_part;
        int y_start_part = search_region_y_start_part;
        int y_end_part = search_region_y_end_part;

        if (x_start_part < 0)
            x_start_part = 0;
        if (x_end_part > search_region_x_parts)
            x_end_part = search_region_x_parts;
        if (y_start_part < 0)
            y_start_part = 0;
        if (y_end_part > search_region_y_parts)
            y_end_part = search_region_y_parts;

        if (x_end_part <= x_start_part || y_end_part <= y_start_part) {
            return result;
        }

        int search_x_start = width * x_start_part / search_region_x_parts;
        int search_x_end = width * x_end_part / search_region_x_parts;
        int search_y_start = height * y_start_part / search_region_y_parts;
        int search_y_end = height * y_end_part / search_region_y_parts;

        int search_width = search_x_end - search_x_start;
        int search_height = search_y_end - search_y_start;

        if (search_height < template_height || search_width < template_width) {
            return result;
        }

        // 裁剪搜索区域
        cv::Mat search_roi = gray_mat(
            cv::Rect(
                search_x_start,
                search_y_start,
                search_width,
                search_height
            )
        );

        // 使用 TM_CCOEFF_NORMED 进行模板匹配
        cv::Mat match_result;
        cv::matchTemplate(
            search_roi,
            templ_mat,
            match_result,
            cv::TM_CCOEFF_NORMED
        );

        // 查找最佳匹配位置
        double min_val, max_val;
        cv::Point min_loc, max_loc;
        cv::minMaxLoc(match_result, &min_val, &max_val, &min_loc, &max_loc);

        if (max_val >= match_threshold) {
            result.detected = true;
            result.match_x = search_x_start + max_loc.x;
            result.match_y = search_y_start + max_loc.y;
            result.match_score = max_val;
        }

        return result;
    }

    FailMatchResult ImageBackend::MatchFailTemplateFast(
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
    ) {
        const double scale = 0.5;

        FailMatchResult result{};
        result.detected = false;
        result.match_x = -1;
        result.match_y = -1;
        result.match_score = 0.0;

        if (!gray || template_width <= 0 || template_height <= 0 ||
            fail_template.empty()) {
            return result;
        }
        if (scale <= 0.0 || scale >= 1.0) {
            return result;
        }

        if (search_region_x_parts <= 0 || search_region_y_parts <= 0) {
            return result;
        }

        int x_start_part = search_region_x_start_part;
        int x_end_part = search_region_x_end_part;
        int y_start_part = search_region_y_start_part;
        int y_end_part = search_region_y_end_part;

        if (x_start_part < 0)
            x_start_part = 0;
        if (x_end_part > search_region_x_parts)
            x_end_part = search_region_x_parts;
        if (y_start_part < 0)
            y_start_part = 0;
        if (y_end_part > search_region_y_parts)
            y_end_part = search_region_y_parts;

        if (x_end_part <= x_start_part || y_end_part <= y_start_part) {
            return result;
        }

        int search_x_start = width * x_start_part / search_region_x_parts;
        int search_x_end = width * x_end_part / search_region_x_parts;
        int search_y_start = height * y_start_part / search_region_y_parts;
        int search_y_end = height * y_end_part / search_region_y_parts;

        int search_width = search_x_end - search_x_start;
        int search_height = search_y_end - search_y_start;
        if (search_width <= 0 || search_height <= 0) {
            return result;
        }

        cv::Mat
            gray_mat(height, width, CV_8UC1, const_cast<std::uint8_t *>(gray));

        cv::Mat templ_mat(
            template_height,
            template_width,
            CV_8UC1,
            const_cast<std::uint8_t *>(fail_template.data())
        );

        cv::Mat search_roi = gray_mat(
            cv::Rect(
                search_x_start,
                search_y_start,
                search_width,
                search_height
            )
        );

        int scaled_search_w =
            static_cast<int>(std::round(search_width * scale));
        int scaled_search_h =
            static_cast<int>(std::round(search_height * scale));
        int scaled_templ_w =
            static_cast<int>(std::round(template_width * scale));
        int scaled_templ_h =
            static_cast<int>(std::round(template_height * scale));

        if (scaled_search_w <= 0 || scaled_search_h <= 0 ||
            scaled_templ_w <= 0 || scaled_templ_h <= 0) {
            return result;
        }

        if (scaled_search_w < scaled_templ_w ||
            scaled_search_h < scaled_templ_h) {
            return result;
        }

        cv::Mat scaled_search;
        cv::resize(
            search_roi,
            scaled_search,
            cv::Size(scaled_search_w, scaled_search_h),
            0.0,
            0.0,
            cv::INTER_AREA
        );

        cv::Mat scaled_templ;
        cv::resize(
            templ_mat,
            scaled_templ,
            cv::Size(scaled_templ_w, scaled_templ_h),
            0.0,
            0.0,
            cv::INTER_AREA
        );

        cv::Mat match_result;
        cv::matchTemplate(
            scaled_search,
            scaled_templ,
            match_result,
            cv::TM_CCOEFF_NORMED
        );

        double min_val = 0.0, max_val = 0.0;
        cv::Point min_loc, max_loc;
        cv::minMaxLoc(match_result, &min_val, &max_val, &min_loc, &max_loc);

        if (max_val >= match_threshold) {
            int dx = static_cast<int>(std::lround(max_loc.x / scale));
            int dy = static_cast<int>(std::lround(max_loc.y / scale));
            result.detected = true;
            result.match_x = search_x_start + dx;
            result.match_y = search_y_start + dy;
            result.match_score = max_val;
        }

        return result;
    }

} // namespace play_runner
