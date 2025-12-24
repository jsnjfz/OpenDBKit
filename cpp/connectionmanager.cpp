#include "connectionmanager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

namespace {

QJsonArray propertiesToJson(const QList<ConnectionProperty> &props)
{
    QJsonArray array;
    for(const auto &prop : props){
        QJsonObject obj;
        obj["name"] = prop.name;
        obj["value"] = prop.value;
        array.append(obj);
    }
    return array;
}

QList<ConnectionProperty> propertiesFromJson(const QJsonArray &array)
{
    QList<ConnectionProperty> props;
    for(const auto &value : array){
        const auto obj = value.toObject();
        ConnectionProperty prop;
        prop.name = obj.value("name").toString();
        prop.value = obj.value("value").toString();
        props.append(prop);
    }
    return props;
}

QJsonArray driversToJson(const QList<JdbcDriverEntry> &drivers)
{
    QJsonArray array;
    for(const auto &driver : drivers){
        QJsonObject obj;
        obj["type"] = driver.type;
        obj["file"] = driver.file;
        obj["driverClass"] = driver.driverClass;
        array.append(obj);
    }
    return array;
}

QList<JdbcDriverEntry> driversFromJson(const QJsonArray &array)
{
    QList<JdbcDriverEntry> drivers;
    for(const auto &value : array){
        const auto obj = value.toObject();
        JdbcDriverEntry entry;
        entry.type = obj.value("type").toString();
        entry.file = obj.value("file").toString();
        entry.driverClass = obj.value("driverClass").toString();
        drivers.append(entry);
    }
    return drivers;
}

QJsonObject sshToJson(const SshSettings &ssh)
{
    QJsonObject obj;
    obj["enabled"] = ssh.enabled;
    obj["host"] = ssh.host;
    obj["port"] = int(ssh.port);
    obj["user"] = ssh.user;
    obj["password"] = ssh.password;
    obj["savePassword"] = ssh.savePassword;
    return obj;
}

SshSettings sshFromJson(const QJsonObject &obj)
{
    SshSettings ssh;
    ssh.enabled = obj.value("enabled").toBool(false);
    ssh.host = obj.value("host").toString();
    ssh.port = quint16(obj.value("port").toInt(22));
    ssh.user = obj.value("user").toString();
    ssh.password = obj.value("password").toString();
    ssh.savePassword = obj.value("savePassword").toBool(true);
    if(!ssh.savePassword){
        ssh.password.clear();
    }
    return ssh;
}

QJsonObject toJson(const ConnectionInfo &info)
{
    QJsonObject obj;
    obj["name"] = info.name;
    obj["host"] = info.host;
    obj["port"] = int(info.port);
    obj["user"] = info.user;
    obj["password"] = info.savePassword ? info.password : QString();
    obj["savePassword"] = info.savePassword;
    obj["driver"] = info.driver;
    obj["useUrl"] = info.useUrl;
    obj["url"] = info.url;
    obj["defaultDb"] = info.defaultDb;
    obj["production"] = info.production;
    obj["charset"] = info.charset;
    obj["serverTimeZone"] = info.serverTimeZone;
    obj["localClient"] = info.localClient;
    obj["autoSubmit"] = info.autoSubmit;
    obj["ssh"] = sshToJson(info.ssh);
    obj["properties"] = propertiesToJson(info.properties);
    obj["startupScript"] = info.startupScript;
    obj["jdbcDrivers"] = driversToJson(info.jdbcDrivers);
    return obj;
}

ConnectionInfo fromJson(const QJsonObject &obj)
{
    ConnectionInfo info;
    info.name = obj.value("name").toString();
    info.host = obj.value("host").toString("localhost");
    info.port = quint16(obj.value("port").toInt(3306));
    info.user = obj.value("user").toString();
    info.password = obj.value("password").toString();
    info.savePassword = obj.value("savePassword").toBool(true);
    if(!info.savePassword){
        info.password.clear();
    }
    info.driver = obj.value("driver").toString("Default");
    info.useUrl = obj.value("useUrl").toBool(false);
    info.url = obj.value("url").toString();
    info.defaultDb = obj.value("defaultDb").toString();
    info.production = obj.value("production").toBool(false);
    info.charset = obj.value("charset").toString();
    info.serverTimeZone = obj.value("serverTimeZone").toString();
    info.localClient = obj.value("localClient").toString();
    info.autoSubmit = obj.value("autoSubmit").toBool(true);
    info.ssh = sshFromJson(obj.value("ssh").toObject());
    info.properties = propertiesFromJson(obj.value("properties").toArray());
    info.startupScript = obj.value("startupScript").toString();
    info.jdbcDrivers = driversFromJson(obj.value("jdbcDrivers").toArray());
    return info;
}

QString uniqueConnectionName(const QString &prefix)
{
    return QStringLiteral("%1_%2_%3")
        .arg(prefix)
        .arg(QDateTime::currentMSecsSinceEpoch())
        .arg(QRandomGenerator::global()->generate());
}
}

