#include "path_utils.h"
#include <cstdlib>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#endif

std::filesystem::path get_app_config_dir() {
#ifdef _WIN32
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(NULL, buffer, MAX_PATH);
    std::filesystem::path exePath(buffer);
    return exePath.parent_path();
#else
    std::filesystem::path configDir;
    const char *homeDir = getenv("HOME");
    if (!homeDir) {
        struct passwd *pwd = getpwuid(getuid());
        if (pwd) homeDir = pwd->pw_dir;
    }
    if (homeDir) {
        configDir = std::filesystem::path(homeDir) / ".config" / "SharpMark";
    } else {
        configDir = std::filesystem::current_path() / ".sharpmark";
    }

    if (!std::filesystem::exists(configDir)) {
        std::error_code ec;
        std::filesystem::create_directories(configDir, ec);
    }
    return configDir;
#endif
}

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
