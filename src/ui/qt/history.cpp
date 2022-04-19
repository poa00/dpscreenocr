
#include "history.h"

#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

#include "dpso/dpso.h"
#include "dpso_intl/dpso_intl.h"

#include "ui_common/ui_common.h"

#include "utils.h"


#define _(S) gettext(S)


History::History(const std::string& cfgDirPath, QWidget* parent)
    : QWidget{parent}
    , nativeFileDialogs{}
{
    historyFilePath = (
        cfgDirPath + *dpsoDirSeparators + historyFileName);

    wordWrapCheck = new QCheckBox(_("Wrap words"));
    wordWrapCheck->setChecked(true);
    connect(
        wordWrapCheck, SIGNAL(toggled(bool)),
        this, SLOT(setWordWrap(bool)));

    textEdit = new QTextEdit();
    textEdit->setReadOnly(true);
    textEdit->setUndoRedoEnabled(false);
    textEdit->setWordWrapMode(QTextOption::WordWrap);

    blockFormat.setLineHeight(
        140, QTextBlockFormat::ProportionalHeight);
    blockMargin = QFontMetrics(charFormat.font()).height();

    clearButton = new QPushButton(_("Clear"));
    connect(
        clearButton, SIGNAL(clicked()),
        this, SLOT(clear()));

    saveAsButton = new QPushButton(_("Save as\342\200\246"));
    saveAsButton->setToolTip(
        _("Save the history as plain text, HTML, or JSON"));
    connect(
        saveAsButton, SIGNAL(clicked()),
        this, SLOT(saveAs()));

    setButtonsEnabled(false);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins({});

    layout->addWidget(wordWrapCheck);
    layout->addWidget(textEdit);

    auto* buttonsLayout = new QHBoxLayout();
    buttonsLayout->addWidget(clearButton);
    buttonsLayout->addWidget(saveAsButton);
    buttonsLayout->addStretch();
    layout->addLayout(buttonsLayout);

    lastFileName = _("History");
}


History::DynamicStrings::DynamicStrings()
    : clearQuestion{_("Clear the history?")}
    , cancel{_("Cancel")}
    , clear{_("Clear")}
    , saveHistory{_("Save history")}
    , nameFilters{
        QString{_("Plain text")} + " (*.txt);;"
        + "HTML (*.html *.htm);;"
        + "JSON (*.json);;"
        + _("All files") + " (*)"}
{
}


void History::setButtonsEnabled(bool enabled)
{
    clearButton->setEnabled(enabled);
    saveAsButton->setEnabled(enabled);
}


void History::setWordWrap(bool wordWrap)
{
    textEdit->setWordWrapMode(
        wordWrap ? QTextOption::WordWrap : QTextOption::NoWrap);
}


void History::clear()
{
    if (!history)
        return;

    if (!confirmation(
            this,
            dynStr.clearQuestion, dynStr.cancel, dynStr.clear))
        return;

    if (!dpsoHistoryClear(history.get())) {
        QMessageBox::critical(
            this,
            appName,
            QString("Can't clear history: ") + dpsoGetError());
        return;
    }

    textEdit->clear();
    setButtonsEnabled(false);
}


void History::saveAs()
{
    if (!history)
        return;

    QFileDialog::Options options;
    if (!nativeFileDialogs)
        options |= QFileDialog::DontUseNativeDialog;

    auto dirPath = lastDirPath;
    // Don't pass an empty path to QDir since in this case QDir points
    // to the current working directory.
    if (dirPath.isEmpty() || !QDir(dirPath).exists())
        dirPath = QDir::homePath();

    const auto filePath = QDir::toNativeSeparators(
        QFileDialog::getSaveFileName(
            this,
            dynStr.saveHistory,
            QDir(dirPath).filePath(lastFileName),
            dynStr.nameFilters,
            &selectedNameFilter,
            options));
    if (filePath.isEmpty())
        return;

    const auto filePathUtf8 = filePath.toUtf8();
    if (!dpsoHistoryExport(
            history.get(),
            filePathUtf8.data(),
            dpsoHistoryDetectExportFormat(
                filePathUtf8.data(),
                dpsoHistoryExportFormatPlainText)))
        QMessageBox::critical(
            this,
            appName,
            QString("Can't save \"%1\": %2").arg(
                filePath, dpsoGetError()));

    const QFileInfo fileInfo(filePath);

    lastFileName = fileInfo.fileName();

    lastDirPath = QDir::toNativeSeparators(
        fileInfo.dir().absolutePath());
}


