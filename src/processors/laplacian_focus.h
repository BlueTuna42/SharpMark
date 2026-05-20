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

    std::optional<ProcessingResult> tryProcessFromCache(const ProcessingContext& ctx) override {
        if (!ctx.settings.cacheLaplacian) return std::nullopt;

        auto cacheFile = getCachePath(ctx);
        if (std::filesystem::exists(cacheFile)) {
            auto lapImg = LaplacianProcessor::loadLaplacian(cacheFile.string());
            if (lapImg) {
                double maxVariance = LaplacianProcessor::evaluateSharpnessFromLaplacian(*lapImg, 5, 5);
                ProcessingResult res;
                res.isBlurry = (maxVariance < FOCUS_CONST);
                res.processorName = name();
                res.metrics.push_back({"variance", maxVariance});
                return res;
            }
        }
        return std::nullopt;
    }

    ProcessingResult process(const GrayscaleImage& image, const ProcessingContext& ctx) override {
        ProcessingResult res;
        res.processorName = name();
        double maxVariance = 0.0;

        if (ctx.settings.cacheLaplacian) {
            auto lapImg = LaplacianProcessor::applyLaplacian(image);
            maxVariance = LaplacianProcessor::evaluateSharpnessFromLaplacian(*lapImg, 5, 5);
            
            std::error_code ec;
            if (!std::filesystem::exists(ctx.cacheDir, ec)) {
                std::filesystem::create_directories(ctx.cacheDir, ec);
            }
            LaplacianProcessor::saveLaplacian(*lapImg, getCachePath(ctx).string());
        } else {
            maxVariance = LaplacianProcessor::evaluateSharpness(image, 5, 5);
        }

        res.isBlurry = (maxVariance < FOCUS_CONST);
        res.metrics.push_back({"variance", maxVariance});
        return res;
    }
};