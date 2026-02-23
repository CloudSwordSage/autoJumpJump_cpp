#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "play_runner/config.h"

// onnxruntime C++ API headers
#include <onnxruntime_cxx_api.h>

namespace play_runner {

    struct BlockCandidate {
            int x;
            int y;
            int width;
            int height;
            int area;
            int center_x;
            int center_y;
            float score;
            int class_id;
    };

    struct TargetBlock {
            bool has_target;
            BlockCandidate block;
            double distance;
    };

    class OnnxYoloInference {
        public:
            explicit OnnxYoloInference(const OnnxConfig & config);

            const OnnxConfig & GetConfig() const;

            std::vector<BlockCandidate> Run(
                const std::uint8_t * bgr,
                int width,
                int height,
                int roi_y_min,
                int roi_y_max
            );

        private:
            OnnxConfig config_;
            Ort::Env env_;
            Ort::Session session_;
            Ort::AllocatorWithDefaultOptions allocator_;

            float last_scale_;
            int last_pad_left_;
            int last_pad_top_;
            int last_input_width_;
            int last_input_height_;

            std::vector<float> BuildInputTensor(
                const std::uint8_t * bgr,
                int width,
                int height,
                int roi_y_min,
                int roi_y_max,
                int & out_input_width,
                int & out_input_height
            );

            std::vector<BlockCandidate> DecodeOutput(
                const std::vector<Ort::Value> & output_tensors,
                int roi_y_min,
                int roi_y_max,
                int original_width,
                int original_height
            );
    };

    TargetBlock SelectTargetBlock(
        const std::vector<BlockCandidate> & candidates,
        int foot_x,
        int foot_y
    );

} // namespace play_runner
