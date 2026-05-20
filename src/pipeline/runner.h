#pragma once
#include "interfaces.h"
#include <vector>
#include <memory>

class PipelineRunner {
private:
    std::unique_ptr<IImageLoader> loader_;
    std::vector<std::unique_ptr<IImageProcessor>> processors_;
    std::vector<std::unique_ptr<IPostProcessor>> postProcessors_;

public:
    void setLoader(std::unique_ptr<IImageLoader> loader) {
        loader_ = std::move(loader);
    }

    void addProcessor(std::unique_ptr<IImageProcessor> processor) {
        processors_.push_back(std::move(processor));
    }

    void addPostProcessor(std::unique_ptr<IPostProcessor> postProcessor) {
        postProcessors_.push_back(std::move(postProcessor));
    }

    ProcessingResult run(const ProcessingContext& ctx) {
        ProcessingResult finalResult;
        finalResult.success = false;

        IImageProcessor* activeProcessor = nullptr;
        for (auto& p : processors_) {
            if (p->supports(ctx)) {
                activeProcessor = p.get();
                break;
            }
        }

        if (!activeProcessor) {
            finalResult.warnings.push_back("No suitable processor found");
            return finalResult;
        }

        // 1. Try Cache First
        auto cachedResult = activeProcessor->tryProcessFromCache(ctx);
        if (cachedResult) {
            finalResult = *cachedResult;
        } else {
            // 2. Load & Process
            auto image = loader_->load(ctx);
            if (!image) {
                finalResult.warnings.push_back("Failed to load image");
                return finalResult;
            }
            finalResult = activeProcessor->process(*image, ctx);
        }

        // 3. Post-processing (e.g., XMP writing)
        for (auto& pp : postProcessors_) {
            pp->handle(ctx, finalResult);
        }

        return finalResult;
    }
};