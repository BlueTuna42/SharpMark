#pragma once
#include "../pipeline/interfaces.h"
#include "../img_tools/laplacian.h"

class StateCacheProcessor : public IImageProcessor {
public:
    std::string name() const override { return "state_cache"; }
    bool supports(const ProcessingContext& ctx) const override { return true; }

    bool tryProcessFromCache(const ProcessingContext& ctx, ProcessingResult& result) override {
        std::filesystem::path csvPath = ctx.cacheDir / "state.csv";
        if (!std::filesystem::exists(csvPath)) return false;

        std::ifstream in(csvPath);
        std::string line;
        while (std::getline(in, line)) {
            size_t comma = line.find(',');
            if (comma != std::string::npos && line.substr(0, comma) == ctx.rawFilePath) {
                result.isBlurry = (line.substr(comma + 1) == "1");
                result.sharedData["loaded_from_state"] = true;
                return true; 
            }
        }
        return false;
    }

    void process(std::unique_ptr<GrayscaleImage>& image, const ProcessingContext& ctx, ProcessingResult& result) override {}
};