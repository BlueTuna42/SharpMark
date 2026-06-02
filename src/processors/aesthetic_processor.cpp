#include "aesthetic_processor.h"
#include <fstream>
#include <cmath>
#include <iostream>
#include <numeric>
#include <QDebug>

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "../Lib/stb_image_resize2.h"

// RAII Guard for std::counting_semaphore to guarantee release on exception
struct SemaphoreGuard {
    std::counting_semaphore<4>& sem_;
    explicit SemaphoreGuard(std::counting_semaphore<4>& sem) : sem_(sem) { sem_.acquire(); }
    ~SemaphoreGuard() { sem_.release(); }
};

AestheticProcessor::AestheticProcessor(const std::filesystem::path& modelsDir, const std::filesystem::path& appDataDir) {
    weights_path_ = appDataDir / "user_aesthetic_weights.bin";
    user_weights_.resize(512, 0.0f);
    loadUserWeights();

    loadBaseLUTs(modelsDir / "base_luts.bin");

#ifdef USE_ONNXRUNTIME
    Ort::SessionOptions sessionOptions;
    sessionOptions.SetIntraOpNumThreads(1);
    sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

    auto classifierPath = modelsDir / "vision_model_quantized.onnx";
    auto clipPath = modelsDir / "vision_model_quantized.onnx";
    auto laionPath = modelsDir / "laion_aesthetic.onnx";

#ifdef _WIN32
    try {
        clip_session_ = std::make_unique<Ort::Session>(env_, clipPath.wstring().c_str(), sessionOptions);
        qDebug() << "[AI] CLIP model loaded successfully.";
    } catch (const Ort::Exception& e) {
        qCritical() << "[AI] Failed to load CLIP model:" << e.what() << "Path:" << QString::fromStdWString(clipPath.wstring());
    }

    try {
        laion_session_ = std::make_unique<Ort::Session>(env_, laionPath.wstring().c_str(), sessionOptions);
        qDebug() << "[AI] LAION model loaded successfully.";
    } catch (const Ort::Exception& e) {
        qCritical() << "[AI] Failed to load LAION model:" << e.what() << "Path:" << QString::fromStdWString(laionPath.wstring());
    }

    try {
        classifier_session_ = std::make_unique<Ort::Session>(env_, classifierPath.wstring().c_str(), sessionOptions);
        qDebug() << "[AI] 3DLUT Classifier loaded successfully.";
    } catch (const Ort::Exception& e) {
        qWarning() << "[AI] 3DLUT Classifier missing/failed (Enhancement disabled):" << e.what();
    }
#else
    try {
        clip_session_ = std::make_unique<Ort::Session>(env_, clipPath.c_str(), sessionOptions);
        qDebug() << "[AI] CLIP model loaded successfully.";
    } catch (const Ort::Exception& e) {
        qCritical() << "[AI] Failed to load CLIP model:" << e.what();
    }

    try {
        laion_session_ = std::make_unique<Ort::Session>(env_, laionPath.c_str(), sessionOptions);
        qDebug() << "[AI] LAION model loaded successfully.";
    } catch (const Ort::Exception& e) {
        qCritical() << "[AI] Failed to load LAION model:" << e.what();
    }

    try {
        classifier_session_ = std::make_unique<Ort::Session>(env_, classifierPath.c_str(), sessionOptions);
        qDebug() << "[AI] 3DLUT Classifier loaded successfully.";
    } catch (const Ort::Exception& e) {
        qWarning() << "[AI] 3DLUT Classifier missing/failed (Enhancement disabled):" << e.what();
    }
#endif

#endif
}

void AestheticProcessor::loadBaseLUTs(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        std::cerr << "WARNING: base_luts.bin not found at " << path << std::endl;
        return;
    }
    
    std::ifstream in(path, std::ios::binary);
    if (in) {
        base_luts_.resize(NUM_BASE_LUTS * LUT_ELEMENTS);
        in.read(reinterpret_cast<char*>(base_luts_.data()), base_luts_.size() * sizeof(float));
    }
}

