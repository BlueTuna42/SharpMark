#pragma once

#include <QQuickImageProvider>
#include <QImage>
#include <QImageReader>
#include <QString>
#include <QFileInfo>
#include <QUrl>
#include <QDebug>
#include <libraw/libraw.h>

class ThumbnailProvider : public QQuickImageProvider {
public:
    ThumbnailProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override {
        // 1. Decode the URL (fixes spaces and Cyrillic characters in paths)
        QString filePath = QUrl::fromPercentEncoding(id.toUtf8());
        
        // 2. Fix potential QML Windows leading slash issue (e.g., "/C:/Photos/...")
#ifdef Q_OS_WIN
        if (filePath.startsWith("/")) {
            filePath = filePath.mid(1);
        }
#endif

        const int targetWidth = 256;
        const int targetHeight = 256;
        if (size) *size = QSize(targetWidth, targetHeight);

        // 3. Verify file exists
        if (!QFileInfo::exists(filePath)) {
            qWarning() << "[Thumbnail] File not found:" << filePath;
            return createBlackImage(targetWidth, targetHeight);
        }

        QString suffix = QFileInfo(filePath).suffix().toLower();
        QImage resultImage;

        // 4. Fast RAW Extraction
        if (suffix == "cr2" || suffix == "nef" || suffix == "arw" || suffix == "dng" || suffix == "raw") {
            resultImage = extractRawThumbnail(filePath);
            
            if (resultImage.isNull()) {
                qWarning() << "[Thumbnail] LibRaw failed to extract thumbnail for:" << filePath;
            }
            
            // CRITICAL FIX: Do NOT let QImageReader touch RAW files. 
            // Qt's raw plugins are extremely slow and cause the program to hang.
        } 
        else {
            // 5. Fast JPEG/PNG extraction
            QImageReader reader(filePath);
            if (reader.canRead()) {
                reader.setScaledSize(QSize(targetWidth, targetHeight));
                resultImage = reader.read();
            } else {
                qWarning() << "[Thumbnail] QImageReader failed on:" << filePath;
            }
        }

        if (resultImage.isNull()) {
            return createBlackImage(targetWidth, targetHeight);
        }

        return resultImage;
    }

private:
    QImage createBlackImage(int w, int h) {
        QImage img(w, h, QImage::Format_RGB32);
        img.fill(Qt::black);
        return img;
    }

    QImage extractRawThumbnail(const QString &filePath) {
        LibRaw rawProcessor;
        
        // Optimization: Disable everything except what is needed for thumbnails
        rawProcessor.imgdata.params.use_camera_wb = 1;
        rawProcessor.imgdata.params.half_size = 1; 
        
        int ret = LIBRAW_SUCCESS;
        
#if defined(_WIN32)
        // Try Windows wchar_t API first
        ret = rawProcessor.open_file(filePath.toStdWString().c_str());
        if (ret != LIBRAW_SUCCESS) {
            // Fallback for MSYS2 MinGW which might expect char*
            ret = rawProcessor.open_file(filePath.toLocal8Bit().constData());
        }
#else
        ret = rawProcessor.open_file(filePath.toUtf8().constData());
#endif

        if (ret != LIBRAW_SUCCESS) {
            return QImage();
        }

        if (rawProcessor.unpack_thumb() != LIBRAW_SUCCESS) {
            rawProcessor.recycle();
            return QImage();
        }

        QImage thumbnail;
        
        if (rawProcessor.imgdata.thumbnail.tformat == LIBRAW_THUMBNAIL_JPEG) {
            // Extract embedded JPEG bytes
            thumbnail.loadFromData(
                reinterpret_cast<const uchar*>(rawProcessor.imgdata.thumbnail.thumb), 
                rawProcessor.imgdata.thumbnail.tlength, 
                "JPEG"
            );
        } 
        else if (rawProcessor.imgdata.thumbnail.tformat == LIBRAW_THUMBNAIL_BITMAP) {
            // Very rare for modern cameras, but supported just in case
            thumbnail = QImage(
                reinterpret_cast<const uchar*>(rawProcessor.imgdata.thumbnail.thumb),
                rawProcessor.imgdata.thumbnail.twidth,
                rawProcessor.imgdata.thumbnail.theight,
                QImage::Format_RGB888
            ).copy();
        }

        rawProcessor.recycle();
        return thumbnail;
    }
};