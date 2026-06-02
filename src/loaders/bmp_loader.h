#pragma once
#include "../pipeline/interfaces.h"
#include "../img_tools/bmp.h"

class DefaultImageLoader : public IImageLoader {
public:
    std::unique_ptr<ImageBuffer> load(const ProcessingContext& ctx) override {
        return ImageIO::readImage(ctx.rawFilePath, ctx.settings.rawAnalysisMode, true);
    }
};