void AestheticProcessor::apply3DLUT(std::vector<float>& chw_tensor, const float* lut, int targetSize, int lut_dim) const {
    const int stride = targetSize * targetSize;
    float* r_plane = chw_tensor.data();
    float* g_plane = chw_tensor.data() + stride;
    float* b_plane = chw_tensor.data() + stride * 2;

    const int dim2 = lut_dim * lut_dim;
    const int dim3 = lut_dim * lut_dim * lut_dim;

    // Helper to read from flat [C, B, G, R] tensor
    auto get_val = [&](int c, int b_idx, int g_idx, int r_idx) {
        return lut[c * dim3 + b_idx * dim2 + g_idx * lut_dim + r_idx];
    };

    for (int i = 0; i < stride; ++i) {
        // Map [0, 1] color value to [0, lut_dim - 1] grid space
        float r = r_plane[i] * (lut_dim - 1);
        float g = g_plane[i] * (lut_dim - 1);
        float b = b_plane[i] * (lut_dim - 1);

        // Integer coordinates for the 8 corners of the cube
        int r0 = std::clamp(static_cast<int>(r), 0, lut_dim - 2);
        int g0 = std::clamp(static_cast<int>(g), 0, lut_dim - 2);
        int b0 = std::clamp(static_cast<int>(b), 0, lut_dim - 2);
        
        int r1 = r0 + 1, g1 = g0 + 1, b1 = b0 + 1;

        // Fractional distance for interpolation
        float rd = r - r0;
        float gd = g - g0;
        float bd = b - b0;

        for (int c = 0; c < 3; ++c) {
            // Fetch 8 corners
            float c000 = get_val(c, b0, g0, r0);
            float c100 = get_val(c, b0, g0, r1);
            float c010 = get_val(c, b0, g1, r0);
            float c110 = get_val(c, b0, g1, r1);
            float c001 = get_val(c, b1, g0, r0);
            float c101 = get_val(c, b1, g0, r1);
            float c011 = get_val(c, b1, g1, r0);
            float c111 = get_val(c, b1, g1, r1);

            // Interpolate along R
            float c00 = c000 * (1 - rd) + c100 * rd;
            float c01 = c001 * (1 - rd) + c101 * rd;
            float c10 = c010 * (1 - rd) + c110 * rd;
            float c11 = c011 * (1 - rd) + c111 * rd;

            // Interpolate along G
            float c0 = c00 * (1 - gd) + c10 * gd;
            float c1 = c01 * (1 - gd) + c11 * gd;

            // Interpolate along B
            float val = c0 * (1 - bd) + c1 * bd;

            // Save back to the tensor plane
            if (c == 0) r_plane[i] = std::clamp(val, 0.0f, 1.0f);
            else if (c == 1) g_plane[i] = std::clamp(val, 0.0f, 1.0f);
            else b_plane[i] = std::clamp(val, 0.0f, 1.0f);
        }
    }
}

void AestheticProcessor::loadUserWeights() {
    std::lock_guard<std::mutex> lock(weights_mtx_);
    if (!std::filesystem::exists(weights_path_)) return;

    std::ifstream in(weights_path_, std::ios::binary);
    if (in) {
        in.read(reinterpret_cast<char*>(user_weights_.data()), 512 * sizeof(float));
        in.read(reinterpret_cast<char*>(&user_bias_), sizeof(float));
    }
}

void AestheticProcessor::saveUserWeights() const {
    std::ofstream out(weights_path_, std::ios::binary);
    if (out) {
        out.write(reinterpret_cast<const char*>(user_weights_.data()), 512 * sizeof(float));
        out.write(reinterpret_cast<const char*>(&user_bias_), sizeof(float));
    }
}

bool AestheticProcessor::tryProcessFromCache(const ProcessingContext& ctx, ProcessingResult& result) {
    return false; 
}

std::vector<Ort::Value> AestheticProcessor::runOnnxModel(Ort::Session& session, std::vector<float>& inputTensor, const std::vector<int64_t>& dims) {
    Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value inputOrtTensor = Ort::Value::CreateTensor<float>(
        memoryInfo, inputTensor.data(), inputTensor.size(), dims.data(), dims.size());

    Ort::AllocatorWithDefaultOptions allocator;
    auto inputName = session.GetInputNameAllocated(0, allocator);
    auto outputName = session.GetOutputNameAllocated(0, allocator);

    const char* inputNames[] = {inputName.get()};
    const char* outputNames[] = {outputName.get()};

    return session.Run(Ort::RunOptions{nullptr}, inputNames, &inputOrtTensor, 1, outputNames, 1);
}

