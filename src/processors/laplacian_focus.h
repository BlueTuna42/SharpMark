#pragma once
#include "../pipeline/interfaces.h"
#include "../img_tools/laplacian.h"
#include <QVariantMap>

class LaplacianFocusProcessor : public IImageProcessor {
private:
    double m_focusConst = 150.0;
    std::filesystem::path getCachePath(const ProcessingContext& ctx) const {
        return ctx.cacheDir / (ctx.filePath.filename().string() + ".rawlap");
    }

public:
    std::string name() const override { return "laplacian_focus"; }

    bool supports(const ProcessingContext& ctx) const override { return true; }

    QVariantMap settings() const override {
        return {{"focusThreshold", m_focusConst}};
    }

    void setSettings(const QVariantMap& s) override {
        if (s.contains("focusThreshold"))
            m_focusConst = s["focusThreshold"].toDouble();
    }

    bool tryProcessFromCache(const ProcessingContext&, ProcessingResult&) override {
        return false;
    }

    void process(std::unique_ptr<GrayscaleImage>& image, const ProcessingContext& ctx, ProcessingResult& result) override {
        double maxVariance = LaplacianProcessor::evaluateSharpness(*image, 5, 5);

        result.isBlurry = (maxVariance < m_focusConst);
        result.metrics.push_back({"variance", maxVariance});
        result.sharedData["laplacian_variance"] = maxVariance;
    }
};