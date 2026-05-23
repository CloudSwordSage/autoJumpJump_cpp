#include "play_runner/internal/play_session.h"

#include "play_runner/config_ui_state.h"
#include "play_runner/debug_draw.h"
#include "play_runner/image_backend.h"
#include "play_runner/inference.h"
#include "play_runner/input_control.h"
#include "play_runner/internal/fail_template_engine.h"
#include "play_runner/internal/jump_adaptive.h"
#include "play_runner/internal/performance_profiler.h"
#include "play_runner/logging.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <opencv2/opencv.hpp>

#include <windows.h>

namespace play_runner {

    namespace {

        bool IsOnnxConfigEqual(const OnnxConfig & a, const OnnxConfig & b) {
            return a.model_path == b.model_path &&
                   a.input_width == b.input_width &&
                   a.input_height == b.input_height &&
                   a.score_threshold == b.score_threshold &&
                   a.nms_iou_threshold == b.nms_iou_threshold;
        }

        bool FocusWindow(HWND hwnd) {
            if (!hwnd || !::IsWindow(hwnd)) {
                return false;
            }

            DWORD target_thread = ::GetWindowThreadProcessId(hwnd, nullptr);
            DWORD cur_thread = ::GetCurrentThreadId();
            HWND fg = ::GetForegroundWindow();
            DWORD fg_thread = fg ? ::GetWindowThreadProcessId(fg, nullptr) : 0;

            if (target_thread != 0 && target_thread != cur_thread) {
                ::AttachThreadInput(cur_thread, target_thread, TRUE);
            }
            if (fg_thread != 0 && fg_thread != cur_thread) {
                ::AttachThreadInput(cur_thread, fg_thread, TRUE);
            }

            ::ShowWindowAsync(hwnd, SW_RESTORE);
            ::SetWindowPos(
                hwnd,
                HWND_TOPMOST,
                0,
                0,
                0,
                0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW
            );
            ::SetWindowPos(
                hwnd,
                HWND_NOTOPMOST,
                0,
                0,
                0,
                0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW
            );

            INPUT alt_down{};
            alt_down.type = INPUT_KEYBOARD;
            alt_down.ki.wVk = VK_MENU;
            INPUT alt_up = alt_down;
            alt_up.ki.dwFlags = KEYEVENTF_KEYUP;
            ::SendInput(1, &alt_down, sizeof(INPUT));
            ::SendInput(1, &alt_up, sizeof(INPUT));

            ::BringWindowToTop(hwnd);
            ::SetForegroundWindow(hwnd);
            ::SetActiveWindow(hwnd);
            ::SetFocus(hwnd);

            if (fg_thread != 0 && fg_thread != cur_thread) {
                ::AttachThreadInput(cur_thread, fg_thread, FALSE);
            }
            if (target_thread != 0 && target_thread != cur_thread) {
                ::AttachThreadInput(cur_thread, target_thread, FALSE);
            }

            return ::GetForegroundWindow() == hwnd;
        }

        double EvalPressDurationMs(double distance, const JumpConfig & jump) {
            if (jump.press_duration_mode == 1) {
                return distance * jump.jump_alpha + jump.jump_beta;
            }
            const JumpSegment * selected = nullptr;
            for (const auto & seg : jump.params.segments) {
                if (distance >= seg.x_start &&
                    (seg.x_end == -1.0 || distance <= seg.x_end)) {
                    selected = &seg;
                    break;
                }
            }
            if (selected) {
                return distance * selected->a + selected->b;
            }
            return distance * jump.jump_alpha + jump.jump_beta;
        }

        const BlockCandidate * SelectFootBlock(
            const std::vector<BlockCandidate> & candidates,
            int foot_x,
            int foot_y
        ) {
            const BlockCandidate * best_containing = nullptr;
            int best_area = 0;
            for (const auto & c : candidates) {
                const int x2 = c.x + c.width;
                const int y2 = c.y + c.height;
                if (foot_x >= c.x && foot_x < x2 && foot_y >= c.y &&
                    foot_y < y2) {
                    if (!best_containing || c.area < best_area) {
                        best_containing = &c;
                        best_area = c.area;
                    }
                }
            }
            if (best_containing) {
                return best_containing;
            }

            const BlockCandidate * best_nearest = nullptr;
            double best_dist2 = std::numeric_limits<double>::infinity();
            for (const auto & c : candidates) {
                const double dx = static_cast<double>(c.center_x - foot_x);
                const double dy = static_cast<double>(c.center_y - foot_y);
                const double dist2 = dx * dx + dy * dy;
                if (dist2 < best_dist2) {
                    best_dist2 = dist2;
                    best_nearest = &c;
                }
            }
            return best_nearest;
        }

