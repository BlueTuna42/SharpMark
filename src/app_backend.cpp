#include "app_backend.h"
#include <QDebug>
#include <QFileInfo>
#include <filesystem>
#include <chrono>

#include "tools/scan.h"
#include "pipeline/interfaces.h"
#include "pipeline/runner.h"
#include "loaders/bmp_loader.h"
#include "processors/laplacian_focus.h"
#include "processors/state_cache.h"
#include "processors/clip_embedding.h"
#include "processors/aesthetic_scorer.h"
#include "postprocessors/xmp_rating.h"
#include "postprocessors/state_cache.h"
#include "img_tools/bmp.h"

// Helper function exactly as it was in your main.cpp
PipelineRunner createPipeline() {
    PipelineRunner runner;
    
    // Using the correct loader class name
    runner.setLoader(std::make_unique<DefaultImageLoader>());
    
    runner.addProcessor(std::make_unique<StateCacheProcessor>());
    runner.addProcessor(std::make_unique<LaplacianFocusProcessor>());
    
#ifdef USE_ONNXRUNTIME
    // runner.addProcessor(std::make_unique<ClipEmbeddingProcessor>("vision_model_quantized.onnx"));
    // runner.addProcessor(std::make_unique<AestheticScorer>());
#endif

    runner.addPostProcessor(std::make_unique<XmpRatingPostProcessor>());
    runner.addPostProcessor(std::make_unique<StateCachePostProcessor>());
    
    return runner;
}

AppBackend::AppBackend(QObject *parent) : QObject(parent) {
    m_statusText = "Ready";
}

AppBackend::~AppBackend() {
    cancelScan();
    if (m_scanThread.joinable()) {
        m_scanThread.join();
    }
}

void AppBackend::selectFolder(const QString &folderPath) {
    if (m_isScanning) return;

    QString cleanPath = folderPath;
    if (cleanPath.startsWith("file:///")) {
#ifdef Q_OS_WIN
        cleanPath = cleanPath.mid(8);
#else
        cleanPath = cleanPath.mid(7);
#endif
    }
    
    m_currentFolder = cleanPath;
    setStatusText("Selected: " + m_currentFolder);
}

void AppBackend::startScan() {
    if (m_currentFolder.isEmpty() || m_isScanning) return;

    m_isScanning = true;
    m_cancelRequested = false;
    setProgress(0);
    setTotalFiles(0);
    setStatusText("Scanning...");

    if (m_scanThread.joinable()) {
        m_scanThread.join();
    }

    // Spawn the background scanner thread
    m_scanThread = std::thread(&AppBackend::runScannerTask, this);
}

void AppBackend::cancelScan() {
    m_cancelRequested = true;
}

void AppBackend::runScannerTask() {
    // 1. Use your existing Scanner class
    std::string dirpath = m_currentFolder.toStdString();
    auto files = Scanner::scanFiles(dirpath);
    const int totalFiles = static_cast<int>(files.size());
    
    setTotalFiles(totalFiles);

    if (totalFiles == 0) {
        setStatusText("No images found.");
        m_isScanning = false;
        return;
    }

    // 2. Thread Pool Setup (Exactly like your main.cpp)
    std::atomic<size_t> fileIndex{0};
    const unsigned int numThreads = std::thread::hardware_concurrency();
    const unsigned int threadsToUse = (numThreads > 1) ? numThreads - 2 : 1;
    std::vector<std::thread> workers;

    for (unsigned int i = 0; i < threadsToUse; ++i) {
        workers.emplace_back([this, &files, &fileIndex, totalFiles]() {
            
            // Each thread gets its own pipeline instance
            PipelineRunner runner = createPipeline();
            
            // Dummy settings for now. In the future, we will pass these from QML.
            AppSettings settings; 

            while (!m_cancelRequested) {
                size_t idx = fileIndex.fetch_add(1, std::memory_order_relaxed);
                if (idx >= files.size()) break;

                const std::string& file = files[idx];

                // Setup Context
                ProcessingContext ctx;
                ctx.rawFilePath = file;
                ctx.settings = settings;
#ifdef _WIN32
                ctx.filePath = std::filesystem::u8path(file);
#else
                ctx.filePath = std::filesystem::path(file);
#endif
                ctx.cacheDir = ctx.filePath.parent_path() / ".laplacian_cache";

                // Run real pipeline
                ProcessingResult result = runner.run(ctx);

                if (result.success) {
                    // Extract metadata
                    int w = 0, h = 0;
                    ImageIO::readOriginalSize(file, w, h);
                    QString fileName = QFileInfo(QString::fromStdString(file)).fileName();
                    
                    // You can add aestheticScore to ProcessingResult later, using a dummy 0.0f for now
                    float aestheticScore = 0.0f; 

                    // Emit to GUI thread
                    QString absolutePath = QString::fromStdString(file);
                    emit fileProcessed(fileName, absolutePath, result.isBlurry, aestheticScore, w, h);
                }

                // Update Progress (every 5 files to reduce signal flood, exactly like GTK)
                if (idx % 5 == 0 || idx == files.size() - 1) {
                    QMetaObject::invokeMethod(this, [this, idx]() {
                        setProgress(static_cast<int>(idx + 1));
                    }, Qt::QueuedConnection);
                }
            }
        });
    }

    // Wait for all workers to finish
    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    m_isScanning = false;
    setStatusText(m_cancelRequested ? "Cancelled" : "Finished");
    emit scanFinished();
}

// --- Properties getters/setters ---
QString AppBackend::statusText() const { return m_statusText; }
void AppBackend::setStatusText(const QString &text) {
    if (m_statusText != text) { m_statusText = text; emit statusTextChanged(); }
}
int AppBackend::progress() const { return m_progress; }
void AppBackend::setProgress(int value) {
    if (m_progress != value) { m_progress = value; emit progressChanged(); }
}
int AppBackend::totalFiles() const { return m_totalFiles; }
void AppBackend::setTotalFiles(int value) {
    if (m_totalFiles != value) { m_totalFiles = value; emit totalFilesChanged(); }
}