ConnectionManager *ConnectionManager::instance()
{
    static ConnectionManager *ins = new ConnectionManager;
    return ins;
}

ConnectionManager::ConnectionManager(QObject *parent)
    : QObject(parent)
{
    load();
    ensureDefaultConnection();
}

QList<ConnectionInfo> ConnectionManager::connections() const
{
    return m_connections;
}

ConnectionInfo ConnectionManager::connection(const QString &name) const
{
    for(const auto &info : m_connections){
        if(info.name == name){
            return info;
        }
    }
    return {};
}

void ConnectionManager::saveConnection(const ConnectionInfo &info)
{
    bool updated = false;
    for(auto &conn : m_connections){
        if(conn.name == info.name){
            conn = info;
            updated = true;
            break;
        }
    }
    if(!updated){
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
        m_connections.push_back(info);
#else
        m_connections.append(info);
#endif
    }
    persist();
    emit connectionsChanged();
}

bool ConnectionManager::removeConnection(const QString &name)
{
    for(int i = 0; i < m_connections.size(); ++i){
        if(m_connections.at(i).name == name){
            m_connections.removeAt(i);
            persist();
            emit connectionsChanged();
            return true;
        }
    }
    return false;
}

bool ConnectionManager::testConnection(const ConnectionInfo &info, QString *errorMessage) const
{
    QString connName = uniqueConnectionName(QStringLiteral("test"));
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QMYSQL"), connName);
    db.setHostName(info.host);
    db.setPort(info.port);
    db.setUserName(info.user);
    db.setPassword(info.password);
    if(!info.defaultDb.isEmpty()){
        db.setDatabaseName(info.defaultDb);
    }
    bool ok = db.open();
    if(!ok && errorMessage){
        *errorMessage = db.lastError().text();
    }
    QSqlDatabase::removeDatabase(connName);
    return ok;
}

QStringList ConnectionManager::fetchDatabases(const ConnectionInfo &info, QString *errorMessage) const
{
    QString connName = uniqueConnectionName(QStringLiteral("listdb"));
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QMYSQL"), connName);
    db.setHostName(info.host);
    db.setPort(info.port);
    db.setUserName(info.user);
    db.setPassword(info.password);
    QStringList dbs;
    if(!db.open()){
        if(errorMessage){
            *errorMessage = db.lastError().text();
        }
        QSqlDatabase::removeDatabase(connName);
        return dbs;
    }
    QSqlQuery query(db);
    if(query.exec(QStringLiteral("SHOW DATABASES"))){
        while(query.next()){
            dbs << query.value(0).toString();
        }
    }else if(errorMessage){
        *errorMessage = query.lastError().text();
    }
    QSqlDatabase::removeDatabase(connName);
    return dbs;
}

