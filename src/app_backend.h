#pragma once

#include "tools/cache_manager.h"
#include <QObject>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QAbstractListModel>
#include <QSortFilterProxyModel>

#include <atomic>
#include <mutex>
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
        EnabledRole,
        SettingsRole
    };

    struct Step {
        QString id;
        QString name;
        bool enabled;
        QVariantMap settings;
    };

    explicit PipelineConfigModel(QObject *parent = nullptr) : QAbstractListModel(parent) {
        // Initialize with default tools
        m_steps.push_back({"exposure", "Exposure Check", true, {{"clipThreshold", 0.15}}});
        m_steps.push_back({"laplacian", "Laplacian Focus Check", true, {{"focusThreshold", 150.0}}});
        m_steps.push_back({"aiaesthetic", "AI Aesthetic Scorer", true, {{"showScore", true}, {"colorScore", true}, {"applyUserBias", true}}});
    }

    void clear() {
        beginResetModel();
        m_steps.clear();
        endResetModel();
    }
    void addStep(const QString& id, const QString& name, bool enabled, const QVariantMap& settings = {}) {
        beginInsertRows(QModelIndex(), m_steps.size(), m_steps.size());
        m_steps.push_back({id, name, enabled, settings});
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

    Q_INVOKABLE QVariantMap getStepSettings(int index) const {
        if (index >= 0 && index < m_steps.size())
            return m_steps[index].settings;
        return {};
    }

    Q_INVOKABLE void setStepEnabled(int index, bool enabled) {
        if (index >= 0 && index < m_steps.size()) {
            m_steps[index].enabled = enabled;
            emit dataChanged(this->index(index), this->index(index), {EnabledRole});
            emit pipelineChanged();
        }
    }

    Q_INVOKABLE void setStepSettings(int index, const QVariantMap& settings) {
        if (index >= 0 && index < m_steps.size()) {
            m_steps[index].settings = settings;
            emit dataChanged(this->index(index), this->index(index), {SettingsRole});
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

// ---------------------------------------------------------------------------
// PreprocessorConfigModel — drives the "Preprocessors" section in the sidebar.
// Each row has: id, name, enabled, supportsDisable.
// ---------------------------------------------------------------------------
class PreprocessorConfigModel : public QAbstractListModel {
    Q_OBJECT

signals:
    void preprocessorChanged();

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        EnabledRole,
        SupportsDisableRole,
        SettingsRole
    };

    struct Step {
        QString id;
        QString name;
        bool    enabled;
        bool    supportsDisable;
        QVariantMap settings;
    };

    explicit PreprocessorConfigModel(QObject *parent = nullptr) : QAbstractListModel(parent) {
        m_steps.push_back({"visual_hash", "Burst Grouping (Visual Hash)", true,  true, {{"hammingThreshold", 20}}});
        m_steps.push_back({"lut_3d",      "Color LUT (3D)",               false, true, {}});
    }

    void clear() { beginResetModel(); m_steps.clear(); endResetModel(); }

    void addStep(const QString& id, const QString& name, bool enabled, bool supportsDisable, const QVariantMap& settings = {}) {
        beginInsertRows(QModelIndex(), m_steps.size(), m_steps.size());
        m_steps.push_back({id, name, enabled, supportsDisable, settings});
        endInsertRows();
    }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override {
        if (parent.isValid()) return 0;
        return static_cast<int>(m_steps.size());
    }

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override {
        if (!index.isValid() || index.row() >= static_cast<int>(m_steps.size()))
            return QVariant();
        const Step& s = m_steps[index.row()];
        switch (role) {
            case IdRole:             return s.id;
            case NameRole:           return s.name;
            case EnabledRole:        return s.enabled;
            case SupportsDisableRole:return s.supportsDisable;
            default:                 return QVariant();
        }
    }

    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override {
        if (!index.isValid() || index.row() >= static_cast<int>(m_steps.size())) return false;
        if (role == EnabledRole) {
            m_steps[index.row()].enabled = value.toBool();
            emit dataChanged(index, index, {role});
            return true;
        }
        return false;
    }

    QHash<int, QByteArray> roleNames() const override {
        QHash<int, QByteArray> roles;
        roles[IdRole]              = "id";
        roles[NameRole]            = "name";
        roles[EnabledRole]         = "enabled";
        roles[SupportsDisableRole] = "supportsDisable";
        return roles;
    }

    Q_INVOKABLE QVariantMap getStepSettings(int index) const {
        if (index >= 0 && index < static_cast<int>(m_steps.size()))
            return m_steps[index].settings;
        return {};
    }

    Q_INVOKABLE void setStepEnabled(int index, bool enabled) {
        if (index >= 0 && index < static_cast<int>(m_steps.size())) {
            m_steps[index].enabled = enabled;
            emit dataChanged(this->index(index), this->index(index), {EnabledRole});
            emit preprocessorChanged();
        }
    }

    Q_INVOKABLE void setStepSettings(int index, const QVariantMap& settings) {
        if (index >= 0 && index < static_cast<int>(m_steps.size())) {
            m_steps[index].settings = settings;
            emit dataChanged(this->index(index), this->index(index), {SettingsRole});
            emit preprocessorChanged();
        }
    }

    bool isEnabled(const QString& id) const {
        for (const auto& s : m_steps)
            if (s.id == id) return s.enabled;
        return false;
    }

    const std::vector<Step>& getSteps() const { return m_steps; }

private:
    std::vector<Step> m_steps;
};


// ---------------------------------------------------------------------------
// PostprocessorConfigModel — drives the "Postprocessors" section in the sidebar.
// Same structure as PreprocessorConfigModel.
// ---------------------------------------------------------------------------
class PostprocessorConfigModel : public QAbstractListModel {
    Q_OBJECT

signals:
    void postprocessorChanged();

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        EnabledRole,
        SupportsDisableRole,
        SettingsRole
    };

    struct Step {
        QString id;
        QString name;
        bool    enabled;
        bool    supportsDisable;
        QVariantMap settings;
    };

    explicit PostprocessorConfigModel(QObject *parent = nullptr) : QAbstractListModel(parent) {
        m_steps.push_back({"clip_embedding", "Burst Grouping (CLIP Embedding)", false, true, {{"cosineThreshold", 0.90}}});
    }

    void clear() { beginResetModel(); m_steps.clear(); endResetModel(); }

    void addStep(const QString& id, const QString& name, bool enabled, bool supportsDisable, const QVariantMap& settings = {}) {
        beginInsertRows(QModelIndex(), m_steps.size(), m_steps.size());
        m_steps.push_back({id, name, enabled, supportsDisable, settings});
        endInsertRows();
    }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override {
        if (parent.isValid()) return 0;
        return static_cast<int>(m_steps.size());
    }

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override {
        if (!index.isValid() || index.row() >= static_cast<int>(m_steps.size()))
            return QVariant();
        const Step& s = m_steps[index.row()];
        switch (role) {
            case IdRole:              return s.id;
            case NameRole:            return s.name;
            case EnabledRole:         return s.enabled;
            case SupportsDisableRole: return s.supportsDisable;
            default:                  return QVariant();
        }
    }

    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override {
        if (!index.isValid() || index.row() >= static_cast<int>(m_steps.size())) return false;
        if (role == EnabledRole) {
            m_steps[index.row()].enabled = value.toBool();
            emit dataChanged(index, index, {role});
            return true;
        }
        return false;
    }

    QHash<int, QByteArray> roleNames() const override {
        QHash<int, QByteArray> roles;
        roles[IdRole]              = "id";
        roles[NameRole]            = "name";
        roles[EnabledRole]         = "enabled";
        roles[SupportsDisableRole] = "supportsDisable";
        return roles;
    }

    Q_INVOKABLE QVariantMap getStepSettings(int index) const {
        if (index >= 0 && index < static_cast<int>(m_steps.size()))
            return m_steps[index].settings;
        return {};
    }

    Q_INVOKABLE void setStepEnabled(int index, bool enabled) {
        if (index >= 0 && index < static_cast<int>(m_steps.size())) {
            m_steps[index].enabled = enabled;
            emit dataChanged(this->index(index), this->index(index), {EnabledRole});
            emit postprocessorChanged();
        }
    }

    Q_INVOKABLE void setStepSettings(int index, const QVariantMap& settings) {
        if (index >= 0 && index < static_cast<int>(m_steps.size())) {
            m_steps[index].settings = settings;
            emit dataChanged(this->index(index), this->index(index), {SettingsRole});
            emit postprocessorChanged();
        }
    }

    bool isEnabled(const QString& id) const {
        for (const auto& s : m_steps)
            if (s.id == id) return s.enabled;
        return false;
    }

    const std::vector<Step>& getSteps() const { return m_steps; }

private:
    std::vector<Step> m_steps;
};


class BurstFilterProxyModel : public QSortFilterProxyModel {
    Q_OBJECT
    Q_PROPERTY(QObject* source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(bool groupBursts READ groupBursts WRITE setGroupBursts NOTIFY groupBurstsChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(SortMode sortMode READ sortMode WRITE setSortMode NOTIFY sortModeChanged)
    Q_PROPERTY(QString colorLabelFilter READ colorLabelFilter WRITE setColorLabelFilter NOTIFY colorLabelFilterChanged)
    Q_PROPERTY(int ratingFilter READ ratingFilter WRITE setRatingFilter NOTIFY ratingFilterChanged)

public:
    enum SortMode {
        SortDefault = 0,
        SortBestFirst,
        SortWorstFirst,
        SortRatingHigh,
        SortRatingLow,
        SortColorLabel
    };
    Q_ENUM(SortMode)

    explicit BurstFilterProxyModel(QObject* parent = nullptr) : QSortFilterProxyModel(parent) {
        setDynamicSortFilter(true);
    }

    QObject* source() const { return m_source; }
    void setSource(QObject* source) {
        if (m_source != source) {
            m_source = source;
            setSourceModel(qobject_cast<QAbstractItemModel*>(source));
            if (sourceModel()) {
                connect(sourceModel(), &QAbstractItemModel::rowsInserted, this, [this](){ emit countChanged(); });
                connect(sourceModel(), &QAbstractItemModel::rowsRemoved, this, [this](){ emit countChanged(); });
                connect(sourceModel(), &QAbstractItemModel::modelReset, this, [this](){ emit countChanged(); });
            }
            emit sourceChanged();
            emit countChanged();
        }
    }

    bool groupBursts() const { return m_groupBursts; }
    void setGroupBursts(bool group) {
        if (m_groupBursts != group) {
            m_groupBursts = group;
            invalidateFilter();
            emit groupBurstsChanged();
            emit countChanged();
        }
    }

    int count() const { return rowCount(); }

    // Replicate QML's ListModel.get(index) so our Viewer window still works
    Q_INVOKABLE QVariantMap get(int row) const {
        QVariantMap map;
        QModelIndex idx = index(row, 0);
        if (!idx.isValid()) return map;
        QHash<int, QByteArray> roles = roleNames();
        for (auto it = roles.begin(); it != roles.end(); ++it) {
            map.insert(QString::fromUtf8(it.value()), data(idx, it.key()));
        }
        return map;
    }

    // Map the visible proxy index back to the true source list
    Q_INVOKABLE int mapToSourceRow(int proxyRow) const {
        return mapToSource(index(proxyRow, 0)).row();
    }

     SortMode sortMode() const { return m_sortMode; }
    
    void setSortMode(SortMode mode) {
        if (m_sortMode != mode) {
            m_sortMode = mode;
            Qt::SortOrder order = Qt::AscendingOrder;
            if (m_sortMode == SortBestFirst || m_sortMode == SortRatingHigh) {
                order = Qt::DescendingOrder;
            } else {
                order = Qt::AscendingOrder;
            }
            sort(0, order);
            emit sortModeChanged();
        }
    }

    QString colorLabelFilter() const { return m_colorLabelFilter; }
    void setColorLabelFilter(const QString& filter) {
        if (m_colorLabelFilter != filter) {
            m_colorLabelFilter = filter;
            invalidateFilter();
            emit colorLabelFilterChanged();
            emit countChanged();
        }
    }

    int ratingFilter() const { return m_ratingFilter; }
    void setRatingFilter(int rating) {
        if (m_ratingFilter != rating) {
            m_ratingFilter = rating;
            invalidateFilter();
            emit ratingFilterChanged();
            emit countChanged();
        }
    }

protected:
    bool filterAcceptsRow(int source_row, const QModelIndex& source_parent) const override {
        QModelIndex idx = sourceModel()->index(source_row, 0, source_parent);

        // ── Color label filter ───────────────────────────────────────────────
        if (!m_colorLabelFilter.isEmpty()) {
            if (m_colorLabelRole == -1) m_colorLabelRole = roleNames().key("colorLabel", -1);
            QString rowLabel = (m_colorLabelRole != -1)
                               ? sourceModel()->data(idx, m_colorLabelRole).toString()
                               : QString();
            if (rowLabel != m_colorLabelFilter) return false;
        }

        // ── Rating filter ────────────────────────────────────────────────────
        if (m_ratingFilter > 0) {
            if (m_ratingRole == -1) m_ratingRole = roleNames().key("rating", -1);
            int rowRating = (m_ratingRole != -1)
                            ? sourceModel()->data(idx, m_ratingRole).toInt()
                            : 0;
            if (rowRating != m_ratingFilter) return false;
        }

        // ── Burst group filter ───────────────────────────────────────────────
        if (!m_groupBursts) return true;

        if (m_isLeadRole == -1)     m_isLeadRole     = roleNames().key("isLead",     -1);
        if (m_isExpandedRole == -1) m_isExpandedRole = roleNames().key("isExpanded", -1);

        bool isLead     = (m_isLeadRole     != -1) ? sourceModel()->data(idx, m_isLeadRole).toBool()     : true;
        bool isExpanded = (m_isExpandedRole != -1) ? sourceModel()->data(idx, m_isExpandedRole).toBool() : true;

        return isLead || isExpanded;
    }

    bool lessThan(const QModelIndex &source_left, const QModelIndex &source_right) const override {
        if (m_sortMode == SortDefault) {
            return source_left.row() < source_right.row();
        }

        if (m_sortMode == SortBestFirst || m_sortMode == SortWorstFirst) {
            if (m_scoreRole == -1) m_scoreRole = roleNames().key("score", -1);
            float scoreL = 0.0f;
            float scoreR = 0.0f;
            if (m_scoreRole != -1) {
                scoreL = sourceModel()->data(source_left, m_scoreRole).toFloat();
                scoreR = sourceModel()->data(source_right, m_scoreRole).toFloat();
            }
            if (qFuzzyCompare(scoreL, scoreR)) {
                return source_left.row() < source_right.row();
            }
            return scoreL < scoreR;
        }

        if (m_sortMode == SortRatingHigh || m_sortMode == SortRatingLow) {
            if (m_ratingRole == -1) m_ratingRole = roleNames().key("rating", -1);
            int ratingL = 0;
            int ratingR = 0;
            if (m_ratingRole != -1) {
                ratingL = sourceModel()->data(source_left, m_ratingRole).toInt();
                ratingR = sourceModel()->data(source_right, m_ratingRole).toInt();
            }
            if (ratingL != ratingR) {
                return ratingL < ratingR;
            }
            if (m_scoreRole == -1) m_scoreRole = roleNames().key("score", -1);
            float scoreL = (m_scoreRole != -1) ? sourceModel()->data(source_left, m_scoreRole).toFloat() : 0.0f;
            float scoreR = (m_scoreRole != -1) ? sourceModel()->data(source_right, m_scoreRole).toFloat() : 0.0f;
            if (!qFuzzyCompare(scoreL, scoreR)) {
                return scoreL < scoreR;
            }
            return source_left.row() < source_right.row();
        }

        if (m_sortMode == SortColorLabel) {
            if (m_colorLabelRole == -1) m_colorLabelRole = roleNames().key("colorLabel", -1);
            QString labelL = (m_colorLabelRole != -1) ? sourceModel()->data(source_left, m_colorLabelRole).toString() : QString();
            QString labelR = (m_colorLabelRole != -1) ? sourceModel()->data(source_right, m_colorLabelRole).toString() : QString();

            auto colorRank = [](const QString& lbl) -> int {
                if (lbl == "Red")    return 1;
                if (lbl == "Yellow") return 2;
                if (lbl == "Green")  return 3;
                if (lbl == "Blue")   return 4;
                if (lbl == "Purple") return 5;
                return 6;
            };

            int rankL = colorRank(labelL);
            int rankR = colorRank(labelR);
            if (rankL != rankR) {
                return rankL < rankR;
            }
            if (m_ratingRole == -1) m_ratingRole = roleNames().key("rating", -1);
            int ratingL = (m_ratingRole != -1) ? sourceModel()->data(source_left, m_ratingRole).toInt() : 0;
            int ratingR = (m_ratingRole != -1) ? sourceModel()->data(source_right, m_ratingRole).toInt() : 0;
            if (ratingL != ratingR) {
                return ratingL > ratingR;
            }
            return source_left.row() < source_right.row();
        }

        return source_left.row() < source_right.row();
    }

signals:
    void sourceChanged();
    void groupBurstsChanged();
    void countChanged();
    void sortModeChanged();
    void colorLabelFilterChanged();
    void ratingFilterChanged();

private:
    QObject* m_source = nullptr;
    bool m_groupBursts = true;
    mutable int m_isLeadRole = -1;
    mutable int m_isExpandedRole = -1;
    SortMode m_sortMode = SortDefault;
    mutable int m_scoreRole = -1;
    mutable int m_ratingRole = -1;
    QString m_colorLabelFilter;               // "" = show all
    int m_ratingFilter = 0;                   // 0 = show all
    mutable int m_colorLabelRole = -1;
};

class FullImageProvider;

class AppBackend : public QObject {
    Q_OBJECT
    
    // UI Properties
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(int totalFiles READ totalFiles NOTIFY totalFilesChanged)
    Q_PROPERTY(int acceptedCount READ acceptedCount NOTIFY acceptedCountChanged)
    Q_PROPERTY(int rejectedCount READ rejectedCount NOTIFY rejectedCountChanged)
    
    // Settings Properties
    Q_PROPERTY(int themeMode READ themeMode WRITE setThemeMode NOTIFY themeModeChanged)
    Q_PROPERTY(bool writeExif READ writeExif WRITE setWriteExif NOTIFY writeExifChanged)

    Q_PROPERTY(int rawViewMode READ rawViewMode WRITE setRawViewMode NOTIFY rawViewModeChanged)
    Q_PROPERTY(int rawAnalysisMode READ rawAnalysisMode WRITE setRawAnalysisMode NOTIFY rawAnalysisModeChanged)
    Q_PROPERTY(QString histogramBase64 READ histogramBase64 NOTIFY histogramUpdated)
    Q_PROPERTY(bool groupBursts READ groupBursts WRITE setGroupBursts NOTIFY groupBurstsChanged)

    // LUT properties
    Q_PROPERTY(bool lutEnabled READ lutEnabled WRITE setLutEnabled NOTIFY lutEnabledChanged)
    Q_PROPERTY(QString activeLutName READ activeLutName NOTIFY activeLutChanged)
    Q_PROPERTY(QStringList availableLuts READ availableLuts NOTIFY activeLutChanged)

    // External editor
    Q_PROPERTY(QString externalEditorPath READ externalEditorPath WRITE setExternalEditorPath NOTIFY externalEditorPathChanged)

    // AI aesthetic display settings
    Q_PROPERTY(bool showAiScore READ showAiScore WRITE setShowAiScore NOTIFY showAiScoreChanged)
    Q_PROPERTY(bool colorAiScore READ colorAiScore WRITE setColorAiScore NOTIFY colorAiScoreChanged)
    Q_PROPERTY(bool applyUserBias READ applyUserBias WRITE setApplyUserBias NOTIFY applyUserBiasChanged)

public:
    explicit AppBackend(QObject *parent = nullptr);
    ~AppBackend();

    Q_INVOKABLE void selectFolder(const QString &folderPath);
    Q_INVOKABLE void startScan();
    Q_INVOKABLE void cancelScan();
    Q_INVOKABLE bool trashFile(const QString &filePath);
    Q_INVOKABLE QVariantMap getPhotoMetadata(const QString& filePath);
    Q_INVOKABLE int getPhotoRating(const QString& filePath);
    Q_INVOKABLE void setPhotoRating(const QString& filePath, int rating, float baseScore = 0.0f);

    // Color labels — "" | "Red" | "Yellow" | "Green" | "Blue" | "Purple"
    Q_INVOKABLE QString getPhotoColorLabel(const QString& filePath);
    Q_INVOKABLE void    setPhotoColorLabel(const QString& filePath, const QString& label);

    Q_INVOKABLE QString logFilePath() const;

    // Fast Viewer Preloading
    void setFullImageProvider(FullImageProvider* provider);
    Q_INVOKABLE void preloadViewerWindow(int currentIndex);
    Q_INVOKABLE void preloadImage(const QString& filePath);
    Q_INVOKABLE void clearViewerPreloadCache();

    // Cache Management
    Q_INVOKABLE QVariantList getCachedFoldersList() const;
    Q_INVOKABLE void deleteCacheFolders(const QStringList& hashes);
    Q_INVOKABLE void clearAllCacheData();
    Q_INVOKABLE QString getTotalCacheSizeString() const;

    // External editor — open one or more files in the configured application
    Q_INVOKABLE void openInExternalEditor(const QStringList& filePaths);
    QString externalEditorPath() const { return m_externalEditorPath; }
    void setExternalEditorPath(const QString& path);

    // LUT management
    Q_INVOKABLE void loadLutFile(const QString& filePath);
    Q_INVOKABLE void selectLutPreset(const QString& name); // "none" or filename

    Q_PROPERTY(BurstFilterProxyModel* burstProxy READ burstProxy CONSTANT)
    BurstFilterProxyModel* burstProxy() { return &m_burstProxy; }

    void updateHistogramFromImage(const QImage& image);
    QString histogramBase64() const { return m_histogramBase64; }

    QImage applyViewerLut(const QImage& image) const;

    // LUT getters/setters
    bool lutEnabled() const { return m_lutEnabled; }
    void setLutEnabled(bool v);
    QString activeLutName() const { return m_activeLutName; }
    QStringList availableLuts() const;

    QString statusText() const;
    int progress() const;
    int totalFiles() const;
    int acceptedCount() const;
    int rejectedCount() const;
    
    // Settings Getters and Setters
    int themeMode() const; 
    void setThemeMode(int mode);
    
    bool writeExif() const; 
    void setWriteExif(bool write);
    
    int rawViewMode() const; 
    void setRawViewMode(int mode);
    
    int rawAnalysisMode() const; 
    void setRawAnalysisMode(int mode);

    bool groupBursts() const { return m_groupBursts; }
    void setGroupBursts(bool group);

    Q_PROPERTY(PipelineConfigModel* pipelineModel READ pipelineModel CONSTANT)
    PipelineConfigModel* pipelineModel() { return &m_pipelineModel; }

    Q_PROPERTY(PreprocessorConfigModel* preprocessorModel READ preprocessorModel CONSTANT)
    PreprocessorConfigModel* preprocessorModel() { return &m_preprocessorModel; }

    Q_PROPERTY(PostprocessorConfigModel* postprocessorModel READ postprocessorModel CONSTANT)
    PostprocessorConfigModel* postprocessorModel() { return &m_postprocessorModel; }

    // AI aesthetic display settings
    bool showAiScore() const { return m_showAiScore; }
    void setShowAiScore(bool v);
    bool colorAiScore() const { return m_colorAiScore; }
    void setColorAiScore(bool v);
    bool applyUserBias() const { return m_applyUserBias; }
    void setApplyUserBias(bool v);

    // Semaphore toggle: "visual_hash" | "clip_embedding" | "none"
    // Exactly one of the two grouping algorithms can be active at a time.
    Q_INVOKABLE void setGroupingMode(const QString& mode);

signals:
    void statusTextChanged();
    void progressChanged();
    void totalFilesChanged();
    void acceptedCountChanged();
    void rejectedCountChanged();
    
    void themeModeChanged();
    void writeExifChanged();
    void rawViewModeChanged();
    void rawAnalysisModeChanged();

    void showAiScoreChanged();
    void colorAiScoreChanged();
    void applyUserBiasChanged();

    void groupBurstsChanged();

    void lutEnabledChanged();
    void activeLutChanged();
    void externalEditorPathChanged();
    
    void fileFound(const QString &fileName, const QString &filePath, int index);
    void fileProcessed(int index, bool isRejected, QString rejectReason, float aestheticScore, int width, int height);
    void scanFinished();

    void histogramUpdated();
    void groupAssigned(int index, int leadIndex, bool isLead, int groupSize);
    void bestShotAssigned(int index, bool isBestShot, int leadIndex);
    // Emitted after setPhotoColorLabel so QML can update grid badges live
    void colorLabelChanged(const QString& filePath, const QString& label);
    void photoRatingChanged(const QString& filePath, int rating);
    // Emitted from background thread after XMP is read for a file on folder open
    void fileMetadataLoaded(int index, int rating, const QString& colorLabel);

private:
    QString m_statusText;
    int m_progress = 0;
    int m_totalFiles = 0;
    int m_acceptedCount = 0;
    int m_rejectedCount = 0;
    QString m_currentFolder;

    std::atomic<bool> m_isScanning{false};
    std::atomic<bool> m_cancelRequested{false};
    std::thread m_scanThread;
    std::thread m_metaThread;   // background XMP read on folder open
    
    std::vector<QString> m_files;

    // Settings Variables
    int m_themeMode = 0;
    bool m_writeExif = false;
    int m_rawViewMode = 0;
    int m_rawAnalysisMode = 0;

    bool m_groupBursts = true;
    BurstFilterProxyModel m_burstProxy;
    FullImageProvider* m_fullImageProvider = nullptr;

    void setStatusText(const QString &text);
    void setProgress(int value);
    void setTotalFiles(int value);
    void setAcceptedCount(int value);
    void setRejectedCount(int value);
    void runScannerTask();
    
    void loadSettings();
    void saveSettings();
    QString getSettingsFilePath() const;
    QString getLutsDir() const;           // user-writable LUTs directory
    QString getLutsSystemDir() const;     // read-only system LUTs directory (Linux only)
    QString resolveLutPath(const QString& baseName) const; // finds file in user dir, then system dir
    bool m_loadingSettings = false;

    QString m_histogramBase64;
    std::vector<uint64_t> m_hashes;
    std::vector<std::vector<float>> m_clipVectors; // 512-float CLIP embeddings, parallel to m_files
    std::vector<float> m_aestheticScores; // AI aesthetic scores, parallel to m_files
    std::vector<bool> m_isRejected; // Rejected status, parallel to m_files

    PipelineConfigModel m_pipelineModel;

    // Preprocessor config
    PreprocessorConfigModel m_preprocessorModel;

    // Postprocessor config
    PostprocessorConfigModel m_postprocessorModel;

    // External editor
    QString m_externalEditorPath;

    // LUT state
    bool            m_lutEnabled    = false;
    QString         m_activeLutName = "none";  
    // Cached LUT data for the viewer (loaded on demand)
    mutable std::vector<float> m_viewerLutData;
    mutable int                m_viewerLutDim = 33;
    void reloadViewerLut() const;

    PipelineRunner createPipeline();

    // AI aesthetic display settings
    bool m_showAiScore = true;
    bool m_colorAiScore = true;
    bool m_applyUserBias = true;
    void syncAiSettingsFromModel();

    mutable std::mutex         m_metaMutex;    // guards m_ratings and m_colorLabels
    std::map<QString, int>     m_ratings;
    std::map<QString, QString> m_colorLabels;  // filePath -> "Red"|"Yellow"|"Green"|"Blue"|"Purple"|""
    void loadRatings();
    void saveRatings();
};