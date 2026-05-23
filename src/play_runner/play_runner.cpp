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
#include <chrono>

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
            const std::string config_path = GetDefaultConfigPath();
            ConfigUi ui(ui_state_, config_path);
            std::thread ui_thread([&]() {
                ui.Run();
                ui_state_->exit_requested.store(true);
            });

            ui_state_->SetTargetWindowFound(false);

            {
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
            }

            bool last_window_found = false;
            bool has_last_window_found = false;
            std::string last_have_title_name;

            int final_result = 0;
            while (!ui_state_->exit_requested.load()) {
                ui_state_->SetTargetWindowFound(false);

                WindowInfo window{};
                while (!ui_state_->exit_requested.load()) {
                    AppConfig config = ui_state_->GetConfigSnapshot();

                    const std::string & have_title_name =
                        config.capture.have_title_name;
                    if (have_title_name != last_have_title_name) {
                        last_have_title_name = have_title_name;
                        has_last_window_found = false;
                    }

                    if (have_title_name.empty()) {
                        ui_state_->SetTargetWindowFound(false);
                        if (!has_last_window_found) {
                            Logger::Instance().Warn(
                                "窗口标题关键字为空，无法查找目标窗口"
                            );
                            has_last_window_found = true;
                            last_window_found = false;
                        }
                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(500)
                        );
                        continue;
                    }

                    bool found = false;
                    found = FindWindowByTitleRules(
                        have_title_name,
                        config.capture.skip_title_names,
                        window
                    );
                    ui_state_->SetTargetWindowFound(found);
                    if (found) {
                        break;
                    }

                    if (!has_last_window_found || last_window_found) {
                        Logger::Instance().Warn(
                            "未找到标题包含 \"" + have_title_name +
                            "\" 的窗口，继续轮询"
                        );
                    }

                    last_window_found = false;
                    has_last_window_found = true;

                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                }

                if (ui_state_->exit_requested.load()) {
                    break;
                }

                Logger::Instance().Info("Target window found, starting loop");
                PlaySession session(ui_state_, window);
                int result = session.Run();
                if (ui_state_->exit_requested.load()) {
                    final_result = result;
                    break;
                }

                if (result == 1) {
                    Logger::Instance().Warn("游戏窗口已消失，回退到等待窗口");
                    has_last_window_found = false;
                    continue;
                }

                final_result = result;
                break;
            }

            ui_state_->exit_requested.store(true);
            if (ui_thread.joinable()) {
                ui_thread.join();
            }
            return final_result;
        } catch (const std::exception & ex) {
            Logger::Instance().Error(ex.what());
        }

        Logger::Instance().Info("PlayRunner finished");
        return 0;
    }

} // namespace play_runner
