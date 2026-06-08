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
#include <QProcess>

#include "tools/XMP_tools.h"  
#include "tools/scan.h"
#include "img_tools/bmp.h"
#include "gui/utils/path_utils.h"
#include "pipeline/interfaces.h"
#include "pipeline/runner.h"

#include "loaders/bmp_loader.h"
#include "processors/laplacian_focus.h"
#include "processors/state_cache.h"
#include "processors/aesthetic_processor.h"
#include "processors/exposure_check.h"
#include "postprocessors/state_cache.h"
#include "preprocessors/visual_hash_preprocessor.h"
#include "preprocessors/lut_preprocessor.h"


PipelineRunner AppBackend::createPipeline() {
    PipelineRunner runner;
    runner.setLoader(std::make_unique<DefaultImageLoader>());

    // --- Preprocessors ---

    // Visual hash preprocessor (replaces the old VisualHashProcessor)
    {
        auto vhPre = std::make_unique<VisualHashPreprocessor>();
        vhPre->setEnabled(m_preprocessorModel.isEnabled("visual_hash"));
        runner.addPreprocessor(std::move(vhPre));
    }

    // 3D LUT preprocessor
    {
        auto lutPre = std::make_unique<LutPreprocessor>();
        bool lutOn = m_lutEnabled && m_preprocessorModel.isEnabled("lut_3d")
                     && m_activeLutName != "none" && !m_activeLutName.isEmpty();
        lutPre->setEnabled(lutOn);
        if (lutOn) {
            QString lutPath = getLutsDir() + "/" + m_activeLutName;
            try {
                int dim = 33;
#ifdef _WIN32
                auto cubeData = LutPreprocessor::parseCubeFile(
                    std::filesystem::path(lutPath.toStdWString()), dim);
#else
                auto cubeData = LutPreprocessor::parseCubeFile(
                    std::filesystem::path(lutPath.toStdString()), dim);
#endif
                lutPre->setLut(cubeData, dim);
                qDebug() << "[LUT] Pipeline LUT loaded:" << m_activeLutName << "dim=" << dim;
            } catch (const std::exception& e) {
                qWarning() << "[LUT] Failed to load LUT for pipeline:" << e.what();
                lutPre->setEnabled(false);
            }
        }
        runner.addPreprocessor(std::move(lutPre));
    }

    // --- Processors ---

    // Always add state cache first (infrastructure)
    runner.addProcessor(std::make_unique<StateCacheProcessor>());
    // Second state cache check after preprocessors have run
    runner.addProcessor(std::make_unique<StateCacheProcessor>());

    const auto& steps = m_pipelineModel.getSteps();
    for (const auto& step : steps) {
        if (!step.enabled) continue;

        if (step.id == "exposure") {
            runner.addProcessor(std::make_unique<ExposureCheckProcessor>());
        } else if (step.id == "laplacian") {
            runner.addProcessor(std::make_unique<LaplacianFocusProcessor>());
        } else if (step.id == "aiaesthetic") {
            std::filesystem::path appDataDir = get_app_config_dir();
#ifdef _WIN32
            std::filesystem::path modelsDir(QCoreApplication::applicationDirPath().toStdWString());
#else
            std::filesystem::path modelsDir(QCoreApplication::applicationDirPath().toUtf8().constData());
#endif
            runner.addProcessor(std::make_unique<AestheticProcessor>(modelsDir, appDataDir));
        }
    }

    // Always add post-processors at the end (infrastructure)
    runner.addPostProcessor(std::make_unique<StateCachePostProcessor>());

    return runner;
}

AppBackend::AppBackend(QObject *parent)
    : QObject(parent), m_statusText("Ready") {
    connect(&m_pipelineModel,      &PipelineConfigModel::pipelineChanged,
            this, [this]() { if (!m_loadingSettings) saveSettings(); });
    connect(&m_preprocessorModel,  &PreprocessorConfigModel::preprocessorChanged,
            this, [this]() {
                if (m_loadingSettings) return;
                saveSettings();
                reloadViewerLut();
                emit activeLutChanged();
            });

    loadSettings();
    reloadViewerLut();
}

