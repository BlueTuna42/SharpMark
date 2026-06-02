#include "scan.h"
#include <QDirIterator>
#include <QFileInfo>

std::vector<QString> Scanner::scanFiles(const QString& path) {
    std::vector<QString> res;
    
    // Qt filters use wildcards
    QStringList nameFilters = {
        "*.jpg", "*.jpeg", "*.bmp", "*.png", // Standard formats
        "*.cr2", "*.cr3", "*.crw",           // Canon
        "*.nef", "*.nrw",                    // Nikon
        "*.arw", "*.srf", "*.sr2",           // Sony
        "*.pef", "*.ptx",                    // Pentax
        "*.dng",                             // Adobe
        "*.raf",                             // Fujifilm
        "*.orf",                             // Olympus
        "*.rw2",                             // Panasonic
        "*.srw",                             // Samsung
        "*.x3f",                             // Sigma
        "*.erf",                             // Epson
        "*.mef",                             // Mamiya
        "*.mos",                             // Leaf
        "*.kdc", "*.dcr",                    // Kodak
        "*.fff", "*.3fr",                    // Hasselblad
        "*.gpr",                             // GoPro
        "*.raw"                              // Generic RAW
    };

    QDirIterator it(path, nameFilters, QDir::Files | QDir::NoSymLinks, QDirIterator::NoIteratorFlags);
    
    while (it.hasNext()) {
        res.push_back(it.next());
    }

    return res;
}