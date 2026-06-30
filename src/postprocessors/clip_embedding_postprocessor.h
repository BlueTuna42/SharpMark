#pragma once
#include "../pipeline/interfaces.h"
#include <fstream>
#include <filesystem>
#include <QDebug>
#include <QVariantMap>

class ClipEmbeddingPostProcessor : public IPostProcessor {
public:
    std::string name() const override { return "clip_embedding"; }
    bool supportsDisable() const override { return true; }

    QVariantMap settings() const override {
        return {{"cosineThreshold", static_cast<double>(m_cosineThreshold)}};
    }

    void setSettings(const QVariantMap& s) override {
        if (s.contains("cosineThreshold"))
            m_cosineThreshold = static_cast<float>(s["cosineThreshold"].toDouble());
    }

    float cosineThreshold() const { return m_cosineThreshold; }

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

private:
    float m_cosineThreshold = 0.90f;
};
