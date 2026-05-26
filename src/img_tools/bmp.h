#ifndef BMP_H 
#define BMP_H

#include "../struct.h"
#include <string>
#include <memory>

class ImageIO {
public:
    static std::unique_ptr<ImageBuffer> readImage(const std::string& filename, int rawMode, bool wantRGB);
    static bool readOriginalSize(const std::string& filename, int& w, int& h);
};

#endif