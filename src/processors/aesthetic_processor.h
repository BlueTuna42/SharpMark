#pragma once

#include "../pipeline/interfaces.h"
#include <onnxruntime_cxx_api.h>
#include <filesystem>
#include <vector>
#include <string>
#include <mutex>
#include <semaphore>
#include <memory>

class AestheticProcessor : public IImageProcessor {
public:
    // Initialize with paths to the 3 ONNX models
    AestheticProcessor(const std::filesystem::path& modelsDir, const std::filesystem::path& appDataDir);
    ~AestheticProcessor() override = default;

    std::string name() const override { return "aiaesthetic"; }
    bool supports(const ProcessingContext& ctx) const override { return true; }
    bool tryProcessFromCache(const ProcessingContext& ctx, ProcessingResult& result) override;
    
    void process(std::unique_ptr<ImageBuffer>& image, const ProcessingContext& ctx, ProcessingResult& result) override;

    // Active Learning: delta predicts the difference between User Rating and Base LAION score
    void train(const std::vector<float>& features, float baseScore, int userRating);
    static double evaluateClipVector(const std::filesystem::path& modelsDir, const std::filesystem::path& appDataDir, const std::vector<float>& clip_features);

private:
    // ONNX Runtime State
    Ort::Env env_{ORT_LOGGING_LEVEL_WARNING, "AestheticProcessor"};
    std::unique_ptr<Ort::Session> classifier_session_;
    std::unique_ptr<Ort::Session> clip_session_;
    std::unique_ptr<Ort::Session> laion_session_;

    // Base LUTs Storage
    std::vector<float> base_luts_; 
    static constexpr int LUT_DIM = 33;
    static constexpr int LUT_ELEMENTS = 3 * LUT_DIM * LUT_DIM * LUT_DIM; // 107,811 per LUT
    static constexpr int NUM_BASE_LUTS = 3; 

    void loadBaseLUTs(const std::filesystem::path& path);
    void apply3DLUT(std::vector<float>& chw_tensor, const float* lut_data, int targetSize, int lut_dim) const;


    // Concurrency control: limits VRAM/RAM spikes by restricting active ML inferences
    std::counting_semaphore<4> inference_semaphore_{4}; 

    // User Model (Linear Perceptron) State
    std::vector<float> user_weights_;
    float user_bias_ = 0.0f;
    std::mutex weights_mtx_;
    std::filesystem::path weights_path_;

    void loadUserWeights();
    void saveUserWeights() const;

    // Pipeline Helpers
    std::vector<float> downsampleAndToCHW(const ImageBuffer& img, int targetSize) const;
    void normalizeForClip(std::vector<float>& chw_tensor) const;
    
    // ONNX Execution Helper
    std::vector<Ort::Value> runOnnxModel(Ort::Session& session, std::vector<float>& inputTensor, const std::vector<int64_t>& dims);
};