AppBackend::~AppBackend() {
    cancelScan();
    if (m_scanThread.joinable()) {
        m_scanThread.join();
    }
    saveSettings();
}

QString AppBackend::getLutsDir() const {
    QString configDir;
#ifdef Q_OS_WIN
    configDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
#else
    configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
#endif
    QString lutsDir = configDir + "/luts";
    QDir d;
    if (!d.exists(lutsDir)) d.mkpath(lutsDir);
    return lutsDir;
}

QStringList AppBackend::availableLuts() const {
    QDir dir(getLutsDir());
    QStringList filters; filters << "*.cube" << "*.CUBE";
    QStringList files = dir.entryList(filters, QDir::Files, QDir::Name);
    QStringList names;
    names.reserve(files.size());
    for (const QString &f : files)
        names << QFileInfo(f).completeBaseName();
    return names;
}

void AppBackend::loadLutFile(const QString& rawPath) {
    QString srcPath = QUrl::fromPercentEncoding(rawPath.toUtf8());
#ifdef Q_OS_WIN
    if (srcPath.startsWith("file:///")) srcPath = srcPath.mid(8);
#else
    if (srcPath.startsWith("file://"))  srcPath = srcPath.mid(7);
#endif
    srcPath.replace('\\', '/');

    QFileInfo fi(srcPath);
    if (!fi.exists()) {
        qWarning() << "[LUT] loadLutFile: source does not exist:" << srcPath;
        return;
    }

    QString destPath = getLutsDir() + "/" + fi.fileName();
    if (QFile::exists(destPath)) QFile::remove(destPath);
    if (!QFile::copy(srcPath, destPath)) {
        qWarning() << "[LUT] Failed to copy" << srcPath << "to" << destPath;
        return;
    }

    m_activeLutName = fi.fileName();
    m_lutEnabled    = true;
    // Sync the preprocessor model's lut_3d row enabled state
    for (int i = 0; i < static_cast<int>(m_preprocessorModel.getSteps().size()); ++i) {
        if (m_preprocessorModel.getSteps()[i].id == "lut_3d")
            m_preprocessorModel.setStepEnabled(i, true);
    }

    reloadViewerLut();
    saveSettings();
    emit lutEnabledChanged();
    emit activeLutChanged();
    qDebug() << "[LUT] Loaded and activated:" << m_activeLutName;
}

void AppBackend::selectLutPreset(const QString& name) {
    if (name == "none" || name.isEmpty()) {
        m_activeLutName = "none";
        m_lutEnabled    = false;
        m_viewerLutData.clear();
        // Sync the preprocessor model
        for (int i = 0; i < static_cast<int>(m_preprocessorModel.getSteps().size()); ++i) {
            if (m_preprocessorModel.getSteps()[i].id == "lut_3d")
                m_preprocessorModel.setStepEnabled(i, false);
        }
    } else {
        m_activeLutName = name;
        m_lutEnabled    = true;
        // Sync the preprocessor model
        for (int i = 0; i < static_cast<int>(m_preprocessorModel.getSteps().size()); ++i) {
            if (m_preprocessorModel.getSteps()[i].id == "lut_3d")
                m_preprocessorModel.setStepEnabled(i, true);
        }
        reloadViewerLut();
    }
    saveSettings();
    emit lutEnabledChanged();
    emit activeLutChanged();
}

void AppBackend::setLutEnabled(bool v) {
    if (m_lutEnabled == v) return;
    m_lutEnabled = v;
    if (!v) m_viewerLutData.clear();
    else    reloadViewerLut();
    saveSettings();
    emit lutEnabledChanged();
    emit activeLutChanged();
}

