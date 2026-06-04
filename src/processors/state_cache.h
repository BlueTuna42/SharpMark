#pragma once
#include "../pipeline/interfaces.h"
#include "../img_tools/laplacian.h"
#include <QFile>
#include <QTextStream>

class StateCacheProcessor : public IImageProcessor {
public:
    std::string name() const override { return "state_cache"; }
    bool supports(const ProcessingContext& ctx) const override { return true; }

    bool tryProcessFromCache(const ProcessingContext& ctx, ProcessingResult& result) override {
        std::filesystem::path csvPath = ctx.cacheDir / "state.csv";
        if (!std::filesystem::exists(csvPath)) return false;

#ifdef _WIN32
        QFile qf(QString::fromStdWString(csvPath.wstring()));
#else
        QFile qf(QString::fromUtf8(csvPath.c_str()));
#endif
        if (!qf.open(QIODevice::ReadOnly | QIODevice::Text)) return false;

        QTextStream in(&qf);
        while (!in.atEnd()) {
            QString line = in.readLine();
            int comma = line.indexOf(',');
            if (comma != -1 && line.left(comma) == ctx.rawFilePath) {
                result.isBlurry = (line.mid(comma + 1).startsWith('1'));
                result.sharedData["loaded_from_state"] = true;
                return true;
            }
        }
        return false;
    }

    void process(std::unique_ptr<GrayscaleImage>& image, const ProcessingContext& ctx, ProcessingResult& result) override {}
};