#ifndef PATH_UTILS_H
#define PATH_UTILS_H

#include <string>
#include <filesystem>

std::string path_filename(const std::string& path);
std::string directory_name(const std::string& path);
std::filesystem::path get_app_config_dir();

#endif