void AppBackend::reloadViewerLut() const {
    m_viewerLutData.clear();
    if (!m_lutEnabled || m_activeLutName == "none" || m_activeLutName.isEmpty()) return;

    QString lutPath = getLutsDir() + "/" + m_activeLutName;
    try {
        int dim = 33;
#ifdef _WIN32
        auto cubeData = LutPreprocessor::parseCubeFile(
            std::filesystem::path(lutPath.toStdWString()), dim);
#else
        auto cubeData = LutPreprocessor::parseCubeFile(
            std::filesystem::path(lutPath.toStdString()), dim);
#endif
        // Repack from cube (R-major RGB triplets) into [C][B][G][R] for applyLutToQImage
        m_viewerLutDim = dim;
        const int dim3 = dim * dim * dim;
        m_viewerLutData.resize(3 * dim3);
        for (int b = 0; b < dim; ++b)
        for (int g = 0; g < dim; ++g)
        for (int r = 0; r < dim; ++r) {
            int cubeIdx = (b * dim * dim + g * dim + r) * 3;
            int lutIdx  =  b * dim * dim + g * dim + r;
            m_viewerLutData[0 * dim3 + lutIdx] = cubeData[cubeIdx + 0];
            m_viewerLutData[1 * dim3 + lutIdx] = cubeData[cubeIdx + 1];
            m_viewerLutData[2 * dim3 + lutIdx] = cubeData[cubeIdx + 2];
        }
        qDebug() << "[LUT] Viewer LUT loaded:" << m_activeLutName << "dim=" << dim;
    } catch (const std::exception& e) {
        qWarning() << "[LUT] Failed to reload viewer LUT:" << e.what();
        m_viewerLutData.clear();
    }
}

QImage AppBackend::applyViewerLut(const QImage& image) const {
    if (!m_lutEnabled || m_viewerLutData.empty()) return image;
    if (m_rawViewMode == 0) return image;  // thumbnail mode — skip
    if (image.isNull()) return image;
    return applyLutToQImage(image, m_viewerLutData, m_viewerLutDim);
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
    
    // Build formatted HTML output for the UI
    std::ostringstream finalHtml;
    finalHtml << "<style>"
              << "th { text-align: left; padding-right: 15px; color: #888; font-weight: normal; }"
              << "td { color: #ddd; font-weight: bold; }"
              << "h3 { color: #fff; margin-bottom: 5px; border-bottom: 1px solid #444; padding-bottom: 5px; }"
              << "</style>";

    // Output EXIF data if found during previous steps
    if (foundData) {
        finalHtml << "<h3>Camera & File</h3><table>";
        // Simple parser for the data already collected in 'ss'
        QString rawExif = QString::fromStdString(ss.str());
        QStringList lines = rawExif.split('\n', Qt::SkipEmptyParts);
        for (const QString& line : lines) {
            int colonIdx = line.indexOf(':');
            if (colonIdx != -1) {
                finalHtml << "<tr><th>" << line.left(colonIdx).toStdString() << "</th>"
                          << "<td>" << line.mid(colonIdx + 1).trimmed().toStdString() << "</td></tr>";
            }
        }
        finalHtml << "</table><br>";
    }

    // Read metrics from CSV cache
    bool analysisFound = false;
    double laplacianScore = 0.0;
    double aiScore = 0.0;
    bool isBlurry = false;
    bool hasAiScore = false;

    if (csvFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&csvFile);
        QString targetPath = fileInfo.absoluteFilePath();

        // Skip header if it exists
        QString header = in.readLine(); 
        
        while (!in.atEnd()) {
            QString line = in.readLine();
            QStringList parts = line.split(',');
            
            // CSV format: filePath, isBlurry, laplacian_variance, ai_score
            if (parts.size() >= 3) {
                if (QFileInfo(parts[0]).absoluteFilePath() == targetPath || parts[0] == cleanPath) {
                    isBlurry = (parts[1] == "1" || parts[1].toLower() == "true");
                    laplacianScore = parts[2].toDouble();
                    if (parts.size() >= 4) {
                        aiScore = parts[3].toDouble();
                        hasAiScore = true;
                    }
                    analysisFound = true;
                    foundData = true;
                    break;
                }
            }
        }
    }

    // Recalculate AI score on the fly
    #ifdef Q_OS_WIN
        std::filesystem::path imgPath(cleanPath.toStdWString());
    #else
        std::filesystem::path imgPath(cleanPath.toUtf8().constData());
    #endif
    std::filesystem::path clipPath = imgPath.parent_path() / ".laplacian_cache" / (imgPath.filename().u8string());
    clipPath += ".clip"; 

    if (std::filesystem::exists(clipPath)) {
        std::vector<float> clipVector(512);
        std::ifstream clipFile(clipPath, std::ios::binary);
        
        if (clipFile.is_open()) {
            clipFile.read(reinterpret_cast<char*>(clipVector.data()), 512 * sizeof(float));
            clipFile.close();
            
#ifdef _WIN32
            std::filesystem::path exeDir(QCoreApplication::applicationDirPath().toStdWString());
#else
            std::filesystem::path exeDir(QCoreApplication::applicationDirPath().toUtf8().constData());
#endif
            std::filesystem::path appDataDir = get_app_config_dir();
            
            aiScore = AestheticProcessor::evaluateClipVector(exeDir, appDataDir, clipVector);
            
            hasAiScore = true;
            analysisFound = true;
            foundData = true;
        }
    }

    // Append pipeline metrics to HTML
    if (analysisFound) {
        finalHtml << "<h3>Pipeline Metrics</h3><table>";
        
        // Status indicator
        QString statusColor = isBlurry ? "#ff4444" : "#44ff44";
        QString statusText = isBlurry ? "BLURRY" : "SHARP";
        finalHtml << "<tr><th>Decision</th><td style='color: " << statusColor.toStdString() << "'>" 
                  << statusText.toStdString() << "</td></tr>";
        
        // Focus score (Laplacian)
        finalHtml << "<tr><th>Laplacian Focus</th><td>" 
                  << std::fixed << std::setprecision(2) << laplacianScore << "</td></tr>";
                  
        // Aesthetic score (AI)
         if (hasAiScore)  {
            finalHtml << "<tr><th>AI metric</th><td>" 
                      << std::fixed << std::setprecision(2) << aiScore << "</td></tr>";
        }
        
        finalHtml << "</table>";
    }

    // Finalize output
    if (!foundData) {
        result["infoText"] = "<p style='color: #888;'>No EXIF or Analysis data available</p>";
    } else {
        result["infoText"] = QString::fromStdString(finalHtml.str());
    }

    return result;
}

