#pragma once

#include <memory>

namespace play_runner {

    struct AppConfig;
    class ConfigUiState;

    class PlayRunner {
        public:
            PlayRunner();

            int Run();

        private:
            std::shared_ptr<ConfigUiState> ui_state_;
    };

} // namespace play_runner
