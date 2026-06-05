#pragma once
#include "../pipeline/interfaces.h"
#include <string>
#include <sstream>
#include <iomanip>
#include <vector>

// VisualHashPreprocessor
// Computes a 64-bit dHash of the loaded image and stores it in:
//   result.metrics       → {"visual_hash", "<hex string>"}   (same as the old IImageProcessor)
//   result.sharedData    → {"visual_hash_u64", <double-encoded uint64>}

class VisualHashPreprocessor : public IImagePreprocessor {
public:
    std::string name() const override { return "visual_hash"; }
    bool supportsDisable() const override { return true; }

    void preprocess(std::unique_ptr<GrayscaleImage>& image,
                    const ProcessingContext& /*ctx*/,
                    ProcessingResult& result) override
    {
        if (!image || image->width == 0 || image->height == 0) return;

        // Nearest-neighbor downsample to 9×8 using the first (or only) channel
        constexpr int targetW = 9, targetH = 8;
        std::vector<unsigned char> small(targetW * targetH);

        for (int y = 0; y < targetH; ++y) {
            for (int x = 0; x < targetW; ++x) {
                int srcX = x * image->width  / targetW;
                int srcY = y * image->height / targetH;

                float luma;
                if (image->channels == 1) {
                    luma = image->data[srcY * image->width + srcX];
                } else {
                    // BT.601 luma from RGB
                    int base = (srcY * image->width + srcX) * image->channels;
                    luma = 0.299f * image->data[base + 0]
                         + 0.587f * image->data[base + 1]
                         + 0.114f * image->data[base + 2];
                }
                small[y * targetW + x] = static_cast<unsigned char>(
                    std::min(255.0f, std::max(0.0f, luma)));
            }
        }

        // 64-bit dHash
        uint64_t hash = 0;
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                if (small[y * 9 + x] > small[y * 9 + x + 1])
                    hash |= (1ULL << (y * 8 + x));
            }
        }

        std::ostringstream oss;
        oss << std::hex << std::setw(16) << std::setfill('0') << hash;
        result.metrics.push_back({"visual_hash", oss.str()});

        result.sharedData["visual_hash_u64"] = static_cast<double>(hash);
    }
};
