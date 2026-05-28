#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <atomic>
#include <thread>
#include <vector>

class AppBackend : public QObject {
    Q_OBJECT
    
    // UI Properties
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(int totalFiles READ totalFiles NOTIFY totalFilesChanged)
    
    // Settings Properties
    Q_PROPERTY(int themeMode READ themeMode WRITE setThemeMode NOTIFY themeModeChanged)
    Q_PROPERTY(bool writeExif READ writeExif WRITE setWriteExif NOTIFY writeExifChanged)
    Q_PROPERTY(bool cacheLaplacian READ cacheLaplacian WRITE setCacheLaplacian NOTIFY cacheLaplacianChanged)
    Q_PROPERTY(int rawViewMode READ rawViewMode WRITE setRawViewMode NOTIFY rawViewModeChanged)
    Q_PROPERTY(int rawAnalysisMode READ rawAnalysisMode WRITE setRawAnalysisMode NOTIFY rawAnalysisModeChanged)

public:
    explicit AppBackend(QObject *parent = nullptr);
    ~AppBackend();

    Q_INVOKABLE void selectFolder(const QString &folderPath);
    Q_INVOKABLE void startScan();
    Q_INVOKABLE void cancelScan();

    QString statusText() const;
    int progress() const;
    int totalFiles() const;
    
    // Settings Getters and Setters
    int themeMode() const; 
    void setThemeMode(int mode);
    
    bool writeExif() const; 
    void setWriteExif(bool write);
    
    bool cacheLaplacian() const; 
    void setCacheLaplacian(bool cache);
    
    int rawViewMode() const; 
    void setRawViewMode(int mode);
    
    int rawAnalysisMode() const; 
    void setRawAnalysisMode(int mode);

signals:
    void statusTextChanged();
    void progressChanged();
    void totalFilesChanged();
    
    void themeModeChanged();
    void writeExifChanged();
    void cacheLaplacianChanged();
    void rawViewModeChanged();
    void rawAnalysisModeChanged();
    
    void fileFound(const QString &fileName, const QString &filePath, int index);
    void fileProcessed(int index, bool isBlurry, float aestheticScore, int width, int height);
    void scanFinished();

private:
    QString m_statusText;
    int m_progress = 0;
    int m_totalFiles = 0;
    QString m_currentFolder;

    std::atomic<bool> m_isScanning{false};
    std::atomic<bool> m_cancelRequested{false};
    std::thread m_scanThread;
    
    std::vector<std::string> m_files;

    // Settings Variables
    int m_themeMode = 0;
    bool m_writeExif = false;
    bool m_cacheLaplacian = false;
    int m_rawViewMode = 0;
    int m_rawAnalysisMode = 0;

    void setStatusText(const QString &text);
    void setProgress(int value);
    void setTotalFiles(int value);
    void runScannerTask();
    
    void loadSettings();
    void saveSettings();
    QString getSettingsFilePath() const;
};