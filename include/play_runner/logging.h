#pragma once

#include <string>
#include <memory>
#include <vector>
#include <cstdint>

namespace play_runner {

    enum class LogLevel { Debug, Info, Warn, Error };

    class Logger {
        public:
            struct Entry {
                std::uint64_t seq;
                LogLevel level;
                std::string line;
            };

            static Logger & Instance();

            ~Logger();

            void SetLevel(LogLevel level);

            void SetFilePath(const std::string & path);

            void Debug(const std::string & message);
            void Info(const std::string & message);
            void Warn(const std::string & message);
            void Error(const std::string & message);

            std::uint64_t GetLastSeq() const;
            std::vector<Entry> GetEntriesSince(
                std::uint64_t after_seq,
                std::size_t max_count
            ) const;

        private:
            Logger();

            void Write(LogLevel level, const std::string & message);

            struct Impl;
            std::unique_ptr<Impl> impl_;
    };

} // namespace play_runner
