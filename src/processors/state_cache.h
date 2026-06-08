#pragma once
#include "../pipeline/interfaces.h"
#include "../img_tools/laplacian.h"
#include <QFile>
#include <QTextStream>
#include <unordered_map>
#include <string>
#include <sstream>
#include <iomanip>

// Cached entry parsed from state.csv
// CSV column order:
//   0: filePath
//   1: isBlurry          (0|1)
//   2: laplacianVariance (double)
//   3: aiScore           (double)
//   4: visualHash        (hex16, optional — 0 if absent)
//   5: isRejected        (0|1, optional)
//   6: rejectReason      (string, optional — may be empty)
//   7: underExposedPct   (double %, optional)
//   8: overExposedPct    (double %, optional)
struct StateCacheEntry {
    bool        isBlurry          = false;
    double      laplacianVariance = 0.0;
    double      aiScore           = 0.0;
    uint64_t    visualHash        = 0;
    bool        isRejected        = false;
    std::string rejectReason;
    double      underExposedPct   = 0.0;
    double      overExposedPct    = 0.0;
};

class StateCacheProcessor : public IImageProcessor {
public:
    std::string name() const override { return "state_cache"; }
    bool supports(const ProcessingContext& ctx) const override { return true; }

    bool tryProcessFromCache(const ProcessingContext& ctx, ProcessingResult& result) override {
        // Load the CSV into memory on first use for this cacheDir (O(N) once, O(1) lookups after)
        ensureLoaded(ctx.cacheDir);

        auto it = m_cache.find(ctx.rawFilePath.toStdString());
        if (it == m_cache.end()) return false;

        const StateCacheEntry& entry = it->second;

        result.isBlurry               = entry.isBlurry;
        result.rejected               = entry.isRejected;
        result.rejectReason           = entry.rejectReason;

        result.sharedData["loaded_from_state"]  = true;
        result.sharedData["laplacian_variance"] = entry.laplacianVariance;
        result.sharedData["aesthetic_score"]    = entry.aiScore;

        // Restore exposure metrics so the UI can display per-image percentages
        result.sharedData["underexposed_pct"]   = entry.underExposedPct;
        result.sharedData["overexposed_pct"]    = entry.overExposedPct;
        result.metrics.push_back({"Underexposed", entry.underExposedPct});
        result.metrics.push_back({"Overexposed",  entry.overExposedPct});

        // Restore visual hash so burst grouping works correctly after a cache reload
        if (entry.visualHash != 0) {
            std::ostringstream oss;
            oss << std::hex << std::setw(16) << std::setfill('0') << entry.visualHash;
            result.metrics.push_back({"visual_hash", oss.str()});
            result.sharedData["visual_hash_u64"] = static_cast<double>(entry.visualHash);
        }

        return true;
    }

    void process(std::unique_ptr<GrayscaleImage>& image, const ProcessingContext& ctx, ProcessingResult& result) override {}

private:
    std::filesystem::path m_loadedCacheDir;
    // Key: rawFilePath as UTF-8 std::string for std::hash compatibility
    std::unordered_map<std::string, StateCacheEntry> m_cache;

    // Pull the next comma-delimited token from `rest`, advance `rest` past it.
    static QString nextToken(QString& rest) {
        int comma = rest.indexOf(',');
        if (comma == -1) {
            QString tok = rest;
            rest.clear();
            return tok;
        }
        QString tok = rest.left(comma);
        rest = rest.mid(comma + 1);
        return tok;
    }

    void ensureLoaded(const std::filesystem::path& cacheDir) {
        if (m_loadedCacheDir == cacheDir) return; // already loaded for this dir
        m_cache.clear();
        m_loadedCacheDir = cacheDir;

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
            if (line.isEmpty()) continue;

            // col 0: filePath (everything up to the first comma)
            int c0 = line.indexOf(',');
            if (c0 == -1) continue;
            QString filePath = line.left(c0);
            QString rest     = line.mid(c0 + 1);

            StateCacheEntry entry;
            entry.isBlurry          = nextToken(rest).startsWith('1');
            entry.laplacianVariance = nextToken(rest).toDouble();
            entry.aiScore           = nextToken(rest).toDouble();

            // col 4: visualHash (hex, optional)
            QString hashStr = nextToken(rest);
            if (!hashStr.isEmpty()) {
                bool ok = false;
                entry.visualHash = hashStr.toULongLong(&ok, 16);
                if (!ok) entry.visualHash = 0;
            }

            // col 5: isRejected (optional)
            if (!rest.isEmpty()) entry.isRejected = nextToken(rest).startsWith('1');

            // col 6: rejectReason (optional, may be empty string)
            if (!rest.isEmpty()) entry.rejectReason = nextToken(rest).toStdString();

            // col 7: underExposedPct (optional)
            if (!rest.isEmpty()) entry.underExposedPct = nextToken(rest).toDouble();

            // col 8: overExposedPct (optional)
            if (!rest.isEmpty()) entry.overExposedPct  = nextToken(rest).toDouble();

            // Later entries for the same file overwrite earlier ones (deduplication)
            m_cache[filePath.toStdString()] = entry;
        }
    }
};
