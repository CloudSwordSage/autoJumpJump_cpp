#pragma once

#include "play_runner/config.h"
#include "play_runner/screen_capture.h"

namespace play_runner {

    class PlaySession {
        public:
            PlaySession(const AppConfig & config, const WindowInfo & target_window);

            int Run();

        private:
            const AppConfig & config_;
            WindowInfo window_;
    };

} // namespace play_runner

