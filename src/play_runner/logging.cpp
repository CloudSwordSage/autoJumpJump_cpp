#include "play_runner/logging.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <deque>
#include <mutex>
#include <sstream>
#include <string>
#include <memory>
#include <vector>

namespace play_runner {

    namespace {

        std::string LevelToString(LogLevel level) {
            switch (level) {
                case LogLevel::Debug:
                    return "DEBUG";
                case LogLevel::Info:
                    return "INFO";
                case LogLevel::Warn:
                    return "WARN";
                case LogLevel::Error:
                    return "ERROR";
            }
            return "INFO";
        }

    } // namespace

    struct Logger::Impl {
            std::mutex mutex;
            std::ofstream file_stream;
            std::string file_path;
            LogLevel level;
            std::uint64_t seq;
            std::deque<Logger::Entry> entries;
            std::size_t max_entries;

            Impl() : level(LogLevel::Info), seq(0), max_entries(5000) {
                try {
                    std::filesystem::create_directories("logs");
                } catch (...) {
                }
            }

            ~Impl() {
                if (file_stream.is_open()) {
                    file_stream.close();
                }
            }
    };

    Logger::Logger() : impl_(std::make_unique<Impl>()) {
    }

    Logger::~Logger() = default;

    Logger & Logger::Instance() {
        static Logger instance;
        return instance;
    }

    void Logger::SetLevel(LogLevel level) {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->level = level;
    }

    void Logger::SetFilePath(const std::string & path) {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->file_stream.is_open()) {
            impl_->file_stream.close();
        }
        impl_->file_path = path;
        if (!impl_->file_path.empty()) {
            try {
                std::filesystem::path p(impl_->file_path);
                if (p.has_parent_path()) {
                    std::filesystem::create_directories(p.parent_path());
                }
                impl_->file_stream.open(impl_->file_path, std::ios::app);
            } catch (...) {
            }
        }
    }

    void Logger::Debug(const std::string & message) {
        Write(LogLevel::Debug, message);
    }

    void Logger::Info(const std::string & message) {
        Write(LogLevel::Info, message);
    }

    void Logger::Warn(const std::string & message) {
        Write(LogLevel::Warn, message);
    }

    void Logger::Error(const std::string & message) {
        Write(LogLevel::Error, message);
    }

    void Logger::Write(LogLevel level, const std::string & message) {
        {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            if (level < impl_->level) {
                return;
            }
        }

        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      now.time_since_epoch()
                  ) %
                  1000;

        std::tm tm_time;
#ifdef _WIN32
        localtime_s(&tm_time, &time);
#else
        localtime_r(&time, &tm_time);
#endif

        std::ostringstream oss;
        oss << std::put_time(&tm_time, "%Y-%m-%d %H:%M:%S") << '.'
            << std::setfill('0') << std::setw(3) << ms.count() << " ["
            << LevelToString(level) << "] " << message;

        const std::string line = oss.str();

        std::cerr << line << '\n';

        {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            impl_->seq += 1;
            impl_->entries.push_back(Logger::Entry{impl_->seq, level, line});
            if (impl_->entries.size() > impl_->max_entries) {
                impl_->entries.pop_front();
            }

            if (impl_->file_stream.is_open()) {
                impl_->file_stream << line << '\n';
                impl_->file_stream.flush();
            }
        }
    }

    std::uint64_t Logger::GetLastSeq() const {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        return impl_->seq;
    }

    std::vector<Logger::Entry> Logger::GetEntriesSince(
        std::uint64_t after_seq,
        std::size_t max_count
    ) const {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        std::vector<Logger::Entry> out;
        out.reserve(impl_->entries.size());
        for (const auto & e : impl_->entries) {
            if (e.seq > after_seq) {
                out.push_back(e);
            }
        }
        if (max_count > 0 && out.size() > max_count) {
            out.erase(
                out.begin(),
                out.end() - static_cast<std::ptrdiff_t>(max_count)
            );
        }
        return out;
    }

} // namespace play_runner
