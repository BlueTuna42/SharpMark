#include "app_backend.h"
#include <QDebug>
#include <QFileInfo>
#include <filesystem>
#include <chrono>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QBuffer>
#include <fstream>
#include <libraw/libraw.h>
#include <QVariantMap>
#include <iomanip>
#include <sstream>
#include <QUrl>
#include <QImageReader>
#include <QImageIOHandler>
#include <QPainter>
#include <QPainterPath>
#include <QBuffer>
#include <QFile>
#include <QTextStream>

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
    loadSettings();
}

AppBackend::~AppBackend() {
    cancelScan();
    if (m_scanThread.joinable()) {
        m_scanThread.join();
    }
    saveSettings();
}

QVariantMap AppBackend::getPhotoMetadata(const QString& rawPath) {
    QVariantMap result;
    
    // 1. Properly decode the URI component path from QML
    QString cleanPath = QUrl::fromPercentEncoding(rawPath.toUtf8());
    
#ifdef Q_OS_WIN
    if (cleanPath.startsWith("file:///")) cleanPath = cleanPath.mid(8);
    else if (cleanPath.startsWith('/')) cleanPath = cleanPath.mid(1);
#else
    if (cleanPath.startsWith("file://")) cleanPath = cleanPath.mid(7);
#endif

    QFileInfo fileInfo(cleanPath);
    if (!fileInfo.exists()) {
        result["infoText"] = "File not found: " + cleanPath;
        return result;
    }

    std::ostringstream ss;
    bool foundData = false;

    // 2. Fast Path: Use Qt's built-in image reader for standard formats (JPG, PNG)
    QImageReader reader(cleanPath);
    QSize imgSize = reader.size();
    
    if (imgSize.isValid()) {
        QString make = reader.text("Make");
        if (make.isEmpty()) make = reader.text("exif:Make");
        
        QString model = reader.text("Model");
        if (model.isEmpty()) model = reader.text("exif:Model");
        
        QString date = reader.text("DateTime");
        if (date.isEmpty()) date = reader.text("exif:DateTimeOriginal");
        
        QString camera = (make + " " + model).trimmed();
        
        if (!camera.isEmpty()) ss << "<b>Camera:</b> " << camera.toStdString() << "<br>";
        if (!date.isEmpty()) ss << "<b>Date:</b> " << date.toStdString() << "<br><br>";
        
        ss << "<b>Resolution:</b> " << imgSize.width() << " x " << imgSize.height() << "px<br>";
        ss << "<b>File Size:</b> " << std::fixed << std::setprecision(2) << (fileInfo.size() / 1024.0 / 1024.0) << " MB";
        
        foundData = true;
    }

    // 3. Deep Path: Use LibRaw for professional formats (RAW)
    if (!foundData) {
        LibRaw lr;
        int ret = LIBRAW_SUCCESS;
        
#if defined(_WIN32)
        ret = lr.open_file(cleanPath.toStdWString().c_str());
        if (ret != LIBRAW_SUCCESS) {
            ret = lr.open_file(cleanPath.toLocal8Bit().constData());
        }
#else
        ret = lr.open_file(cleanPath.toUtf8().constData());
#endif

        if (ret == LIBRAW_SUCCESS) {
            if (strlen(lr.imgdata.idata.make) > 0 || strlen(lr.imgdata.idata.model) > 0) {
                ss << "<b>Camera:</b> " << lr.imgdata.idata.make << " " << lr.imgdata.idata.model << "<br>";
            }
            if (strlen(lr.imgdata.lens.Lens) > 0) {
                ss << "<b>Lens:</b> " << lr.imgdata.lens.Lens << "<br>";
            }
            if (lr.imgdata.other.timestamp > 0) {
                ss << "<b>Date:</b> " << std::put_time(std::localtime(&lr.imgdata.other.timestamp), "%Y-%m-%d %H:%M:%S") << "<br><br>";
            }
            if (lr.imgdata.other.aperture > 0.0f) {
                ss << "<b>Aperture:</b> f/" << lr.imgdata.other.aperture << "<br>";
            }
            if (lr.imgdata.other.shutter > 0.0f) {
                ss << "<b>Shutter:</b> 1/" << (int)(1.0f / lr.imgdata.other.shutter) << "s<br>";
            }
            if (lr.imgdata.other.iso_speed > 0.0f) {
                ss << "<b>ISO:</b> " << lr.imgdata.other.iso_speed << "<br>";
            }
            if (lr.imgdata.other.focal_len > 0.0f) {
                ss << "<b>Focal Length:</b> " << lr.imgdata.other.focal_len << "mm<br>";
            }
            
            ss << "<br><b>Resolution:</b> " << lr.imgdata.sizes.width << " x " << lr.imgdata.sizes.height << "px<br>";
            ss << "<b>File Size:</b> " << std::fixed << std::setprecision(2) << (fileInfo.size() / 1024.0 / 1024.0) << " MB";
            
            foundData = true;
        }
        lr.recycle();
    }

    // 4. Analysis Data: Read from .laplacian_cache/state.csv
    QFile csvFile(fileInfo.absolutePath() + "/.laplacian_cache/state.csv");
    if (csvFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&csvFile);
        QString targetPath = fileInfo.absoluteFilePath();
        
        while (!in.atEnd()) {
            QString line = in.readLine();
            QStringList parts = line.split(',');
            // CSV format: filePath, isBlurry, variance
            if (parts.size() >= 3) {
                if (QFileInfo(parts[0]).absoluteFilePath() == targetPath || parts[0] == cleanPath) {
                    bool isBlurry = (parts[1] == "1" || parts[1].toLower() == "true");
                    double variance = parts[2].toDouble();
                    
                    ss << "<br><br><span style='color:#aaaaaa'>— Analysis —</span><br>";
                    ss << "<b>Status:</b> <span style='color:" << (isBlurry ? "#ff5555" : "#55ff55") << "'>" 
                       << (isBlurry ? "Blurry" : "Sharp") << "</span><br>";
                    ss << "<b>Focus Score:</b> " << std::fixed << std::setprecision(2) << variance;
                    
                    foundData = true;
                    break;
                }
            }
        }
    }

    if (foundData) {
        result["infoText"] = QString::fromStdString(ss.str());
    } else {
        result["infoText"] = "No EXIF or Analysis data available";
    }
    
    return result;
}

