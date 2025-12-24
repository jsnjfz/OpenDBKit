#include "languagemanager.h"

LanguageManager *LanguageManager::instance()
{
    static LanguageManager *ins = new LanguageManager;
    return ins;
}

LanguageManager::LanguageManager(QObject *parent)
    : QObject(parent)
{
}

void LanguageManager::initialize(Language lang)
{
    m_language = lang;
    m_initialized = true;
}

LanguageManager::Language LanguageManager::language() const
{
    return m_language;
}

QString LanguageManager::text(const QString &zh, const QString &en) const
{
    return m_language == Language::Chinese ? zh : en;
}

QString LanguageManager::languageCode() const
{
    return m_language == Language::Chinese ? QStringLiteral("zh")
                                           : QStringLiteral("en");
}

LanguageManager::Language LanguageManager::languageFromCode(const QString &code, Language fallback)
{
    if(code.compare(QStringLiteral("en"), Qt::CaseInsensitive) == 0){
        return Language::English;
    }
    if(code.compare(QStringLiteral("zh"), Qt::CaseInsensitive) == 0 ||
            code.compare(QStringLiteral("zh_cn"), Qt::CaseInsensitive) == 0){
        return Language::Chinese;
    }
    return fallback;
}

void LanguageManager::setLanguage(Language lang)
{
    if(m_initialized && lang == m_language){
        return;
    }
    m_language = lang;
    m_initialized = true;
    emit languageChanged(lang);
}
