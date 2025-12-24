#include "myedit.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QFile>
#include <QKeyEvent>
#include <QScrollBar>
#include <QStandardItemModel>
#include <QTextCodec>
#include <QTextStream>

MyEdit::MyEdit(QWidget *parent) : QPlainTextEdit(parent)
{
    QFont f = font();
    f.setFamily(QStringLiteral("Consolas"));
    f.setFixedPitch(true);
    setFont(f);
    setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * 4);

    m_completionModel = new QStandardItemModel(this);
    m_completer = new QCompleter(this);
    m_completer->setModel(m_completionModel);
    m_completer->setWidget(this);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setFilterMode(Qt::MatchContains);
    m_completer->popup()->setMinimumWidth(400);
    connect(m_completer, QOverload<const QModelIndex &>::of(&QCompleter::activated),
            this, &MyEdit::insertCompletion);
}

bool MyEdit::loadFromFile(const QString &filePath, const QByteArray &codecName)
{
    QFile file(filePath);
    if(!file.open(QIODevice::ReadOnly)){
        return false;
    }
    QTextCodec *codec = codecName.isEmpty() ? QTextCodec::codecForLocale() : QTextCodec::codecForName(codecName);
    if(!codec){
        codec = QTextCodec::codecForLocale();
    }
    QTextStream stream(&file);
    stream.setCodec(codec);
    const QString text = stream.readAll();
    setPlainText(text);
    document()->setModified(false);
    setFilePath(filePath);
    setCodecName(codec->name());
    return true;
}

bool MyEdit::saveToFile(const QString &filePath, const QByteArray &codecName)
{
    QFile file(filePath);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Truncate)){
        return false;
    }
    const QByteArray codec = codecName.isEmpty() ? m_codecName : codecName;
    QTextCodec *codecObj = QTextCodec::codecForName(codec);
    if(!codecObj){
        codecObj = QTextCodec::codecForLocale();
    }
    QTextStream stream(&file);
    stream.setCodec(codecObj);
    stream << toPlainText();
    stream.flush();
    document()->setModified(false);
    setFilePath(filePath);
    setCodecName(codec);
    return true;
}

QString MyEdit::filePath() const
{
    return m_filePath;
}

void MyEdit::setFilePath(const QString &path)
{
    m_filePath = path;
}

QString MyEdit::codecName() const
{
    return QString::fromUtf8(m_codecName);
}

void MyEdit::setCodecName(const QByteArray &codec)
{
    if(!codec.isEmpty()){
        m_codecName = codec;
    }
}

bool MyEdit::useAutoComplete() const
{
    return m_useAutoComplete;
}

void MyEdit::setUseAutoComplete(bool enabled)
{
    m_useAutoComplete = enabled;
}

void MyEdit::setCompletionItems(const QList<CompletionItem> &items)
{
    m_allItems = items;
    m_lastContext = UnknownContext;
    rebuildCompletionModel();
}

MyEdit::ContextType MyEdit::detectContext() const
{
    static const QStringList tableKeywords = {
        QStringLiteral("from"), QStringLiteral("join"), QStringLiteral("into"),
        QStringLiteral("update"), QStringLiteral("table"), QStringLiteral("truncate")
    };
    static const QStringList columnKeywords = {
        QStringLiteral("select"), QStringLiteral("where"), QStringLiteral("on"),
        QStringLiteral("set"), QStringLiteral("and"), QStringLiteral("or"),
        QStringLiteral("by"), QStringLiteral("having")
    };

    QTextCursor tc = textCursor();
    tc.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
    QString lineText = tc.selectedText().toLower();

    int tablePos = -1, columnPos = -1;
    for(const QString &kw : tableKeywords){
        int pos = lineText.lastIndexOf(kw);
        if(pos > tablePos) tablePos = pos;
    }
    for(const QString &kw : columnKeywords){
        int pos = lineText.lastIndexOf(kw);
        if(pos > columnPos) columnPos = pos;
    }

    if(tablePos > columnPos) return TableContext;
    if(columnPos > tablePos) return ColumnContext;
    return UnknownContext;
}

