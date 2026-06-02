#include "path_utils.h"
#include <cstdlib>
#include <filesystem>
#include <QFileInfo>
#include <QDir>

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

QString path_filename(const QString& path) {
    return QFileInfo(path).fileName();
}

QString directory_name(const QString& path) {
    QFileInfo fi(path);
    if (fi.isDir()) {
        return fi.fileName();
    }
    return fi.dir().dirName();
}