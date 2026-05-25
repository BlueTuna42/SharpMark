#include <iostream>
#include <vector>
#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <filesystem>
#include <fstream>
#include <chrono>

#include "tools/scan.h"
#include "gui/gui.h"
#include "pipeline/interfaces.h"
#include "pipeline/runner.h"
#include "loaders/bmp_loader.h"
#include "processors/laplacian_focus.h"
#include "postprocessors/xmp_rating.h"

#include "processors/draw_square.h"

class FocusCheckerApp {
private:
    std::mutex output_mutex;

    // assemble the processing pipeline
    PipelineRunner createPipeline() {
        PipelineRunner runner;
        runner.setLoader(std::make_unique<DefaultImageLoader>());
        runner.addProcessor(std::make_unique<LaplacianFocusProcessor>());
        runner.addPostProcessor(std::make_unique<XmpRatingPostProcessor>());
        return runner;
    }

public:
    bool processDirectory(const std::string& dirpath, VisualGUI& gui) {
        auto files = Scanner::scanFiles(dirpath);
        std::cout << "Found " << files.size() << " image file(s):" << std::endl;
        const int totalFiles = static_cast<int>(files.size());

        if (totalFiles == 0) return true;

        std::atomic<int> processedFiles{0};
        std::atomic<int> sharpFiles{0};
        std::atomic<int> blurryFiles{0};

        gui.SetCurrentDirectory(dirpath);
        gui.ResetProgress(totalFiles);
        
        AppSettings settings = gui.GetSettings();

        #ifdef _WIN32
        std::ofstream log("NUL");
        #else
        std::ofstream log("/dev/null");
        #endif

        if (!log.is_open()) {
            std::cerr << "Failed to open dummy log stream." << std::endl;
        }

        const unsigned int numThreads = std::thread::hardware_concurrency();
        const unsigned int threadsToUse = (numThreads > 0) ? numThreads : 4;

        std::atomic<size_t> fileIndex{0};
        std::vector<std::thread> workers;

        // Launch worker threads
        for (unsigned int i = 0; i < threadsToUse; ++i) {
            workers.emplace_back([this, &files, &log, &gui, settings, &processedFiles, &sharpFiles, &blurryFiles, totalFiles, &fileIndex]() {
                PipelineRunner runner = createPipeline();
                
                while (true) {
                    size_t idx = fileIndex.fetch_add(1, std::memory_order_relaxed);
                    if (idx >= files.size()) {
                        break;
                    }

                    const std::string& file = files[idx];
                    
                    ProcessingContext ctx;
                    ctx.filePath = file;
                    ctx.settings = settings;
                    
                    #ifdef _WIN32
                    std::filesystem::path origPath = std::filesystem::u8path(file);
                    #else
                    std::filesystem::path origPath(file);
                    #endif
                    ctx.cacheDir = origPath.parent_path() / ".laplacian_cache";
                    
                    ProcessingResult result = runner.run(ctx);

                    if (result.success) {
                        if (result.isBlurry) {
                            ++blurryFiles;
                        } else {
                            ++sharpFiles;
                        }
                        gui.AddResult(file, result.isBlurry);

                        std::lock_guard<std::mutex> lock(output_mutex);
                        if (result.isBlurry) {
                            log << file << std::endl;
                        }
                    } else {
                        std::lock_guard<std::mutex> lock(output_mutex);
                        std::cerr << "Failed to process " << file << std::endl;
                    }

                    // Update GUI progress bar
                    const int current = ++processedFiles;
                    gui.UpdateProgress(current, totalFiles);
                }
            });
        }

        // Wait for all workers
        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }

        log.close();
        gui.ShowFinished(sharpFiles.load(), blurryFiles.load());
        gui.SetCurrentDirectory(dirpath); 
        return true;
    }
};

int main(int argc, char** argv) {
    std::string dirpath;
    VisualGUI gui;
    FocusCheckerApp app;

    if (argc > 1) {
        dirpath = argv[1];
        if (dirpath.empty()) {
            std::cerr << "No directory selected." << std::endl;
            return 1;
        }
        return app.processDirectory(dirpath, gui) ? 0 : 1;
    } else {
        while (!gui.IsClosed()) {
            std::cout << "Waiting for directory selection in GUI..." << std::endl;
            dirpath = gui.SelectDirectory();

            if (dirpath.empty()) {
                break;
            }

            if (!app.processDirectory(dirpath, gui)) {
                return 1;
            }
        }
        return 0;
    }
}