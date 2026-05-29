#pragma once
#include "../pipeline/interfaces.h"
#include <vector>
#include <string>
#include <filesystem>
#include <mutex>

class AestheticScorer : public IImageProcessor {
private:
    std::vector<float> user_weights_;
    std::mutex weights_mtx_;
    std::filesystem::path weights_path_;

    void loadWeights();
    void saveWeights() const;

public:
    explicit AestheticScorer(const std::filesystem::path& appDataDir);
    ~AestheticScorer() override = default;

    std::string name() const override { return "aesthetic_scorer"; }
    bool supports(const ProcessingContext& ctx) const override { return true; }

    bool tryProcessFromCache(const ProcessingContext& ctx, ProcessingResult& result) override { return false; }
    void process(std::unique_ptr<ImageBuffer>& image, const ProcessingContext& ctx, ProcessingResult& result) override;

    // The Active Learning function
    void train(const std::vector<float>& features, int rating);
    float evaluate(const std::vector<float>& features);
};