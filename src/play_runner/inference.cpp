#include "play_runner/inference.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include <opencv2/opencv.hpp>

#include "play_runner/logging.h"

namespace play_runner {

    namespace {

        inline float Clamp01(float v) {
            if (v < 0.0f)
                return 0.0f;
            if (v > 1.0f)
                return 1.0f;
            return v;
        }

        inline float IoU(
            float x1,
            float y1,
            float x2,
            float y2,
            float x1b,
            float y1b,
            float x2b,
            float y2b
        ) {
            float inter_left = std::max(x1, x1b);
            float inter_top = std::max(y1, y1b);
            float inter_right = std::min(x2, x2b);
            float inter_bottom = std::min(y2, y2b);

            float inter_w = inter_right - inter_left;
            float inter_h = inter_bottom - inter_top;
            if (inter_w <= 0.0f || inter_h <= 0.0f)
                return 0.0f;

            float inter_area = inter_w * inter_h;
            float area_a = (x2 - x1) * (y2 - y1);
            float area_b = (x2b - x1b) * (y2b - y1b);
            float union_area = area_a + area_b - inter_area;
            if (union_area <= 0.0f)
                return 0.0f;
            return inter_area / union_area;
        }

        struct RawDet {
                float x1;
                float y1;
                float x2;
                float y2;
                float score;
                int class_id;
        };

    } // namespace

    OnnxYoloInference::OnnxYoloInference(const OnnxConfig & config)
        : config_(config),
          env_(ORT_LOGGING_LEVEL_WARNING, "cpp-play-runner-onnx"),
          session_(nullptr), allocator_(), last_scale_(1.0f), last_pad_left_(0),
          last_pad_top_(0), last_input_width_(config.input_width),
          last_input_height_(config.input_height) {
        std::filesystem::path model_path(config_.model_path);
        if (!std::filesystem::exists(model_path)) {
            Logger::Instance().Error(
                "ONNX model not found: " + model_path.string()
            );
            throw std::runtime_error("ONNX model not found");
        }

        Ort::SessionOptions session_options;
        session_options.SetIntraOpNumThreads(4); // 优化：使用 4 个线程
        session_options.SetGraphOptimizationLevel(
            GraphOptimizationLevel::ORT_ENABLE_EXTENDED
        );
        session_options.SetExecutionMode(ORT_SEQUENTIAL);

        bool gpu_enabled = false;
        try {
            OrtCUDAProviderOptions cuda_options;
            cuda_options.device_id = 0;
            session_options.AppendExecutionProvider_CUDA(cuda_options);
            gpu_enabled = true;
            Logger::Instance().Info(
                "ONNXRuntime ExecutionProvider: CUDA (GPU preferred)"
            );
        } catch (const Ort::Exception & ex) {
            Logger::Instance().Warn(
                std::string(
                    "Failed to enable CUDA ExecutionProvider, fallback to CPU: "
                ) +
                ex.what()
            );
        }

        session_ =
            Ort::Session(env_, model_path.wstring().c_str(), session_options);

        if (!gpu_enabled) {
            Logger::Instance().Info(
                "ONNXRuntime ExecutionProvider: default CPU"
            );
        }
    }

    const OnnxConfig & OnnxYoloInference::GetConfig() const {
        return config_;
    }

