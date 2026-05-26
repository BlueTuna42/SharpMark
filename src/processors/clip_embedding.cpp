#include "clip_embedding.h"
#include <iostream>
#include <fstream>

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "../Lib/stb_image_resize2.h"

ClipEmbeddingProcessor::ClipEmbeddingProcessor(const std::string& modelPath) {
#ifdef USE_ONNXRUNTIME
    Ort::SessionOptions sessionOptions;
    sessionOptions.SetIntraOpNumThreads(1); // Crucial: Thread pool handles concurrency, not the ML engine!
    sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    
    try {
#ifdef _WIN32
        std::wstring wModelPath = std::filesystem::u8path(modelPath).wstring();
        session_ = std::make_unique<Ort::Session>(env_, wModelPath.c_str(), sessionOptions);
#else
        session_ = std::make_unique<Ort::Session>(env_, modelPath.c_str(), sessionOptions);
#endif
    } catch (const Ort::Exception& e) {
        std::cerr << "Failed to load ONNX model: " << e.what() << std::endl;
        session_.reset();
    }
#else
    std::cerr << "Warning: Compiled without ONNX Runtime support!" << std::endl;
#endif
}

bool ClipEmbeddingProcessor::tryProcessFromCache(const ProcessingContext& ctx, ProcessingResult& result) {
    auto cacheFile = getCachePath(ctx);
    if (!std::filesystem::exists(cacheFile)) return false;

    std::ifstream in(cacheFile, std::ios::binary);
    if (!in) return false;

    std::vector<float> features(512); 
    in.read(reinterpret_cast<char*>(features.data()), 512 * sizeof(float));
    
    if (in.gcount() == 512 * sizeof(float)) {
        result.sharedData["clip_vector"] = std::move(features);
        return true;
    }
    return false;
}

void ClipEmbeddingProcessor::process(std::unique_ptr<ImageBuffer>& image, const ProcessingContext& ctx, ProcessingResult& result) {
    if (!image || image->channels < 3) {
        result.warnings.push_back("Image is not RGB. CLIP requires 3 channels.");
        return;
    }

#ifdef USE_ONNXRUNTIME
    if (!session_) {
        result.warnings.push_back("ONNX session is not initialized.");
        return;
    }

    std::vector<float> inputTensorValues = prepareTensor(*image);
    std::vector<int64_t> inputDims = {1, 3, 224, 224}; 
    
    Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
        memoryInfo_, inputTensorValues.data(), inputTensorValues.size(), inputDims.data(), inputDims.size());

    try {
        Ort::AllocatorWithDefaultOptions allocator;
        
        auto input_name_ptr = session_->GetInputNameAllocated(0, allocator);
        auto output_name_ptr = session_->GetOutputNameAllocated(0, allocator);
        
        const char* inputNames[] = {input_name_ptr.get()};
        const char* outputNames[] = {output_name_ptr.get()};

        auto outputTensors = session_->Run(Ort::RunOptions{nullptr}, inputNames, &inputTensor, 1, outputNames, 1);
        
        if (outputTensors.empty()) return;

        float* floatArr = outputTensors.front().GetTensorMutableData<float>();
        size_t featureSize = outputTensors.front().GetTensorTypeAndShapeInfo().GetElementCount();
        
        if (featureSize != 512) {
            result.warnings.push_back("Unexpected model size: " + std::to_string(featureSize));
            return;
        }

        std::vector<float> features(floatArr, floatArr + featureSize);

        // Сначала сохраняем на диск
        std::error_code ec;
        if (!std::filesystem::exists(ctx.cacheDir, ec)) {
            std::filesystem::create_directories(ctx.cacheDir, ec);
        }
        std::ofstream out(getCachePath(ctx), std::ios::binary);
        if (out) {
            out.write(reinterpret_cast<const char*>(features.data()), features.size() * sizeof(float));
        }

        result.sharedData["clip_vector"] = std::move(features);

    } catch (const Ort::Exception& e) {
        result.warnings.push_back(std::string("ONNX Inference Error: ") + e.what());
    } catch (const std::exception& e) {
        result.warnings.push_back(std::string("C++ Exception: ") + e.what());
    }
#endif
}

std::vector<float> ClipEmbeddingProcessor::prepareTensor(const ImageBuffer& img) const {
    const int targetSize = 224;
    std::vector<float> tensor(3 * targetSize * targetSize);
    std::vector<float> resized(3 * targetSize * targetSize);

    // 1. Fast Float Linear Resize
    stbir_resize_float_linear(
        img.data, img.width, img.height, 0,
        resized.data(), targetSize, targetSize, 0,
        STBIR_RGB
    );

    // 2. Normalize and convert HWC -> CHW
    // CLIP expectations: Image pixels must be in [0, 1] range before normalization
    const float mean[3] = {0.48145466f, 0.4578275f, 0.40821073f};
    const float std_dev[3] = {0.26862954f, 0.26130258f, 0.27577711f};

    const int stride = targetSize * targetSize;
    float* r_plane = tensor.data();
    float* g_plane = tensor.data() + stride;
    float* b_plane = tensor.data() + stride * 2;

    for (int i = 0; i < stride; ++i) {
        float r = resized[i * 3 + 0] / 255.0f;
        float g = resized[i * 3 + 1] / 255.0f;
        float b = resized[i * 3 + 2] / 255.0f;

        r_plane[i] = (r - mean[0]) / std_dev[0];
        g_plane[i] = (g - mean[1]) / std_dev[1];
        b_plane[i] = (b - mean[2]) / std_dev[2];
    }

    return tensor;
}