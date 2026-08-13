#pragma once

#include <QQuickImageProvider>
#include <QImage>
#include <QImageReader>
#include <QBuffer>
#include <QTransform>
#include <QString>
#include <QUrl>
#include <QFileInfo>
#include <QSet>
#include <QDebug>
#include <QMetaObject>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <thread>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <algorithm>
#include "../app_backend.h"
#include <libraw/libraw.h>
#include "../tools/raw_mutex.h"
#include "thumbnail_provider.h"

class FullImageProvider : public QQuickImageProvider {
    AppBackend* m_backend;

    struct CacheEntry {
        QImage previewImage; // Half size (mode 1) or thumbnail (mode 0)
        QImage fullImage;    // Full resolution decode (mode 2)
    };

    struct PreloadTask {
        QString path;
        int mode; // 0 = thumb, 1 = half-size, 2 = full-size
    };

    static constexpr size_t MAX_CACHE_CAPACITY = 35; // 35-image maximum RAM ceiling

    mutable std::mutex m_cacheMutex;
    std::unordered_map<QString, CacheEntry> m_cache;
    std::deque<QString> m_lruKeys;

    std::vector<std::thread> m_workers;
    std::mutex m_queueMutex;
    std::condition_variable m_cv;
    std::deque<PreloadTask> m_queue;
    std::atomic<bool> m_stopWorkers{false};

    static QString cleanPathKey(const QString& rawPath) {
        if (rawPath.isEmpty()) return QString();
        QString p = QUrl::fromPercentEncoding(rawPath.toUtf8());
#ifdef Q_OS_WIN
        if      (p.startsWith("file:///")) p = p.mid(8);
        else if (p.startsWith("file://"))  p = p.mid(7);
        else if (p.startsWith("/"))        p = p.mid(1);
#else
        if (p.startsWith("file://")) p = p.mid(7);
#endif
        return QDir::cleanPath(p);
    }

    static bool isRawExtension(const QString& ext) {
        static const QSet<QString> rawExts = {
            "3fr", "arw", "cr2", "cr3", "crw", "dcr", "dng", "erf", "fff",
            "gpr", "kdc", "mef", "mos", "nef", "nrw", "orf", "pef", "ptx",
            "raf", "raw", "rw2", "sr2", "srf", "srw", "x3f"
        };
        return rawExts.contains(ext);
    }

    void putInCache(const QString& rawPath, const QImage& img, int mode) {
        QString path = cleanPathKey(rawPath);
        if (path.isEmpty() || img.isNull()) return;

        std::lock_guard<std::mutex> lock(m_cacheMutex);
        
        auto it = m_cache.find(path);
        if (it != m_cache.end()) {
            if (mode == 2) {
                it->second.fullImage = img;
            } else {
                it->second.previewImage = img;
            }
            auto lruIt = std::find(m_lruKeys.begin(), m_lruKeys.end(), path);
            if (lruIt != m_lruKeys.end()) m_lruKeys.erase(lruIt);
            m_lruKeys.push_back(path);
            return;
        }

        while (m_cache.size() >= MAX_CACHE_CAPACITY && !m_lruKeys.empty()) {
            QString oldest = m_lruKeys.front();
            m_lruKeys.pop_front();
            m_cache.erase(oldest);
        }

        CacheEntry entry;
        if (mode == 2) {
            entry.fullImage = img;
        } else {
            entry.previewImage = img;
        }
        m_cache[path] = entry;
        m_lruKeys.push_back(path);
    }

public:
    explicit FullImageProvider(AppBackend* backend)
        : QQuickImageProvider(QQuickImageProvider::Image), m_backend(backend) {
        startWorkerPool();
    }

    ~FullImageProvider() override {
        stopWorkerPool();
    }