    std::vector<float> OnnxYoloInference::BuildInputTensor(
        const std::uint8_t * bgr,
        int width,
        int height,
        int roi_y_min,
        int roi_y_max,
        int & out_input_width,
        int & out_input_height
    ) {
        if (!bgr) {
            throw std::invalid_argument("bgr is null");
        }
        if (width <= 0 || height <= 0) {
            throw std::invalid_argument("invalid width/height");
        }

        int roi_top = roi_y_min;
        int roi_bottom = roi_y_max;
        if (roi_top < 0)
            roi_top = 0;
        if (roi_bottom > height)
            roi_bottom = height;
        if (roi_top >= roi_bottom) {
            roi_top = 0;
            roi_bottom = height;
        }

        int roi_h = roi_bottom - roi_top;
        int roi_w = width;

        int target_w = config_.input_width;
        int target_h = config_.input_height;
        if (target_w <= 0 || target_h <= 0) {
            target_w = roi_w;
            target_h = roi_h;
        }

        out_input_width = target_w;
        out_input_height = target_h;

        // 使用 OpenCV 进行 resize 和 letterbox
        cv::Mat roi_mat(
            roi_h,
            roi_w,
            CV_8UC3,
            const_cast<std::uint8_t *>(bgr) + roi_top * width * 3
        );

        float scale = std::min(
            static_cast<float>(target_w) / static_cast<float>(roi_w),
            static_cast<float>(target_h) / static_cast<float>(roi_h)
        );
        if (scale <= 0.0f) {
            scale = 1.0f;
        }

        int new_w =
            static_cast<int>(std::round(static_cast<float>(roi_w) * scale));
        int new_h =
            static_cast<int>(std::round(static_cast<float>(roi_h) * scale));
        if (new_w <= 0)
            new_w = 1;
        if (new_h <= 0)
            new_h = 1;

        int pad_left = (target_w - new_w) / 2;
        int pad_top = (target_h - new_h) / 2;

        last_scale_ = scale;
        last_pad_left_ = pad_left;
        last_pad_top_ = pad_top;
        last_input_width_ = target_w;
        last_input_height_ = target_h;

        // 使用 OpenCV resize
        cv::Mat resized_mat;
        cv::resize(
            roi_mat,
            resized_mat,
            cv::Size(new_w, new_h),
            0,
            0,
            cv::INTER_LINEAR
        );

        // 使用 copyMakeBorder 进行 letterbox
        cv::Mat letterbox_mat;
        cv::copyMakeBorder(
            resized_mat,
            letterbox_mat,
            pad_top,
            target_h - new_h - pad_top,
            pad_left,
            target_w - new_w - pad_left,
            cv::BORDER_CONSTANT,
            cv::Scalar(114, 114, 114)
        );

        // 转换为 CHW 格式并归一化
        std::vector<float> input(
            static_cast<std::size_t>(3 * target_h * target_w)
        );
        const int total_pixels = target_w * target_h;

        for (int y = 0; y < target_h; ++y) {
            const std::uint8_t * row_ptr = letterbox_mat.ptr<std::uint8_t>(y);
            for (int x = 0; x < target_w; ++x) {
                const int idx = y * target_w + x;
                const int src_idx = x * 3;
                const float inv_255 = 1.0f / 255.0f;

                input[idx] = row_ptr[src_idx + 2] * inv_255;                // R
                input[idx + total_pixels] = row_ptr[src_idx + 1] * inv_255; // G
                input[idx + 2 * total_pixels] =
                    row_ptr[src_idx + 0] * inv_255; // B
            }
        }

        return input;
    }

    std::vector<BlockCandidate> OnnxYoloInference::DecodeOutput(
        const std::vector<Ort::Value> & output_tensors,
        int roi_y_min,
        int roi_y_max,
        int original_width,
        int original_height
    ) {
        std::vector<BlockCandidate> result;

        if (output_tensors.empty()) {
            return result;
        }

        const Ort::Value & out = output_tensors[0];
        if (!out.IsTensor()) {
            return result;
        }

        Ort::TensorTypeAndShapeInfo info = out.GetTensorTypeAndShapeInfo();
        if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
            return result;
        }

        std::vector<int64_t> dims = info.GetShape();
        if (dims.size() != 2 && dims.size() != 3) {
            return result;
        }

        const float * data = out.GetTensorData<float>();

        float scale = last_scale_;
        if (scale <= 0.0f) {
            scale = 1.0f;
        }
        int pad_left = last_pad_left_;
        int pad_top = last_pad_top_;

