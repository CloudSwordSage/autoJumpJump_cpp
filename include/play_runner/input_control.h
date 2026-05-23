#pragma once

#include <cstdint>

namespace play_runner {

    class InputControl {
        public:
            InputControl();

            void MoveMouseAbsolute(int x, int y);

            void LeftClick();
            void RightClick();

            std::uint64_t LeftLongPress(int duration_ms);
            std::uint64_t LeftLongPressAt(int x, int y, int duration_ms);
            std::uint64_t RightLongPress(int duration_ms);
            std::uint64_t RightLongPressAt(int x, int y, int duration_ms);
            bool IsLongPressCompleted(std::uint64_t task_id) const;

        private:
            void SendMouseEvent(
                std::uint32_t flags,
                int x,
                int y,
                bool absolute,
                int max_retries
            );
    };

} // namespace play_runner
