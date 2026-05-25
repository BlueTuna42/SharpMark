#pragma once
#include "../pipeline/interfaces.h"
#include "../tools/XMP_tools.h"

class StateCachePostProcessor : public IPostProcessor {
private:
    std::mutex mtx;
public:
    std::string name() const override { return "state_cache_writer"; }
    void handle(const ProcessingContext& ctx, const ProcessingResult& result) override {
        if (result.sharedData.count("loaded_from_state")) return;

        std::lock_guard<std::mutex> lock(mtx);
        std::error_code ec;
        if (!std::filesystem::exists(ctx.cacheDir, ec)) {
            std::filesystem::create_directories(ctx.cacheDir, ec);
        }
        std::ofstream out(ctx.cacheDir / "state.csv", std::ios::app);
        out << ctx.rawFilePath << "," << (result.isBlurry ? "1" : "0") << "\n";
    }
};