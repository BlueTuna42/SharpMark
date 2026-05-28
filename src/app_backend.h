#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <atomic>
#include <thread>
#include <vector>

class AppBackend : public QObject {
    Q_OBJECT
    
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(int totalFiles READ totalFiles NOTIFY totalFilesChanged)

public:
    explicit AppBackend(QObject *parent = nullptr);
    ~AppBackend();

    Q_INVOKABLE void selectFolder(const QString &folderPath);
    Q_INVOKABLE void startScan();
    Q_INVOKABLE void cancelScan();

    QString statusText() const;
    int progress() const;
    int totalFiles() const;

signals:
    void statusTextChanged();
    void progressChanged();
    void totalFilesChanged();
    
    // Cross-thread communication: Worker threads emit this, Qt safely routes it to the Main GUI Thread
    void fileProcessed(const QString &fileName, const QString &filePath, bool isBlurry, float aestheticScore, int width, int height);
    void scanFinished();

private:
    QString m_statusText;
    int m_progress = 0;
    int m_totalFiles = 0;
    QString m_currentFolder;

    std::atomic<bool> m_isScanning{false};
    std::atomic<bool> m_cancelRequested{false};
    std::thread m_scanThread;

    void setStatusText(const QString &text);
    void setProgress(int value);
    void setTotalFiles(int value);
    
    // The main background orchestrator function
    void runScannerTask();
};