void AestheticProcessor::process(std::unique_ptr<ImageBuffer>& image, const ProcessingContext& ctx, ProcessingResult& result) {
    qDebug() << "[AI] Starting aesthetic processing. Image:" << (ctx.filePath).u8string();

    if (!image || image->channels < 3) {
        qWarning() << "[AI] Image invalid or not RGB. Channels:" << (image ? image->channels : 0);
        result.warnings.push_back("Aesthetic pipeline requires 3-channel RGB images.");
        return;
    }

#ifndef USE_ONNXRUNTIME
    qWarning() << "[AI] App was built without ONNX. Scoring unavailable.";
    result.warnings.push_back("Built without ONNX. Aesthetic scoring unavailable.");
    return;
#else
    if (!clip_session_ || !laion_session_) {
        qCritical() << "[AI] ERROR: ONNX CLIP or LAION sessions are NULL! Models probably failed to load in constructor.";
        result.warnings.push_back("ONNX CLIP or LAION sessions not fully initialized.");
        return;
    }

    try {
        qDebug() << "[AI] Preprocessing 224x224 tensor for CLIP...";
        const int clipSize = 224;
        std::vector<float> clip_chw = downsampleAndToCHW(*image, clipSize);
        std::vector<int64_t> clip_dims = {1, 3, clipSize, clipSize};

        float baseAestheticScore = 0.0f;
        std::vector<float> clip_features(512);

        {
            qDebug() << "[AI] Waiting for inference semaphore...";
            SemaphoreGuard lock(inference_semaphore_);
            qDebug() << "[AI] Semaphore acquired. Normalizing tensor...";

            normalizeForClip(clip_chw); 
            
            qDebug() << "[AI] Executing CLIP ONNX session...";
            auto clip_tensors = runOnnxModel(*clip_session_, clip_chw, clip_dims);
            
            float* clip_data = clip_tensors.front().GetTensorMutableData<float>();
            std::copy(clip_data, clip_data + 512, clip_features.begin());

            qDebug() << "[AI] CLIP successful. Extracted 512 features. First feature val:" << clip_features[0];

            qDebug() << "[AI] Applying L2 normalization to features...";
            float sum_sq = std::inner_product(clip_features.begin(), clip_features.end(), clip_features.begin(), 0.0f);
            float length = std::sqrt(std::max(sum_sq, 1e-8f));
            for (float& v : clip_features) v /= length;

            qDebug() << "[AI] Executing LAION ONNX session...";
            std::vector<int64_t> laion_dims = {1, 512};
            auto laion_tensors = runOnnxModel(*laion_session_, clip_features, laion_dims);
            baseAestheticScore = laion_tensors.front().GetTensorMutableData<float>()[0];
            
            qDebug() << "[AI] LAION base score calculated:" << baseAestheticScore;
        } // Semaphore released
        
        qDebug() << "[AI] Semaphore released. Applying user delta...";

        float userDelta = user_bias_;
        {
            std::lock_guard<std::mutex> lock(weights_mtx_);
            for (size_t i = 0; i < 512; ++i) {
                userDelta += clip_features[i] * user_weights_[i];
            }
        }

        float finalScore = baseAestheticScore + userDelta;
        qDebug() << "[AI] Final Score:" << finalScore << "(Base:" << baseAestheticScore << "UserDelta:" << userDelta << ")";

        result.metrics.push_back({"aesthetic_score", static_cast<double>(finalScore)});
        result.metrics.push_back({"aesthetic_base_score", static_cast<double>(baseAestheticScore)});
        result.sharedData["clip_vector"] = std::move(clip_features);
        result.sharedData["aesthetic_score"] = static_cast<double>(finalScore);

        qDebug() << "[AI] Done.";
    } catch (const Ort::Exception& e) {
        qCritical() << "[AI] ONNX EXCEPTION:" << e.what();
        result.warnings.push_back(std::string("ONNX Runtime Error: ") + e.what());
    } catch (const std::exception& e) {
        qCritical() << "[AI] STD EXCEPTION:" << e.what();
        result.warnings.push_back(std::string("Aesthetic Pipeline Error: ") + e.what());
    } catch (...) {
        qCritical() << "[AI] UNKNOWN EXCEPTION!";
        result.warnings.push_back("Unknown Aesthetic Pipeline Error");
    }
#endif
}

