#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <chrono>
#include <future>
#include <mutex>
#include <atomic>
#include "img_tools/laplacian.h"
#include "img_tools/bmp.h"
#include "tools/scan.h"
#include "tools/XMP_tools.h"
#include "gui/gui.h"

class FocusCheckerApp {
private:
    const double focusConst = 150.0;
    std::mutex output_mutex; 

public:
        bool processDirectory(const std::string& dirpath, VisualGUI& gui) {
        auto files = Scanner::scanFiles(dirpath);
        std::cout << "Found " << files.size() << " image file(s):" << std::endl;
        const int totalFiles = static_cast<int>(files.size());
        
        std::atomic<int> processedFiles{0};
        std::atomic<int> sharpFiles{0};
        std::atomic<int> blurryFiles{0};
        
        gui.SetCurrentDirectory(dirpath);
        gui.ResetProgress(totalFiles);

#ifdef _WIN32
        std::ofstream log("NUL");
#else
        std::ofstream log("/dev/null");
#endif

        if (!log.is_open()) {
            std::cerr << "Failed to open dummy log stream." << std::endl;
        }

#ifdef DEBUG_BENCHMARK
        auto ts_total_start = std::chrono::high_resolution_clock::now();
#endif

        // Determine optimal number of threads (fallback to 4 if OS fails to report)
        const unsigned int numThreads = std::thread::hardware_concurrency();
        const unsigned int threadsToUse = (numThreads > 0) ? numThreads : 4;
        
        std::atomic<size_t> fileIndex{0};
        std::vector<std::thread> workers;

        // Launch fixed number of worker threads
        for (unsigned int i = 0; i < threadsToUse; ++i) {
            workers.emplace_back([this, &files, &log, &gui, &processedFiles, &sharpFiles, &blurryFiles, totalFiles, &fileIndex]() {
                while (true) {
                    // Thread-safe fetch of the next file index
                    size_t idx = fileIndex.fetch_add(1, std::memory_order_relaxed);
                    if (idx >= files.size()) {
                        break;
                    }
                    
                    const std::string& f = files[idx];
                    const int result = processFile(f, log, gui);
                    
                    if (result == 0) {
                        ++sharpFiles;
                    } else if (result == 1) {
                        ++blurryFiles;
                    }

                    const int current = ++processedFiles;
                    gui.UpdateProgress(current, totalFiles);
                }
            });
        }

        // Wait for all worker threads to complete
        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }

#ifdef DEBUG_BENCHMARK
        auto ts_total_end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> total_time = ts_total_end - ts_total_start;
        std::cout << "\n========================================" << std::endl;
        std::cout << " [TOTAL] Directory processing time: " << total_time.count() << " ms" << std::endl;
        std::cout << "========================================" << std::endl;
#endif

        log.close();
        gui.ShowFinished(sharpFiles.load(), blurryFiles.load());
        return true;
    }

    int processFile(const std::string& file, std::ofstream& log, VisualGUI& gui) {
    AppSettings settings = gui.GetSettings();
    double maxVariance = 0.0;
    
    // Convert path using UTF-8 to avoid issues with special characters
#ifdef _WIN32
    std::filesystem::path origPath = std::filesystem::u8path(file);
#else
    std::filesystem::path origPath(file);
#endif
    
    // Cache path
    std::filesystem::path cacheDir = origPath.parent_path() / ".laplacian_cache";
    std::filesystem::path cacheFile = cacheDir / (origPath.filename().string() + ".rawlap");

    bool usedCache = false;

#ifdef DEBUG_BENCHMARK
    auto ts_read = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> t_read(0);
    auto ts_laplace = std::chrono::high_resolution_clock::now();
#endif

    // 1. Attempt to load from cache
    if (settings.cacheLaplacian && std::filesystem::exists(cacheFile)) {
        auto lapImg = LaplacianProcessor::loadLaplacian(cacheFile.string());
        if (lapImg) {
#ifdef DEBUG_BENCHMARK
            auto te_read = std::chrono::high_resolution_clock::now();
            t_read = te_read - ts_read;
            ts_laplace = std::chrono::high_resolution_clock::now();
#endif
            maxVariance = LaplacianProcessor::evaluateSharpnessFromLaplacian(*lapImg, 5, 5);
            usedCache = true;
        }
    }

    // 2. If no cache exists, read the image and process it
    if (!usedCache) {
        auto image = ImageIO::readImage(file, settings.rawMode); 
        if (!image) return -1;

#ifdef DEBUG_BENCHMARK
        auto te_read = std::chrono::high_resolution_clock::now();
        t_read = te_read - ts_read;
        ts_laplace = std::chrono::high_resolution_clock::now();
#endif

        if (settings.cacheLaplacian) {
            auto lapImg = LaplacianProcessor::applyLaplacian(*image);
            maxVariance = LaplacianProcessor::evaluateSharpnessFromLaplacian(*lapImg, 5, 5);
            
            // Safe directory creation across threads
            std::error_code ec;
            if (!std::filesystem::exists(cacheDir, ec)) {
                std::filesystem::create_directories(cacheDir, ec);
            }
            
            LaplacianProcessor::saveLaplacian(*lapImg, cacheFile.string());
        } else {
            maxVariance = LaplacianProcessor::evaluateSharpness(*image, 5, 5);
        }
    }

#ifdef DEBUG_BENCHMARK
    auto te_laplace = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> t_laplace = te_laplace - ts_laplace;
#endif

    bool isBlurry = (maxVariance < focusConst);
    
    if (settings.writeExif) {
        if (isBlurry) {
            XMPTools::writeXmpRating(file, 1);
        } else {
            XMPTools::writeXmpRating(file, 5);
        }
    }

    gui.AddResult(file, isBlurry);

    std::lock_guard<std::mutex> lock(output_mutex);

#ifdef DEBUG_BENCHMARK
    std::cout << "\nFile: " << file << (usedCache ? " (CACHED)" : "") << std::endl;
    std::cout << "  [READ]      Time: " << t_read.count() << " ms" << std::endl;
    std::cout << "  [LAPLACIAN] Time: " << t_laplace.count() << " ms" << std::endl;
    std::cout << "  [DATA]      Var:  " << maxVariance << std::endl;
#endif

    if (isBlurry) {
#ifndef DEBUG_BENCHMARK
        std::cout << file << " is blurry" << std::endl;
#endif
        log << file << std::endl;
    } else {
#ifndef DEBUG_BENCHMARK
        std::cout << file << " is sharp" << std::endl;
#endif
    }

    return isBlurry ? 1 : 0;
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
    }
    
    return 0;
}