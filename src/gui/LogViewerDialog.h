#pragma once

#include <QTimer>

#include "BaseDialog.h"

namespace Ui {
class LogViewerDialog;
}

namespace ModeFlow::Core {
class IDialogManager;
class ISettingsManager;
} // namespace ModeFlow::Core

namespace ModeFlow::Gui {

class LogHighlighter;

class LogViewerDialog : public BaseDialog {
    Q_OBJECT

public:
    explicit LogViewerDialog(Core::IDialogManager* dialogManager, Core::ISettingsManager* settingsManager,
                             Core::IStyleManager* styleManager, QWidget* parent = nullptr);
    ~LogViewerDialog() override;

    /// What the next viewer pass has to do with the log file.
    enum class LoadAction { Nothing, Append, Reload };

    /**
     * @brief Offset where a bounded read of the log tail starts.
     * @param fileSize Size of the log file in bytes.
     * @return 0 when the file fits the read budget, otherwise its last budget of bytes.
     */
    static qint64 tailStartOffset(qint64 fileSize);

    /**
     * @brief How many of the oldest lines the display buffer gives up after an append.
     * @param total Number of entries held once the newest lines were added.
     * @return Entries to remove from the front, 0 while the buffer is under its cap.
     */
    static qsizetype entriesToDrop(qsizetype total);

    /**
     * @brief Plans the next pass against the size the viewer already read.
     * @param newSize Size of the log file now.
     * @param consumedBytes Size reached by the previous pass.
     * @return Reload when the file shrank, which is how rotation looks from the viewer.
     */
    static LoadAction loadAction(qint64 newSize, qint64 consumedBytes);

protected:
    void showEvent(QShowEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    struct LogEntry {
        QString rawLine;
        QString timestamp;
        QString category;
        QString function;
        QString message;
        int level = 1; // 0: DEBUG, 1: INFO, 2: WARN, 3: CRIT, 4: FATAL
    };

    void init();
    void setupConnections();
    void updateViewMode();
    void loadLogFile();
    void incrementalLoad();
    void rebuildDisplay();
    void appendToDisplay(const QString& text);
    void updateStatusBar();
    void scrollToBottom();
    void refreshCategories(const QSet<QString>& categories);
    void updateRecordButtonVisuals();

    static LogEntry parseLine(const QString& line);
    static QString levelToString(int level);

    void onTimerTick();
    void onFollowToggled(bool checked);
    void onRefreshClicked();
    void onClearClicked();
    void onCopyClicked();
    void onSaveClicked();
    void onOpenFolderClicked();
    void onFilterTextChanged(const QString& text);
    void onLevelFilterChanged(int index);
    void onCategoryFilterChanged(int index);
    void onApplyFilters();
    void onEnableLoggingClicked();
    void onRecordToggled(bool checked);
    void onScrollBarValueChanged(int value);

    std::unique_ptr<Ui::LogViewerDialog> ui;
    Core::ISettingsManager* m_settingsManager = nullptr;
    Core::IDialogManager* m_dialogManager = nullptr;
    LogHighlighter* m_highlighter = nullptr;
    QString m_logFilePath;
    QList<LogEntry> m_allEntries;
    QList<int> m_filteredIndices;

    QTimer m_refreshTimer;
    QTimer m_filterTimer;
    qint64 m_fileSize = 0;
    bool m_followMode = true;
    bool m_userIsScrolledUp = false;
    bool m_isRefreshing = false;
};

} // namespace ModeFlow::Gui