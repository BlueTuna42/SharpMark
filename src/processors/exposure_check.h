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

    bool tryProcessFromCache(const ProcessingContext& ctx, ProcessingResult& result) override {
        // StateCacheProcessor restores underexposed_pct / overexposed_pct into sharedData
        // when it finds a cached entry. If those keys are present the pixel scan is not needed.
        auto it_u = result.sharedData.find("underexposed_pct");
        auto it_o = result.sharedData.find("overexposed_pct");
        if (it_u == result.sharedData.end() || it_o == result.sharedData.end()) return false;

        auto* underPct = std::get_if<double>(&it_u->second);
        auto* overPct  = std::get_if<double>(&it_o->second);
        if (!underPct || !overPct) return false;

        // Re-populate metrics so the UI still sees per-image exposure values
        result.metrics.push_back({"Underexposed", *underPct});
        result.metrics.push_back({"Overexposed",  *overPct});

        // Rejection state is already set by StateCacheProcessor (result.rejected /
        // result.rejectReason), so nothing more to do here.
        return true;
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