void AppBackend::updateHistogramFromImage(const QImage& src) {
    if (src.isNull()) {
        m_histogramBase64 = "";
        emit histogramUpdated();
        return;
    }

    int w = 300;
    int h = 150;
    QImage histImg(w, h, QImage::Format_ARGB32);
    histImg.fill(QColor(0x25, 0x25, 0x25, 255));

    int hist_r[256] = {0}, hist_g[256] = {0}, hist_b[256] = {0}, hist_l[256] = {0};
    int max_count = 0;
    const int step = 4;

    for (int y = 0; y < src.height(); y += step) {
        const QRgb* row = reinterpret_cast<const QRgb*>(src.constScanLine(y));
        for (int x = 0; x < src.width(); x += step) {
            QRgb pixel = row[x];
            int r = qRed(pixel);
            int g = qGreen(pixel);
            int b = qBlue(pixel);
            int luma = (r * 299 + g * 587 + b * 114) / 1000;

            hist_r[r]++;
            hist_g[g]++;
            hist_b[b]++;
            hist_l[luma]++;

            if (hist_l[luma] > max_count) max_count = hist_l[luma];
        }
    }

    if (max_count > 0) {
        QPainter p(&histImg);
        p.setRenderHint(QPainter::Antialiasing);
        p.setCompositionMode(QPainter::CompositionMode_Plus);

        auto drawChannel = [&](int* hist, QColor color) {
            QPainterPath path;
            path.moveTo(0, h);
            for (int i = 0; i < 256; ++i) {
                double x = (i / 255.0) * w;
                double val = std::pow((double)hist[i] / max_count, 0.5); 
                double y = h - (val * h * 0.95);
                path.lineTo(x, y);
            }
            path.lineTo(w, h);
            path.closeSubpath();
            p.fillPath(path, color);
        };

        drawChannel(hist_r, QColor(255, 0, 0, 150));
        drawChannel(hist_g, QColor(0, 255, 0, 150));
        drawChannel(hist_b, QColor(0, 0, 255, 150));
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        drawChannel(hist_l, QColor(255, 255, 255, 60));
    }

    QByteArray ba;
    QBuffer buffer(&ba);
    buffer.open(QIODevice::WriteOnly);
    histImg.save(&buffer, "PNG");
    m_histogramBase64 = "data:image/png;base64," + QString(ba.toBase64());
    emit histogramUpdated();
}

QString AppBackend::getSettingsFilePath() const {
#ifdef Q_OS_WIN
    // Portable Windows mode: save next to the executable
    return QCoreApplication::applicationDirPath() + "/settings.conf";
#else
    // Linux/macOS mode: save in user's config directory
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/SharpMark";
    QDir dir;
    if (!dir.exists(configDir)) {
        dir.mkpath(configDir);
    }
    return configDir + "/settings.conf";
#endif
}

void AppBackend::loadSettings() {
    std::ifstream in(getSettingsFilePath().toStdString());
    if (!in.is_open()) return;

    std::string line;
    while (std::getline(in, line)) {
        std::istringstream iss(line);
        std::string key;
        if (std::getline(iss, key, '=')) {
            std::string value;
            if (std::getline(iss, value)) {
                if (key == "themeMode") m_themeMode = std::stoi(value);
                else if (key == "writeExif") m_writeExif = (value == "1");
                else if (key == "cacheLaplacian") m_cacheLaplacian = (value == "1");
                else if (key == "rawViewMode") m_rawViewMode = std::stoi(value);
                else if (key == "rawAnalysisMode") m_rawAnalysisMode = std::stoi(value);
            }
        }
    }
}