void AppBackend::loadRatings() {
    m_ratings.clear();
}

void AppBackend::saveRatings() {
}

int AppBackend::getPhotoRating(const QString& rawPath) {
    QString cleanPath = QUrl::fromPercentEncoding(rawPath.toUtf8());
#ifdef Q_OS_WIN
    if (cleanPath.startsWith("file:///")) {
        cleanPath = cleanPath.mid(8);
    } else if (cleanPath.startsWith("/")) {
        cleanPath = cleanPath.mid(1);
    } else if (cleanPath.startsWith("file://")) {
        cleanPath = cleanPath.mid(7);
    }
#endif

    // 1. Return instantly if we already loaded or set this rating in this session
    if (m_ratings.find(cleanPath) != m_ratings.end()) {
        return m_ratings[cleanPath];
    }

    // 2. Fast native XMP read on the UI thread (Do NOT spawn QProcess/exiftool here)
    int rating = 0;
    QString fileToRead = cleanPath;
    QFileInfo fi(cleanPath);
    QString ext = fi.suffix().toLower();
    QStringList rawFormats = {"cr2", "cr3", "nef", "arw", "dng", "raf", "orf", "rw2"};
    
    if (rawFormats.contains(ext)) {
        QString sidecarPath = cleanPath + ".xmp";
        if (QFileInfo::exists(sidecarPath)) {
            fileToRead = sidecarPath;
        }
    }

    #ifdef Q_OS_WIN
        std::ifstream file(fileToRead.toStdWString(), std::ios::binary);
    #else
        std::ifstream file(fileToRead.toUtf8().constData(), std::ios::binary);
    #endif
    if (file.is_open()) {
        const size_t bufferSize = 1024 * 1024; 
        std::string buffer;
        buffer.resize(bufferSize);
        file.read(&buffer[0], bufferSize);
        size_t bytesRead = file.gcount();
        buffer.resize(bytesRead);

        std::vector<std::pair<std::string, int>> patterns = {
            {"xmp:Rating>", 11},
            {"xmp:Rating=\"", 12},
            {"<Rating>", 8},
            {" Rating=\"", 9}
        };

        for (const auto& pat : patterns) {
            size_t pos = buffer.find(pat.first);
            if (pos != std::string::npos && pos + pat.second < buffer.size()) {
                char val = buffer[pos + pat.second];
                if (isdigit(val)) {
                    rating = val - '0';
                    break;
                }
            }
        }
    }

    // 3. Cache it in memory for the rest of the session
    m_ratings[cleanPath] = rating;

    return rating;
}

