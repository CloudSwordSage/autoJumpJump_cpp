#pragma once

namespace play_runner {

    struct AppConfig;

    class PlayRunner {
        public:
            PlayRunner();

            int Run();

        private:
            AppConfig * config_;
    };

} // namespace play_runner
