#pragma once

#include <cstdint>

namespace play_runner {

    class InputControl {
        public:
            InputControl();

            void MoveMouseAbsolute(int x, int y);

            void LeftClick();
            void RightClick();

            void LeftLongPress(int duration_ms);
            void LeftLongPressAt(int x, int y, int duration_ms);
            void RightLongPress(int duration_ms);
            void RightLongPressAt(int x, int y, int duration_ms);

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
