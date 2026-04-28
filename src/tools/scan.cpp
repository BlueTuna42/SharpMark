#include "scan.h"
#include <filesystem>

namespace fs = std::filesystem;

std::vector<std::string> Scanner::scanFiles(const std::string& path) {
    std::vector<std::string> res;
    std::vector<std::string> target_exts = {".bmp", ".jpg", ".png", ".jpeg", ".arw", ".dng", ".cr2", ".nef", ".rw2", ".raf"};

    for (const auto& entry : fs::directory_iterator(path)) {
        if (entry.is_regular_file()) {
            std::string ext = entry.path().extension().string();
            for(auto &c : ext) c = tolower(c);
            
            for(const auto& t_ext : target_exts) {
                if(ext == t_ext) {
                    res.push_back(entry.path().string());
                    break;
                }
            }
        }
    }
    return res;
}