#include "play_runner/play_runner.h"

#include "play_runner/config.h"
#include "play_runner/config_ui.h"
#include "play_runner/config_ui_state.h"
#include "play_runner/debug_draw.h"
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

            bool last_process_running = false;
            bool has_last_process_running = false;
            std::string last_target;

            int final_result = 0;
            while (!ui_state_->exit_requested.load()) {
                ui_state_->SetTargetWindowFound(false);

                WindowInfo window{};
                while (!ui_state_->exit_requested.load()) {
                    AppConfig config = ui_state_->GetConfigSnapshot();

                    const std::string & target = config.capture.process_name;
                    if (target != last_target) {
                        last_target = target;
                        has_last_process_running = false;
                    }

                    if (target.empty()) {
                        ui_state_->SetTargetWindowFound(false);
                        if (!has_last_process_running) {
                            Logger::Instance().Warn("进程名为空，无法查找窗口");
                            has_last_process_running = true;
                            last_process_running = false;
                        }
                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(500)
                        );
                        continue;
                    }

                    bool found = false;
                    found = FindWindow(target, "", window);
                    if (!found) {
                        found = FindWindow("", target, window);
                    }
                    ui_state_->SetTargetWindowFound(found);
                    if (found) {
                        break;
                    }

                    bool process_running = false;
                    process_running = IsProcessRunning(target);

                    if (!process_running) {
                        if (!has_last_process_running || last_process_running) {
                            Logger::Instance().Warn(
                                "未找到进程: " + target + "，等待进程打开"
                            );
                        }
                    } else if (!has_last_process_running ||
                               !last_process_running) {
                        Logger::Instance().Info(
                            "进程已打开: " + target + "，开始轮询窗口"
                        );
                    }

                    last_process_running = process_running;
                    has_last_process_running = true;

                    int sleep_ms = process_running ? 200 : 500;
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(sleep_ms)
                    );
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
                    ShutdownDebugWindow();
                    has_last_process_running = false;
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
