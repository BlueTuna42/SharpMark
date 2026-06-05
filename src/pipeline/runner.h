#pragma once
#include "interfaces.h"
#include <vector>
#include <memory>
#include <QDebug>
#include <QString> 
#include <exception>

class PipelineRunner {
private:
    std::unique_ptr<IImageLoader> loader_;
    std::vector<std::unique_ptr<IImagePreprocessor>> preprocessors_;
    std::vector<std::unique_ptr<IImageProcessor>> processors_;
    std::vector<std::unique_ptr<IPostProcessor>> postProcessors_;

public:
    void setLoader(std::unique_ptr<IImageLoader> loader) { loader_ = std::move(loader); }

    // Preprocessors run after the image is loaded, before any processor
    void addPreprocessor(std::unique_ptr<IImagePreprocessor> preprocessor) { preprocessors_.push_back(std::move(preprocessor)); }

    // Processors will be executed in the order they are added
    void addProcessor(std::unique_ptr<IImageProcessor> processor) { processors_.push_back(std::move(processor)); }
    
    // Post-processors will all be executed at the end
    void addPostProcessor(std::unique_ptr<IPostProcessor> postProcessor) { postProcessors_.push_back(std::move(postProcessor)); }

    ProcessingResult run(const ProcessingContext& ctx) {
        ProcessingResult result;
        result.success = true;

        std::unique_ptr<GrayscaleImage> currentImage = nullptr;
        bool imageLoaded = false;

        // Wrap the ENTIRE pipeline in a try-catch block
        try {
            // Run the chain of Processors
            for (auto& p : processors_) {
                if (!p->supports(ctx)) continue;

                if (result.rejected) break; 

                // Try Cache
                if (p->tryProcessFromCache(ctx, result)) {
                    result.processorsRun.push_back(p->name() + " (cached)");
                    continue;
                }

                if (!imageLoaded) {
                    currentImage = loader_->load(ctx);
                    imageLoaded = true;
                    if (!currentImage) {
                        result.success = false;
                        result.warnings.push_back("Failed to load image");
                        break;
                    }
                    
                    for (auto& pre : preprocessors_) {
                        if (pre->isEnabled()) {
                            pre->preprocess(currentImage, ctx, result);
                            result.processorsRun.push_back(pre->name() + " (pre)");
                        }
                    }
                }

                // Execute processing
                if (currentImage) {
                    p->process(currentImage, ctx, result);
                    result.processorsRun.push_back(p->name());
                }
            }

            // Run multiple Post-processors
            for (auto& pp : postProcessors_) {
                pp->handle(ctx, result);
            }

        } catch (const std::exception& e) {
#ifdef _WIN32
            QString safePath = QString::fromStdWString(ctx.filePath.wstring());
#else
            QString safePath = QString::fromStdString(ctx.filePath.string());
#endif

            qWarning() << "[Pipeline Fatal Error] on file:" << safePath << "Exception:" << e.what();
            
            result.success = false;
            result.rejected = true;
            result.warnings.push_back(std::string("Fatal C++ Exception: ") + e.what());
            
        } catch (...) {
#ifdef _WIN32
            QString safePath = QString::fromStdWString(ctx.filePath.wstring());
#else
            QString safePath = QString::fromStdString(ctx.filePath.string());
#endif

            qWarning() << "[Pipeline Fatal Error] Unknown exception on file:" << safePath;
            
            result.success = false;
            result.rejected = true;
            result.warnings.push_back("Fatal Unknown Exception occurred.");
        }

        return result;
    }
};