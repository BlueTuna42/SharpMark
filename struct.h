#ifndef STRUCT_H 
#define STRUCT_H

#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <fftw3.h>

#define PATH_MAX 256

// Keep BMP structures for backward compatibility if needed
#pragma pack(push, 1)
struct BITMAPFILEHEADER {
    uint16_t bfType;      // BM identifier, should be 0x4D42
    uint32_t bfSize;      // Size of the file in bytes
    uint16_t bfReserved1; // Reserved
    uint16_t bfReserved2; // Reserved
    uint32_t bfOffBits;   // Offset to start of pixel data
};

struct BITMAPINFOHEADER {
    uint32_t biSize;          // Size of this header (40 bytes)
    int32_t  biWidth;         // Width in pixels
    int32_t  biHeight;        // Height in pixels
    uint16_t biPlanes;        // Number of color planes
    uint16_t biBitCount;      // Bits per pixel (e.g., 24)
    uint32_t biCompression;   // Compression type (0 for uncompressed)
    uint32_t biSizeImage;     // Image size in bytes
    int32_t  biXPelsPerMeter; // Horizontal resolution
    int32_t  biYPelsPerMeter; // Vertical resolution
    uint32_t biClrUsed;       // Number of colors in the color palette
    uint32_t biClrImportant;  // Number of important colors
};
#pragma pack(pop)

struct Pixel {
    unsigned int blue;
    unsigned int green;
    unsigned int red;
};

// Class for storing RGB image data
class RGBImage {
public:
    int width;
    int height;
    fftwf_complex *red;
    fftwf_complex *green;
    fftwf_complex *blue;

    // Constructor: allocates memory using fftwf_malloc for proper alignment (SIMD)
    RGBImage(int w, int h) : width(w), height(h) {
        int total = width * height;
        red = static_cast<fftwf_complex*>(fftwf_malloc(sizeof(fftwf_complex) * total));
        green = static_cast<fftwf_complex*>(fftwf_malloc(sizeof(fftwf_complex) * total));
        blue = static_cast<fftwf_complex*>(fftwf_malloc(sizeof(fftwf_complex) * total));
    }

    // Destructor: automatic memory deallocation
    ~RGBImage() {
        if (red) fftwf_free(red);
        if (green) fftwf_free(green);
        if (blue) fftwf_free(blue);
    }

    // Disable copying to prevent double memory freeing
    RGBImage(const RGBImage&) = delete;
    RGBImage& operator=(const RGBImage&) = delete;
};

// Class for storing complex RGB spectra of the image
class ComplexRGB {
public:
    int width;
    int height;
    fftwf_complex *red;
    fftwf_complex *green;
    fftwf_complex *blue;

    ComplexRGB(int w, int h) : width(w), height(h) {
        int total = width * height;
        red = static_cast<fftwf_complex*>(fftwf_malloc(sizeof(fftwf_complex) * total));
        green = static_cast<fftwf_complex*>(fftwf_malloc(sizeof(fftwf_complex) * total));
        blue = static_cast<fftwf_complex*>(fftwf_malloc(sizeof(fftwf_complex) * total));
    }

    ~ComplexRGB() {
        if (red) fftwf_free(red);
        if (green) fftwf_free(green);
        if (blue) fftwf_free(blue);
    }

    ComplexRGB(const ComplexRGB&) = delete;
    ComplexRGB& operator=(const ComplexRGB&) = delete;
};

#endif