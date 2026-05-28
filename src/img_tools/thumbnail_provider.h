#pragma once

#include <QQuickImageProvider>
#include <QImage>
#include <QImageReader>
#include <QDebug>

class ThumbnailProvider : public QQuickImageProvider {
public:
    ThumbnailProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override {
        // 'id' contains the absolute file path passed from QML
        QString filePath = id;
        
        int width = 256;
        int height = 256;
        if (size) {
            *size = QSize(width, height);
        }

        // TODO: Here you should use your libraw / stb_image wrapper 
        // to extract the FAST embedded EXIF thumbnail!
        // Example: ImageIO::loadPreviewToBuffer(...) -> build QImage from raw bytes.

        // Fallback for now: Use Qt's built-in fast scaled reader for JPEGs
        QImageReader reader(filePath);
        if (!reader.canRead()) {
            return QImage(width, height, QImage::Format_RGB32); // Return empty/black image on failure
        }

        // Crucial for performance: Tell Qt to read only a scaled down version!
        reader.setScaledSize(QSize(width, height));
        return reader.read();
    }
};