void AppBackend::setPhotoRating(const QString& rawPath, int rating, float baseScore) {
    if (rating < 0 || rating > 5) return; 

    QString cleanPath = QUrl::fromPercentEncoding(rawPath.toUtf8());
#ifdef Q_OS_WIN
    if (cleanPath.startsWith("file:///")) cleanPath = cleanPath.mid(8);
#else
    if (cleanPath.startsWith("file://")) cleanPath = cleanPath.mid(7);
#endif

    m_ratings[cleanPath] = rating;
    saveRatings();

    if (m_writeExif) {
        QString exiftoolPath = QCoreApplication::applicationDirPath() + "/exiftool";
#ifdef Q_OS_WIN
        exiftoolPath += ".exe";
#endif

        if (!QFileInfo::exists(exiftoolPath)) {
            qWarning() << "WARNING: exiftool NOT FOUND at" << exiftoolPath << ". Skipping XMP write, but continuing AI train.";
        } else {
            QString targetPath = cleanPath;
            QFileInfo fi(cleanPath);
            QString ext = fi.suffix().toLower();
            QStringList rawFormats = {"cr2", "cr3", "nef", "arw", "dng", "raf", "orf", "rw2", "pef", "srw"};
            
            if (rawFormats.contains(ext)) {
                targetPath = cleanPath + ".xmp";
            }

            QStringList args;
            args << "-xmp:Rating=" + QString::number(rating)
                 << "-overwrite_original"
                 << QDir::toNativeSeparators(targetPath);

            QProcess::startDetached(exiftoolPath, args);
            qDebug() << "DEBUG WRITE: ExifTool spawned for" << targetPath;
        }
    }

    if (rating == 0) return; 

    // Safe path construction for AI training
    QFileInfo fi(cleanPath);
    QString clipPath = fi.absolutePath() + "/.laplacian_cache/" + fi.fileName() + ".clip";

    if (!QFileInfo::exists(clipPath)) {
        qWarning() << "AI Train: .clip vector NOT FOUND at" << clipPath;
        return;
    }

    std::vector<float> clipVector(512);
    QFile clipFile(clipPath);
    if (clipFile.open(QIODevice::ReadOnly)) {
        clipFile.read(reinterpret_cast<char*>(clipVector.data()), 512 * sizeof(float));
        clipFile.close();
    } else {
        qWarning() << "AI Train: Failed to read .clip file!";
        return;
    }

#ifdef _WIN32
    std::filesystem::path modelsDir(QCoreApplication::applicationDirPath().toStdWString());
#else
    std::filesystem::path modelsDir(QCoreApplication::applicationDirPath().toUtf8().constData());
#endif
    std::filesystem::path appDataDir = get_app_config_dir();

    qDebug() << "AI Train Started for rating:" << rating << "with base score:" << baseScore;
    
    AestheticProcessor processor(modelsDir, appDataDir);
    processor.train(clipVector, baseScore, rating);
    
    qDebug() << "AI Train Success! Weights saved.";
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
    m_loadingSettings = true;

    std::vector<QString> loadedIds;
    std::vector<QString> loadedPreIds;
    bool pipelineLoaded    = false;
    bool preprocessorLoaded = false;

    QFile qf(getSettingsFilePath());
    if (qf.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&qf);
        while (!in.atEnd()) {
            QString qline = in.readLine();
            int eq = qline.indexOf('=');
            if (eq == -1) continue;
            QString key   = qline.left(eq);
            QString value = qline.mid(eq + 1);

            if (key == "themeMode") m_themeMode = value.toInt();
            else if (key == "writeExif") m_writeExif = (value == "1");
            else if (key == "cacheLaplacian") m_cacheLaplacian = (value == "1");
            else if (key == "rawViewMode") m_rawViewMode = value.toInt();
            else if (key == "rawAnalysisMode") m_rawAnalysisMode = value.toInt();
            else if (key == "groupBursts") m_groupBursts = (value == "1");
            else if (key == "lutEnabled") m_lutEnabled = (value == "1");
            else if (key == "lutPreset")  m_activeLutName = value.trimmed();
            else if (key == "pipeline") {
                m_pipelineModel.clear();
                const QStringList tokens = value.split(',');
                for (const QString& token : tokens) {
                    int colonIdx = token.indexOf(':');
                    if (colonIdx != -1) {
                        QString id      = token.left(colonIdx);
                        bool    enabled = (token.mid(colonIdx + 1) == "1");

                        QString name = id;
                        if (id == "exposure")    name = "Exposure Check";
                        if (id == "laplacian")   name = "Laplacian Focus Check";
                        if (id == "aiaesthetic") name = "AI Aesthetic Scorer";

                        m_pipelineModel.addStep(id, name, enabled);
                        loadedIds.push_back(id);
                    }
                }
                pipelineLoaded = true;
            }
            else if (key == "preprocessors") {
                m_preprocessorModel.clear();
                const QStringList tokens = value.split(',');
                for (const QString& token : tokens) {
                    int colonIdx = token.indexOf(':');
                    if (colonIdx != -1) {
                        QString id      = token.left(colonIdx);
                        bool    enabled = (token.mid(colonIdx + 1) == "1");

                        QString name = id;
                        bool    canDisable = true;
                        if (id == "visual_hash") { name = "Burst Grouping (Visual Hash)"; canDisable = true; }
                        if (id == "lut_3d")      { name = "Color LUT (3D)";               canDisable = true; }

                        m_preprocessorModel.addStep(id, name, enabled, canDisable);
                        loadedPreIds.push_back(id);
                    }
                }
                preprocessorLoaded = true;
            }
        }
        qf.close();
    }

    // If config didn't exist at all, clear and load defaults
    if (!pipelineLoaded) {
        m_pipelineModel.clear();
        m_pipelineModel.addStep("exposure", "Exposure Check", true);
        m_pipelineModel.addStep("laplacian", "Laplacian Focus Check", true);
        m_pipelineModel.addStep("aiaesthetic", "AI Aesthetic Scorer", true);
    } else {
        // MERGE: If the config loaded, but is missing new tools, append them at the end.
        if (std::find(loadedIds.begin(), loadedIds.end(), "exposure") == loadedIds.end()) {
            m_pipelineModel.addStep("exposure", "Exposure Check", true);
        }
        if (std::find(loadedIds.begin(), loadedIds.end(), "laplacian") == loadedIds.end()) {
            m_pipelineModel.addStep("laplacian", "Laplacian Focus Check", true);
        }
        if (std::find(loadedIds.begin(), loadedIds.end(), "aiaesthetic") == loadedIds.end()) {
            m_pipelineModel.addStep("aiaesthetic", "AI Aesthetic Scorer", true);
        }
    }

    if (!preprocessorLoaded) {
        m_preprocessorModel.clear();
        m_preprocessorModel.addStep("visual_hash", "Burst Grouping (Visual Hash)", true,  true);
        m_preprocessorModel.addStep("lut_3d",      "Color LUT (3D)",               false, true);
    } else {
        // MERGE: add any missing preprocessors
        if (std::find(loadedPreIds.begin(), loadedPreIds.end(), "visual_hash") == loadedPreIds.end())
            m_preprocessorModel.addStep("visual_hash", "Burst Grouping (Visual Hash)", true,  true);
        if (std::find(loadedPreIds.begin(), loadedPreIds.end(), "lut_3d") == loadedPreIds.end())
            m_preprocessorModel.addStep("lut_3d", "Color LUT (3D)", false, true);
    }

    // Keep lut_3d model row in sync with m_lutEnabled
    for (int i = 0; i < static_cast<int>(m_preprocessorModel.getSteps().size()); ++i) {
        if (m_preprocessorModel.getSteps()[i].id == "lut_3d")
            m_preprocessorModel.setStepEnabled(i, m_lutEnabled);
    }

    m_loadingSettings = false;
}

