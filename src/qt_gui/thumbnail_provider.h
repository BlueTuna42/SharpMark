#pragma once

#include <QQuickImageProvider>
#include <QImage>
#include <QImageReader>
#include <QString>
#include <QFileInfo>
#include <QUrl>
#include <QSet>
#include <QDebug>
#include <libraw/libraw.h>

class ThumbnailProvider : public QQuickImageProvider {
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
        QString filePath = QUrl::fromPercentEncoding(id.toUtf8());
        
#ifdef Q_OS_WIN
        if (filePath.startsWith("/")) {
            filePath = filePath.mid(1);
        }
#endif

        const int targetWidth = 256;
        const int targetHeight = 256;
        if (size) *size = QSize(targetWidth, targetHeight);

        if (!QFileInfo::exists(filePath)) {
            return createBlackImage(targetWidth, targetHeight);
        }

        const QString suffix = QFileInfo(filePath).suffix().toLower();
        QImage resultImage;

        if (isRawExtension(suffix)) {
            resultImage = extractRawThumbnail(filePath);
        } else {
            QImageReader reader(filePath);
            if (reader.canRead()) {
                reader.setScaledSize(QSize(targetWidth, targetHeight));
                resultImage = reader.read();
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
            thumbnail.loadFromData(
                reinterpret_cast<const uchar*>(rawProcessor.imgdata.thumbnail.thumb), 
                rawProcessor.imgdata.thumbnail.tlength, 
                "JPEG"
            );
        } 
        else if (rawProcessor.imgdata.thumbnail.tformat == LIBRAW_THUMBNAIL_BITMAP) {
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