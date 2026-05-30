#pragma once
#include "../pipeline/interfaces.h"
#include <string>
#include <sstream>
#include <iomanip>

class VisualHashProcessor : public IImageProcessor {
public:
    std::string name() const override { return "visual_hash"; }
    bool supports(const ProcessingContext& ctx) const override { return true; }
    
    // We don't cache this right now because it takes ~5 microseconds to compute
    bool tryProcessFromCache(const ProcessingContext& ctx, ProcessingResult& result) override { return false; }

    void process(std::unique_ptr<GrayscaleImage>& image, const ProcessingContext& ctx, ProcessingResult& result) override {
        if (!image || image->width == 0 || image->height == 0) return;

        // Fast nearest-neighbor downsample to 9x8
        int targetW = 9, targetH = 8;
        std::vector<unsigned char> small(targetW * targetH);
        for(int y = 0; y < targetH; ++y) {
            for(int x = 0; x < targetW; ++x) {
                int srcX = x * image->width / targetW;
                int srcY = y * image->height / targetH;
                small[y * targetW + x] = image->data[srcY * image->width + srcX];
            }
        }

        // Compute 64-bit dHash
        uint64_t hash = 0;
        for(int y = 0; y < 8; ++y) {
            for(int x = 0; x < 8; ++x) {
                int left = small[y * 9 + x];
                int right = small[y * 9 + x + 1];
                if (left > right) {
                    hash |= (1ULL << (y * 8 + x));
                }
            }
        }

        // Store as a hex string metric
        std::ostringstream oss;
        oss << std::hex << std::setw(16) << std::setfill('0') << hash;
        result.metrics.push_back({"visual_hash", oss.str()});
    }
};