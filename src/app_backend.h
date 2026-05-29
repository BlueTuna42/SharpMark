#pragma once

#include <QObject>
#include <QImage>
#include <QString>
#include <QVariantList>
#include <QAbstractListModel>
#include <atomic>
#include <thread>
#include <vector>
#include <map>
#include "pipeline/runner.h"

class PipelineConfigModel : public QAbstractListModel {
    Q_OBJECT

signals:
    void pipelineChanged();

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        EnabledRole
    };

    struct Step {
        QString id;
        QString name;
        bool enabled;
        // In the future, you can add parameters here (e.g., float threshold)
    };

    explicit PipelineConfigModel(QObject *parent = nullptr) : QAbstractListModel(parent) {
        // Initialize with default tools
        m_steps.push_back({"laplacian", "Laplacian Focus Check", true});
        m_steps.push_back({"ai_aesthetic", "AI Aesthetic Scorer", true});
    }

    void clear() {
        beginResetModel();
        m_steps.clear();
        endResetModel();
    }
    void addStep(const QString& id, const QString& name, bool enabled) {
        beginInsertRows(QModelIndex(), m_steps.size(), m_steps.size());
        m_steps.push_back({id, name, enabled});
        endInsertRows();
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override {
        if (parent.isValid()) return 0;
        return static_cast<int>(m_steps.size());
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override {
        if (!index.isValid() || index.row() >= static_cast<int>(m_steps.size()))
            return QVariant();

        const Step &step = m_steps[index.row()];
        switch (role) {
            case IdRole: return step.id;
            case NameRole: return step.name;
            case EnabledRole: return step.enabled;
            default: return QVariant();
        }
    }

    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override {
        if (!index.isValid() || index.row() >= static_cast<int>(m_steps.size()))
            return false;

        Step &step = m_steps[index.row()];
        if (role == EnabledRole) {
            step.enabled = value.toBool();
            emit dataChanged(index, index, {role});
            return true;
        }
        return false;
    }

    QHash<int, QByteArray> roleNames() const override {
        QHash<int, QByteArray> roles;
        roles[IdRole] = "id";
        roles[NameRole] = "name";
        roles[EnabledRole] = "enabled";
        return roles;
    }

    Q_INVOKABLE void setStepEnabled(int index, bool enabled) {
        if (index >= 0 && index < m_steps.size()) {
            m_steps[index].enabled = enabled;
            emit dataChanged(this->index(index), this->index(index), {EnabledRole});
            emit pipelineChanged();
        }
    }

    // Allow reordering from QML
    Q_INVOKABLE void moveStep(int fromIndex, int toIndex) {
        if (fromIndex < 0 || fromIndex >= m_steps.size() || 
            toIndex < 0 || toIndex >= m_steps.size() || 
            fromIndex == toIndex) return;

        int destRow = toIndex > fromIndex ? toIndex + 1 : toIndex;
        beginMoveRows(QModelIndex(), fromIndex, fromIndex, QModelIndex(), destRow);
        auto step = m_steps[fromIndex];
        m_steps.erase(m_steps.begin() + fromIndex);
        m_steps.insert(m_steps.begin() + toIndex, step);
        endMoveRows();
        
        emit pipelineChanged();
    }

    const std::vector<Step>& getSteps() const { return m_steps; }

private:
    std::vector<Step> m_steps;
};

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
    Q_PROPERTY(QString histogramBase64 READ histogramBase64 NOTIFY histogramUpdated)

public:
    explicit AppBackend(QObject *parent = nullptr);
    ~AppBackend();

    Q_INVOKABLE void selectFolder(const QString &folderPath);
    Q_INVOKABLE void startScan();
    Q_INVOKABLE void cancelScan();
    Q_INVOKABLE bool trashFile(const QString &filePath);
    Q_INVOKABLE QVariantMap getPhotoMetadata(const QString& filePath);
    Q_INVOKABLE int getPhotoRating(const QString& filePath);
    Q_INVOKABLE void setPhotoRating(const QString& filePath, int rating);

    void updateHistogramFromImage(const QImage& image);
    QString histogramBase64() const { return m_histogramBase64; }

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

    Q_PROPERTY(PipelineConfigModel* pipelineModel READ pipelineModel CONSTANT)
    PipelineConfigModel* pipelineModel() { return &m_pipelineModel; }

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

    void histogramUpdated();

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

    QString m_histogramBase64;

    PipelineConfigModel m_pipelineModel;

    PipelineRunner createPipeline();

    std::map<QString, int> m_ratings;
    void loadRatings();
    void saveRatings();
};