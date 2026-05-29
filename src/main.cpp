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
#include "gui/gui.h"
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
    QFile outFile("sharpmark_debug.log");
    outFile.open(QIODevice::WriteOnly | QIODevice::Append);
    QTextStream ts(&outFile);
    ts << QDateTime::currentDateTime().toString("hh:mm:ss.zzz ") << msg << "\n";
}

int main(int argc, char *argv[]) {
    qInstallMessageHandler(customMessageHandler); 
    // Required for High DPI displays
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

    // Register custom image provider
    engine.addImageProvider(QLatin1String("preview"), new ThumbnailProvider);
    engine.addImageProvider(QLatin1String("full"), new FullImageProvider(&backend));
    // Load the main QML file
    const QUrl url(u"qrc:/SharpMark/qml/main.qml"_qs);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
        &app, [url](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl)
                QCoreApplication::exit(-1);
        }, Qt::QueuedConnection);
        
    engine.load(url);

    return app.exec(); // Start the Qt event loop
}