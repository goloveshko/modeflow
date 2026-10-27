#include "LogViewerDialog.h"

#include "ui_LogViewerDialog.h"

#include <QClipboard>
#include <QDir>
#include <QProcess>
#include <QScrollBar>
#include <QTextCursor>

#include "Constants.h"
#include "FontAwesome.h"
#include "IDialogManager.h"
#include "ISettingsManager.h"
#include "IStyleManager.h"
#include "LogHighlighter.h"
#include "LogManager.h"

namespace ModeFlow::Gui {

using namespace Qt::StringLiterals;

namespace {
constexpr int RefreshIntervalMs = 500;
constexpr int FilterDebounceMs = 150;
} // namespace

LogViewerDialog::LogViewerDialog(Core::IDialogManager* dialogManager, Core::ISettingsManager* settingsManager,
                                 Core::IStyleManager* styleManager, QWidget* parent)
    : BaseDialog(styleManager, parent), ui(std::make_unique<Ui::LogViewerDialog>()), m_dialogManager(dialogManager),
      m_settingsManager(settingsManager) {
    Q_ASSERT(m_dialogManager);
    Q_ASSERT(m_settingsManager);
    ui->setupUi(this);
    init();
}

LogViewerDialog::~LogViewerDialog() = default;

void LogViewerDialog::init() {
    m_logFilePath = Utils::LogManager::logFilePath();

    QFont monoFont(u"Consolas"_s);
    monoFont.setStyleHint(QFont::Monospace);
    monoFont.setPointSize(10);
    ui->textLog->setFont(monoFont);
    ui->textLog->setMaximumBlockCount(Utils::MaxViewerEntries);

    m_highlighter = new LogHighlighter(ui->textLog->document());

    ui->comboLevel->addItem(tr("All Levels"), -1);
    ui->comboLevel->addItem(tr("Debug"), 0);
    ui->comboLevel->addItem(tr("Info"), 1);
    ui->comboLevel->addItem(tr("Warning"), 2);
    ui->comboLevel->addItem(tr("Critical"), 3);
    ui->comboLevel->addItem(tr("Fatal"), 4);

    ui->comboCategory->addItem(tr("All Categories"), QString());

    ui->labelEmptyIcon->setFont(QFont(FontAwesome::fontFamily(), 36));
    ui->labelEmptyIcon->setText(FontAwesome::Terminal);

    ui->btnFollow->setIcon(FontAwesome::icon(FontAwesome::ArrowDown, 16));
    ui->btnRefresh->setIcon(FontAwesome::icon(FontAwesome::RotateRight, 16));
    ui->btnClear->setIcon(FontAwesome::icon(FontAwesome::Trash, 16));
    ui->btnCopy->setIcon(FontAwesome::icon(FontAwesome::Copy, 16));
    ui->btnSave->setIcon(FontAwesome::icon(FontAwesome::FloppyDisk, 16));
    ui->btnOpenFolder->setIcon(FontAwesome::icon(FontAwesome::FolderOpen, 16));

    m_refreshTimer.setInterval(RefreshIntervalMs);
    m_refreshTimer.setSingleShot(false);

    m_filterTimer.setSingleShot(true);
    m_filterTimer.setInterval(FilterDebounceMs);

    setupConnections();
    updateRecordButtonVisuals();
    updateViewMode();

    connect(ui->btnClose, &QPushButton::clicked, this, &LogViewerDialog::accept);
}

void LogViewerDialog::setupConnections() {
    connect(&m_refreshTimer, &QTimer::timeout, this, &LogViewerDialog::onTimerTick);
    connect(&m_filterTimer, &QTimer::timeout, this, &LogViewerDialog::onApplyFilters);

    connect(ui->btnRecordLog, &QToolButton::toggled, this, &LogViewerDialog::onRecordToggled);
    connect(ui->btnFollow, &QToolButton::toggled, this, &LogViewerDialog::onFollowToggled);
    connect(ui->btnRefresh, &QToolButton::clicked, this, &LogViewerDialog::onRefreshClicked);
    connect(ui->btnClear, &QToolButton::clicked, this, &LogViewerDialog::onClearClicked);
    connect(ui->btnCopy, &QToolButton::clicked, this, &LogViewerDialog::onCopyClicked);
    connect(ui->btnSave, &QToolButton::clicked, this, &LogViewerDialog::onSaveClicked);
    connect(ui->btnOpenFolder, &QToolButton::clicked, this, &LogViewerDialog::onOpenFolderClicked);

    connect(ui->btnEnableLogging, &QPushButton::clicked, this, &LogViewerDialog::onEnableLoggingClicked);

    connect(ui->editFilter, &QLineEdit::textChanged, this, &LogViewerDialog::onFilterTextChanged);
    connect(ui->comboLevel, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &LogViewerDialog::onLevelFilterChanged);
    connect(ui->comboCategory, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &LogViewerDialog::onCategoryFilterChanged);

    connect(ui->textLog->verticalScrollBar(), &QScrollBar::valueChanged, this,
            &LogViewerDialog::onScrollBarValueChanged);
}

void LogViewerDialog::updateViewMode() {
    const bool loggingActive = m_settingsManager->loggingEnabled();
    updateRecordButtonVisuals();

    const QFileInfo fi(m_logFilePath);
    const bool hasLogContent = fi.exists() && fi.size() > 0;

    if (!loggingActive && !hasLogContent) {
        ui->stackedWidget->setCurrentIndex(1); // Show Zero State card
        m_refreshTimer.stop();
    } else {
        ui->stackedWidget->setCurrentIndex(0); // Show log view
        loadLogFile();
        m_refreshTimer.start();
    }
    updateStatusBar();
}

void LogViewerDialog::updateRecordButtonVisuals() {
    const bool active = m_settingsManager->loggingEnabled();

    {
        const QSignalBlocker blocker(ui->btnRecordLog);
        ui->btnRecordLog->setChecked(active);
    }

    ui->btnRecordLog->setIcon(FontAwesome::icon(FontAwesome::Circle, 14));
    ui->btnRecordLog->setToolTip(active ? tr("Logging active (click to disable)") : tr("Enable diagnostic logging"));
}

void LogViewerDialog::showEvent(QShowEvent* event) {
    BaseDialog::showEvent(event);
    m_refreshTimer.start();
    updateStatusBar();
}

void LogViewerDialog::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);
        updateRecordButtonVisuals();
    }
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ThemeChange ||
        event->type() == QEvent::StyleChange) {
        if (m_highlighter) {
            m_highlighter->updateColors();
        }
        ui->labelEmptyIcon->setFont(QFont(FontAwesome::fontFamily(), 36));
        ui->labelEmptyIcon->setText(FontAwesome::Terminal);
        updateRecordButtonVisuals();
    }
    BaseDialog::changeEvent(event);
}