void AppBackend::saveSettings() {
    if (m_loadingSettings) return;
    QFile qf(getSettingsFilePath());
    if (!qf.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return;

    QTextStream out(&qf);
    out << "themeMode="      << m_themeMode                  << "\n";
    out << "writeExif="      << (m_writeExif ? 1 : 0)        << "\n";
    out << "cacheLaplacian=" << (m_cacheLaplacian ? 1 : 0)   << "\n";
    out << "rawViewMode="    << m_rawViewMode                 << "\n";
    out << "rawAnalysisMode="<< m_rawAnalysisMode             << "\n";
    out << "groupBursts="    << (m_groupBursts ? 1 : 0)      << "\n";
    out << "lutEnabled="     << (m_lutEnabled ? 1 : 0)       << "\n";
    out << "lutPreset="      << m_activeLutName               << "\n";

    out << "pipeline=";
    const auto& steps = m_pipelineModel.getSteps();
    for (size_t i = 0; i < steps.size(); ++i) {
        out << steps[i].id << ":" << (steps[i].enabled ? 1 : 0);
        if (i < steps.size() - 1) out << ",";
    }
    out << "\n";

    out << "preprocessors=";
    const auto& preSteps = m_preprocessorModel.getSteps();
    for (size_t i = 0; i < preSteps.size(); ++i) {
        out << preSteps[i].id << ":" << (preSteps[i].enabled ? 1 : 0);
        if (i < preSteps.size() - 1) out << ",";
    }
    out << "\n";

    qf.close();
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

    QString cleanPath = QUrl::fromPercentEncoding(folderPath.toUtf8());
#ifdef Q_OS_WIN
    if (cleanPath.startsWith("file:///")) {
        cleanPath = cleanPath.mid(8);
    }
#else
    if (cleanPath.startsWith("file://")) {
        cleanPath = cleanPath.mid(7);
    }
#endif

    m_currentFolder = cleanPath;
    setStatusText("Selected " + m_currentFolder);

    // 1. FAST SCAN DIRECTORY IMMEDIATELY
    m_files = Scanner::scanFiles(m_currentFolder);
    m_hashes.assign(m_files.size(), 0);
    setTotalFiles(static_cast<int>(m_files.size()));
    setProgress(0);

    // 2. EMIT TO QML INSTANTLY
    for (size_t i = 0; i < m_files.size(); ++i) {
        QFileInfo fi(m_files[i]);
        emit fileFound(fi.fileName(), m_files[i], static_cast<int>(i));
    }
}

void AppBackend::startScan() {
    if (m_files.empty() || m_isScanning) return;

    m_isScanning = true;
    m_cancelRequested = false;
    setProgress(0);
    setAcceptedCount(0);
    setRejectedCount(0);
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
            PipelineRunner runner = this->createPipeline();
            AppSettings settings; 

                    while (!m_cancelRequested) {
            size_t idx = fileIndex.fetch_add(1, std::memory_order_relaxed);
            if (idx >= m_files.size()) break;

            const QString qFile = m_files[idx];

            ProcessingContext ctx;
            ctx.settings = settings;

#ifdef _WIN32
            ctx.filePath = std::filesystem::path(qFile.toStdWString());
#else
            ctx.filePath = std::filesystem::path(qFile.toUtf8().constData());
#endif
            ctx.rawFilePath = qFile;
            ctx.cacheDir = ctx.filePath.parent_path() / ".laplacian_cache";

            ProcessingResult result = runner.run(ctx);

            if (result.success) {
                int w = 0, h = 0;
                ImageIO::readOriginalSize(ctx.rawFilePath, w, h);
                    float aestheticScore = 0.0f;
                    uint64_t currentHash = 0;

                    for (const auto& metric : result.metrics) {
                        QString key = QString::fromStdString(metric.key).toLower();
                        if (key == "aesthetic_score" || key == "ai_score" || key == "score") {
                            if (std::holds_alternative<double>(metric.value)) aestheticScore = static_cast<float>(std::get<double>(metric.value));
                            else if (std::holds_alternative<int>(metric.value)) aestheticScore = static_cast<float>(std::get<int>(metric.value));
                        }
                        if (key == "visual_hash" && std::holds_alternative<std::string>(metric.value)) {
                        std::string hashStr = std::get<std::string>(metric.value);
                        try {
                            currentHash = std::stoull(hashStr, nullptr, 16);
                            //qDebug() << "Extracted hash:" << QString::fromStdString(hashStr) << "for" << QString::fromStdString(file);
                        } catch (...) {
                            currentHash = 0;
                        }
                    }
                }

                // Store hash in the thread-safe array
                m_hashes[idx] = currentHash;
                
                    // For Laplacian backwards compatibility, if it's blurry, mark as rejected
                    if (result.isBlurry && !result.rejected) {
                        result.rejected = true;
                        result.rejectReason = "Blurry";
                    }

                    emit fileProcessed(static_cast<int>(idx), result.rejected, QString::fromStdString(result.rejectReason), aestheticScore, w, h);

                    bool wasRejected = result.rejected;
                    QMetaObject::invokeMethod(this, [this, wasRejected]() {
                        if (wasRejected) setRejectedCount(m_rejectedCount + 1);
                        else             setAcceptedCount(m_acceptedCount + 1);
                    }, Qt::QueuedConnection);
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

    int lastLead = 0;
    std::vector<int> groupLead(m_files.size(), 0);
    std::vector<int> groupSize(m_files.size(), 1);

    for (size_t i = 0; i < m_files.size(); ++i) {
        if (i == 0 || m_hashes[i] == 0 || m_hashes[lastLead] == 0) {
            lastLead = static_cast<int>(i);
            groupLead[i] = static_cast<int>(i);
        } else {
            // Compute Hamming Distance (number of differing bits)
            uint64_t xor_val = m_hashes[i] ^ m_hashes[lastLead];
            int dist = 0;
            while (xor_val) { dist += xor_val & 1; xor_val >>= 1; }

            // Threshold: 10 bits difference means ~15% visual change. 
            // This groups highly similar bursts together.
            if (dist <= 20) {
                groupLead[i] = lastLead;
                groupSize[lastLead]++;
                qDebug() << "Grouped photo" << i << "with lead" << lastLead << "Dist:" << dist;
            } else {
                lastLead = static_cast<int>(i);
                groupLead[i] = static_cast<int>(i);
                qDebug() << "New group lead at" << i << "Dist was:" << dist;
            }
        }
    }

    // Emit assignments back to QML safely
    for (size_t i = 0; i < m_files.size(); ++i) {
        bool isLead = (groupLead[i] == static_cast<int>(i));
        emit groupAssigned(static_cast<int>(i), groupLead[i], isLead, groupSize[groupLead[i]]);
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

int AppBackend::acceptedCount() const { return m_acceptedCount; }
void AppBackend::setAcceptedCount(int value) {
    if (m_acceptedCount != value) { m_acceptedCount = value; emit acceptedCountChanged(); }
}

int AppBackend::rejectedCount() const { return m_rejectedCount; }
void AppBackend::setRejectedCount(int value) {
    if (m_rejectedCount != value) { m_rejectedCount = value; emit rejectedCountChanged(); }
}

void AppBackend::setGroupBursts(bool group) {
    if (m_groupBursts != group) {
        m_groupBursts = group;
        saveSettings();
        emit groupBurstsChanged();
    }
}