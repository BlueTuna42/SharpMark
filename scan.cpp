#include "scan.h"
#include <glob.h>

std::vector<std::string> Scanner::scanBmpFiles(const std::string& path) {
    glob_t g;
    std::vector<std::string> results;
    
    // Vector with required file extensions
    std::vector<std::string> extensions = {
        "*.bmp", "*.jpg", "*.png", "*.psd", "*.jpeg", "*.ARW", "*.RW2", "*.CR2"
    };

    for (size_t i = 0; i < extensions.size(); ++i) {
        std::string pattern = path + "/" + extensions[i];
        int flags = (i == 0) ? 0 : GLOB_APPEND;
        glob(pattern.c_str(), flags, nullptr, &g);
    }

    for (size_t i = 0; i < g.gl_pathc; ++i) {
        results.push_back(g.gl_pathv[i]);
    }
    
    globfree(&g);
    return results;
}