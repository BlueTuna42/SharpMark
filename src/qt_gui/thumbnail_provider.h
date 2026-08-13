#pragma once

#include <QQuickImageProvider>
#include <QImage>
#include <QImageReader>
#include <QBuffer>
#include <QTransform>
#include <QString>
#include <QFileInfo>
#include <QUrl>
#include <QSet>
#include <QDebug>
#include <libraw/libraw.h>
#include <atomic>
#include "../tools/raw_mutex.h"

class ThumbnailProvider : public QQuickImageProvider {
public:
    inline static std::atomic<int> s_activePreviewCount{0};

    static bool isPreviewLoading() {
        return s_activePreviewCount.load() > 0;
    }

private:
    static bool isRawExtension(const QString& ext) {
        static const QSet<QString> rawExts = {
            "cr2", "cr3", "crw", "nef", "nrw", "arw", "srf", "sr2", 
            "pef", "ptx", "dng", "raf", "orf", "rw2", "srw", "x3f", 
            "erf", "mef", "mos", "kdc", "dcr", "fff", "gpr", "3fr", "raw"
        };
        return rawExts.contains(ext);
    }

public:
    ThumbnailProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override {
        struct PreviewScope {
            PreviewScope() { ThumbnailProvider::s_activePreviewCount++; }
            ~PreviewScope() { ThumbnailProvider::s_activePreviewCount--; }
        } scope;

        QString filePath = QUrl::fromPercentEncoding(id.toUtf8());
        
#ifdef Q_OS_WIN
        if (filePath.startsWith("/")) {
            filePath = filePath.mid(1);
        }
#endif

        const int targetWidth = 256;
        const int targetHeight = 256;

        if (!QFileInfo::exists(filePath)) {
            if (size) *size = QSize(targetWidth, targetHeight);
            return createBlackImage(targetWidth, targetHeight);
        }

        const QString suffix = QFileInfo(filePath).suffix().toLower();
        QImage resultImage;

        if (isRawExtension(suffix)) {
            resultImage = extractRawThumbnail(filePath);
            if (!resultImage.isNull() && (resultImage.width() > targetWidth || resultImage.height() > targetHeight)) {
                resultImage = resultImage.scaled(targetWidth, targetHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }
        } else {
            QImageReader reader(filePath);
            if (reader.canRead()) {
                reader.setAutoTransform(true);
                QSize origSize = reader.size();
                if (origSize.isValid()) {
                    QSize scaledSize = origSize.scaled(targetWidth, targetHeight, Qt::KeepAspectRatio);
                    reader.setScaledSize(scaledSize);
                }
                resultImage = reader.read();
            }
        }

        if (resultImage.isNull()) {
            if (size) *size = QSize(targetWidth, targetHeight);
            return createBlackImage(targetWidth, targetHeight);
        }

        if (size) *size = resultImage.size();
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
        
        rawProcessor.imgdata.params.use_camera_wb = 1;
        rawProcessor.imgdata.params.half_size = 1; 
        
        int ret = LIBRAW_SUCCESS;
        
#if defined(_WIN32)
        ret = rawProcessor.open_file(filePath.toStdWString().c_str());
        if (ret != LIBRAW_SUCCESS) {
            ret = rawProcessor.open_file(filePath.toLocal8Bit().constData());
        }
#else
        ret = rawProcessor.open_file(filePath.toUtf8().constData());
#endif

        if (ret != LIBRAW_SUCCESS) return QImage();
        if (rawProcessor.unpack_thumb() != LIBRAW_SUCCESS) {
            rawProcessor.recycle();
            return QImage();
        }

        QImage thumbnail;
        
        if (rawProcessor.imgdata.thumbnail.tformat == LIBRAW_THUMBNAIL_JPEG) {
            QByteArray thumbData(
                reinterpret_cast<const char*>(rawProcessor.imgdata.thumbnail.thumb),
                rawProcessor.imgdata.thumbnail.tlength
            );
            QBuffer buffer(&thumbData);
            buffer.open(QIODevice::ReadOnly);
            QImageReader reader(&buffer);
            reader.setAutoTransform(true);
            thumbnail = reader.read();
        } 
        else if (rawProcessor.imgdata.thumbnail.tformat == LIBRAW_THUMBNAIL_BITMAP) {
            thumbnail = QImage(
                reinterpret_cast<const uchar*>(rawProcessor.imgdata.thumbnail.thumb),
                rawProcessor.imgdata.thumbnail.twidth,
                rawProcessor.imgdata.thumbnail.theight,
                QImage::Format_RGB888
            ).copy();
        }

        int flip = rawProcessor.imgdata.sizes.flip;
        if (!thumbnail.isNull() && flip != 0) {
            if ((flip == 5 || flip == 6 || flip == 8) && thumbnail.width() >= thumbnail.height()) {
                QTransform t;
                if (flip == 6) t.rotate(90);
                else if (flip == 5 || flip == 8) t.rotate(270);
                thumbnail = thumbnail.transformed(t, Qt::SmoothTransformation);
            } else if (flip == 3 && thumbnail.width() >= thumbnail.height()) {
                QTransform t;
                t.rotate(180);
                thumbnail = thumbnail.transformed(t, Qt::SmoothTransformation);
            }
        }

        rawProcessor.recycle();
        return thumbnail;
    }
};