        if (dims.size() == 2) {
            int num_det = static_cast<int>(dims[0]);
            int stride = static_cast<int>(dims[1]);
            if (stride < 6) {
                return result;
            }
            for (int i = 0; i < num_det; ++i) {
                const float * p = data + i * stride;
                float x1 = p[0];
                float y1 = p[1];
                float x2 = p[2];
                float y2 = p[3];
                float score = p[4];
                int class_id = static_cast<int>(p[5]);
                if (score < config_.score_threshold) {
                    continue;
                }
                if (class_id != 0) {
                    continue;
                }

                float xx1 = (x1 - static_cast<float>(pad_left)) / scale;
                float yy1 = (y1 - static_cast<float>(pad_top)) / scale;
                float xx2 = (x2 - static_cast<float>(pad_left)) / scale;
                float yy2 = (y2 - static_cast<float>(pad_top)) / scale;

                float oy1 = yy1 + static_cast<float>(roi_y_min);
                float oy2 = yy2 + static_cast<float>(roi_y_min);

                int ix1 = std::max(
                    0,
                    std::min(original_width, static_cast<int>(std::round(xx1)))
                );
                int iy1 = std::max(
                    0,
                    std::min(original_height, static_cast<int>(std::round(oy1)))
                );
                int ix2 = std::max(
                    0,
                    std::min(original_width, static_cast<int>(std::round(xx2)))
                );
                int iy2 = std::max(
                    0,
                    std::min(original_height, static_cast<int>(std::round(oy2)))
                );

                int w_rect = std::max(0, ix2 - ix1);
                int h_rect = std::max(0, iy2 - iy1);
                if (w_rect <= 0 || h_rect <= 0) {
                    continue;
                }
                int cx = ix1 + w_rect / 2;
                int cy = iy1 + h_rect / 2;
                int area = w_rect * h_rect;

                BlockCandidate c{};
                c.x = ix1;
                c.y = iy1;
                c.width = w_rect;
                c.height = h_rect;
                c.area = area;
                c.center_x = cx;
                c.center_y = cy;
                c.score = score;
                c.class_id = class_id;
                result.push_back(c);
            }
        } else if (dims.size() == 3) {
            int num_det = static_cast<int>(dims[1]);
            int stride = static_cast<int>(dims[2]);
            if (stride < 6) {
                return result;
            }
            for (int i = 0; i < num_det; ++i) {
                const float * p = data + i * stride;
                float x1 = p[0];
                float y1 = p[1];
                float x2 = p[2];
                float y2 = p[3];
                float score = p[4];
                int class_id = static_cast<int>(p[5]);
                if (score < config_.score_threshold) {
                    continue;
                }
                if (class_id != 0) {
                    continue;
                }

                float xx1 = (x1 - static_cast<float>(pad_left)) / scale;
                float yy1 = (y1 - static_cast<float>(pad_top)) / scale;
                float xx2 = (x2 - static_cast<float>(pad_left)) / scale;
                float yy2 = (y2 - static_cast<float>(pad_top)) / scale;

                float oy1 = yy1 + static_cast<float>(roi_y_min);
                float oy2 = yy2 + static_cast<float>(roi_y_min);

                int ix1 = std::max(
                    0,
                    std::min(original_width, static_cast<int>(std::round(xx1)))
                );
                int iy1 = std::max(
                    0,
                    std::min(original_height, static_cast<int>(std::round(oy1)))
                );
                int ix2 = std::max(
                    0,
                    std::min(original_width, static_cast<int>(std::round(xx2)))
                );
                int iy2 = std::max(
                    0,
                    std::min(original_height, static_cast<int>(std::round(oy2)))
                );

                int w_rect = std::max(0, ix2 - ix1);
                int h_rect = std::max(0, iy2 - iy1);
                if (w_rect <= 0 || h_rect <= 0) {
                    continue;
                }
                int cx = ix1 + w_rect / 2;
                int cy = iy1 + h_rect / 2;
                int area = w_rect * h_rect;

                BlockCandidate c{};
                c.x = ix1;
                c.y = iy1;
                c.width = w_rect;
                c.height = h_rect;
                c.area = area;
                c.center_x = cx;
                c.center_y = cy;
                c.score = score;
                c.class_id = class_id;
                result.push_back(c);
            }
        }

        return result;
    }

    std::vector<BlockCandidate> OnnxYoloInference::Run(
        const std::uint8_t * bgr,
        int width,
        int height,
        int roi_y_min,
        int roi_y_max
    ) {
        int input_w = 0;
        int input_h = 0;
        std::vector<float> input_data = BuildInputTensor(
            bgr,
            width,
            height,
            roi_y_min,
            roi_y_max,
            input_w,
            input_h
        );

        std::array<int64_t, 4> input_shape = {
            1,
            3,
            static_cast<int64_t>(input_h),
            static_cast<int64_t>(input_w)
        };

        Ort::AllocatedStringPtr input_name_alloc =
            session_.GetInputNameAllocated(0, allocator_);
        Ort::AllocatedStringPtr output_name_alloc =
            session_.GetOutputNameAllocated(0, allocator_);
        const char * input_name = input_name_alloc.get();
        const char * output_name = output_name_alloc.get();

        Ort::MemoryInfo mem_info =
            Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            mem_info,
            input_data.data(),
            static_cast<std::size_t>(input_data.size()),
            input_shape.data(),
            input_shape.size()
        );

        std::vector<const char *> input_names = {input_name};
        std::vector<const char *> output_names = {output_name};

        std::vector<Ort::Value> output_tensors = session_.Run(
            Ort::RunOptions{nullptr},
            input_names.data(),
            &input_tensor,
            1,
            output_names.data(),
            1
        );

        return DecodeOutput(
            output_tensors,
            roi_y_min,
            roi_y_max,
            width,
            height
        );
    }

    TargetBlock SelectTargetBlock(
        const std::vector<BlockCandidate> & candidates,
        int foot_x,
        int foot_y
    ) {
        TargetBlock result{};
        result.has_target = false;

        if (candidates.empty())
            return result;
        if (foot_x < 0 || foot_y < 0)
            return result;

        const BlockCandidate * best = nullptr;
        int best_center_y = 0;

        for (const auto & c : candidates) {
            if (!best || c.center_y < best_center_y) {
                best = &c;
                best_center_y = c.center_y;
            }
        }

        if (!best)
            return result;

        double dx = static_cast<double>(best->center_x - foot_x);
        double dy = static_cast<double>(best->center_y - foot_y);
        double dist = std::sqrt(dx * dx + dy * dy);

        result.has_target = true;
        result.block = *best;
        result.distance = dist;
        return result;
    }

} // namespace play_runner
