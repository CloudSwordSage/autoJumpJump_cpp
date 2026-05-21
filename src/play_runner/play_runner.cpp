#include "play_runner/play_runner.h"

#include "play_runner/config.h"
#include "play_runner/internal/play_session.h"
#include "play_runner/logging.h"
#include "play_runner/screen_capture.h"

#include <vector>
#include <string>
#include <exception>

namespace play_runner {

    PlayRunner::PlayRunner() {
        const std::string path = GetDefaultConfigPath();
        AppConfig config = LoadConfigOrDefault(path);

        Logger::Instance().SetFilePath(config.logging.file_path);
        Logger::Instance().SetLevel(config.logging.level);

        config_ = new AppConfig(std::move(config));
    }

    int PlayRunner::Run() {
        Logger::Instance().Info("PlayRunner started");

        try {
            if (config_->debug) {
                std::vector<WindowInfo> windows = EnumerateWindows();
                Logger::Instance().Info("Visible windows:");
                for (const auto & win : windows) {
                    Logger::Instance().Info(
                        "title=\"" + win.title +
                        "\", "
                        "class=\"" +
                        win.class_name +
                        "\", "
                        "process=\"" +
                        win.process_name + "\""
                    );
                }
            }

            WindowInfo window{};
            bool found = FindWindow("", config_->capture.process_name, window);
            if (!found) {
                Logger::Instance().Warn("Target window not found");
                return 0;
            }

            Logger::Instance().Info("Target window found, starting loop");
            PlaySession session(*config_, window);
            return session.Run();
        } catch (const std::exception & ex) {
            Logger::Instance().Error(ex.what());
        }

        Logger::Instance().Info("PlayRunner finished");
        return 0;
    }

} // namespace play_runner
