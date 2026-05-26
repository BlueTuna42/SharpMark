#include <iostream>
#include <vector>
#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <filesystem>
#include <fstream>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#undef SetCurrentDirectory 
#endif

#include "tools/scan.h"
#include "gui/gui.h"
#include "pipeline/interfaces.h"
#include "pipeline/runner.h"
#include "gui/utils/path_utils.h"
#include "img_tools/bmp.h"

#include "loaders/bmp_loader.h"
#include "processors/laplacian_focus.h"
#include "processors/state_cache.h"
#include "processors/clip_embedding.h"
#include "processors/aesthetic_scorer.h"
#include "postprocessors/xmp_rating.h"
#include "postprocessors/state_cache.h"


class FocusCheckerApp {
private:
    std::mutex output_mutex;
    std::thread manager_thread;
    std::atomic<bool> cancel_requested{false};

    PipelineRunner createPipeline() {
        PipelineRunner runner;
        runner.setLoader(std::make_unique<DefaultImageLoader>());
        runner.addProcessor(std::make_unique<StateCacheProcessor>());
        runner.addProcessor(std::make_unique<ClipEmbeddingProcessor>("vision_model_quantized.onnx"));
        std::filesystem::path configDir = get_app_config_dir();
        runner.addProcessor(std::make_unique<AestheticScorer>(configDir));
        //runner.addProcessor(std::make_unique<LaplacianFocusProcessor>());
        runner.addPostProcessor(std::make_unique<XmpRatingPostProcessor>());
        runner.addPostProcessor(std::make_unique<StateCachePostProcessor>());
        return runner;
    }

    void stopCurrentTask() {
        cancel_requested = true;
        if (manager_thread.joinable()) {
            manager_thread.join();
        }
        cancel_requested = false;
    }

public:
    ~FocusCheckerApp() {
        stopCurrentTask();
    }

    bool processDirectory(const std::string& dirpath, VisualGUI& gui) {
        stopCurrentTask();

        auto files = Scanner::scanFiles(dirpath);
        const int totalFiles = static_cast<int>(files.size());

        if (totalFiles == 0) {
            gui.ShowFinished(0, 0);
            gui.SetCurrentDirectory(dirpath);
            return true;
        }

        gui.ResetProgress(totalFiles);
        gui.SetCurrentDirectory(dirpath);
        AppSettings settings = gui.GetSettings();

        // Run the thread pool inside a manager thread to prevent GTK blockage
        manager_thread = std::thread([this, files, dirpath, &gui, settings, totalFiles]() {
            std::atomic<int> processedFiles{0};
            std::atomic<int> sharpFiles{0};
            std::atomic<int> blurryFiles{0};
            std::atomic<size_t> fileIndex{0};

            const unsigned int numThreads = std::thread::hardware_concurrency();
            const unsigned int threadsToUse = (numThreads > 1) ? numThreads - 2 : 1;
            std::vector<std::thread> workers;

            std::ofstream debug_log("sharpmark_debug.log", std::ios::out | std::ios::trunc);
            if (!debug_log) {
                debug_log.open("C:\\sharpmark_debug.log", std::ios::out | std::ios::trunc);
            }

            for (unsigned int i = 0; i < threadsToUse; ++i) {
                workers.emplace_back([&]() {
                    #ifdef _WIN32
                        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
                    #endif
                    PipelineRunner runner = createPipeline();
                    
                    while (!cancel_requested && !gui.IsClosed()) {
                        if (gui.IsPaused()) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                            continue;
                        }
                        
                        size_t idx = fileIndex.fetch_add(1, std::memory_order_relaxed);
                        if (idx >= files.size()) break;

                        const std::string& currentFilePath = files[idx];
                        
                        ProcessingContext ctx;
                        ctx.rawFilePath = currentFilePath;
                        ctx.settings = settings;
                        #ifdef _WIN32
                        ctx.filePath = std::filesystem::u8path(currentFilePath);
                        #else
                        ctx.filePath = std::filesystem::path(currentFilePath);
                        #endif
                        ctx.cacheDir = ctx.filePath.parent_path() / ".laplacian_cache";

                        ProcessingResult result = runner.run(ctx);

                        if (!result.warnings.empty()) {
                            std::lock_guard<std::mutex> lock(output_mutex);
                            debug_log << "[WARNING] " << currentFilePath << ":\n";
                            for (const auto& w : result.warnings) {
                                debug_log << "  -> " << w << "\n";
                            }
                            debug_log.flush();
                        }
    
                        if (result.success) {
                            auto it = result.sharedData.find("clip_vector");
                            if (it == result.sharedData.end()) {
                                std::lock_guard<std::mutex> lock(output_mutex);
                                debug_log << "[ERROR] No clip_vector in sharedData for " << currentFilePath << "!\n";
                                debug_log.flush();
                            }
                        }

                        if (result.success) {
                            if (result.isBlurry) {
                                ++blurryFiles;
                            } else {
                                 ++sharpFiles;
                            }

                            int w = 0, h = 0;
                            ImageIO::readOriginalSize(currentFilePath, w, h);
                            gui.AddResult(currentFilePath, result.isBlurry, w, h);

                            std::lock_guard<std::mutex> lock(output_mutex);
                            if (result.isBlurry && debug_log.is_open()) {
                                debug_log << currentFilePath << std::endl;
                            }
                        }

                        const int current = ++processedFiles;
                        if (current % 5 == 0 || current == totalFiles) {
                            gui.UpdateProgress(current, totalFiles);
                        }
                    }
                });
            }

            // Wait for all workers
            for (auto& worker : workers) {
                if (worker.joinable()) {
                    worker.join();
                }
            }

            if (!cancel_requested && !gui.IsClosed()) {
                gui.ShowFinished(sharpFiles.load(), blurryFiles.load());
                gui.SetCurrentDirectory(dirpath);
            }
        });

        return true;
    }
};

int main(int argc, char** argv) {
    std::string dirpath;
    VisualGUI gui;
    FocusCheckerApp app;

    if (argc > 1) {
        dirpath = argv[1];
        if (!dirpath.empty()) {
            app.processDirectory(dirpath, gui);
        }
    }

    while (!gui.IsClosed()) {
        dirpath = gui.SelectDirectory();
        if (!dirpath.empty()) {
            app.processDirectory(dirpath, gui);
        }
    }

    return 0;
}