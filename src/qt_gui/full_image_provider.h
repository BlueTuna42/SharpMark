#pragma once

#include <QQuickImageProvider>
#include <QImage>
#include <QImageReader>
#include <QString>
#include <QUrl>
#include <QFileInfo>
#include <QSet>
#include <QDebug>
#include "../app_backend.h"
#include <libraw/libraw.h>

class FullImageProvider : public QQuickImageProvider {
    AppBackend* m_backend;

    static bool isRawExtension(const QString& ext) {
        static const QSet<QString> rawExts = {
            "3fr", "arw", "cr2", "cr3", "crw", "dcr", "dng", "erf", "fff",
            "gpr", "kdc", "mef", "mos", "nef", "nrw", "orf", "pef", "ptx",
            "raf", "raw", "rw2", "sr2", "srf", "srw", "x3f"
        };
        return rawExts.contains(ext);
    }

public:
    explicit FullImageProvider(AppBackend* backend)
        : QQuickImageProvider(QQuickImageProvider::Image), m_backend(backend) {}

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

        QImage image;
        if (isRawExtension(suffix)) {
            int mode = m_backend->rawViewMode();
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
            qDebug() << "[FullImageProvider] ERROR: Final image is NULL. Returning empty QImage.";
            return QImage();
        }

        qDebug() << "[FullImageProvider] Success! Image size:" << image.width() << "x" << image.height() << "Format:" << image.format();

        if (image.format() != QImage::Format_RGB32 && image.format() != QImage::Format_ARGB32) {
            qDebug() << "[FullImageProvider] Converting format to RGB32 for QML rendering...";
            image = image.convertToFormat(QImage::Format_RGB32);
        }

        if (size) {
            *size = image.size();
        }
        
        qDebug() << "======================================";
        return image;
    }

private:
    QImage loadRaw(const QString& filePath, int mode) {
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
                    thumb.loadFromData(
                        reinterpret_cast<const uchar*>(lr.imgdata.thumbnail.thumb),
                        lr.imgdata.thumbnail.tlength,
                        "JPEG"
                    );
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
        return result;
    }
};