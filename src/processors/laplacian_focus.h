#pragma once
#include "../pipeline/interfaces.h"
#include "../img_tools/laplacian.h"

class LaplacianFocusProcessor : public IImageProcessor {
private:
    const double FOCUS_CONST = 150.0;
    std::filesystem::path getCachePath(const ProcessingContext& ctx) const {
        return ctx.cacheDir / (ctx.filePath.filename().string() + ".rawlap");
    }

public:
    std::string name() const override { return "laplacian_focus"; }

    bool supports(const ProcessingContext& ctx) const override { return true; }

    bool tryProcessFromCache(const ProcessingContext& ctx, ProcessingResult& result) override {
        if (!ctx.settings.cacheLaplacian) return false;

        auto cacheFile = getCachePath(ctx);
        if (std::filesystem::exists(cacheFile)) {
            auto lapImg = LaplacianProcessor::loadLaplacian(cacheFile);
            if (lapImg) {
                double maxVariance = LaplacianProcessor::evaluateSharpnessFromLaplacian(*lapImg, 5, 5);
                
                // Set the final verdict
                result.isBlurry = (maxVariance < FOCUS_CONST);
                result.metrics.push_back({"variance", maxVariance});
                
                // Share this calculation with next processors just in case
                result.sharedData["laplacian_variance"] = maxVariance; 
                return true; 
            }
        }
        return false;
    }

    void process(std::unique_ptr<GrayscaleImage>& image, const ProcessingContext& ctx, ProcessingResult& result) override {
        double maxVariance = 0.0;

        if (ctx.settings.cacheLaplacian) {
            auto lapImg = LaplacianProcessor::applyLaplacian(*image);
            maxVariance = LaplacianProcessor::evaluateSharpnessFromLaplacian(*lapImg, 5, 5);
            std::error_code ec;
            if (!std::filesystem::exists(ctx.cacheDir, ec)) {
                std::filesystem::create_directories(ctx.cacheDir, ec);
            }
            LaplacianProcessor::saveLaplacian(*lapImg, getCachePath(ctx));
        } else {
            maxVariance = LaplacianProcessor::evaluateSharpness(*image, 5, 5);
        }

        result.isBlurry = (maxVariance < FOCUS_CONST);
        result.metrics.push_back({"variance", maxVariance});
        result.sharedData["laplacian_variance"] = maxVariance; // Export to Blackboard
    }
};