#pragma once
#include <string>
#include <variant>
#include <vector>
#include <memory>
#include <filesystem>
#include <unordered_map>
#include "../struct.h"
#include "../gui/gui.h"

struct AnalysisValue {
    std::string key;
    std::variant<int, double, bool, std::string> value;
};

struct ProcessingContext {
    std::string rawFilePath;
    std::filesystem::path filePath;
    AppSettings settings;
    std::filesystem::path cacheDir;
    bool requireRGB = false;
};

struct ProcessingResult {
    bool success = true;
    bool isBlurry = false;
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
    virtual void handle(const ProcessingContext& ctx, const ProcessingResult& result) = 0;
};

class IImageLoader {
public:
    virtual ~IImageLoader() = default;
    virtual std::unique_ptr<GrayscaleImage> load(const ProcessingContext& ctx) = 0;
};