#include "play_runner/play_runner.h"

#include "play_runner/config.h"
#include "play_runner/config_ui.h"
#include "play_runner/config_ui_state.h"
#include "play_runner/internal/play_session.h"
#include "play_runner/logging.h"
#include "play_runner/screen_capture.h"

#include <thread>
#include <vector>
#include <string>
#include <exception>

namespace play_runner {

    PlayRunner::PlayRunner() {
        const std::string path = GetDefaultConfigPath();
        AppConfig config = LoadConfigOrDefault(path);

        Logger::Instance().SetFilePath(config.logging.file_path);
        Logger::Instance().SetLevel(config.logging.level);

        ui_state_ = std::make_shared<ConfigUiState>(config);
    }

    int PlayRunner::Run() {
        Logger::Instance().Info("PlayRunner started");

        try {
            AppConfig config = ui_state_->GetConfigSnapshot();
            if (config.debug) {
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
            bool found = FindWindow("", config.capture.process_name, window);
            if (!found) {
                Logger::Instance().Warn("Target window not found");
                return 0;
            }

            Logger::Instance().Info("Target window found, starting loop");
            const std::string config_path = GetDefaultConfigPath();
            ConfigUi ui(ui_state_, config_path);
            std::thread ui_thread([&]() {
                ui.Run();
                ui_state_->exit_requested.store(true);
            });

            PlaySession session(ui_state_, window);
            int result = session.Run();
            ui_state_->exit_requested.store(true);
            if (ui_thread.joinable()) {
                ui_thread.join();
            }
            return result;
        } catch (const std::exception & ex) {
            Logger::Instance().Error(ex.what());
        }

        Logger::Instance().Info("PlayRunner finished");
        return 0;
    }

} // namespace play_runner