QStringList ConnectionManager::fetchTables(const ConnectionInfo &info,
                                           const QString &database,
                                           QString *errorMessage) const
{
    QString targetDb = database;
    if(targetDb.isEmpty()){
        targetDb = info.defaultDb;
    }
    if(targetDb.isEmpty()){
        if(errorMessage){
            *errorMessage = tr("Database name is empty.");
        }
        return {};
    }

    QString connName = uniqueConnectionName(QStringLiteral("listtbl"));
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QMYSQL"), connName);
    db.setHostName(info.host);
    db.setPort(info.port);
    db.setUserName(info.user);
    db.setPassword(info.password);
    db.setDatabaseName(targetDb);

    QStringList tables;
    if(!db.open()){
        if(errorMessage){
            *errorMessage = db.lastError().text();
        }
        QSqlDatabase::removeDatabase(connName);
        return tables;
    }

    QSqlQuery query(db);
    if(query.exec(QStringLiteral("SHOW TABLES"))){
        while(query.next()){
            tables << query.value(0).toString();
        }
    }else if(errorMessage){
        *errorMessage = query.lastError().text();
    }

    QSqlDatabase::removeDatabase(connName);
    return tables;
}

QString ConnectionManager::storagePath() const
{
    QDir dir(QCoreApplication::applicationDirPath());
    const QString fileName = dir.filePath(QStringLiteral("connections.json"));
    return fileName;
}

void ConnectionManager::load()
{
    QFile file(storagePath());
    if(!file.exists()){
        return;
    }
    if(!file.open(QIODevice::ReadOnly)){
        return;
    }
    const auto doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if(!doc.isArray()){
        return;
    }
    m_connections.clear();
    for(const auto &val : doc.array()){
        m_connections.append(fromJson(val.toObject()));
    }
}

void ConnectionManager::persist() const
{
    QJsonArray array;
    for(const auto &info : m_connections){
        array.append(toJson(info));
    }
    QFile file(storagePath());
    if(!file.open(QIODevice::WriteOnly | QIODevice::Truncate)){
        return;
    }
    file.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
    file.close();
}

void ConnectionManager::ensureDefaultConnection()
{
    if(!m_connections.isEmpty()){
        return;
    }
    ConnectionInfo info;
    info.name = QStringLiteral("local-mysql");
    info.host = QStringLiteral("localhost");
    info.port = 3306;
    info.user = QStringLiteral("root");
    info.password = QStringLiteral("123456");
    info.savePassword = true;
    info.driver = QStringLiteral("Default");
    info.charset = QStringLiteral("utf8mb4");
    info.autoSubmit = true;
    info.defaultDb = QStringLiteral("test");
    info.properties = defaultMysqlProperties();
    m_connections.append(info);
    persist();
}

QList<ConnectionProperty> ConnectionManager::defaultMysqlProperties()
{
    QList<ConnectionProperty> props;
    auto appendProp = [&props](const QString &name, const QString &value) {
        props.append(ConnectionProperty{name, value});
    };
    appendProp(QStringLiteral("HOST"), QStringLiteral("localhost"));
    appendProp(QStringLiteral("PORT"), QStringLiteral("3306"));
    appendProp(QStringLiteral("DBNAME"), QStringLiteral("test"));
    appendProp(QStringLiteral("user"), QStringLiteral("root"));
    appendProp(QStringLiteral("password"), QString());
    appendProp(QStringLiteral("allowMultiQueries"), QStringLiteral("true"));
    appendProp(QStringLiteral("autoReconnect"), QStringLiteral("false"));
    appendProp(QStringLiteral("useSSL"), QStringLiteral("false"));
    appendProp(QStringLiteral("allowLoadLocalInfile"), QStringLiteral("false"));
    appendProp(QStringLiteral("characterEncoding"), QStringLiteral("utf8mb4"));
    appendProp(QStringLiteral("serverTimezone"), QString());
    return props;
}