LogViewerDialog::LogEntry LogViewerDialog::parseLine(const QString& line) {
    LogEntry entry;
    entry.rawLine = line;

    if (line.isEmpty() || !line.startsWith(u'['))
        return entry;

    const QStringView lineView(line);

    const int tsEnd = line.indexOf(u']');
    if (tsEnd <= 0)
        return entry;

    entry.timestamp = lineView.mid(1, tsEnd - 1).toString();

    const int restStart = line.indexOf(u'[', tsEnd + 1);
    if (restStart <= 0)
        return entry;

    const int lvlEnd = line.indexOf(u']', restStart);
    if (lvlEnd <= 0)
        return entry;

    const QStringView levelStr = lineView.mid(restStart + 1, lvlEnd - restStart - 1).trimmed();
    if (levelStr == u"DEBUG"_sv)
        entry.level = 0;
    else if (levelStr.startsWith(u"INFO"_sv))
        entry.level = 1;
    else if (levelStr.startsWith(u"WARN"_sv))
        entry.level = 2;
    else if (levelStr.startsWith(u"CRIT"_sv))
        entry.level = 3;
    else if (levelStr == u"FATAL"_sv)
        entry.level = 4;
    else
        entry.level = 1;

    const int catStart = line.indexOf(u'[', lvlEnd + 1);
    if (catStart <= 0)
        return entry;

    const int catEnd = line.indexOf(u']', catStart);
    if (catEnd <= 0)
        return entry;

    entry.category = lineView.mid(catStart + 1, catEnd - catStart - 1).trimmed().toString();

    const int funcStart = line.indexOf(u'[', catEnd + 1);
    if (funcStart > 0) {
        const int funcEnd = line.indexOf(u']', funcStart);
        if (funcEnd > 0) {
            entry.function = lineView.mid(funcStart + 1, funcEnd - funcStart - 1).trimmed().toString();
            entry.message = lineView.mid(funcEnd + 2).toString();
        } else {
            entry.message = lineView.mid(catEnd + 2).toString();
        }
    } else {
        entry.message = lineView.mid(catEnd + 2).toString();
    }

    return entry;
}

