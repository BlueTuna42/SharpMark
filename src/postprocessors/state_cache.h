#pragma once
#include "../pipeline/interfaces.h"
#include "../tools/XMP_tools.h"
#include <mutex>
#include <fstream>
#include <filesystem>
#include <variant>

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
        
        double variance = 0.0;
        auto it_lap = result.sharedData.find("laplacian_variance");
        if (it_lap != result.sharedData.end()) {
            if (auto* v = std::get_if<double>(&it_lap->second)) {
                variance = *v;
            }
        }

        double aiScore = 0.0;
        auto it_ai = result.sharedData.find("aesthetic_score");
        if (it_ai != result.sharedData.end()) {
            if (auto* d = std::get_if<double>(&it_ai->second)) {
                aiScore = *d;
            }
        }

        out << ctx.rawFilePath << "," 
            << (result.isBlurry ? "1" : "0") << "," 
            << variance << "," 
            << aiScore << "\n";
    }
};