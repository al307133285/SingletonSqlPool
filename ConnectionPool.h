#pragma once
#include <QObject>
#include <QSqlDatabase>
#include <QQueue>
#include <QMutex>
#include <QTimer>
#include <QDateTime>
#include <QWaitCondition>
#include <QSqlError>
#include <QDebug>
#include <QThread>
#include <QSqlQuery>

struct ConnectionInfo {
	QSqlDatabase database;
	qint64 lastUsedTimestamp;
	bool inUse;
};

class ConnectionPool : public QObject
{
    Q_OBJECT
public:
    static QSharedPointer<ConnectionPool> instance(const QString& databaseName = QStringLiteral("account"));

    void setup(const QString& driver,
               const QString& host, int port,
               const QString& dbName,
               const QString& user, const QString& password,
               int maxConnections = 10, int expireTimeoutMs = 1*60*1000);

    QSqlDatabase acquire(int timeoutMs = 3000);
    void release(const QString& connectionName);

	void setMaxConnectionCount(int count);
    int getMaxConnectionCount() const;
	void enableSqlLog(bool enable);

    ~ConnectionPool();
private:
    ConnectionPool();
    
    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;
  
    QString createConnectionName();
    bool isConnectionAlive(QSqlDatabase& db);
    void clearExpiredConnections();
 
private slots:
	void onTimeoutCheck();


private:
    QString m_driver;
    QString m_host;
    int m_port;
    QString m_dbName;
    QString m_user;
    QString m_password;

	QMutex m_mutex;
	QWaitCondition m_condition;
	QMap<QString, ConnectionInfo> m_connectionMap;

	int m_maxConnectionCount = 20;
	int m_expireTimeoutMs = 1 * 60 * 1000; // 30分钟
    QTimer* m_timeoutTimer;
};