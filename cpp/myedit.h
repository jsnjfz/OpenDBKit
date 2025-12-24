#ifndef MYEDIT_H
#define MYEDIT_H

#include <QPlainTextEdit>
#include <QStringList>

class QCompleter;
class QStandardItemModel;

class MyEdit : public QPlainTextEdit
{
    Q_OBJECT
public:
    enum CompletionType { KeywordType, TableType, ColumnType };
    enum ContextType { UnknownContext, TableContext, ColumnContext };

    struct CompletionItem {
        QString name;
        CompletionType type;
        QString dataType;
        QString tableName;
    };

    explicit MyEdit(QWidget *parent = nullptr);

    bool loadFromFile(const QString &filePath, const QByteArray &codecName = QByteArray());
    bool saveToFile(const QString &filePath, const QByteArray &codecName = QByteArray());

    QString filePath() const;
    void setFilePath(const QString &path);

    QString codecName() const;
    void setCodecName(const QByteArray &codec);

    bool useAutoComplete() const;
    void setUseAutoComplete(bool enabled);

    void setCompletionItems(const QList<CompletionItem> &items);

signals:
    void searchTriggered();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void insertCompletion(const QModelIndex &index);

private:
    QString textUnderCursor() const;
    ContextType detectContext() const;
    void rebuildCompletionModel();

    QString m_filePath;
    QByteArray m_codecName = QByteArrayLiteral("UTF-8");
    bool m_useAutoComplete = true;
    QCompleter *m_completer = nullptr;
    QStandardItemModel *m_completionModel = nullptr;
    QList<CompletionItem> m_allItems;
    ContextType m_lastContext = UnknownContext;
};

#endif // MYEDIT_H
