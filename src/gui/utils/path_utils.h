#ifndef PATH_UTILS_H
#define PATH_UTILS_H

#include <QString>
#include <filesystem>

QString path_filename(const QString& path);
QString directory_name(const QString& path);
std::filesystem::path get_app_config_dir();

#endif