#pragma once

#include <string>
#include <memory>

namespace play_runner {

    enum class LogLevel { Debug, Info, Warn, Error };

    class Logger {
        public:
            static Logger & Instance();

            ~Logger();

            void SetLevel(LogLevel level);

            void SetFilePath(const std::string & path);

            void Debug(const std::string & message);
            void Info(const std::string & message);
            void Warn(const std::string & message);
            void Error(const std::string & message);

        private:
            Logger();

            void Write(LogLevel level, const std::string & message);

            struct Impl;
            std::unique_ptr<Impl> impl_;
    };

} // namespace play_runner
