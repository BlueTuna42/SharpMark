#ifndef LAPLACIAN_H 
#define LAPLACIAN_H

#include "../struct.h"
#include <fstream>
#include <filesystem>
#include <memory>

class LaplacianProcessor {
public:
    // Calculates the Laplacian variance for a specific image block
    static double blockVariance(const GrayscaleImage& img, int startX, int startY, int blockW, int blockH);

    // Evaluates sharpness using a grid (default 5x5) and returns the maximum block variance
    static double evaluateSharpness(const GrayscaleImage& img, int gridCols = 5, int gridRows = 5);

    static std::unique_ptr<GrayscaleImage> applyLaplacian(const GrayscaleImage& img);
    static double evaluateSharpnessFromLaplacian(const GrayscaleImage& lapImg, int gridCols = 5, int gridRows = 5);
    
    static bool saveLaplacian(const GrayscaleImage& lapImg, const std::filesystem::path& filepath);
    static std::unique_ptr<GrayscaleImage> loadLaplacian(const std::filesystem::path& filepath);
};

#endif