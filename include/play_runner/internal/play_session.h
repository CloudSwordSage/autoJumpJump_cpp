#pragma once

#include "play_runner/screen_capture.h"

#include <memory>

namespace play_runner {

    class ConfigUiState;

    class PlaySession {
        public:
            PlaySession(
                std::shared_ptr<ConfigUiState> ui_state,
                const WindowInfo & target_window
            );

            int Run();

        private:
            std::shared_ptr<ConfigUiState> ui_state_;
            WindowInfo window_;
    };

} // namespace play_runner
