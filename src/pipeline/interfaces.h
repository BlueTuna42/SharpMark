#pragma once
#include <string>
#include <variant>
#include <vector>
#include <memory>
#include <filesystem>
#include <optional>
#include "../struct.h"
#include "../gui/gui.h"

struct AnalysisValue {
    std::string key;
    std::variant<int, double, bool, std::string> value;
};

struct ProcessingResult {
    bool success = true;
    bool isBlurry = false;
    std::string processorName;
    std::vector<AnalysisValue> metrics;
    std::vector<std::string> warnings;
};

struct ProcessingContext {
    std::filesystem::path filePath;
    AppSettings settings;
    std::filesystem::path cacheDir;
};

class IImageProcessor {
public:
    virtual ~IImageProcessor() = default;
    virtual std::string name() const = 0;
    virtual bool supports(const ProcessingContext& ctx) const = 0;
    
    // Core processing method
    virtual ProcessingResult process(const GrayscaleImage& image, const ProcessingContext& ctx) = 0;
    
    // Optional fast-path if cached data is available (no image load needed)
    virtual std::optional<ProcessingResult> tryProcessFromCache(const ProcessingContext& ctx) { 
        return std::nullopt; 
    }
};

class IPostProcessor {
public:
    virtual ~IPostProcessor() = default;
    virtual std::string name() const = 0;
    virtual void handle(const ProcessingContext& ctx, const ProcessingResult& result) = 0;
};

class IImageLoader {
public:
    virtual ~IImageLoader() = default;
    virtual std::unique_ptr<GrayscaleImage> load(const ProcessingContext& ctx) = 0;
};