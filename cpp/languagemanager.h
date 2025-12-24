#ifndef LANGUAGEMANAGER_H
#define LANGUAGEMANAGER_H

#include <QObject>
#include <QString>

class LanguageManager : public QObject
{
    Q_OBJECT
public:
    enum class Language {
        Chinese,
        English
    };

    static LanguageManager *instance();

    void initialize(Language lang);
    Language language() const;
    QString text(const QString &zh, const QString &en) const;
    QString languageCode() const;
    static Language languageFromCode(const QString &code, Language fallback = Language::Chinese);

public slots:
    void setLanguage(Language lang);

signals:
    void languageChanged(Language lang);

private:
    explicit LanguageManager(QObject *parent = nullptr);

    Language m_language = Language::Chinese;
    bool m_initialized = false;
};

inline QString trLang(const QString &zh, const QString &en)
{
    return LanguageManager::instance()->text(zh, en);
}

#endif // LANGUAGEMANAGER_H
