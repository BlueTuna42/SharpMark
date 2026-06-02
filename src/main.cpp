#include <iostream>
#include <vector>
#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "app_backend.h"

#ifdef _WIN32
#include <windows.h>
#undef SetCurrentDirectory 
#endif

#include "tools/scan.h"
#include "pipeline/interfaces.h"
#include "pipeline/runner.h"
#include "gui/utils/path_utils.h"
#include "img_tools/bmp.h"
#include "qt_gui/thumbnail_provider.h"
#include "qt_gui/full_image_provider.h"

#include "loaders/bmp_loader.h"
#include "processors/laplacian_focus.h"
#include "processors/state_cache.h"
#include "processors/clip_embedding.h"
#include "processors/aesthetic_scorer.h"
#include "postprocessors/state_cache.h"

void customMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg) {
    static QFile outFile("sharpmark_debug.log");
    if (!outFile.isOpen()) {
        outFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    }

    QTextStream ts(&outFile);
    QString timeStr = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
    
    QString typeStr;
    switch (type) {
        case QtDebugMsg:    typeStr = "[DEBUG]   "; break;
        case QtInfoMsg:     typeStr = "[INFO]    "; break;
        case QtWarningMsg:  typeStr = "[WARNING] "; break;
        case QtCriticalMsg: typeStr = "[CRITICAL]"; break;
        case QtFatalMsg:    typeStr = "[FATAL]   "; break;
    }

    QString contextStr;
    if (context.file && !QString(context.file).isEmpty()) {
        QString file = QString(context.file).section('\\', -1).section('/', -1);
        contextStr = QString("[%1:%2 %3] ").arg(file).arg(context.line).arg(context.function);
    }

    QString finalLog = QString("%1 %2 %3%4\n").arg(timeStr, typeStr, contextStr, msg);
    
    ts << finalLog;
    ts.flush();
    outFile.flush();
    std::cerr << finalLog.toStdString();

    if (type == QtFatalMsg) {
        abort();
    }
}


int main(int argc, char *argv[]) {
    // Set environment variable so Qt provides file/line info even outside of debug mode
    qputenv("QT_MESSAGE_PATTERN", "%{time hh:mm:ss.zzz} %{type} %{file}:%{line} %{function} - %{message}");
    qInstallMessageHandler(customMessageHandler); 

    qInfo() << "========================================";
    qInfo() << "SharpMark Starting...";
    qInfo() << "========================================";

    try {
        QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

        QGuiApplication app(argc, argv);
        app.setOrganizationName("SharpMark");
        app.setOrganizationDomain("sharpmark.local");
        app.setApplicationName("SharpMark");

        // Instantiate our C++ backend
        AppBackend backend;

        QQmlApplicationEngine engine;

        // Expose the backend object to QML under the name "backend"
        engine.rootContext()->setContextProperty("backend", &backend);

        // Register custom image providers
        engine.addImageProvider(QLatin1String("preview"), new ThumbnailProvider);
        engine.addImageProvider(QLatin1String("full"), new FullImageProvider(&backend));

        const QUrl url(u"qrc:/SharpMark/qml/main.qml"_qs);
        QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
            &app, [url](QObject *obj, const QUrl &objUrl) {
                if (!obj && url == objUrl) {
                    qFatal("Failed to load QML root object.");
                    QCoreApplication::exit(-1);
                }
            }, Qt::QueuedConnection);

        engine.load(url);

        qInfo() << "Entering Qt event loop.";
        return app.exec(); 

    } catch (const std::exception& e) {
        qCritical() << "UNCAUGHT C++ EXCEPTION:" << e.what();
        return -1;
    } catch (...) {
        qCritical() << "UNCAUGHT UNKNOWN C++ EXCEPTION!";
        return -1;
    }
}