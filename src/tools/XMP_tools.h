#ifndef XMPTOOLS_H
#define XMPTOOLS_H

#include <string>

namespace XMPTools {
    int writeXmpRating(const std::string& exiftoolPath, const std::string& sourceFile, const std::string& targetFile, int rating);
}

#endif