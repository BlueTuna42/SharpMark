#ifndef STRUCT_H 
#define STRUCT_H

#include <cstddef>
#include <vector>

class ImageBuffer {
public:
    int width;
    int height;
    int channels;
    float *data; // 1 channel = Grayscale, 3 channels = RGB (Interleaved: RGBRGBRGB...)

    ImageBuffer(int w, int h, int c = 1) : width(w), height(h), channels(c) {
        size_t total = (size_t)width * height * channels;
        data = new float[total]();
    }

    ~ImageBuffer() {
        if (data) delete[] data;
    }

    ImageBuffer(const ImageBuffer&) = delete;
    ImageBuffer& operator=(const ImageBuffer&) = delete;
};

// Alias to avoid breaking old Laplacian code that explicitly used "GrayscaleImage"
using GrayscaleImage = ImageBuffer;

#endif