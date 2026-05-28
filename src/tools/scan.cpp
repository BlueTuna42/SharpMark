#include "scan.h"
#include <filesystem>

namespace fs = std::filesystem;

std::vector<std::string> Scanner::scanFiles(const std::string& path) {
    std::vector<std::string> res;
    std::vector<std::string> target_exts = {
        // Standard formats
        ".jpg", ".jpeg", ".bmp", ".png", 
        // Canon
        ".cr2", ".cr3", ".crw",
        // Nikon
        ".nef", ".nrw",
        // Sony
        ".arw", ".srf", ".sr2",
        // Pentax
        ".pef", ".ptx",
        // Adobe
        ".dng",
        // Fujifilm
        ".raf",
        // Olympus
        ".orf",
        // Panasonic
        ".rw2",
        // Samsung
        ".srw",
        // Sigma
        ".x3f",
        // Epson
        ".erf",
        // Mamiya
        ".mef",
        // Leaf
        ".mos",
        // Kodak
        ".kdc", ".dcr",
        // Hasselblad
        ".fff", ".3fr",
        // GoPro
        ".gpr",
        // Generic RAW
        ".raw"
    };

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