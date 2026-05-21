#include "play_runner/internal/performance_profiler.h"

#include "play_runner/logging.h"

#include <iomanip>
#include <sstream>

namespace play_runner {

    PerformanceProfiler::PerformanceProfiler(bool enabled) : enabled_(enabled) {
    }

    void PerformanceProfiler::SetEnabled(bool enabled) {
        if (enabled_ == enabled) {
            return;
        }
        enabled_ = enabled;
        if (!enabled_) {
            Reset();
        }
    }

    void PerformanceProfiler::StartTiming(const std::string & name) {
        if (!enabled_) {
            return;
        }

        auto & data = timings_[name];
        data.last_start = std::chrono::steady_clock::now();
        data.is_running = true;
    }

    void PerformanceProfiler::EndTiming(const std::string & name) {
        if (!enabled_) {
            return;
        }

        auto & data = timings_[name];
        if (data.is_running) {
            auto end_time = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
                end_time - data.last_start
            );
            data.total_time += duration;
            data.call_count++;
            data.is_running = false;
        }
    }

    void PerformanceProfiler::PrintStats() {
        if (!enabled_ || timings_.empty()) {
            return;
        }

        Logger::Instance().Info("=== Performance Stats ===");
        for (const auto & [name, data] : timings_) {
            if (data.call_count > 0) {
                double avg_ms = (data.total_time.count() / 1000.0) / data.call_count;
                double total_ms = data.total_time.count() / 1000.0;
                double freq_hz = data.call_count / (total_ms / 1000.0);

                std::ostringstream oss;
                oss << std::fixed << std::setprecision(2);
                oss << name << ": "
                    << "avg=" << avg_ms << "ms, "
                    << "total=" << total_ms << "ms, "
                    << "calls=" << data.call_count << ", "
                    << "freq=" << freq_hz << "Hz";

                Logger::Instance().Info(oss.str());
            }
        }
        Logger::Instance().Info("========================");
    }

    void PerformanceProfiler::Reset() {
        timings_.clear();
    }

} // namespace play_runner
