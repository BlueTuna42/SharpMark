#pragma once
#include "../pipeline/interfaces.h"
#include <vector>
#include <string>
#include <memory>
#include <filesystem>

#ifdef USE_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#endif

class ClipEmbeddingProcessor : public IImageProcessor {
private:
#ifdef USE_ONNXRUNTIME
    Ort::Env env_{ORT_LOGGING_LEVEL_WARNING, "SharpMark-CLIP"};
    std::unique_ptr<Ort::Session> session_;
    Ort::MemoryInfo memoryInfo_{Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)};
#endif

    std::filesystem::path getCachePath(const ProcessingContext& ctx) const {
        return ctx.cacheDir / (ctx.filePath.filename().string() + ".clip");
    }

    std::vector<float> prepareTensor(const ImageBuffer& img) const;

public:
    explicit ClipEmbeddingProcessor(const std::string& modelPath);
    ~ClipEmbeddingProcessor() override = default;

    std::string name() const override { return "clip_embedding"; }
    bool supports(const ProcessingContext& ctx) const override { return true; }

    bool tryProcessFromCache(const ProcessingContext& ctx, ProcessingResult& result) override;
    void process(std::unique_ptr<ImageBuffer>& image, const ProcessingContext& ctx, ProcessingResult& result) override;
};