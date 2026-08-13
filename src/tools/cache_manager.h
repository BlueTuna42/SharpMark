#pragma once

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QDebug>
#include <filesystem>
#include <string>

class CacheManager {
public:
    // Returns the root cache directory (Portable App 'cache/' on Windows, ~/.cache/SharpMark/ on Linux)
    static std::filesystem::path getAppCacheRootDir();

    // Returns the specific cache subfolder path for a given scanned image folder
    static std::filesystem::path getFolderCacheDir(const QString& scanFolderPath);

    // Migrates old legacy '.laplacian_cache' from scanning directory into centralized cache
    static void migrateLegacyCache(const QString& scanFolderPath);

    // Returns details of all cached folders for UI display
    static QVariantList getCachedFoldersList();

    // Deletes specific cache folder by hash/name
    static bool deleteCacheFolder(const QString& hashName);

    // Deletes all cached scan data
    static bool clearAllCache();

    // Calculates total cache size in bytes
    static qint64 getTotalCacheSizeBytes();

    // Formats byte count to human readable string (KB, MB, GB)
    static QString formatSize(qint64 bytes);
};