    void clearCache() {
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            m_queue.clear();
        }
        {
            std::lock_guard<std::mutex> lock(m_cacheMutex);
            m_cache.clear();
            m_lruKeys.clear();
        }
    }

    bool isFullImageLoaded(const QString& rawPath) const {
        QString path = cleanPathKey(rawPath);
        if (path.isEmpty()) return false;
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        auto it = m_cache.find(path);
        if (it != m_cache.end()) {
            return !it->second.fullImage.isNull();
        }
        return false;
    }

    void prioritizeFullImage(const QString& rawPath) {
        QString filePath = cleanPathKey(rawPath);
        if (filePath.isEmpty()) return;

        {
            std::lock_guard<std::mutex> clock(m_cacheMutex);
            auto it = m_cache.find(filePath);
            if (it != m_cache.end() && !it->second.fullImage.isNull()) {
                if (m_backend) {
                    AppBackend* b = m_backend;
                    QMetaObject::invokeMethod(b, [b, filePath]() {
                        emit b->fullImageLoaded(filePath);
                    }, Qt::QueuedConnection);
                }
                return;
            }
        }

        {
            std::lock_guard<std::mutex> qlock(m_queueMutex);
            for (auto it = m_queue.begin(); it != m_queue.end(); ) {
                if (it->path == filePath && it->mode == 2) {
                    it = m_queue.erase(it);
                } else {
                    ++it;
                }
            }
            m_queue.push_front({ filePath, 2 });
        }
        m_cv.notify_one();
    }

    void preloadSingleImage(const QString& rawPath) {
        QString filePath = cleanPathKey(rawPath);
        if (filePath.isEmpty()) return;

        int viewMode = m_backend ? m_backend->rawViewMode() : 0;
        int targetMode = (viewMode == 0 ? 0 : 1);

        {
            std::lock_guard<std::mutex> lock(m_cacheMutex);
            auto it = m_cache.find(filePath);
            if (it != m_cache.end() && !it->second.previewImage.isNull()) {
                return;
            }
        }

        {
            std::lock_guard<std::mutex> qlock(m_queueMutex);
            for (const auto& item : m_queue) {
                if (item.path == filePath && item.mode == targetMode) return;
            }
            if (m_queue.size() >= 35) {
                m_queue.pop_front();
            }
            m_queue.push_back({ filePath, targetMode });
        }
        m_cv.notify_one();
    }

    // Thread-safe: preloads half-sized images for window and full-sized image for current photo
    void updatePreloadWindow(int currentIndex, const std::vector<QString>& priorityPaths) {
        Q_UNUSED(currentIndex)
        int viewMode = m_backend ? m_backend->rawViewMode() : 0;

        QSet<QString> validWindowPaths;
        std::vector<QString> cleanedPriority;
        cleanedPriority.reserve(priorityPaths.size());

        for (const QString& path : priorityPaths) {
            QString clean = cleanPathKey(path);
            if (!clean.isEmpty()) {
                validWindowPaths.insert(clean);
                cleanedPriority.push_back(clean);
            }
        }

        // Evict items from cache outside window
        {
            std::lock_guard<std::mutex> lock(m_cacheMutex);
            for (auto it = m_cache.begin(); it != m_cache.end(); ) {
                if (!validWindowPaths.contains(it->first)) {
                    auto lruIt = std::find(m_lruKeys.begin(), m_lruKeys.end(), it->first);
                    if (lruIt != m_lruKeys.end()) m_lruKeys.erase(lruIt);
                    it = m_cache.erase(it);
                } else {
                    ++it;
                }
            }
        }

        // Enqueue prioritized tasks into background worker queue
        {
            std::lock_guard<std::mutex> qlock(m_queueMutex);
            m_queue.clear();

            if (!cleanedPriority.empty()) {
                const QString& currentPath = cleanedPriority.front();
                bool needFull = (viewMode == 2);
                bool needHalf = true;

                {
                    std::lock_guard<std::mutex> clock(m_cacheMutex);
                    auto it = m_cache.find(currentPath);
                    if (it != m_cache.end()) {
                        if (!it->second.previewImage.isNull()) needHalf = false;
                        if (!it->second.fullImage.isNull()) needFull = false;
                    }
                }

                // 1. Current photo preview (half size) first if missing (< 30ms)
                if (needHalf) {
                    m_queue.push_back({ currentPath, (viewMode == 0 ? 0 : 1) });
                }

                // 2. Current photo full resolution with top priority if in full size mode
                if (needFull) {
                    m_queue.push_back({ currentPath, 2 });
                }

                // 3. Preload half-size previews for all remaining images in window
                int previewMode = (viewMode == 0 ? 0 : 1);
                for (size_t i = 1; i < cleanedPriority.size(); ++i) {
                    const QString& p = cleanedPriority[i];
                    std::lock_guard<std::mutex> clock(m_cacheMutex);
                    auto it = m_cache.find(p);
                    if (it == m_cache.end() || it->second.previewImage.isNull()) {
                        m_queue.push_back({ p, previewMode });
                    }
                }
            }
        }
        m_cv.notify_all();
    }

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override {
        Q_UNUSED(requestedSize)
        QString rawId = id;
        bool forceFull = false;
        if (rawId.contains("?full=") || rawId.contains("&full=")) {
            forceFull = true;
            int qIdx = rawId.indexOf('?');
            if (qIdx != -1) rawId = rawId.left(qIdx);
        }

        QString filePath = cleanPathKey(rawId);

        QFileInfo fileInfo(filePath);
        if (!fileInfo.exists()) {
            return QImage();
        }

        const QString suffix = fileInfo.suffix().toLower();
        int viewMode = m_backend ? m_backend->rawViewMode() : 0;

        // 1. Cache lookup
        {
            std::lock_guard<std::mutex> lock(m_cacheMutex);
            auto it = m_cache.find(filePath);
            if (it != m_cache.end()) {
                QImage img;
                if (forceFull) {
                    img = !it->second.fullImage.isNull() ? it->second.fullImage : it->second.previewImage;
                } else {
                    img = !it->second.fullImage.isNull() ? it->second.fullImage : it->second.previewImage;
                }

                if (!img.isNull()) {
                    auto lruIt = std::find(m_lruKeys.begin(), m_lruKeys.end(), filePath);
                    if (lruIt != m_lruKeys.end()) m_lruKeys.erase(lruIt);
                    m_lruKeys.push_back(filePath);

                    if (size) *size = img.size();
                    if (m_backend) {
                        AppBackend* b = m_backend;
                        QMetaObject::invokeMethod(b, [b, img]() {
                            b->updateHistogramFromImage(img);
                        }, Qt::QueuedConnection);
                    }
                    return img;
                }
            }
        }

        // 2. Direct decode fallback if not in cache
        QImage image;
        if (isRawExtension(suffix)) {
            if (viewMode == 2 || forceFull) {
                // Return fast preview (< 30ms) immediately to keep UI fluid
                image = loadRaw(filePath, 1);
                if (!image.isNull()) {
                    if (image.format() != QImage::Format_RGB32 && image.format() != QImage::Format_ARGB32) {
                        image = image.convertToFormat(QImage::Format_RGB32);
                    }
                    if (m_backend) image = m_backend->applyViewerLut(image);
                    putInCache(filePath, image, 1);
                    // Ensure full resolution decode is queued in background
                    prioritizeFullImage(filePath);
                }
            } else {
                image = loadImageDirect(filePath, viewMode);
                if (!image.isNull()) {
                    putInCache(filePath, image, viewMode);
                }
            }
        } else {
            image = loadImageDirect(filePath, viewMode);
            if (!image.isNull()) {
                putInCache(filePath, image, 2);
            }
        }

        if (image.isNull()) {
            return QImage();
        }

        if (size) *size = image.size();

        if (m_backend) {
            AppBackend* b = m_backend;
            QMetaObject::invokeMethod(b, [b, image]() {
                b->updateHistogramFromImage(image);
            }, Qt::QueuedConnection);
        }

        return image;
    }

