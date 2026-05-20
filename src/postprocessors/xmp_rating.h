#pragma once
#include "../pipeline/interfaces.h"
#include "../tools/XMP_tools.h"

class XmpRatingPostProcessor : public IPostProcessor {
public:
    std::string name() const override { return "xmp_rating"; }

    void handle(const ProcessingContext& ctx, const ProcessingResult& result) override {
        if (ctx.settings.writeExif && result.success) {
            int rating = result.isBlurry ? 1 : 5;
            XMPTools::writeXmpRating(ctx.filePath.string(), rating);
        }
    }
};