void History::append(const char* timestamp, const char* text)
{
    if (!history)
        return;

    const DpsoHistoryEntry entry{timestamp, text};
    if (!dpsoHistoryAppend(history.get(), &entry)) {
        QMessageBox::critical(
            this,
            appName,
            QString("Can't append to history: ") + dpsoGetError());
        return;
    }

    appendToTextEdit(timestamp, text);

    setButtonsEnabled(true);
}


void History::appendToTextEdit(
    const char* timestamp, const char* text)
{
    auto cursor = textEdit->textCursor();
    cursor.movePosition(QTextCursor::End);

    charFormat.setFontWeight(QFont::Bold);
    blockFormat.setLeftMargin(0);
    blockFormat.setRightMargin(0);

    if (textEdit->document()->isEmpty()) {
        // An empty document still has a block. Reuse it so it doesn't
        // result in an empty line.
        cursor.setBlockFormat(blockFormat);
        cursor.setBlockCharFormat(charFormat);
    } else {
        cursor.insertHtml("<hr>");
        cursor.insertBlock(blockFormat, charFormat);
    }

    cursor.insertText(timestamp);

    // Remember the position so we can scroll up later.
    cursor.movePosition(QTextCursor::StartOfBlock);
    const auto textBegin = cursor.position();
    cursor.movePosition(QTextCursor::End);

    charFormat.setFontWeight(QFont::Normal);
    blockFormat.setLeftMargin(blockMargin);
    blockFormat.setRightMargin(blockMargin);
    cursor.insertBlock(blockFormat, charFormat);

    cursor.insertText(QString(text).trimmed());

    // We must scroll to the bottom before scrolling to textBegin,
    // so that textBegin becomes the first line in the viewport
    // rather than the last.
    textEdit->setTextCursor(cursor);
    cursor.setPosition(textBegin);
    textEdit->setTextCursor(cursor);
}


bool History::loadState(const DpsoCfg* cfg)
{
    history.reset(dpsoHistoryOpen(historyFilePath.c_str()));
    if (!history) {
        QMessageBox::critical(
            nullptr,
            appName,
            QString("Can't open \"%1\": %2").arg(
                historyFilePath.c_str(), dpsoGetError()));
        return false;
    }

    textEdit->clear();
    for (int i = 0; i < dpsoHistoryCount(history.get()); ++i) {
        DpsoHistoryEntry entry;
        dpsoHistoryGet(history.get(), i, &entry);
        appendToTextEdit(entry.timestamp, entry.text);
    }

    wordWrapCheck->setChecked(
        dpsoCfgGetBool(
            cfg,
            cfgKeyHistoryWrapWords,
            cfgDefaultValueHistoryWrapWords));

    setButtonsEnabled(dpsoHistoryCount(history.get()) > 0);

    nativeFileDialogs = dpsoCfgGetBool(
        cfg,
        cfgKeyUiNativeFileDialogs,
        cfgDefaultValueUiNativeFileDialogs);
    lastDirPath = dpsoCfgGetStr(cfg, cfgKeyHistoryExportDir, "");

    return true;
}


void History::saveState(DpsoCfg* cfg) const
{
    dpsoCfgSetBool(
        cfg, cfgKeyHistoryWrapWords, wordWrapCheck->isChecked());
    dpsoCfgSetStr(
        cfg, cfgKeyHistoryExportDir, lastDirPath.toUtf8().data());
}