private:
    QImage loadImageDirect(const QString& filePath, int mode) {
        QFileInfo fileInfo(filePath);
        if (!fileInfo.exists()) {
            qDebug() << "[FullImageProvider] ERROR: File DOES NOT EXIST at path:" << filePath;
            return QImage();
        }

        const QString suffix = fileInfo.suffix().toLower();
        QImage image;

        if (isRawExtension(suffix)) {
            qDebug() << "[FullImageProvider] Loading RAW with mode:" << mode;
            image = loadRaw(filePath, mode);
        } else {
            qDebug() << "[FullImageProvider] Loading standard image with QImageReader...";
            QImageReader reader(filePath);
            reader.setAutoTransform(true);
            image = reader.read();
            if (image.isNull()) {
                qDebug() << "[FullImageProvider] ERROR: QImageReader failed:" << reader.errorString();
            }
        }

        if (image.isNull()) {
            return QImage();
        }

        if (image.format() != QImage::Format_RGB32 && image.format() != QImage::Format_ARGB32) {
            qDebug() << "[FullImageProvider] Converting format to RGB32 for QML rendering...";
            image = image.convertToFormat(QImage::Format_RGB32);
        }

        bool isRawThumbnail = isRawExtension(suffix) && (mode == 0);
        if (!isRawThumbnail && m_backend) {
            image = m_backend->applyViewerLut(image);
        }
        return image;
    }

    QImage loadRaw(const QString& filePath, int mode) {
        libraw_processed_image_t* image = nullptr;
        int flip = 0;
        int ret = LIBRAW_SUCCESS;
        LibRaw lr;
        lr.imgdata.params.use_camera_wb = 1;
        lr.imgdata.params.half_size = (mode == 1 || mode == 0) ? 1 : 0;
        lr.imgdata.params.output_bps = 8;

        {
            std::lock_guard<std::mutex> rawLock(getLibRawMutex());

#if defined(_WIN32)
            ret = lr.open_file(filePath.toStdWString().c_str());
            if (ret != LIBRAW_SUCCESS) {
                ret = lr.open_file(filePath.toLocal8Bit().constData());
            }
#else
            ret = lr.open_file(filePath.toUtf8().constData());
#endif
            if (ret != LIBRAW_SUCCESS) {
                return QImage();
            }

            if (mode == 0) {
                if (lr.unpack_thumb() == LIBRAW_SUCCESS) {
                    if (lr.imgdata.thumbnail.tformat == LIBRAW_THUMBNAIL_JPEG) {
                        QByteArray thumbData(
                            reinterpret_cast<const char*>(lr.imgdata.thumbnail.thumb),
                            lr.imgdata.thumbnail.tlength
                        );
                        QBuffer buffer(&thumbData);
                        buffer.open(QIODevice::ReadOnly);
                        QImageReader reader(&buffer);
                        reader.setAutoTransform(true);
                        QImage thumb = reader.read();

                        flip = lr.imgdata.sizes.flip;
                        lr.recycle();

                        if (!thumb.isNull() && flip != 0) {
                            if ((flip == 5 || flip == 6 || flip == 8) && thumb.width() >= thumb.height()) {
                                QTransform t;
                                if (flip == 6) t.rotate(90);
                                else if (flip == 5 || flip == 8) t.rotate(270);
                                thumb = thumb.transformed(t, Qt::SmoothTransformation);
                            } else if (flip == 3 && thumb.width() >= thumb.height()) {
                                QTransform t;
                                t.rotate(180);
                                thumb = thumb.transformed(t, Qt::SmoothTransformation);
                            }
                        }
                        return thumb;
                    }
                }
            }

            if (lr.unpack() != LIBRAW_SUCCESS) {
                lr.recycle();
                return QImage();
            }
        } // Unlocked here so dcraw_process runs 100% in parallel across worker threads

        if (lr.dcraw_process() != LIBRAW_SUCCESS) {
            lr.recycle();
            return QImage();
        }

        image = lr.dcraw_make_mem_image(&ret);
        flip = lr.imgdata.sizes.flip;
        lr.recycle();

        if (!image) {
            return QImage();
        }

        if (image->type != LIBRAW_IMAGE_BITMAP || image->colors < 3) {
            LibRaw::dcraw_clear_mem(image);
            return QImage();
        }

        QImage result(image->width, image->height, QImage::Format_RGB888);
        std::memcpy(result.bits(), image->data, image->data_size);

        LibRaw::dcraw_clear_mem(image);

        if (!result.isNull() && flip != 0) {
            if ((flip == 5 || flip == 6 || flip == 8) && result.width() >= result.height()) {
                QTransform t;
                if (flip == 6) t.rotate(90);
                else if (flip == 5 || flip == 8) t.rotate(270);
                result = result.transformed(t, Qt::SmoothTransformation);
            } else if (flip == 3 && result.width() >= result.height()) {
                QTransform t;
                t.rotate(180);
                result = result.transformed(t, Qt::SmoothTransformation);
            }
        }

        return result;
    }

    void startWorkerPool() {
        unsigned int threads = (std::max)(4u, std::thread::hardware_concurrency() / 2); // Dynamic parallel preloading worker pool
        m_stopWorkers = false;
        for (unsigned int i = 0; i < threads; ++i) {
            m_workers.emplace_back([this]() {
                while (!m_stopWorkers) {
                    PreloadTask task;
                    {
                        std::unique_lock<std::mutex> lock(m_queueMutex);
                        m_cv.wait_for(lock, std::chrono::milliseconds(50), [this]() {
                            return m_stopWorkers || !m_queue.empty();
                        });
                        if (m_stopWorkers) return;
                        if (m_queue.empty()) continue;
                        task = m_queue.front();
                        m_queue.pop_front();
                    }

                    if (task.path.isEmpty()) continue;

                    {
                        std::lock_guard<std::mutex> lock(m_cacheMutex);
                        auto it = m_cache.find(task.path);
                        if (it != m_cache.end()) {
                            if (task.mode == 2 && !it->second.fullImage.isNull()) continue;
                            if (task.mode != 2 && !it->second.previewImage.isNull()) continue;
                        }
                    }

                    QImage img = loadImageDirect(task.path, task.mode);
                    if (!img.isNull()) {
                        putInCache(task.path, img, task.mode);
                        if (task.mode == 2 && m_backend) {
                            AppBackend* b = m_backend;
                            QString p = task.path;
                            QMetaObject::invokeMethod(b, [b, p]() {
                                emit b->fullImageLoaded(p);
                            }, Qt::QueuedConnection);
                        }
                    }
                }
            });
        }
    }

    void stopWorkerPool() {
        m_stopWorkers = true;
        m_cv.notify_all();
        for (auto& t : m_workers) {
            if (t.joinable()) t.join();
        }
        m_workers.clear();
    }
};