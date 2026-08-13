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
        QImage image;
        int mode;
    };

    static constexpr size_t MAX_CACHE_CAPACITY = 25; // Strict 25-image maximum RAM ceiling

    mutable std::mutex m_cacheMutex;
    std::unordered_map<QString, CacheEntry> m_cache;
    std::deque<QString> m_lruKeys;

    std::vector<std::thread> m_workers;
    std::mutex m_queueMutex;
    std::condition_variable m_cv;
    std::deque<QString> m_queue;
    std::atomic<bool> m_stopWorkers{false};

    static bool isRawExtension(const QString& ext) {
        static const QSet<QString> rawExts = {
            "3fr", "arw", "cr2", "cr3", "crw", "dcr", "dng", "erf", "fff",
            "gpr", "kdc", "mef", "mos", "nef", "nrw", "orf", "pef", "ptx",
            "raf", "raw", "rw2", "sr2", "srf", "srw", "x3f"
        };
        return rawExts.contains(ext);
    }

    void putInCache(const QString& path, const QImage& img, int mode) {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        
        auto it = m_cache.find(path);
        if (it != m_cache.end()) {
            it->second = { img, mode };
            auto lruIt = std::find(m_lruKeys.begin(), m_lruKeys.end(), path);
            if (lruIt != m_lruKeys.end()) m_lruKeys.erase(lruIt);
            m_lruKeys.push_back(path);
            return;
        }

        // Strict LRU eviction: keep m_cache.size() strictly under MAX_CACHE_CAPACITY
        while (m_cache.size() >= MAX_CACHE_CAPACITY && !m_lruKeys.empty()) {
            QString oldest = m_lruKeys.front();
            m_lruKeys.pop_front();
            m_cache.erase(oldest);
        }

        m_cache[path] = { img, mode };
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

    void preloadSingleImage(const QString& rawPath) {
        if (rawPath.isEmpty()) return;
        QString filePath = QUrl::fromPercentEncoding(rawPath.toUtf8());
#ifdef Q_OS_WIN
        if (filePath.startsWith("file:///")) {
            filePath = filePath.mid(8);
        } else if (filePath.startsWith('/')) {
            filePath.remove(0, 1);
        }
#endif
        int mode = m_backend ? m_backend->rawViewMode() : 0;

        {
            std::lock_guard<std::mutex> lock(m_cacheMutex);
            auto it = m_cache.find(filePath);
            if (it != m_cache.end() && it->second.mode == mode) {
                return;
            }
        }

        {
            std::lock_guard<std::mutex> qlock(m_queueMutex);
            for (const QString& item : m_queue) {
                if (item == filePath) return;
            }
            // Cap worker queue length to 25 items
            if (m_queue.size() >= 25) {
                m_queue.pop_front();
            }
            m_queue.push_back(filePath);
        }
        m_cv.notify_one();
    }

    // Thread-safe: preloads 10 images behind current index and 20 ahead using main-thread snapshot
    void updatePreloadWindow(int currentIndex, const std::vector<QString>& priorityPaths) {
        Q_UNUSED(currentIndex)
        int mode = m_backend ? m_backend->rawViewMode() : 0;

        QSet<QString> validWindowPaths;
        for (const QString& path : priorityPaths) {
            validWindowPaths.insert(path);
        }

        // Evict items from cache outside window or with mismatched mode
        {
            std::lock_guard<std::mutex> lock(m_cacheMutex);
            for (auto it = m_cache.begin(); it != m_cache.end(); ) {
                if (!validWindowPaths.contains(it->first) || it->second.mode != mode) {
                    auto lruIt = std::find(m_lruKeys.begin(), m_lruKeys.end(), it->first);
                    if (lruIt != m_lruKeys.end()) m_lruKeys.erase(lruIt);
                    it = m_cache.erase(it);
                } else {
                    ++it;
                }
            }
        }

        // Enqueue missing prioritized items into background worker queue
        {
            std::lock_guard<std::mutex> qlock(m_queueMutex);
            m_queue.clear();
            for (const QString& path : priorityPaths) {
                std::lock_guard<std::mutex> clock(m_cacheMutex);
                auto it = m_cache.find(path);
                if (it == m_cache.end() || it->second.mode != mode) {
                    m_queue.push_back(path);
                }
            }
        }
        m_cv.notify_all();
    }

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override {
        Q_UNUSED(requestedSize)
        qDebug() << "======================================";
        qDebug() << "[FullImageProvider] Request received! ID:" << id;

        QString filePath = QUrl::fromPercentEncoding(id.toUtf8());
#ifdef Q_OS_WIN
        if (filePath.startsWith("file:///")) {
            filePath = filePath.mid(8);
        } else if (filePath.startsWith('/')) {
            filePath.remove(0, 1);
        }
#endif

        qDebug() << "[FullImageProvider] Cleaned file path:" << filePath;
        
        QFileInfo fileInfo(filePath);
        if (!fileInfo.exists()) {
            qDebug() << "[FullImageProvider] ERROR: File DOES NOT EXIST at path:" << filePath;
            return QImage();
        }

        const QString suffix = fileInfo.suffix().toLower();
        qDebug() << "[FullImageProvider] Extension:" << suffix << " | isRaw:" << isRawExtension(suffix);

        int mode = m_backend ? m_backend->rawViewMode() : 0;

        // 1. Instant cache lookup
        {
            std::lock_guard<std::mutex> lock(m_cacheMutex);
            auto it = m_cache.find(filePath);
            if (it != m_cache.end() && it->second.mode == mode) {
                QImage img = it->second.image;
                // Touch LRU order
                auto lruIt = std::find(m_lruKeys.begin(), m_lruKeys.end(), filePath);
                if (lruIt != m_lruKeys.end()) m_lruKeys.erase(lruIt);
                m_lruKeys.push_back(filePath);

                qDebug() << "[FullImageProvider] INSTANT CACHE HIT! Returning cached QImage. Size:" << img.width() << "x" << img.height();
                if (size) *size = img.size();
                qDebug() << "======================================";
                if (m_backend) {
                    AppBackend* b = m_backend;
                    QMetaObject::invokeMethod(b, [b, img]() {
                        b->updateHistogramFromImage(img);
                    }, Qt::QueuedConnection);
                }
                return img;
            }
        }

        // 2. Direct decode fallback if not in cache
        qDebug() << "[FullImageProvider] Cache miss! Decoding synchronously...";
        QImage image = loadImageDirect(filePath, mode);
        if (image.isNull()) {
            qDebug() << "[FullImageProvider] ERROR: Final image is NULL. Returning empty QImage.";
            return QImage();
        }

        qDebug() << "[FullImageProvider] Success! Image size:" << image.width() << "x" << image.height() << "Format:" << image.format();

        if (size) *size = image.size();
        qDebug() << "======================================";

        if (m_backend) {
            AppBackend* b = m_backend;
            QMetaObject::invokeMethod(b, [b, image]() {
                b->updateHistogramFromImage(image);
            }, Qt::QueuedConnection);
        }

        putInCache(filePath, image, mode);
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
        std::lock_guard<std::mutex> rawLock(getLibRawMutex());
        LibRaw lr;
        lr.imgdata.params.use_camera_wb = 1;

        int ret = LIBRAW_SUCCESS;
#if defined(_WIN32)
        qDebug() << "[LibRaw] Trying to open via wstring_convert...";
        ret = lr.open_file(filePath.toStdWString().c_str());
        if (ret != LIBRAW_SUCCESS) {
            qDebug() << "[LibRaw] wstring failed. Trying local8bit...";
            ret = lr.open_file(filePath.toLocal8Bit().constData());
        }
#else
        qDebug() << "[LibRaw] Opening utf8...";
        ret = lr.open_file(filePath.toUtf8().constData());
#endif
        if (ret != LIBRAW_SUCCESS) {
            qDebug() << "[LibRaw] ERROR: open_file failed with code:" << ret;
            return QImage();
        }
        qDebug() << "[LibRaw] open_file success.";

        if (mode == 0) {
            qDebug() << "[LibRaw] Unpacking thumbnail...";
            if (lr.unpack_thumb() == LIBRAW_SUCCESS) {
                QImage thumb;
                if (lr.imgdata.thumbnail.tformat == LIBRAW_THUMBNAIL_JPEG) {
                    qDebug() << "[LibRaw] Extracted JPEG thumbnail successfully.";
                    QByteArray thumbData(
                        reinterpret_cast<const char*>(lr.imgdata.thumbnail.thumb),
                        lr.imgdata.thumbnail.tlength
                    );
                    QBuffer buffer(&thumbData);
                    buffer.open(QIODevice::ReadOnly);
                    QImageReader reader(&buffer);
                    reader.setAutoTransform(true);
                    thumb = reader.read();

                    int flip = lr.imgdata.sizes.flip;
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

                    lr.recycle();
                    return thumb;
                } else {
                    qDebug() << "[LibRaw] ERROR: Thumbnail format is not JPEG.";
                }
            } else {
                qDebug() << "[LibRaw] ERROR: unpack_thumb failed.";
            }
        }

        qDebug() << "[LibRaw] Decoding full matrix...";
        // Mode 1: half_size = 1 (Ultra fast 50% RAW matrix decode)
        // Mode 2: half_size = 0 (Full matrix RAW decode)
        lr.imgdata.params.half_size = (mode == 1) ? 1 : 0;
        lr.imgdata.params.output_bps = 8;

        if (lr.unpack() != LIBRAW_SUCCESS) {
            qDebug() << "[LibRaw] ERROR: unpack failed.";
            lr.recycle();
            return QImage();
        }

        if (lr.dcraw_process() != LIBRAW_SUCCESS) {
            qDebug() << "[LibRaw] ERROR: dcraw_process failed.";
            lr.recycle();
            return QImage();
        }

        libraw_processed_image_t* image = lr.dcraw_make_mem_image(&ret);
        if (!image) {
            qDebug() << "[LibRaw] ERROR: dcraw_make_mem_image failed. Code:" << ret;
            lr.recycle();
            return QImage();
        }

        qDebug() << "[LibRaw] Matrix processed! Colors:" << image->colors << "Bits:" << image->bits;

        if (image->type != LIBRAW_IMAGE_BITMAP || image->colors < 3) {
            qDebug() << "[LibRaw] ERROR: Output is not a valid RGB BITMAP.";
            LibRaw::dcraw_clear_mem(image);
            lr.recycle();
            return QImage();
        }

        QImage result(image->width, image->height, QImage::Format_RGB888);

        if (image->bits == 16) {
            const unsigned short* src = reinterpret_cast<const unsigned short*>(image->data);
            for (int y = 0; y < static_cast<int>(image->height); ++y) {
                uchar* dst = result.scanLine(y);
                for (int x = 0; x < static_cast<int>(image->width); ++x) {
                    const int si = (y * static_cast<int>(image->width) + x) * image->colors;
                    const int di = x * 3;
                    dst[di]     = static_cast<uchar>(src[si] >> 8);
                    dst[di + 1] = static_cast<uchar>(src[si + 1] >> 8);
                    dst[di + 2] = static_cast<uchar>(src[si + 2] >> 8);
                }
            }
        } else {
            for (int y = 0; y < static_cast<int>(image->height); ++y) {
                uchar* dst = result.scanLine(y);
                for (int x = 0; x < static_cast<int>(image->width); ++x) {
                    const int si = (y * static_cast<int>(image->width) + x) * image->colors;
                    const int di = x * 3;
                    dst[di]     = image->data[si];
                    dst[di + 1] = image->data[si + 1];
                    dst[di + 2] = image->data[si + 2];
                }
            }
        }

        qDebug() << "[LibRaw] Memory copied successfully to QImage.";
        LibRaw::dcraw_clear_mem(image);
        lr.recycle();

        int flip = lr.imgdata.sizes.flip;
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
        unsigned int threads = 2; // Dedicated preloading worker threads
        m_stopWorkers = false;
        for (unsigned int i = 0; i < threads; ++i) {
            m_workers.emplace_back([this]() {
                while (!m_stopWorkers) {
                    // FIRST: Yield completely while preview thumbnails are loading for the UI!
                    if (ThumbnailProvider::isPreviewLoading()) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(30));
                        continue;
                    }

                    QString path;
                    {
                        std::unique_lock<std::mutex> lock(m_queueMutex);
                        m_cv.wait_for(lock, std::chrono::milliseconds(50), [this]() {
                            return m_stopWorkers || !m_queue.empty();
                        });
                        if (m_stopWorkers) return;
                        if (m_queue.empty()) continue;
                        path = m_queue.front();
                        m_queue.pop_front();
                    }

                    if (path.isEmpty()) continue;
                    int mode = m_backend ? m_backend->rawViewMode() : 0;

                    {
                        std::lock_guard<std::mutex> lock(m_cacheMutex);
                        auto it = m_cache.find(path);
                        if (it != m_cache.end() && it->second.mode == mode) {
                            continue;
                        }
                    }

                    // Check again before heavy decode
                    if (ThumbnailProvider::isPreviewLoading()) {
                        std::lock_guard<std::mutex> qlock(m_queueMutex);
                        m_queue.push_front(path);
                        std::this_thread::sleep_for(std::chrono::milliseconds(30));
                        continue;
                    }

                    QImage img = loadImageDirect(path, mode);
                    if (!img.isNull()) {
                        putInCache(path, img, mode);
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