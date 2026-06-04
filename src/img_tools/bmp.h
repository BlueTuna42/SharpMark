#ifndef BMP_H 
#define BMP_H

#include "../struct.h"
#include <memory>
#include <QString>

class ImageIO {
public:
    static std::unique_ptr<ImageBuffer> readImage(const QString& filename, int rawMode, bool wantRGB);
    static bool readOriginalSize(const QString& filename, int& w, int& h);
};

#endif