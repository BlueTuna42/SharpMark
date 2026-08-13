#include "cache_manager.h"

std::filesystem::path CacheManager::getAppCacheRootDir() {
#if defined(Q_OS_WIN)
    QString appDir = QCoreApplication::applicationDirPath();
    std::filesystem::path p = std::filesystem::path(appDir.toStdWString()) / "cache";
#else
    QString systemCacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (systemCacheDir.isEmpty()) {
        systemCacheDir = QDir::homePath() + "/.cache/SharpMark";
    }
    std::filesystem::path p = std::filesystem::path(systemCacheDir.toStdString());
#endif
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    return p;
}

std::filesystem::path CacheManager::getFolderCacheDir(const QString& scanFolderPath) {
    if (scanFolderPath.isEmpty()) return getAppCacheRootDir() / "scans" / "default";

    QString cleanPath = QDir::cleanPath(scanFolderPath);
    QFileInfo fi(cleanPath);
    if (fi.isFile()) {
        cleanPath = QDir::cleanPath(fi.absolutePath());
        fi = QFileInfo(cleanPath);
    }

    QString baseName = fi.fileName();
    if (baseName.isEmpty()) baseName = "root";

    QByteArray hash = QCryptographicHash::hash(cleanPath.toUtf8(), QCryptographicHash::Sha256).toHex().left(12);
    QString folderDirName = baseName + "_" + QString::fromUtf8(hash);

    std::filesystem::path scansDir = getAppCacheRootDir() / "scans" / folderDirName.toStdString();
    std::error_code ec;
    std::filesystem::create_directories(scansDir, ec);

    // Save/update folder_info.json
    std::filesystem::path infoPath = scansDir / "folder_info.json";
    QJsonObject jsonObj;
    jsonObj["folderPath"] = cleanPath;
    jsonObj["folderHash"] = folderDirName;
    jsonObj["lastScanned"] = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");

    QJsonDocument doc(jsonObj);
    QFile file(QString::fromStdWString(infoPath.wstring()));
    if (file.open(QIODevice::WriteOnly)) {
        file.write(doc.toJson());
        file.close();
    }

    return scansDir;
}

void CacheManager::migrateLegacyCache(const QString& scanFolderPath) {
    if (scanFolderPath.isEmpty()) return;
    QString cleanPath = QDir::cleanPath(scanFolderPath);
    QString legacyDirStr = cleanPath + "/.laplacian_cache";
    QDir legacyDir(legacyDirStr);

    if (!legacyDir.exists()) return;

    std::filesystem::path targetDir = getFolderCacheDir(cleanPath);
    qDebug() << "[CacheManager] Migrating legacy cache from:" << legacyDirStr << "to:" << QString::fromStdWString(targetDir.wstring());

    QFileInfoList fileList = legacyDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo& fi : fileList) {
        QString destPath = QString::fromStdWString((targetDir / fi.fileName().toStdString()).wstring());
        if (QFile::exists(destPath)) QFile::remove(destPath);
        QFile::copy(fi.absoluteFilePath(), destPath);
    }

    // Clean up old legacy directory
    legacyDir.removeRecursively();
}

QVariantList CacheManager::getCachedFoldersList() {
    QVariantList list;
    std::filesystem::path scansDir = getAppCacheRootDir() / "scans";

    if (!std::filesystem::exists(scansDir)) return list;

    for (const auto& entry : std::filesystem::directory_iterator(scansDir)) {
        if (!entry.is_directory()) continue;

        std::filesystem::path dirPath = entry.path();
        QString hashName = QString::fromStdWString(dirPath.filename().wstring());

        QString originalPath = hashName;
        QString lastScanned = "Unknown";
        qint64 totalSize = 0;
        int fileCount = 0;

        std::filesystem::path infoPath = dirPath / "folder_info.json";
        QFile infoFile(QString::fromStdWString(infoPath.wstring()));
        if (infoFile.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(infoFile.readAll());
            if (doc.isObject()) {
                QJsonObject obj = doc.object();
                originalPath = obj["folderPath"].toString(hashName);
                lastScanned = obj["lastScanned"].toString("Unknown");
            }
            infoFile.close();
        }

        std::error_code ec;
        for (const auto& f : std::filesystem::directory_iterator(dirPath, ec)) {
            if (f.is_regular_file(ec)) {
                totalSize += f.file_size(ec);
                fileCount++;
            }
        }

        QVariantMap map;
        map["hash"] = hashName;
        map["folderPath"] = originalPath;
        map["fileCount"] = fileCount;
        map["sizeBytes"] = totalSize;
        map["formattedSize"] = formatSize(totalSize);
        map["lastScanned"] = lastScanned;
        list.append(map);
    }

    return list;
}

bool CacheManager::deleteCacheFolder(const QString& hashName) {
    if (hashName.isEmpty()) return false;
    std::filesystem::path dirPath = getAppCacheRootDir() / "scans" / hashName.toStdString();
    std::error_code ec;
    if (std::filesystem::exists(dirPath, ec)) {
        std::filesystem::remove_all(dirPath, ec);
        return !ec;
    }
    return false;
}

bool CacheManager::clearAllCache() {
    std::filesystem::path scansDir = getAppCacheRootDir() / "scans";
    std::error_code ec;
    if (std::filesystem::exists(scansDir, ec)) {
        std::filesystem::remove_all(scansDir, ec);
        return !ec;
    }
    return false;
}

qint64 CacheManager::getTotalCacheSizeBytes() {
    std::filesystem::path rootDir = getAppCacheRootDir();
    qint64 total = 0;
    std::error_code ec;

    if (!std::filesystem::exists(rootDir, ec)) return 0;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(rootDir, ec)) {
        if (entry.is_regular_file(ec)) {
            total += entry.file_size(ec);
        }
    }
    return total;
}

QString CacheManager::formatSize(qint64 bytes) {
    if (bytes < 1024) return QString::number(bytes) + " B";
    double kb = bytes / 1024.0;
    if (kb < 1024) return QString::number(kb, 'f', 1) + " KB";
    double mb = kb / 1024.0;
    if (mb < 1024) return QString::number(mb, 'f', 1) + " MB";
    double gb = mb / 1024.0;
    return QString::number(gb, 'f', 2) + " GB";
}
