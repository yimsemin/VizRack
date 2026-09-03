#include "core/atomic_file.h"

#include <windows.h>

#include <fstream>
#include <system_error>

namespace vizrack {

bool writeFileAtomically(const std::filesystem::path& path, std::span<const uint8_t> bytes,
                         std::string_view description, std::string& error) {
    std::error_code filesystemError;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), filesystemError);
        if (filesystemError) {
            error = std::string(description) + ": could not create the folder: " +
                    filesystemError.message();
            return false;
        }
    }

    const auto temporary = path.wstring() + L".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = std::string(description) + ": could not open the temporary file.";
        return false;
    }
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    output.flush();
    output.close();
    if (!output) {
        error = std::string(description) + ": write failed.";
        return false;
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const std::error_code moveError(static_cast<int>(GetLastError()), std::system_category());
        error = std::string(description) + ": atomic replace failed: " + moveError.message();
        std::filesystem::remove(temporary, filesystemError);
        return false;
    }
    return true;
}

} // namespace vizrack
