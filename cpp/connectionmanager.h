#ifndef CONNECTIONMANAGER_H
#define CONNECTIONMANAGER_H

#include <QObject>
#include <QList>
#include <QString>

struct ConnectionProperty
{
    QString name;
    QString value;
};

struct JdbcDriverEntry
{
    QString type;
    QString file;
    QString driverClass;
};

struct SshSettings
{
    bool enabled = false;
    QString host;
    quint16 port = 22;
    QString user;
    QString password;
    bool savePassword = true;
};

struct ConnectionInfo
{
    QString name;
    QString host;
    quint16 port = 3306;
    QString user;
    QString password;
    bool savePassword = true;
    QString driver;
    bool useUrl = false;
    QString url;
    QString defaultDb;
    bool production = false;
    QString charset;
    QString serverTimeZone;
    QString localClient;
    bool autoSubmit = true;
    SshSettings ssh;
    QList<ConnectionProperty> properties;
    QString startupScript;
    QList<JdbcDriverEntry> jdbcDrivers;
};

Q_DECLARE_METATYPE(ConnectionInfo)

class ConnectionManager : public QObject
{
    Q_OBJECT
public:
    static ConnectionManager *instance();

    QList<ConnectionInfo> connections() const;
    ConnectionInfo connection(const QString &name) const;
    void saveConnection(const ConnectionInfo &info);
    bool removeConnection(const QString &name);

    bool testConnection(const ConnectionInfo &info, QString *errorMessage = nullptr) const;
    QStringList fetchDatabases(const ConnectionInfo &info, QString *errorMessage = nullptr) const;
    QStringList fetchTables(const ConnectionInfo &info,
                            const QString &database,
                            QString *errorMessage = nullptr) const;
    static QList<ConnectionProperty> defaultMysqlProperties();

signals:
    void connectionsChanged();

private:
    explicit ConnectionManager(QObject *parent = nullptr);
    QString storagePath() const;
    void load();
    void persist() const;
    void ensureDefaultConnection();

    QList<ConnectionInfo> m_connections;
};

#endif // CONNECTIONMANAGER_H
