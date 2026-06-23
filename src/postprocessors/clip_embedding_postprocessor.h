#pragma once
#include "../pipeline/interfaces.h"
#include <fstream>
#include <filesystem>
#include <QDebug>

// ClipEmbeddingPostProcessor
// Persists the 512-float L2-normalised CLIP embedding produced by
// AestheticProcessor to  <cacheDir>/<filename>.clip  as raw binary
// (512 × float32 = 2048 bytes).
//
// The file is read back by:
//   - AppBackend::getPhotoMetadata()  — live AI re-scoring in the info panel
//   - AppBackend::setPhotoRating()    — active-learning training on star ratings
//   - AppBackend::runScannerTask()    — CLIP-cosine burst grouping (when selected)
//
// Skips writing when:
//   - disabled (isEnabled() == false)
//   - result came from state-cache (no new embedding was computed)
//   - "clip_vector" is absent from sharedData (AI scorer not in pipeline)

class ClipEmbeddingPostProcessor : public IPostProcessor {
public:
    std::string name() const override { return "clip_embedding"; }
    bool supportsDisable() const override { return true; }

    void handle(const ProcessingContext& ctx, const ProcessingResult& result) override {
        // Respect enabled flag
        if (!isEnabled()) return;

        // No new embedding was computed — loaded from state cache
        if (result.sharedData.count("loaded_from_state")) return;

        // Retrieve the clip vector
        auto it = result.sharedData.find("clip_vector");
        if (it == result.sharedData.end()) return;
        const auto* vec = std::get_if<std::vector<float>>(&it->second);
        if (!vec || vec->size() != 512) return;

        // Ensure cache directory exists
        std::error_code ec;
        std::filesystem::create_directories(ctx.cacheDir, ec);

        // Build output path: <cacheDir>/<filename>.clip
        // Use .string() — on all our target platforms the native encoding is UTF-8.
        std::filesystem::path outPath =
            ctx.cacheDir / (ctx.filePath.filename().string() + ".clip");

        std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            qWarning() << "[ClipEmbed] Failed to open for writing:"
                       << QString::fromStdString(outPath.string());
            return;
        }
        out.write(reinterpret_cast<const char*>(vec->data()),
                  static_cast<std::streamsize>(512 * sizeof(float)));
        qDebug() << "[ClipEmbed] Saved embedding for"
                 << QString::fromStdString(ctx.filePath.filename().string());
    }
};