void MyEdit::rebuildCompletionModel()
{
    m_completionModel->clear();
    ContextType ctx = detectContext();

    QList<CompletionItem> sorted;
    if(ctx == TableContext){
        for(const auto &item : m_allItems){
            if(item.type == TableType) sorted.append(item);
        }
        for(const auto &item : m_allItems){
            if(item.type == ColumnType) sorted.append(item);
        }
        for(const auto &item : m_allItems){
            if(item.type == KeywordType) sorted.append(item);
        }
    }else if(ctx == ColumnContext){
        for(const auto &item : m_allItems){
            if(item.type == ColumnType) sorted.append(item);
        }
        for(const auto &item : m_allItems){
            if(item.type == TableType) sorted.append(item);
        }
        for(const auto &item : m_allItems){
            if(item.type == KeywordType) sorted.append(item);
        }
    }else{
        sorted = m_allItems;
    }

    for(const auto &item : sorted){
        auto *row = new QStandardItem();
        QString display = item.name;
        if(item.type == ColumnType){
            row->setIcon(QIcon(QStringLiteral(":/images/column.svg")));
            if(!item.dataType.isEmpty()){
                display += QStringLiteral("    %1").arg(item.dataType);
            }
            if(!item.tableName.isEmpty()){
                display += QStringLiteral("    [%1]").arg(item.tableName);
            }
        }else if(item.type == TableType){
            row->setIcon(QIcon(QStringLiteral(":/images/table.svg")));
        }else{
            row->setIcon(QIcon(QStringLiteral(":/images/keyword.svg")));
        }
        row->setText(display);
        row->setData(item.name, Qt::UserRole);
        m_completionModel->appendRow(row);
    }
}

void MyEdit::keyPressEvent(QKeyEvent *event)
{
    if(m_completer->popup()->isVisible()){
        switch(event->key()){
        case Qt::Key_Enter:
        case Qt::Key_Return:
        case Qt::Key_Escape:
        case Qt::Key_Tab:
        case Qt::Key_Backtab:
            event->ignore();
            return;
        default:
            break;
        }
    }

    // Ctrl+Enter triggers search
    if((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
            && (event->modifiers() & Qt::ControlModifier)){
        emit searchTriggered();
        return;
    }

    QPlainTextEdit::keyPressEvent(event);

    if(!m_useAutoComplete){
        return;
    }

    const QString prefix = textUnderCursor();
    if(prefix.length() < 2){
        m_completer->popup()->hide();
        return;
    }

    QTextCursor tc = textCursor();
    tc.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
    QString lineText = tc.selectedText();
    int quoteCount = lineText.count(QLatin1Char('\''));
    if(quoteCount % 2 == 1){
        m_completer->popup()->hide();
        return;
    }

    ContextType ctx = detectContext();
    if(ctx != m_lastContext){
        m_lastContext = ctx;
        rebuildCompletionModel();
    }

    if(prefix != m_completer->completionPrefix()){
        m_completer->setCompletionPrefix(prefix);
        m_completer->popup()->setCurrentIndex(m_completer->completionModel()->index(0, 0));
    }

    QRect cr = cursorRect();
    cr.setWidth(m_completer->popup()->sizeHintForColumn(0)
                + m_completer->popup()->verticalScrollBar()->sizeHint().width());
    m_completer->complete(cr);
}

void MyEdit::insertCompletion(const QModelIndex &index)
{
    const QString completion = index.data(Qt::UserRole).toString();
    QTextCursor tc = textCursor();
    const QString prefix = textUnderCursor();
    tc.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, prefix.length());
    tc.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, prefix.length());
    tc.insertText(completion);
    setTextCursor(tc);
}

QString MyEdit::textUnderCursor() const
{
    QTextCursor tc = textCursor();
    QString word;
    while(tc.position() > 0){
        tc.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, 1);
        QChar ch = tc.selectedText().at(0);
        if(ch.isLetterOrNumber() || ch == QLatin1Char('_')){
            word.prepend(ch);
            tc.clearSelection();
        }else{
            break;
        }
    }
    return word;
}
