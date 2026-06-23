#pragma once
#include <string>
#include <variant>
#include <vector>
#include <memory>
#include <filesystem>
#include <unordered_map>
#include <QString>
#include "../struct.h"
#include "../gui/gui.h"

struct AnalysisValue {
    std::string key;
    std::variant<int, double, bool, std::string> value;
};

struct ProcessingContext {
    QString rawFilePath;
    std::filesystem::path filePath;
    AppSettings settings;
    std::filesystem::path cacheDir;
    bool requireRGB = false;
    bool runFullPipelineOnRejected = false; // Used when CLIP grouping is enabled
};

struct ProcessingResult {
    bool success = true;
    bool isBlurry = false;

    bool rejected = false; 
    std::string rejectReason = ""; 

    std::vector<std::string> processorsRun;
    std::vector<AnalysisValue> metrics;
    std::vector<std::string> warnings;
    std::unordered_map<std::string, std::variant<int, double, bool, std::string, std::vector<float>>> sharedData;
};

class IImageProcessor {
public:
    virtual ~IImageProcessor() = default;
    virtual std::string name() const = 0;
    virtual bool supports(const ProcessingContext& ctx) const = 0;
    
    // Returns true if processor successfully loaded its data from cache and doesn't need the image
    virtual bool tryProcessFromCache(const ProcessingContext& ctx, ProcessingResult& result) { 
        return false; 
    }

    virtual void process(std::unique_ptr<GrayscaleImage>& image, const ProcessingContext& ctx, ProcessingResult& result) = 0;
};

class IPostProcessor {
public:
    virtual ~IPostProcessor() = default;
    virtual std::string name() const = 0;

    // Enable/disable — mirrors IImagePreprocessor.
    // supportsDisable() == false means the entry is always-on (no UI toggle).
    virtual bool isEnabled() const { return m_enabled; }
    virtual void setEnabled(bool v) { m_enabled = v; }
    virtual bool supportsDisable() const { return true; }

    virtual void handle(const ProcessingContext& ctx, const ProcessingResult& result) = 0;
protected:
    bool m_enabled = true;
};

class IImageLoader {
public:
    virtual ~IImageLoader() = default;
    virtual std::unique_ptr<GrayscaleImage> load(const ProcessingContext& ctx) = 0;
};

// Preprocessors run after the image is loaded, before any IImageProcessor.
// They may mutate the image in-place (e.g., apply a 3D LUT) and may also
// write to ProcessingResult::sharedData / metrics (e.g., visual hash).
class IImagePreprocessor {
public:
    virtual ~IImagePreprocessor() = default;
    virtual std::string name() const = 0;

    // Whether this preprocessor is currently active.
    virtual bool isEnabled() const { return m_enabled; }
    virtual void setEnabled(bool v) { m_enabled = v; }

    // If false, the UI shows a locked (non-toggleable) entry.
    virtual bool supportsDisable() const { return true; }

    virtual void preprocess(std::unique_ptr<GrayscaleImage>& image,
                            const ProcessingContext& ctx,
                            ProcessingResult& result) = 0;
protected:
    bool m_enabled = true;
};