qint64 LogViewerDialog::tailStartOffset(qint64 fileSize) {
    if (fileSize <= Utils::MaxViewerTailBytes)
        return 0;

    return fileSize - Utils::MaxViewerTailBytes;
}

qsizetype LogViewerDialog::entriesToDrop(qsizetype total) {
    if (total <= Utils::MaxViewerEntries)
        return 0;

    return total - Utils::MaxViewerEntries;
}

LogViewerDialog::LoadAction LogViewerDialog::loadAction(qint64 newSize, qint64 consumedBytes) {
    if (newSize < consumedBytes)
        return LoadAction::Reload;

    if (newSize == consumedBytes)
        return LoadAction::Nothing;

    return LoadAction::Append;
}

void LogViewerDialog::loadLogFile() {
    m_allEntries.clear();
    m_filteredIndices.clear();

    QFile file(m_logFilePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_fileSize = 0;
        return;
    }

    m_fileSize = file.size();
    const qint64 startOffset = tailStartOffset(m_fileSize);
    if (startOffset > 0) {
        // A long session is read from its tail, which bounds the parse and the strings it produces.
        // The offset lands inside a line, so that fragment is dropped instead of shown as an entry.
        file.seek(startOffset);
    }

    QTextStream stream(&file);
    if (startOffset > 0)
        stream.readLine();

    QSet<QString> categories;
    while (!stream.atEnd()) {
        QString line = stream.readLine();
        if (line.isEmpty())
            continue;

        LogEntry entry = parseLine(line);
        if (!entry.category.isEmpty())
            categories.insert(entry.category);
        m_allEntries.append(entry);
    }

    if (const qsizetype dropped = entriesToDrop(m_allEntries.size()); dropped > 0)
        m_allEntries.remove(0, dropped);

    refreshCategories(categories);
    onApplyFilters();
    updateStatusBar();
}