        bool IsFailTemplateConfigEqual(
            const FailTemplateConfig & a,
            const FailTemplateConfig & b
        ) {
            return a.template_path == b.template_path &&
                   a.template_names == b.template_names &&
                   a.match_threshold == b.match_threshold &&
                   a.search_region_x_parts == b.search_region_x_parts &&
                   a.search_region_x_start_part ==
                       b.search_region_x_start_part &&
                   a.search_region_x_end_part == b.search_region_x_end_part &&
                   a.search_region_y_parts == b.search_region_y_parts &&
                   a.search_region_y_start_part ==
                       b.search_region_y_start_part &&
                   a.search_region_y_end_part == b.search_region_y_end_part &&
                   a.fast_miss_fallback_threshold ==
                       b.fast_miss_fallback_threshold;
        }

        constexpr double AUTO_RESTART_COOLDOWN_SECONDS = 1.0;

    } // namespace

    PlaySession::PlaySession(
        std::shared_ptr<ConfigUiState> ui_state,
        const WindowInfo & target_window
    )
        : ui_state_(std::move(ui_state)), window_(target_window) {
    }

    int PlaySession::Run() {
        InputControl input_control;
        std::atomic<bool> & exit_requested = ui_state_->exit_requested;
        std::atomic<bool> session_stop(false);
        std::atomic<bool> window_lost(false);
        AppConfig initial_config = ui_state_->GetConfigSnapshot();

        JumpAdaptiveFitter adaptive_fitter(ui_state_, GetDefaultConfigPath());

        OnnxConfig last_onnx_config = initial_config.onnx;
        auto onnx_infer =
            std::make_unique<OnnxYoloInference>(initial_config.onnx);

        FailTemplateConfig last_fail_config = initial_config.fail_template;
        auto fail_template_engine =
            std::make_unique<FailTemplateEngine>(initial_config.fail_template);
        fail_template_engine->Load();

        std::atomic<bool> auto_jump_enabled(false);
        std::atomic<bool> manual_jump_requested(false);

        PerformanceProfiler profiler(initial_config.debug);

        int stable_frames = 0;

        int last_foot_x = -1;
        int last_foot_y = -1;

        bool jump_in_progress = false;
        double last_jump_time = 0.0;

        bool prev_fail_detected = false;
        bool prev_auto_jump_enabled = false;
        double last_auto_restart_time = -1e9;

        int frame_count = 0;
        double fps = 0.0;

        auto start_time = std::chrono::steady_clock::now();

        bool monitor_enabled = initial_config.display.enable_monitor_window;
        std::string monitor_title = initial_config.capture.window_title;
        if (monitor_title.empty()) {
            monitor_title = "Real-time Monitor";
        }
        double monitor_scale = 1.0;
        InitDebugWindow(monitor_title, true, monitor_scale);
        SetOverlayVisible(monitor_enabled);

        std::thread worker([&]() {
            struct PendingJumpSample {
                    bool active = false;
                    bool wait_press_done = false;
                    bool seen_movement = false;
                    std::uint64_t press_task_id = 0;
                    int start_foot_x = -1;
                    int start_foot_y = -1;
                    double a_distance = 0.0;
                    double press_duration_ms = 0.0;
                    double forward_x = 0.0;
                    double forward_y = 0.0;
            };

            PendingJumpSample pending_jump{};

            HWND hwnd = reinterpret_cast<HWND>(
                reinterpret_cast<void *>(window_.handle)
            );
            ui_state_->SetTargetWindowFound(true);
            while (!exit_requested.load() && !session_stop.load()) {
                if (!::IsWindow(hwnd)) {
                    ui_state_->SetTargetWindowFound(false);
                    window_lost.store(true);
                    session_stop.store(true);
                    SetOverlayVisible(false);
                    break;
                }

                bool window_active = ::IsWindowVisible(hwnd) &&
                                     !::IsIconic(hwnd) &&
                                     (::GetForegroundWindow() == hwnd);
                if (!window_active) {
                    SetOverlayVisible(false);
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    continue;
                }
                profiler.StartTiming("Frame_Total");

                AppConfig config = ui_state_->GetConfigSnapshot();
                profiler.SetEnabled(config.debug);

                if (!IsOnnxConfigEqual(config.onnx, last_onnx_config)) {
                    last_onnx_config = config.onnx;
                    onnx_infer =
                        std::make_unique<OnnxYoloInference>(config.onnx);
                }

                if (!IsFailTemplateConfigEqual(
                        config.fail_template,
                        last_fail_config
                    )) {
                    last_fail_config = config.fail_template;
                    fail_template_engine = std::make_unique<FailTemplateEngine>(
                        config.fail_template
                    );
                    fail_template_engine->Load();
                }

                if (config.display.enable_monitor_window && !monitor_enabled) {
                    monitor_enabled = true;
                    monitor_title = config.capture.window_title;
                    if (monitor_title.empty()) {
                        monitor_title = "Real-time Monitor";
                    }
                } else if (!config.display.enable_monitor_window &&
                           monitor_enabled) {
                    monitor_enabled = false;
                    SetOverlayVisible(false);
                } else if (monitor_title != config.capture.window_title &&
                           config.display.enable_monitor_window) {
                    monitor_title = config.capture.window_title;
                    if (monitor_title.empty()) {
                        monitor_title = "Real-time Monitor";
                    }
                }
                SetOverlayVisible(monitor_enabled);

                profiler.StartTiming("Capture_Window");
                CapturedFrame frame = CaptureWindowFrame(window_);
                profiler.EndTiming("Capture_Window");
                if (frame.width <= 0 || frame.height <= 0 ||
                    frame.bgra.empty()) {
                    Logger::Instance().Warn("Capture failed or empty frame");
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    continue;
                }

                ui_state_->UpdateFrame(frame);

                const int w = frame.width;
                const int h = frame.height;

                if (w <= config.capture.crop_left + config.capture.crop_right ||
                    h <= config.capture.crop_top + config.capture.crop_bottom) {
                    Logger::Instance().Warn("Frame too small after crop");
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    continue;
                }

                profiler.StartTiming("BGRA_to_BGR_Conversion");
                cv::Mat bgra_mat(h, w, CV_8UC4, frame.bgra.data());
                cv::Mat bgr_mat;
                cv::cvtColor(bgra_mat, bgr_mat, cv::COLOR_BGRA2BGR);
                profiler.EndTiming("BGRA_to_BGR_Conversion");

                int crop_top = config.capture.crop_top;
                int crop_bottom = config.capture.crop_bottom;
                int crop_left = config.capture.crop_left;
                int crop_right = config.capture.crop_right;

                profiler.StartTiming("Image_Crop");
                cv::Rect crop_rect(
                    crop_left,
                    crop_top,
                    w - crop_left - crop_right,
                    h - crop_top - crop_bottom
                );
                cv::Mat cropped_mat =
                    bgr_mat(crop_rect).clone(); // clone() 确保数据独立
                profiler.EndTiming("Image_Crop");

                int cropped_w = cropped_mat.cols;
                int cropped_h = cropped_mat.rows;

                cv::Mat cropped_gray;
                cv::cvtColor(cropped_mat, cropped_gray, cv::COLOR_BGR2GRAY);

                int roi_y_min = 0;
                int roi_y_max = 0;
                ImageBackend::ComputeRoiBounds(
                    cropped_h,
                    config.capture.roi_top_margin,
                    config.capture.roi_bottom_margin,
                    roi_y_min,
                    roi_y_max
                );

                double target_lab[3] = {
                    static_cast<double>(config.lab.target_l),
                    static_cast<double>(config.lab.target_a),
                    static_cast<double>(config.lab.target_b)
                };
                double weights[3] = {
                    config.lab.weight_l,
                    config.lab.weight_a,
                    config.lab.weight_b
                };

                profiler.StartTiming("Build_LabMask");
                std::vector<std::uint8_t> mask;
                ImageBackend::BuildLabMask(
                    cropped_mat.data,
                    cropped_w,
                    cropped_h,
                    target_lab,
                    weights,
                    static_cast<double>(config.lab.distance_threshold),
                    roi_y_min,
                    roi_y_max,
                    3,
                    mask
                );
                profiler.EndTiming("Build_LabMask");

                profiler.StartTiming("Extract_Character");
                CharacterDetection character = ImageBackend::ExtractCharacter(
                    mask,
                    cropped_w,
                    cropped_h,
                    config.lab.distance_threshold *
                        config.lab.distance_threshold
                );
                profiler.EndTiming("Extract_Character");

                int foot_x = -1;
                int foot_y = -1;
                if (character.has_body) {
                    foot_x = character.foot_x;
                    foot_y = character.foot_y;
                }

                int corrected_foot_x = foot_x;
                int corrected_foot_y = foot_y;
                if (foot_x >= 0 && foot_y >= 0) {
                    corrected_foot_x =
                        foot_x - config.jump.foot_center_offset_x;
                    corrected_foot_y =
                        foot_y - config.jump.foot_center_offset_y;
                }

                if (foot_x >= 0 && foot_y >= 0) {
                    if (last_foot_x >= 0 && last_foot_y >= 0) {
                        int dx = foot_x - last_foot_x;
                        int dy = foot_y - last_foot_y;
                        int stable_pos_eps = config.jump.stable_pos_eps;
                        if (std::abs(dx) <= stable_pos_eps &&
                            std::abs(dy) <= stable_pos_eps) {
                            stable_frames += 1;
                        } else {
                            stable_frames = 1;
                        }
                    } else {
                        stable_frames = 1;
                    }
                    last_foot_x = foot_x;
                    last_foot_y = foot_y;
                } else {
                    last_foot_x = -1;
                    last_foot_y = -1;
                    stable_frames = 0;
                }

                profiler.StartTiming("ONNX_Inference");
                std::vector<BlockCandidate> candidates = onnx_infer->Run(
                    cropped_mat.data,
                    cropped_w,
                    cropped_h,
                    roi_y_min,
                    roi_y_max
                );
                profiler.EndTiming("ONNX_Inference");

                profiler.StartTiming("Fail_Template_Match");
                FailTemplateDetection fail_detection =
                    fail_template_engine
                        ->Detect(cropped_gray.data, cropped_w, cropped_h);
                profiler.EndTiming("Fail_Template_Match");

                if (fail_detection.detected) {
                    stable_frames = 0;
                    last_foot_x = -1;
                    last_foot_y = -1;
                    jump_in_progress = false;
                    if (!prev_fail_detected) {
                        pending_jump.active = false;
                        if (config.jump.enable_adaptive_adjustment) {
                            adaptive_fitter.DiscardRecent(
                                config.jump.params.fail_discard_count
                            );
                        }
                    }
                }

                bool auto_jump_now = auto_jump_enabled.load();
                bool auto_jump_rising =
                    auto_jump_now && !prev_auto_jump_enabled;
                if (auto_jump_now && config.auto_restart &&
                    fail_detection.detected &&
                    fail_detection.match.match_x >= 0 &&
                    fail_detection.match.match_y >= 0) {
                    auto restart_now = std::chrono::steady_clock::now();
                    double now_seconds = std::chrono::duration<double>(
                                             restart_now.time_since_epoch()
                    )
                                             .count();
                    if ((auto_jump_rising || !prev_fail_detected) &&
                        now_seconds - last_auto_restart_time >=
                            AUTO_RESTART_COOLDOWN_SECONDS) {
                        int overlay_x = frame.window_rect.left + crop_left;
                        int overlay_y = frame.window_rect.top + crop_top;
                        int click_x = overlay_x + fail_detection.match.match_x +
                                      (fail_detection.template_width / 2);
                        int click_y = overlay_y + fail_detection.match.match_y +
                                      (fail_detection.template_height / 2);
                        try {
                            input_control.MoveMouseAbsolute(click_x, click_y);
                            input_control.LeftClick();
                            last_auto_restart_time = now_seconds;
                        } catch (const std::exception & ex) {
                            Logger::Instance().Error(
                                std::string("Auto restart failed: ") + ex.what()
                            );
                        }
                    }
                }
                prev_fail_detected = fail_detection.detected;
                prev_auto_jump_enabled = auto_jump_now;

                frame_count += 1;
                auto now = std::chrono::steady_clock::now();
                double elapsed =
                    std::chrono::duration<double>(now - start_time).count();
                if (elapsed > 0.5) {
                    fps = frame_count / elapsed;
                    frame_count = 0;
                    start_time = now;
                }

                if (candidates.empty()) {
                    continue;
                }

                std::uint64_t new_calib_request_id = 0;
                if (ui_state_->ConsumeFootCalibrationRequest(
                        new_calib_request_id
                    )) {
                    bool focus_ok = FocusWindow(hwnd);
                    if (!focus_ok) {
                        Logger::Instance().Info(
                            "校准聚焦失败: 目标窗口未获得前台焦点"
                        );
                    }
                    int offset_x = config.jump.foot_center_offset_x;
                    int offset_y = config.jump.foot_center_offset_y;
                    bool ok = false;
                    if (foot_x >= 0 && foot_y >= 0) {
                        const BlockCandidate * foot_block =
                            SelectFootBlock(candidates, foot_x, foot_y);
                        if (foot_block) {
                            offset_x = foot_x - foot_block->center_x;
                            offset_y = foot_y - foot_block->center_y;
                            ok = true;
                        }
                    }

                    if (ok) {
                        AppConfig updated = ui_state_->GetConfigSnapshot();
                        updated.jump.foot_center_offset_x = offset_x;
                        updated.jump.foot_center_offset_y = offset_y;
                        ui_state_->ApplyConfig(updated);
                        SaveConfig(GetDefaultConfigPath(), updated);
                        Logger::Instance().Info(
                            std::string("校准完成: offset_x=") +
                            std::to_string(offset_x) +
                            ", offset_y=" + std::to_string(offset_y)
                        );
                    } else {
                        Logger::Instance().Info(
                            "校准失败: 未检测到脚底或脚下方块"
                        );
                    }

                    ui_state_->PublishFootCalibrationResult(
                        new_calib_request_id,
                        offset_x,
                        offset_y
                    );
                }

                profiler.StartTiming("Select_Target");
                TargetBlock target = SelectTargetBlock(
                    candidates,
                    corrected_foot_x,
                    corrected_foot_y
                );
                profiler.EndTiming("Select_Target");

                if (pending_jump.active && pending_jump.wait_press_done &&
                    input_control.IsLongPressCompleted(
                        pending_jump.press_task_id
                    )) {
                    pending_jump.wait_press_done = false;
                    pending_jump.seen_movement = false;
                    pending_jump.start_foot_x = foot_x;
                    pending_jump.start_foot_y = foot_y;
                    stable_frames = 0;
                    last_foot_x = foot_x;
                    last_foot_y = foot_y;
                }

                if (pending_jump.active && !pending_jump.wait_press_done &&
                    !fail_detection.detected && foot_x >= 0 && foot_y >= 0) {
                    const int movement_threshold =
                        std::max(5, config.jump.stable_pos_eps * 3);
                    if (!pending_jump.seen_movement) {
                        const int dx = foot_x - pending_jump.start_foot_x;
                        const int dy = foot_y - pending_jump.start_foot_y;
                        if (std::abs(dx) > movement_threshold ||
                            std::abs(dy) > movement_threshold) {
                            pending_jump.seen_movement = true;
                        }
                    }

                    if (pending_jump.seen_movement &&
                        stable_frames >= config.jump.stable_min_frames) {
                        const BlockCandidate * foot_block = SelectFootBlock(
                            candidates,
                            corrected_foot_x,
                            corrected_foot_y
                        );
                        if (foot_block) {
                            const double vx = static_cast<double>(
                                corrected_foot_x - foot_block->center_x
                            );
                            const double vy = static_cast<double>(
                                corrected_foot_y - foot_block->center_y
                            );
                            const double proj = vx * pending_jump.forward_x +
                                                vy * pending_jump.forward_y;
                            const int sign = proj >= 0.0 ? 1 : -1;
                            const double b = std::sqrt(vx * vx + vy * vy);
                            const double x = pending_jump.a_distance + b * sign;
                            if (x >= 0.0 &&
                                config.jump.enable_adaptive_adjustment) {
                                adaptive_fitter.PushSample(
                                    x,
                                    pending_jump.press_duration_ms
                                );
                                Logger::Instance().Info(
                                    std::string("Jump sample: x=") +
                                    std::to_string(x) + ", y=" +
                                    std::to_string(
                                        pending_jump.press_duration_ms
                                    ) +
                                    ", a=" +
                                    std::to_string(pending_jump.a_distance) +
                                    ", b=" + std::to_string(b) +
                                    ", sign=" + std::to_string(sign)
                                );
                            }
                        }
                        pending_jump.active = false;
                    }
                }

                bool cursor_on_window = false;
                {
                    POINT cursor{};
                    if (::GetCursorPos(&cursor)) {
                        const RECT & rect = frame.window_rect;
                        cursor_on_window =
                            cursor.x >= rect.left && cursor.x < rect.right &&
                            cursor.y >= rect.top && cursor.y < rect.bottom;
                    }
                }

                bool manual_jump = manual_jump_requested.exchange(false);
                if (manual_jump && cursor_on_window) {
                    profiler.StartTiming("Manual_Jump");
                    if (target.has_target && !jump_in_progress) {
                        double duration =
                            EvalPressDurationMs(target.distance, config.jump);
                        if (duration > 0.0) {
                            jump_in_progress = true;
                            last_jump_time = std::chrono::duration<double>(
                                                 now.time_since_epoch()
                            )
                                                 .count();
                            const int roi_top_edge_offset_px = 50;
                            const int press_y_min_in_cropped = 50;
                            const double dir_x = static_cast<double>(
                                target.block.center_x - corrected_foot_x
                            );
                            const double dir_y = static_cast<double>(
                                target.block.center_y - corrected_foot_y
                            );
                            const double dir_norm =
                                std::sqrt(dir_x * dir_x + dir_y * dir_y);
                            int press_overlay_x =
                                frame.window_rect.left + crop_left;
                            int press_overlay_y =
                                frame.window_rect.top + crop_top;
                            int press_x = press_overlay_x + (cropped_w / 2);
                            int press_y_in_cropped =
                                roi_y_min - roi_top_edge_offset_px;
                            if (press_y_in_cropped < press_y_min_in_cropped) {
                                press_y_in_cropped = press_y_min_in_cropped;
                            }
                            if (press_y_in_cropped >= cropped_h) {
                                press_y_in_cropped = cropped_h - 1;
                            }
                            int press_y = press_overlay_y + press_y_in_cropped;
                            try {
                                std::uint64_t task_id =
                                    input_control.LeftLongPressAt(
                                        press_x,
                                        press_y,
                                        static_cast<int>(duration)
                                    );
                                if (task_id != 0 && dir_norm > 1e-6) {
                                    pending_jump.active = true;
                                    pending_jump.wait_press_done = true;
                                    pending_jump.press_task_id = task_id;
                                    pending_jump.seen_movement = false;
                                    pending_jump.start_foot_x = -1;
                                    pending_jump.start_foot_y = -1;
                                    pending_jump.a_distance = target.distance;
                                    pending_jump.press_duration_ms = duration;
                                    pending_jump.forward_x = dir_x / dir_norm;
                                    pending_jump.forward_y = dir_y / dir_norm;
                                } else {
                                    pending_jump.active = false;
                                }
                            } catch (const std::exception & ex) {
                                pending_jump.active = false;
                                Logger::Instance().Error(
                                    std::string("Manual jump failed: ") +
                                    ex.what()
                                );
                            }
                            jump_in_progress = false;
                            stable_frames = 0;
                        }
                    }
                    profiler.EndTiming("Manual_Jump");
                }

                if (auto_jump_enabled.load() && target.has_target &&
                    stable_frames >= config.jump.stable_min_frames &&
                    !jump_in_progress && cursor_on_window) {
                    profiler.StartTiming("Auto_Jump");
                    double now_seconds =
                        std::chrono::duration<double>(now.time_since_epoch())
                            .count();
                    double duration =
                        EvalPressDurationMs(target.distance, config.jump);

                    double cooldown = (duration / 1000.0) + 0.5;

                    if (duration > 0.0 &&
                        now_seconds - last_jump_time > cooldown) {
                        jump_in_progress = true;
                        last_jump_time = now_seconds;
                        const double dir_x = static_cast<double>(
                            target.block.center_x - corrected_foot_x
                        );
                        const double dir_y = static_cast<double>(
                            target.block.center_y - corrected_foot_y
                        );
                        const double dir_norm =
                            std::sqrt(dir_x * dir_x + dir_y * dir_y);
                        const int roi_top_edge_offset_px = 50;
                        const int press_y_min_in_cropped = 50;
                        int press_overlay_x =
                            frame.window_rect.left + crop_left;
                        int press_overlay_y = frame.window_rect.top + crop_top;
                        int press_x = press_overlay_x + (cropped_w / 2);
                        int press_y_in_cropped =
                            roi_y_min - roi_top_edge_offset_px;
                        if (press_y_in_cropped < press_y_min_in_cropped) {
                            press_y_in_cropped = press_y_min_in_cropped;
                        }
                        if (press_y_in_cropped >= cropped_h) {
                            press_y_in_cropped = cropped_h - 1;
                        }
                        int press_y = press_overlay_y + press_y_in_cropped;
                        try {
                            std::uint64_t task_id =
                                input_control.LeftLongPressAt(
                                    press_x,
                                    press_y,
                                    static_cast<int>(duration)
                                );
                            if (task_id != 0 && dir_norm > 1e-6) {
                                pending_jump.active = true;
                                pending_jump.wait_press_done = true;
                                pending_jump.press_task_id = task_id;
                                pending_jump.seen_movement = false;
                                pending_jump.start_foot_x = -1;
                                pending_jump.start_foot_y = -1;
                                pending_jump.a_distance = target.distance;
                                pending_jump.press_duration_ms = duration;
                                pending_jump.forward_x = dir_x / dir_norm;
                                pending_jump.forward_y = dir_y / dir_norm;
                            } else {
                                pending_jump.active = false;
                            }
                        } catch (const std::exception & ex) {
                            pending_jump.active = false;
                            Logger::Instance().Error(
                                std::string("Auto jump failed: ") + ex.what()
                            );
                        }
                        jump_in_progress = false; // 入队即返回，重置标志
                        stable_frames = 0;        // 重置稳定帧数，避免连续触发
                    }
                    profiler.EndTiming("Auto_Jump");
                }

                std::string capture_method = GetLastCaptureMethod();

                int overlay_x = frame.window_rect.left + crop_left;
                int overlay_y = frame.window_rect.top + crop_top;

                std::string status_text =
                    auto_jump_enabled.load() ? "自动跳跃中" : "等待指令中";

                profiler.StartTiming("Render_Debug");
                RenderDebugFrame(
                    monitor_enabled,
                    monitor_title,
                    monitor_scale,
                    {}, // 空 vector，反正函数里是 (void)cropped_bgr
                    cropped_w,
                    cropped_h,
                    overlay_x,
                    overlay_y,
                    roi_y_min,
                    roi_y_max,
                    character,
                    candidates,
                    target,
                    fps,
                    capture_method,
                    status_text,
                    fail_detection.detected,
                    fail_detection.match.match_x,
                    fail_detection.match.match_y,
                    fail_detection.template_width,
                    fail_detection.template_height
                );
                profiler.EndTiming("Render_Debug");

                profiler.EndTiming("Frame_Total");

                static int frame_counter = 0;
                if (++frame_counter >= 100) {
                    profiler.PrintStats();
                    profiler.Reset();
                    frame_counter = 0;
                }

                if (exit_requested.load() || session_stop.load()) {
                    break;
                }
            }
        });

        bool prev_ctrl_s = false;
        bool prev_ctrl_d = false;
        bool prev_space = false;
        bool prev_ctrl_q = false;

        while (!exit_requested.load() && !session_stop.load()) {
            bool key_ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
            bool key_s = (GetAsyncKeyState('S') & 0x8000) != 0;
            bool key_d = (GetAsyncKeyState('D') & 0x8000) != 0;
            bool key_space = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
            bool key_q = (GetAsyncKeyState('Q') & 0x8000) != 0;

            bool ctrl_s = key_ctrl && key_s;
            bool ctrl_d = key_ctrl && key_d;
            bool ctrl_q = key_ctrl && key_q;

            if (ctrl_s && !prev_ctrl_s) {
                auto_jump_enabled.store(true);
                Logger::Instance().Info("Auto jump enabled");
                profiler.Reset();
            }
            if (ctrl_d && !prev_ctrl_d) {
                auto_jump_enabled.store(false);
                Logger::Instance().Info("Auto jump disabled");
                profiler.Reset();
            }

            if (key_space && !prev_space) {
                manual_jump_requested.store(true);
            }

            if (ctrl_q && !prev_ctrl_q) {
                bool ok = adaptive_fitter.ExportSamplesCsv();
                Logger::Instance().Info(
                    ok ? "已导出样本CSV" : "导出样本CSV失败"
                );
            }

            prev_ctrl_s = ctrl_s;
            prev_ctrl_d = ctrl_d;
            prev_space = key_space;
            prev_ctrl_q = ctrl_q;

            MSG msg;
            while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        session_stop.store(true);
        if (worker.joinable()) {
            worker.join();
        }

        ShutdownDebugWindow();

        if (window_lost.load()) {
            return 1;
        }
        return 0;
    }

} // namespace play_runner