double AestheticProcessor::evaluateClipVector(const std::filesystem::path& modelsDir, const std::filesystem::path& appDataDir, const std::vector<float>& clip_features) {
    if (clip_features.size() != 512) return 0.0;

    float baseScore = 0.0f;
    
#ifdef USE_ONNXRUNTIME
    // 1. Get LAION Base Score (Extremely fast load for 2KB model)
    try {
        Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "AestheticFastEval");
        Ort::SessionOptions sessionOptions;
        sessionOptions.SetIntraOpNumThreads(1);
        
        auto laionPath = modelsDir / "laion_aesthetic.onnx";
        
#ifdef _WIN32
        Ort::Session laion_session(env, laionPath.wstring().c_str(), sessionOptions);
#else
        Ort::Session laion_session(env, laionPath.c_str(), sessionOptions);
#endif

        Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        std::vector<float> features_copy = clip_features; // need mutable for ONNX
        std::vector<int64_t> dims = {1, 512};
        Ort::Value inputTensor = Ort::Value::CreateTensor<float>(memoryInfo, features_copy.data(), features_copy.size(), dims.data(), dims.size());

        Ort::AllocatorWithDefaultOptions allocator;
        auto inputName = laion_session.GetInputNameAllocated(0, allocator);
        auto outputName = laion_session.GetOutputNameAllocated(0, allocator);
        const char* inputNames[] = {inputName.get()};
        const char* outputNames[] = {outputName.get()};

        auto laion_tensors = laion_session.Run(Ort::RunOptions{nullptr}, inputNames, &inputTensor, 1, outputNames, 1);
        baseScore = laion_tensors.front().GetTensorMutableData<float>()[0];
    } catch (...) {
        baseScore = 0.0f; // Fallback if LAION model is missing
    }
#endif

    // 2. Get User Delta from trained weights
    float userDelta = 0.0f;
    std::filesystem::path weights_path = appDataDir / "user_aesthetic_weights.bin";
    if (std::filesystem::exists(weights_path)) {
        std::vector<float> user_weights(512, 0.0f);
        float user_bias = 0.0f;
        std::ifstream in(weights_path, std::ios::binary);
        if (in) {
            in.read(reinterpret_cast<char*>(user_weights.data()), 512 * sizeof(float));
            in.read(reinterpret_cast<char*>(&user_bias), sizeof(float));
            
            userDelta = user_bias;
            for (size_t i = 0; i < 512; ++i) {
                userDelta += user_weights[i] * clip_features[i];
            }
        }
    }

    return static_cast<double>(baseScore + userDelta);
}

void AestheticProcessor::train(const std::vector<float>& features, float baseScore, int userRating) {
    if (features.size() != 512) return;

    // Convert 1-5 star rating to a target shift. 
    // E.g., if baseScore is 4.5, but user rates 1 (bad), target_score should ideally be low (e.g., 2.0).
    // Target score mapping: 1 -> 2.0, 3 -> 5.0, 5 -> 8.0 (Adjust based on your LAION distribution)
    float absolute_target = (userRating - 1.0f) * 1.5f + 2.0f; 
    float target_delta = absolute_target - baseScore;

    std::lock_guard<std::mutex> lock(weights_mtx_);

    float current_delta = user_bias_;
    for (size_t i = 0; i < 512; ++i) {
        current_delta += user_weights_[i] * features[i];
    }

    float error = target_delta - current_delta;
    const float learning_rate = 0.01f; // Keep low to prevent extreme shifts from single clicks

    // Gradient descent for linear layer: w_new = w_old + LR * error * x
    for (size_t i = 0; i < 512; ++i) {
        user_weights_[i] += learning_rate * error * features[i];
    }
    user_bias_ += learning_rate * error; // Bias update

    saveUserWeights();
}

std::vector<float> AestheticProcessor::downsampleAndToCHW(const ImageBuffer& img, int targetSize) const {
    std::vector<float> resized(3 * targetSize * targetSize);
    stbir_resize_float_linear(img.data, img.width, img.height, 0,
                              resized.data(), targetSize, targetSize, 0, STBIR_RGB);

    std::vector<float> chw(3 * targetSize * targetSize);
    const int stride = targetSize * targetSize;
    float* r_plane = chw.data();
    float* g_plane = chw.data() + stride;
    float* b_plane = chw.data() + stride * 2;

    for (int i = 0; i < stride; ++i) {
        r_plane[i] = resized[i * 3 + 0] / 255.0f; // Scale to [0,1]
        g_plane[i] = resized[i * 3 + 1] / 255.0f;
        b_plane[i] = resized[i * 3 + 2] / 255.0f;
    }
    return chw;
}

void AestheticProcessor::normalizeForClip(std::vector<float>& chw_tensor) const {
    const float mean[3] = {0.48145466f, 0.4578275f, 0.40821073f};
    const float std_dev[3] = {0.26862954f, 0.26130258f, 0.27577711f};
    const int stride = chw_tensor.size() / 3;

    float* r = chw_tensor.data();
    float* g = chw_tensor.data() + stride;
    float* b = chw_tensor.data() + stride * 2;

    for (int i = 0; i < stride; ++i) {
        r[i] = (r[i] - mean[0]) / std_dev[0];
        g[i] = (g[i] - mean[1]) / std_dev[1];
        b[i] = (b[i] - mean[2]) / std_dev[2];
    }
}