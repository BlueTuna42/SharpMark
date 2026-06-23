#pragma once
#include "../pipeline/interfaces.h"
#include "../tools/XMP_tools.h"
#include <mutex>
#include <filesystem>
#include <variant>
#include <unordered_set>
#include <QFile>
#include <QTextStream>
#include <QString>

class StateCachePostProcessor : public IPostProcessor {
private:
    // Shared across ALL instances (i.e. all worker threads)
    static std::mutex& sharedMtx() {
        static std::mutex mtx;
        return mtx;
    }
    // Shared set of already-written paths, so no worker re-appends a row that
    // another worker (or a previous scan session) already wrote.
    static std::unordered_set<std::string>& sharedExistingPaths() {
        static std::unordered_set<std::string> paths;
        return paths;
    }
    static std::filesystem::path& sharedLoadedCacheDir() {
        static std::filesystem::path dir;
        return dir;
    }

    // Load the set of already-known paths from state.csv (called once per cacheDir).
    // Must be called while holding sharedMtx().
    void ensureKnownPaths(const std::filesystem::path& cacheDir) {
        if (sharedLoadedCacheDir() == cacheDir) return;
        sharedExistingPaths().clear();
        sharedLoadedCacheDir() = cacheDir;

        std::filesystem::path csvPath = cacheDir / "state.csv";
        if (!std::filesystem::exists(csvPath)) return;

#ifdef _WIN32
        QFile qf(QString::fromStdWString(csvPath.wstring()));
#else
        QFile qf(QString::fromUtf8(csvPath.c_str()));
#endif
        if (!qf.open(QIODevice::ReadOnly | QIODevice::Text)) return;
        QTextStream in(&qf);
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            int comma = line.indexOf(',');
            if (comma != -1) sharedExistingPaths().insert(line.left(comma).toStdString());
        }
    }

    // Helper: extract a double from result.metrics by case-insensitive key name.
    static double metricDouble(const ProcessingResult& result, const char* key) {
        for (const auto& m : result.metrics) {
            if (m.key == key) {
                if (auto* d = std::get_if<double>(&m.value)) return *d;
                if (auto* i = std::get_if<int>(&m.value))    return static_cast<double>(*i);
            }
        }
        return 0.0;
    }

public:
    std::string name() const override { return "state_cache_writer"; }
    bool supportsDisable() const override { return false; } // always runs
    
    void handle(const ProcessingContext& ctx, const ProcessingResult& result) override {
        // Don't write back entries that were loaded from cache
        if (result.sharedData.count("loaded_from_state")) return;

        std::lock_guard<std::mutex> lock(sharedMtx());

        ensureKnownPaths(ctx.cacheDir);

        // Skip if this file path is already recorded — avoids duplicate rows
        if (sharedExistingPaths().count(ctx.rawFilePath.toStdString())) return;

        std::error_code ec;
        if (!std::filesystem::exists(ctx.cacheDir, ec)) {
            std::filesystem::create_directories(ctx.cacheDir, ec);
        }

#ifdef _WIN32
        QString csvQPath = QString::fromStdWString((ctx.cacheDir / "state.csv").wstring());
#else
        QString csvQPath = QString::fromUtf8((ctx.cacheDir / "state.csv").c_str());
#endif
        QFile qf(csvQPath);
        if (!qf.open(QIODevice::Append | QIODevice::Text)) return;
        QTextStream out(&qf);

        // col 2: laplacianVariance
        double variance = 0.0;
        auto it_lap = result.sharedData.find("laplacian_variance");
        if (it_lap != result.sharedData.end()) {
            if (auto* v = std::get_if<double>(&it_lap->second)) variance = *v;
        }

        // col 3: aiScore
        double aiScore = 0.0;
        auto it_ai = result.sharedData.find("aesthetic_score");
        if (it_ai != result.sharedData.end()) {
            if (auto* d = std::get_if<double>(&it_ai->second)) aiScore = *d;
        }

        // col 4: visualHash
        uint64_t visualHash = 0;
        auto it_vh = result.sharedData.find("visual_hash_u64");
        if (it_vh != result.sharedData.end()) {
            if (auto* d = std::get_if<double>(&it_vh->second))
                visualHash = static_cast<uint64_t>(*d);
        }

        // cols 7-8: exposure percentages (written by ExposureCheckProcessor into result.metrics)
        double underPct = metricDouble(result, "Underexposed");
        double overPct  = metricDouble(result, "Overexposed");

        // col 6: rejectReason — must not contain commas; replace any with semicolons
        QString rejectReason = QString::fromStdString(result.rejectReason).replace(',', ';');

        // CSV format:
        //   filePath, isBlurry, laplacianVariance, aiScore, visualHash,
        //   isRejected, rejectReason, underExposedPct, overExposedPct
        out << ctx.rawFilePath                                                              << ","
            << (result.isBlurry   ? "1" : "0")                                            << ","
            << variance                                                                    << ","
            << aiScore                                                                     << ","
            << QString::number(static_cast<qulonglong>(visualHash), 16).rightJustified(16, '0') << ","
            << (result.rejected   ? "1" : "0")                                            << ","
            << rejectReason                                                                << ","
            << underPct                                                                    << ","
            << overPct                                                                     << "\n";
        qf.close();

        // Mark as written so subsequent workers don't re-append for the same file
        sharedExistingPaths().insert(ctx.rawFilePath.toStdString());
    }
};
