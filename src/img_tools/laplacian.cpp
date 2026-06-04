#include "laplacian.h"
#include <algorithm>

double LaplacianProcessor::blockVariance(const GrayscaleImage& img, int startX, int startY, int blockW, int blockH) {
    if (blockW <= 2 || blockH <= 2) return 0.0;

    double sum = 0.0;
    double sq_sum = 0.0;
    int count = (blockW - 2) * (blockH - 2); 
    
    // Laplacian kernel:
    //  0  1  0
    //  1 -4  1
    //  0  1  0

    for (int y = startY + 1; y < startY + blockH - 1; ++y) {
        int rowOffset = y * img.width;
        int rowAbove = (y - 1) * img.width;
        int rowBelow = (y + 1) * img.width;

        for (int x = startX + 1; x < startX + blockW - 1; ++x) {
            float top    = img.data[rowAbove + x];
            float bottom = img.data[rowBelow + x];
            float left   = img.data[rowOffset + (x - 1)];
            float right  = img.data[rowOffset + (x + 1)];
            float center = img.data[rowOffset + x];

            double laplacian = top + bottom + left + right - 4.0 * center;
            
            sum += laplacian;
            sq_sum += laplacian * laplacian;
        }
    }

    double mean = sum / count;
    double variance = (sq_sum / count) - (mean * mean);
    
    return variance;
}

double LaplacianProcessor::evaluateSharpness(const GrayscaleImage& img, int gridCols, int gridRows) {
    std::unique_ptr<GrayscaleImage> convertedGray;
    const GrayscaleImage* sourceImgPtr = &img;

    if (img.channels == 3) {
        convertedGray = std::make_unique<GrayscaleImage>(img.width, img.height, 1);
        const int total = img.width * img.height;

        for (int i = 0; i < total; ++i) {
            convertedGray->data[i] =
                0.299f * img.data[i * 3 + 0] +
                0.587f * img.data[i * 3 + 1] +
                0.114f * img.data[i * 3 + 2];
        }

        sourceImgPtr = convertedGray.get();
    }

    const GrayscaleImage& sourceImg = *sourceImgPtr;

    int blockW = sourceImg.width / gridCols;
    int blockH = sourceImg.height / gridRows;
    double maxVar = 0.0;

    for (int r = 0; r < gridRows; ++r) {
        for (int c = 0; c < gridCols; ++c) {
            int startX = c * blockW;
            int startY = r * blockH;
            int curW = (c == gridCols - 1) ? (sourceImg.width - startX) : blockW;
            int curH = (r == gridRows - 1) ? (sourceImg.height - startY) : blockH;

            double var = blockVariance(sourceImg, startX, startY, curW, curH);
            if (var > maxVar) {
                maxVar = var;
            }
        }
    }

    return maxVar;
}

std::unique_ptr<GrayscaleImage> LaplacianProcessor::applyLaplacian(const GrayscaleImage& img) {
    auto lapImg = std::make_unique<GrayscaleImage>(img.width, img.height);

    for (int y = 1; y < img.height - 1; ++y) {
        int rowOffset = y * img.width;
        int rowAbove = (y - 1) * img.width;
        int rowBelow = (y + 1) * img.width;

        for (int x = 1; x < img.width - 1; ++x) {
            float top = img.data[rowAbove + x];
            float bottom = img.data[rowBelow + x];
            float left = img.data[rowOffset + (x - 1)];
            float right = img.data[rowOffset + (x + 1)];
            float center = img.data[rowOffset + x];

            lapImg->data[rowOffset + x] = top + bottom + left + right - 4.0f * center;
        }
    }
    return lapImg;
}

// Calculates variance from an already computed Laplacian
double LaplacianProcessor::evaluateSharpnessFromLaplacian(const GrayscaleImage& lapImg, int gridCols, int gridRows) {
    int blockW = lapImg.width / gridCols;
    int blockH = lapImg.height / gridRows;
    double maxVar = 0.0;

    for (int r = 0; r < gridRows; ++r) {
        for (int c = 0; c < gridCols; ++c) {
            int startX = c * blockW;
            int startY = r * blockH;
            int curW = (c == gridCols - 1) ? (lapImg.width - startX) : blockW;
            int curH = (r == gridRows - 1) ? (lapImg.height - startY) : blockH;

            if (curW <= 2 || curH <= 2) continue;

            double sum = 0.0;
            double sq_sum = 0.0;
            int count = (curW - 2) * (curH - 2);

            for (int y = startY + 1; y < startY + curH - 1; ++y) {
                int rowOffset = y * lapImg.width;
                for (int x = startX + 1; x < startX + curW - 1; ++x) {
                    float val = lapImg.data[rowOffset + x];
                    sum += val;
                    sq_sum += val * val;
                }
            }
            
            if (count > 0) {
                double mean = sum / count;
                double variance = (sq_sum / count) - (mean * mean);
                maxVar = std::max(maxVar, variance);
            }
        }
    }
    return maxVar;
}

// Save the as a raw binary float array
bool LaplacianProcessor::saveLaplacian(const GrayscaleImage& lapImg, const std::filesystem::path& filepath) {
    std::ofstream out(filepath, std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(&lapImg.width), sizeof(int));
    out.write(reinterpret_cast<const char*>(&lapImg.height), sizeof(int));
    size_t dataSize = static_cast<size_t>(lapImg.width) * lapImg.height * sizeof(float);
    out.write(reinterpret_cast<const char*>(lapImg.data), dataSize);
    return true;
}

// Loads from cache
std::unique_ptr<GrayscaleImage> LaplacianProcessor::loadLaplacian(const std::filesystem::path& filepath) {
    std::ifstream in(filepath, std::ios::binary);
    if (!in) return nullptr;
    
    int w = 0, h = 0;
    in.read(reinterpret_cast<char*>(&w), sizeof(int));
    in.read(reinterpret_cast<char*>(&h), sizeof(int));
    
    if (w <= 0 || h <= 0) return nullptr;
    
    auto lapImg = std::make_unique<GrayscaleImage>(w, h);
    size_t dataSize = static_cast<size_t>(w) * h * sizeof(float);
    in.read(reinterpret_cast<char*>(lapImg->data), dataSize);
    
    if (in.gcount() != dataSize) return nullptr;
    return lapImg;
}