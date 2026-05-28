#pragma once
#include <QQuickImageProvider>
#include "app_backend.h"

class HistogramProvider : public QQuickImageProvider {
    AppBackend* m_backend;
public:
    HistogramProvider(AppBackend* backend) 
        : QQuickImageProvider(QQuickImageProvider::Image), m_backend(backend) {}

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override {
        Q_UNUSED(id)
        Q_UNUSED(requestedSize);
        return QImage();
    }
};