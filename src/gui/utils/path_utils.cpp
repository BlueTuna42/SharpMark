#include "path_utils.h"

#include <filesystem>

std::string path_filename(const std::string& path) {
    const std::filesystem::path fsPath(path);
    const std::string filename = fsPath.filename().string();
    return filename.empty() ? path : filename;
}

std::string directory_name(const std::string& path) {
    const std::filesystem::path fsPath(path);
    std::string name = fsPath.filename().string();
    if (name.empty() && fsPath.has_parent_path()) {
        name = fsPath.parent_path().filename().string();
    }
    return name.empty() ? path : name;
}
