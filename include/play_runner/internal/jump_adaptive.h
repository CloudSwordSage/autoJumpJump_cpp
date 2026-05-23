#pragma once

#include "play_runner/config.h"

#include <memory>
#include <string>

namespace play_runner {

    class ConfigUiState;

    class JumpAdaptiveFitter {
        public:
            JumpAdaptiveFitter(
                std::shared_ptr<ConfigUiState> ui_state,
                std::string config_path
            );
            ~JumpAdaptiveFitter();

            void PushSample(double x, double y);
            void DiscardRecent(int count);
            bool ExportSamplesCsv();

        private:
            class Impl;
            std::unique_ptr<Impl> impl_;
    };

} // namespace play_runner
