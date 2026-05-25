#ifndef BMP_H 
#define BMP_H

#include "../struct.h"
#include <string>
#include <memory>

class ImageIO {
public:
    static std::unique_ptr<GrayscaleImage> readImage(const std::string& filename, int rawMode = 0);
};

#endif