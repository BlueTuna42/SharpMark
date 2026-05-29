#pragma once
#include "../pipeline/interfaces.h"
#include <string>

class ExposureCheckProcessor : public IImageProcessor {
private:
    float m_clipThreshold; 
public:
    // If more than x% of pixels are pure black (0) or pure white (255), reject it
    explicit ExposureCheckProcessor(float clipThreshold = 0.15f) : m_clipThreshold(clipThreshold) {}

    std::string name() const override { return "exposure_check"; }
    bool supports(const ProcessingContext& ctx) const override { return true; }

    // Exposure check is fast enough we don't necessarily need to cache it, 
    // but you could add cache reading here if desired.
    bool tryProcessFromCache(const ProcessingContext& ctx, ProcessingResult& result) override {
        return false; 
    }

    void process(std::unique_ptr<GrayscaleImage>& image, const ProcessingContext& ctx, ProcessingResult& result) override {
        if (!image || image->width == 0 || image->height == 0) return;

        int totalPixels = image->width * image->height;
        int underExposedCount = 0;
        int overExposedCount = 0;

        for (int i = 0; i < totalPixels; ++i) {
            unsigned char px = image->data[i];
            if (px <= 5) underExposedCount++;     // Near pure black
            if (px >= 250) overExposedCount++;    // Near pure white
        }

        float underPct = static_cast<float>(underExposedCount) / totalPixels;
        float overPct  = static_cast<float>(overExposedCount) / totalPixels;

        // Push to metrics so it shows up in the UI automatically
        result.metrics.push_back({"Underexposed", static_cast<double>(underPct * 100.0f)});
        result.metrics.push_back({"Overexposed", static_cast<double>(overPct * 100.0f)});

        if (underPct > m_clipThreshold) {
            result.rejected = true;
            result.rejectReason = "Severely underexposed";
            result.warnings.push_back("Rejected: Underexposed");
        } else if (overPct > m_clipThreshold) {
            result.rejected = true;
            result.rejectReason = "Severely overexposed";
            result.warnings.push_back("Rejected: Overexposed");
        }
    }
};