void LogViewerDialog::incrementalLoad() {
    QFile file(m_logFilePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    const qint64 newSize = file.size();
    if (const LoadAction action = loadAction(newSize, m_fileSize); action != LoadAction::Append) {
        if (action == LoadAction::Reload) {
            // The file shrank: rotation or a cleared log. Every buffered line comes from a generation
            // that is no longer shown, so the only correct move is to read the current tail again.
            loadLogFile();
        }
        return;
    }

    file.seek(m_fileSize);
    m_fileSize = newSize;

    const bool plainView = ui->editFilter->text().isEmpty() && ui->comboLevel->currentData().toInt() == -1 &&
                           ui->comboCategory->currentData().toString().isEmpty();

    QTextStream stream(&file);
    QSet<QString> newCategories;
    QString appendedText;
    while (!stream.atEnd()) {
        QString line = stream.readLine();
        if (line.isEmpty())
            continue;

        LogEntry entry = parseLine(line);
        if (!entry.category.isEmpty())
            newCategories.insert(entry.category);
        m_allEntries.append(entry);

        // Captured while reading: the buffer may shed its oldest lines below, and the indices of the
        // new entries would move with them.
        if (plainView) {
            appendedText += line;
            appendedText += u'\n';
        }
    }

    if (const qsizetype dropped = entriesToDrop(m_allEntries.size()); dropped > 0)
        m_allEntries.remove(0, dropped);

    if (!newCategories.isEmpty()) {
        QSet<QString> existingCategories;
        for (const auto& entry : m_allEntries) {
            if (!entry.category.isEmpty())
                existingCategories.insert(entry.category);
        }
        for (const auto& cat : newCategories) {
            existingCategories.insert(cat);
        }
        refreshCategories(existingCategories);
    }

    if (!plainView) {
        onApplyFilters();
        return;
    }

    // Without filters the document simply mirrors the buffer, so the new lines are appended instead
    // of re-rendering everything the viewer holds. QPlainTextEdit drops leading blocks at the same
    // cap the buffer uses, which keeps the two aligned.
    m_filteredIndices.clear();
    m_filteredIndices.reserve(m_allEntries.size());
    for (qsizetype i = 0; i < m_allEntries.size(); ++i) {
        m_filteredIndices.append(static_cast<int>(i));
    }

    appendToDisplay(appendedText);
    updateStatusBar();
}

void LogViewerDialog::refreshCategories(const QSet<QString>& categories) {
    const QString current = ui->comboCategory->currentData().toString();

    {
        const QSignalBlocker blocker(ui->comboCategory);
        ui->comboCategory->clear();
        ui->comboCategory->addItem(tr("All Categories"), QString());

        QStringList sorted = categories.values();
        sorted.sort(Qt::CaseInsensitive);
        for (const auto& cat : sorted) {
            ui->comboCategory->addItem(cat, cat);
        }

        const int idx = ui->comboCategory->findData(current);
        if (idx >= 0)
            ui->comboCategory->setCurrentIndex(idx);
    }
}

void LogViewerDialog::onApplyFilters() {
    m_filteredIndices.clear();

    const QString textFilter = ui->editFilter->text();
    const int levelFilter = ui->comboLevel->currentData().toInt();
    const QString categoryFilter = ui->comboCategory->currentData().toString();

    m_filteredIndices.reserve(m_allEntries.size());
    for (int i = 0; i < m_allEntries.size(); ++i) {
        const auto& entry = m_allEntries.at(i);

        if (!textFilter.isEmpty() && !entry.rawLine.contains(textFilter, Qt::CaseInsensitive))
            continue;
        if (levelFilter != -1 && entry.level != levelFilter)
            continue;
        if (!categoryFilter.isEmpty() && entry.category != categoryFilter)
            continue;

        m_filteredIndices.append(i);
    }

    rebuildDisplay();
    updateStatusBar();
}

void LogViewerDialog::rebuildDisplay() {
    m_isRefreshing = true;

    QString plainText;
    plainText.reserve(m_filteredIndices.size() * 150);

    for (int idx : m_filteredIndices) {
        plainText += m_allEntries.at(idx).rawLine;
        plainText += u'\n';
    }

    ui->textLog->setPlainText(plainText);

    m_isRefreshing = false;

    if (m_followMode)
        scrollToBottom();
}

void LogViewerDialog::appendToDisplay(const QString& text) {
    if (text.isEmpty())
        return;

    m_isRefreshing = true;
    QTextCursor cursor(ui->textLog->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(text);
    m_isRefreshing = false;

    if (m_followMode)
        scrollToBottom();
}

void LogViewerDialog::onTimerTick() {
    incrementalLoad();
}

void LogViewerDialog::onFollowToggled(bool checked) {
    m_followMode = checked;
    if (m_followMode) {
        m_userIsScrolledUp = false;
        scrollToBottom();
    }
}

void LogViewerDialog::onScrollBarValueChanged(int value) {
    if (m_isRefreshing)
        return;

    const QScrollBar* scrollBar = ui->textLog->verticalScrollBar();
    const bool atBottom = (value >= scrollBar->maximum() - 10);

    if (!atBottom && m_followMode) {
        m_userIsScrolledUp = true;
    } else if (atBottom) {
        m_userIsScrolledUp = false;
    }
}

void LogViewerDialog::onRefreshClicked() {
    loadLogFile();
}

void LogViewerDialog::onClearClicked() {
    m_allEntries.clear();
    m_filteredIndices.clear();
    ui->textLog->clear();
    updateStatusBar();
}

void LogViewerDialog::onCopyClicked() {
    QString text;
    text.reserve(m_filteredIndices.size() * 150);
    for (int idx : m_filteredIndices) {
        text += m_allEntries.at(idx).rawLine;
        text += u'\n';
    }
    QApplication::clipboard()->setText(text);
}

void LogViewerDialog::onSaveClicked() {
    const QString filePath = m_dialogManager->getSaveFileName(this, tr("Save Log"), u"log_export.txt"_s,
                                                              tr("Text files (*.txt);;All files (*)"));
    if (filePath.isEmpty())
        return;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_dialogManager->showWarning(this, tr("Error"), tr("Cannot save to file:\n%1").arg(filePath));
        return;
    }

    QTextStream stream(&file);
    for (int idx : m_filteredIndices) {
        stream << m_allEntries.at(idx).rawLine << u'\n';
    }

    m_dialogManager->showInfo(this, tr("Saved"),
                              tr("Log exported successfully.\n%1 lines written.").arg(m_filteredIndices.size()));
}

void LogViewerDialog::onOpenFolderClicked() {
    if (m_logFilePath.isEmpty() || !QFile::exists(m_logFilePath)) {
        m_dialogManager->showWarning(this, tr("Log File"), tr("Log file does not exist yet."));
        return;
    }

    const QString nativePath = QDir::toNativeSeparators(m_logFilePath);
    QProcess::startDetached(u"explorer.exe"_s, {u"/select,"_s, nativePath});
}

void LogViewerDialog::onEnableLoggingClicked() {
    m_settingsManager->setLoggingEnabled(true);
    Utils::LogManager::setup(true);
    updateViewMode();
}

void LogViewerDialog::onRecordToggled(bool checked) {
    m_settingsManager->setLoggingEnabled(checked);
    Utils::LogManager::setup(checked);
    updateViewMode();
}

void LogViewerDialog::onFilterTextChanged(const QString&) {
    m_filterTimer.start();
}

void LogViewerDialog::onLevelFilterChanged(int) {
    m_filterTimer.start();
}

void LogViewerDialog::onCategoryFilterChanged(int) {
    m_filterTimer.start();
}

void LogViewerDialog::scrollToBottom() {
    if (!m_followMode || m_userIsScrolledUp)
        return;

    QScrollBar* scrollBar = ui->textLog->verticalScrollBar();
    scrollBar->setValue(scrollBar->maximum());
}

void LogViewerDialog::updateStatusBar() {
    const bool loggingActive = m_settingsManager->loggingEnabled();
    const QFileInfo fi(m_logFilePath);
    const bool fileExists = fi.exists();

    if (!loggingActive && !fileExists) {
        ui->labelStatus->setText(tr("Logging is disabled"));
    } else if (!fileExists) {
        ui->labelStatus->setText(tr("Log file not found: %1").arg(m_logFilePath));
    } else {
        const int total = m_allEntries.size();
        const int shown = m_filteredIndices.size();
        ui->labelStatus->setText(tr("Lines: %1/%2").arg(shown).arg(total));
    }
}

QString LogViewerDialog::levelToString(int level) {
    switch (level) {
    case 0:
        return u"DEBUG"_s;
    case 1:
        return u"INFO "_s;
    case 2:
        return u"WARN "_s;
    case 3:
        return u"CRIT "_s;
    case 4:
        return u"FATAL"_s;
    default:
        return u"????? "_s;
    }
}

} // namespace ModeFlow::Gui