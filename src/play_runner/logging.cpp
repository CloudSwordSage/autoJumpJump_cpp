#include "play_runner/logging.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <memory>

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

        Impl() : level(LogLevel::Info) {
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

    Logger::Logger()
        : impl_(std::make_unique<Impl>()) {
    }

    Logger::~Logger() = default;

    Logger & Logger::Instance() {
        static Logger instance;
        return instance;
    }

    void Logger::SetLevel(LogLevel level) {
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
        if (level < impl_->level) {
            return;
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

        if (impl_->file_stream.is_open()) {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            impl_->file_stream << line << '\n';
            impl_->file_stream.flush();
        }
    }

} // namespace play_runner