void AppBackend::saveSettings() {
    std::ofstream out(getSettingsFilePath().toStdString());
    if (!out.is_open()) return;

    out << "themeMode=" << m_themeMode << "\n";
    out << "writeExif=" << (m_writeExif ? 1 : 0) << "\n";
    out << "cacheLaplacian=" << (m_cacheLaplacian ? 1 : 0) << "\n";
    out << "rawViewMode=" << m_rawViewMode << "\n";
    out << "rawAnalysisMode=" << m_rawAnalysisMode << "\n";
}

int AppBackend::themeMode() const { return m_themeMode; }
void AppBackend::setThemeMode(int mode) { if (m_themeMode != mode) { m_themeMode = mode; saveSettings(); emit themeModeChanged(); } }

bool AppBackend::writeExif() const { return m_writeExif; }
void AppBackend::setWriteExif(bool write) { if (m_writeExif != write) { m_writeExif = write; saveSettings(); emit writeExifChanged(); } }

bool AppBackend::cacheLaplacian() const { return m_cacheLaplacian; }
void AppBackend::setCacheLaplacian(bool cache) { if (m_cacheLaplacian != cache) { m_cacheLaplacian = cache; saveSettings(); emit cacheLaplacianChanged(); } }

int AppBackend::rawViewMode() const { return m_rawViewMode; }
void AppBackend::setRawViewMode(int mode) { if (m_rawViewMode != mode) { m_rawViewMode = mode; saveSettings(); emit rawViewModeChanged(); } }

int AppBackend::rawAnalysisMode() const { return m_rawAnalysisMode; }
void AppBackend::setRawAnalysisMode(int mode) { if (m_rawAnalysisMode != mode) { m_rawAnalysisMode = mode; saveSettings(); emit rawAnalysisModeChanged(); } }

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
    
    // 1. FAST SCAN DIRECTORY IMMEDIATELY
    m_files = Scanner::scanFiles(m_currentFolder.toStdString());
    setTotalFiles(static_cast<int>(m_files.size()));
    setProgress(0);
    
    // 2. EMIT TO QML INSTANTLY
    for (size_t i = 0; i < m_files.size(); ++i) {
        QString absolutePath = QString::fromStdString(m_files[i]);
        QString fileName = QFileInfo(absolutePath).fileName();
        emit fileFound(fileName, absolutePath, static_cast<int>(i));
    }
}

void AppBackend::startScan() {
    if (m_files.empty() || m_isScanning) return;

    m_isScanning = true;
    m_cancelRequested = false;
    setProgress(0);
    setStatusText("Scanning...");

    if (m_scanThread.joinable()) {
        m_scanThread.join();
    }

    m_scanThread = std::thread(&AppBackend::runScannerTask, this);
}

void AppBackend::runScannerTask() {
    std::atomic<size_t> fileIndex{0};
    const unsigned int numThreads = std::thread::hardware_concurrency();
    const unsigned int threadsToUse = (numThreads > 1) ? numThreads - 2 : 1;
    std::vector<std::thread> workers;

    for (unsigned int i = 0; i < threadsToUse; ++i) {
        workers.emplace_back([this, &fileIndex]() {
            PipelineRunner runner = createPipeline();
            AppSettings settings; 

            while (!m_cancelRequested) {
                size_t idx = fileIndex.fetch_add(1, std::memory_order_relaxed);
                if (idx >= m_files.size()) break;

                const std::string& file = m_files[idx];

                ProcessingContext ctx;
                ctx.rawFilePath = file;
                ctx.settings = settings;
#ifdef _WIN32
                ctx.filePath = std::filesystem::u8path(file);
#else
                ctx.filePath = std::filesystem::path(file);
#endif
                ctx.cacheDir = ctx.filePath.parent_path() / ".laplacian_cache";

                ProcessingResult result = runner.run(ctx);

                if (result.success) {
                    int w = 0, h = 0;
                    ImageIO::readOriginalSize(file, w, h);
                    float aestheticScore = 0.0f; 
                    
                    // EMIT RESULT WITH INDEX SO QML KNOWS WHICH CELL TO UPDATE
                    emit fileProcessed(static_cast<int>(idx), result.isBlurry, aestheticScore, w, h);
                }

                if (idx % 5 == 0 || idx == m_files.size() - 1) {
                    QMetaObject::invokeMethod(this, [this, idx]() {
                        setProgress(static_cast<int>(idx + 1));
                    }, Qt::QueuedConnection);
                }
            }
        });
    }

    for (auto& worker : workers) {
        if (worker.joinable()) worker.join();
    }

    m_isScanning = false;
    setStatusText(m_cancelRequested ? "Cancelled" : "Finished");
    emit scanFinished();
}

void AppBackend::cancelScan() {
    m_cancelRequested = true;
}

bool AppBackend::trashFile(const QString &filePath) {
    return QFile::moveToTrash(filePath);
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