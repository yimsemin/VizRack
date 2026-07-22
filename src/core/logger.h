#pragma once

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

namespace vizrack {

enum class LogLevel { debug, info, warning, error };

class Logger {
public:
    Logger() = default;
    ~Logger();

    bool open(const std::filesystem::path& directory, size_t maxBytes = 1024 * 1024,
              unsigned maxFiles = 5);
    void log(LogLevel level, std::string_view message);
    void debug(std::string_view message) { log(LogLevel::debug, message); }
    void info(std::string_view message) { log(LogLevel::info, message); }
    void warning(std::string_view message) { log(LogLevel::warning, message); }
    void error(std::string_view message) { log(LogLevel::error, message); }
    bool available() const noexcept { return available_; }

private:
    void rotateIfNeeded(size_t incomingBytes);
    void rotate();

    mutable std::mutex mutex_;
    std::filesystem::path directory_;
    std::filesystem::path path_;
    std::ofstream stream_;
    size_t maxBytes_{1024 * 1024};
    unsigned maxFiles_{5};
    bool available_{false};
};

} // namespace vizrack
