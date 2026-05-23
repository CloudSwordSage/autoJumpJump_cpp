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
        enum class MouseButton { Left, Right };

        struct LongPressTask {
                std::uint64_t id;
                MouseButton button;
                int x;
                int y;
                int duration_ms;
        };

        void NormalizeToVirtualDesktop(
            int x,
            int y,
            LONG & out_dx,
            LONG & out_dy
        );

        // 全局持久化跳跃工作线程
        std::thread g_jump_worker_thread;
        std::queue<LongPressTask> g_jump_queue;
        std::mutex g_jump_mutex;
        std::condition_variable g_jump_cv;
        std::atomic<bool> g_jump_worker_running{true};
        std::atomic<std::uint64_t> g_next_task_id{1};
        std::atomic<std::uint64_t> g_last_completed_task_id{0};

        void JumpWorkerFunc() {
            while (g_jump_worker_running.load()) {
                LongPressTask task{};
                {
                    std::unique_lock<std::mutex> lock(g_jump_mutex);
                    g_jump_cv.wait(lock, [] {
                        return !g_jump_queue.empty() ||
                               !g_jump_worker_running.load();
                    });
                    if (!g_jump_worker_running.load() && g_jump_queue.empty()) {
                        return;
                    }
                    if (g_jump_queue.empty()) {
                        continue;
                    }
                    task = g_jump_queue.front();
                    g_jump_queue.pop();
                }

                // 执行跳跃
                try {
                    LONG dx = 0;
                    LONG dy = 0;
                    NormalizeToVirtualDesktop(task.x, task.y, dx, dy);
                    {
                        INPUT move{};
                        move.type = INPUT_MOUSE;
                        move.mi.dx = dx;
                        move.mi.dy = dy;
                        move.mi.mouseData = 0;
                        move.mi.dwFlags = MOUSEEVENTF_MOVE |
                                          MOUSEEVENTF_ABSOLUTE |
                                          MOUSEEVENTF_VIRTUALDESK;
                        if (::SendInput(1, &move, sizeof(INPUT)) != 1) {
                            throw std::runtime_error("SendInput move failed");
                        }
                    }

                    const std::uint32_t down_flag =
                        task.button == MouseButton::Left
                            ? MOUSEEVENTF_LEFTDOWN
                            : MOUSEEVENTF_RIGHTDOWN;
                    const std::uint32_t up_flag =
                        task.button == MouseButton::Left ? MOUSEEVENTF_LEFTUP
                                                         : MOUSEEVENTF_RIGHTUP;

                    INPUT input{};
                    input.type = INPUT_MOUSE;
                    input.mi.mouseData = 0;
                    input.mi.dwFlags = down_flag;
                    if (::SendInput(1, &input, sizeof(INPUT)) != 1) {
                        throw std::runtime_error("SendInput down failed");
                    }
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(task.duration_ms)
                    );
                    input.mi.dwFlags = up_flag;
                    if (::SendInput(1, &input, sizeof(INPUT)) != 1) {
                        throw std::runtime_error("SendInput up failed");
                    }
                } catch (const std::exception & ex) {
                    Logger::Instance().Error(ex.what());
                }
                g_last_completed_task_id.store(task.id);
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

    std::uint64_t InputControl::LeftLongPress(int duration_ms) {
        POINT cursor{};
        if (!::GetCursorPos(&cursor)) {
            throw std::runtime_error("GetCursorPos failed");
        }
        return LeftLongPressAt(cursor.x, cursor.y, duration_ms);
    }

    std::uint64_t InputControl::LeftLongPressAt(int x, int y, int duration_ms) {
        if (duration_ms < 0) {
            throw std::invalid_argument("duration_ms must be non-negative");
        }

        // 优化：限制队列长度为 1，防止堆积
        EnsureJumpWorkerStarted();
        const std::uint64_t id = g_next_task_id.fetch_add(1);
        {
            std::lock_guard<std::mutex> lock(g_jump_mutex);
            if (!g_jump_queue.empty()) {
                return 0; // 已有跳跃待执行，丢弃本次
            }
            g_jump_queue.push({id, MouseButton::Left, x, y, duration_ms});
        }
        g_jump_cv.notify_one();
        return id;
    }

    std::uint64_t InputControl::RightLongPress(int duration_ms) {
        POINT cursor{};
        if (!::GetCursorPos(&cursor)) {
            throw std::runtime_error("GetCursorPos failed");
        }
        return RightLongPressAt(cursor.x, cursor.y, duration_ms);
    }

    std::uint64_t InputControl::RightLongPressAt(int x, int y, int duration_ms) {
        if (duration_ms < 0) {
            throw std::invalid_argument("duration_ms must be non-negative");
        }

        // 优化：限制队列长度为 1，防止堆积
        EnsureJumpWorkerStarted();
        const std::uint64_t id = g_next_task_id.fetch_add(1);
        {
            std::lock_guard<std::mutex> lock(g_jump_mutex);
            if (!g_jump_queue.empty()) {
                return 0; // 已有跳跃待执行，丢弃本次
            }
            g_jump_queue.push({id, MouseButton::Right, x, y, duration_ms});
        }
        g_jump_cv.notify_one();
        return id;
    }

    bool InputControl::IsLongPressCompleted(std::uint64_t task_id) const {
        if (task_id == 0) {
            return false;
        }
        return g_last_completed_task_id.load() >= task_id;
    }

} // namespace play_runner
