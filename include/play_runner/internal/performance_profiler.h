#pragma once

#include <chrono>
#include <map>
#include <string>

namespace play_runner {

    class PerformanceProfiler {
        private:
            struct TimingData {
                    std::chrono::microseconds total_time{0};
                    int call_count = 0;
                    std::chrono::steady_clock::time_point last_start;
                    bool is_running = false;
            };

            std::map<std::string, TimingData> timings_;
            bool enabled_;

        public:
            explicit PerformanceProfiler(bool enabled);

            void StartTiming(const std::string & name);
            void EndTiming(const std::string & name);

            void PrintStats();
            void Reset();
    };

} // namespace play_runner

