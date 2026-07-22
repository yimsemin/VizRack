#include "core/logger.h"

#include <chrono>
#include <format>
#include <system_error>

namespace vizrack {
namespace {

const char* levelName(LogLevel level) {
    switch (level) {
        case LogLevel::debug: return "DEBUG";
        case LogLevel::info: return "INFO";
        case LogLevel::warning: return "WARN";
        case LogLevel::error: return "ERROR";
    }
    return "INFO";
}

} // namespace

Logger::~Logger() {
    std::scoped_lock lock(mutex_);
    if (stream_) stream_.flush();
}

bool Logger::open(const std::filesystem::path& directory, size_t maxBytes, unsigned maxFiles) {
    std::scoped_lock lock(mutex_);
    directory_ = directory;
    path_ = directory_ / L"vizrack.log";
    maxBytes_ = maxBytes;
    maxFiles_ = std::max(1u, maxFiles);
    std::error_code error;
    std::filesystem::create_directories(directory_, error);
    if (error) return false;
    stream_.open(path_, std::ios::binary | std::ios::app);
    available_ = stream_.is_open();
    return available_;
}

void Logger::log(LogLevel level, std::string_view message) {
    std::scoped_lock lock(mutex_);
    if (!available_) return;
    const auto now = std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now());
    const std::string line = std::format("{:%FT%T} [{}] {}\r\n", now, levelName(level), message);
    rotateIfNeeded(line.size());
    stream_.write(line.data(), static_cast<std::streamsize>(line.size()));
    stream_.flush();
}

void Logger::rotateIfNeeded(size_t incomingBytes) {
    std::error_code error;
    const uintmax_t size = std::filesystem::exists(path_, error)
                               ? std::filesystem::file_size(path_, error)
                               : 0;
    if (!error && size + incomingBytes > maxBytes_) rotate();
}

void Logger::rotate() {
    stream_.close();
    std::error_code error;
    for (unsigned index = maxFiles_; index > 1; --index) {
        const auto source = directory_ / (L"vizrack." + std::to_wstring(index - 1) + L".log");
        const auto destination = directory_ / (L"vizrack." + std::to_wstring(index) + L".log");
        std::filesystem::remove(destination, error);
        error.clear();
        if (std::filesystem::exists(source, error)) {
            std::filesystem::rename(source, destination, error);
        }
        error.clear();
    }
    const auto first = directory_ / L"vizrack.1.log";
    std::filesystem::remove(first, error);
    error.clear();
    if (std::filesystem::exists(path_, error)) std::filesystem::rename(path_, first, error);
    stream_.open(path_, std::ios::binary | std::ios::trunc);
    available_ = stream_.is_open();
}

} // namespace vizrack
