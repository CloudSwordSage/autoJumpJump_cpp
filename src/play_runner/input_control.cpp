#include "play_runner/input_control.h"

#include <chrono>
#include <stdexcept>
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>

#include <windows.h>

#include "play_runner/logging.h"

namespace play_runner {

    namespace {
        // 全局持久化跳跃工作线程
        std::thread g_jump_worker_thread;
        std::queue<int> g_jump_queue;
        std::mutex g_jump_mutex;
        std::condition_variable g_jump_cv;
        std::atomic<bool> g_jump_worker_running{true};

        void JumpWorkerFunc() {
            while (g_jump_worker_running.load()) {
                int duration_ms = 0;
                {
                    std::unique_lock<std::mutex> lock(g_jump_mutex);
                    g_jump_cv.wait(lock, [] {
                        return !g_jump_queue.empty() || !g_jump_worker_running.load();
                    });
                    if (!g_jump_worker_running.load() && g_jump_queue.empty()) {
                        return;
                    }
                    if (g_jump_queue.empty()) {
                        continue;
                    }
                    duration_ms = g_jump_queue.front();
                    g_jump_queue.pop();
                }

                // 执行跳跃
                try {
                    INPUT input{};
                    input.type = INPUT_MOUSE;
                    input.mi.mouseData = 0;
                    input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
                    ::SendInput(1, &input, sizeof(INPUT));
                    std::this_thread::sleep_for(std::chrono::milliseconds(duration_ms));
                    input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
                    ::SendInput(1, &input, sizeof(INPUT));
                } catch (const std::exception & ex) {
                    Logger::Instance().Error(ex.what());
                }
            }
        }

        void EnsureJumpWorkerStarted() {
            static std::once_flag flag;
            std::call_once(flag, [] {
                g_jump_worker_thread = std::thread(JumpWorkerFunc);
            });
        }

    } // namespace

    namespace {

        void NormalizeToVirtualDesktop(
            int x,
            int y,
            LONG & out_dx,
            LONG & out_dy
        ) {
            const int vx = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
            const int vy = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
            const int vw = ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
            const int vh = ::GetSystemMetrics(SM_CYVIRTUALSCREEN);

            if (vw <= 1 || vh <= 1) {
                throw std::runtime_error("Invalid virtual screen size");
            }

            const double fx = (static_cast<double>(x - vx) * 65535.0) /
                              static_cast<double>(vw - 1);
            const double fy = (static_cast<double>(y - vy) * 65535.0) /
                              static_cast<double>(vh - 1);

            out_dx = static_cast<LONG>(fx);
            out_dy = static_cast<LONG>(fy);
        }

    } // namespace

    InputControl::InputControl() {
    }

    void InputControl::SendMouseEvent(
        std::uint32_t flags,
        int x,
        int y,
        bool absolute,
        int max_retries
    ) {
        int attempts = 0;
        while (true) {
            INPUT input{};
            input.type = INPUT_MOUSE;
            input.mi.mouseData = 0;
            input.mi.dwFlags = flags;

            if (absolute) {
                LONG dx = 0;
                LONG dy = 0;
                NormalizeToVirtualDesktop(x, y, dx, dy);
                input.mi.dx = dx;
                input.mi.dy = dy;
                input.mi.dwFlags |= MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE |
                                    MOUSEEVENTF_VIRTUALDESK;
            }

            UINT sent = ::SendInput(1, &input, sizeof(INPUT));
            if (sent == 1) {
                return;
            }

            attempts += 1;
            Logger::Instance().Warn("SendInput failed, retrying");

            if (attempts >= max_retries) {
                throw std::runtime_error("SendInput failed after retries");
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    void InputControl::MoveMouseAbsolute(int x, int y) {
        SendMouseEvent(0, x, y, true, 3);
    }

    void InputControl::LeftClick() {
        try {
            SendMouseEvent(MOUSEEVENTF_LEFTDOWN, 0, 0, false, 3);
            SendMouseEvent(MOUSEEVENTF_LEFTUP, 0, 0, false, 3);
        } catch (const std::exception & ex) {
            Logger::Instance().Error(ex.what());
        }
    }

    void InputControl::RightClick() {
        try {
            SendMouseEvent(MOUSEEVENTF_RIGHTDOWN, 0, 0, false, 3);
            SendMouseEvent(MOUSEEVENTF_RIGHTUP, 0, 0, false, 3);
        } catch (const std::exception & ex) {
            Logger::Instance().Error(ex.what());
        }
    }

    void InputControl::LeftLongPress(int duration_ms) {
        if (duration_ms < 0) {
            throw std::invalid_argument("duration_ms must be non-negative");
        }

        // 优化：限制队列长度为 1，防止堆积
        EnsureJumpWorkerStarted();
        {
            std::lock_guard<std::mutex> lock(g_jump_mutex);
            if (!g_jump_queue.empty()) {
                return; // 已有跳跃待执行，丢弃本次
            }
            g_jump_queue.push(duration_ms);
        }
        g_jump_cv.notify_one();
    }

    void InputControl::RightLongPress(int duration_ms) {
        if (duration_ms < 0) {
            throw std::invalid_argument("duration_ms must be non-negative");
        }

        // 优化：限制队列长度为 1，防止堆积
        EnsureJumpWorkerStarted();
        {
            std::lock_guard<std::mutex> lock(g_jump_mutex);
            if (!g_jump_queue.empty()) {
                return; // 已有跳跃待执行，丢弃本次
            }
            g_jump_queue.push(duration_ms);
        }
        g_jump_cv.notify_one();
    }

} // namespace play_runner
