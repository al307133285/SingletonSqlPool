#include "ConnectionPool.h"


ConnectionPool::ConnectionPool()    
{
	m_timeoutTimer = new QTimer(this);
	connect(m_timeoutTimer, &QTimer::timeout, this, &ConnectionPool::onTimeoutCheck);
	m_timeoutTimer->start(1 * 60 * 1000); // 5分钟检查
}

ConnectionPool::~ConnectionPool()
{
	QMutexLocker locker(&m_mutex);
	for (auto it = m_connectionMap.begin(); it != m_connectionMap.end(); ++it) {
		it->database.close();
	}
	m_connectionMap.clear();
}

QSharedPointer< ConnectionPool> ConnectionPool::instance(const QString& databaseName)
{
    //static QMap<QString, ConnectionPool> pools;
    //return pools[databaseName];

	static QMutex mutex;
	static QMap<QString,QSharedPointer< ConnectionPool>> pools;

	{
		QMutexLocker locker(&mutex);
		if (pools.contains(databaseName))
			return pools[databaseName];
		
	}

	// 外面构造，减少锁时间
    QSharedPointer< ConnectionPool> newPool(new ConnectionPool);

	{
		QMutexLocker locker(&mutex);
		return pools.insert(databaseName, newPool).value();
	}

}

void ConnectionPool::setup(const QString& driver, const QString& host, int port,
                            const QString& dbName, const QString& user, const QString& password,
                            int maxConnections, int expireTimeoutMs)
{
    m_driver = driver;
    m_host = host;
    m_port = port;
    m_dbName = dbName;
    m_user = user;
    m_password = password;
	m_maxConnectionCount = maxConnections;
	m_expireTimeoutMs = expireTimeoutMs;

    ///m_timer.start(30000);
}

QSqlDatabase ConnectionPool::acquire(int timeoutMs)
{
	QMutexLocker locker(&m_mutex);

	QString threadId = QString::number((quintptr)QThread::currentThreadId());
	qint64 startTime = QDateTime::currentMSecsSinceEpoch();

	while (true) {
		for (auto it = m_connectionMap.begin(); it != m_connectionMap.end(); ++it) {
			if (!it->inUse && it.key().contains(threadId)) {
				if (!isConnectionAlive(it->database)) {
					it->database.close();
					if (!it->database.open()) {
						qDebug() << "Failed to open DB connection: " << it->database.lastError().text();
						return QSqlDatabase();
					}					
					/*it->database.exec("SET NAMES 'utf8mb4'");
					it->database.exec("SET character_set_connection = 'utf8mb4'");
					it->database.exec("SET character_set_results = 'utf8mb4'");
					it->database.exec("SET character_set_client = 'utf8mb4'");*/
				}
				it->inUse = true;
				it->lastUsedTimestamp = QDateTime::currentMSecsSinceEpoch();
				return it->database;
			}
		}

		if (m_connectionMap.size() < m_maxConnectionCount) {
			QString connName = createConnectionName();
			QSqlDatabase db = QSqlDatabase::addDatabase(m_driver, connName);
		
			db.setHostName(m_host);
			db.setDatabaseName(m_dbName);
			db.setUserName(m_user);
			db.setPassword(m_password);
			db.setPort(m_port);
			db.setConnectOptions("MYSQL_OPT_CONNECT_TIMEOUT=5;"
				"MYSQL_OPT_READ_TIMEOUT=5;"
				"MYSQL_OPT_WRITE_TIMEOUT=5");
			if (!db.open()) {
				qDebug() << "Failed to open DB:" << db.lastError().text();
			}			

			/*db.exec("SET NAMES 'utf8mb4'");
			db.exec("SET character_set_connection = 'utf8mb4'");
			db.exec("SET character_set_results = 'utf8mb4'");
			db.exec("SET character_set_client = 'utf8mb4'");*/
			ConnectionInfo info{ db, QDateTime::currentMSecsSinceEpoch(), true };
			m_connectionMap.insert(connName, info);
			return db;
		}

		// 超时检查
		qint64 now = QDateTime::currentMSecsSinceEpoch();
		if (now - startTime > timeoutMs) {
			return QSqlDatabase();
		}
		m_condition.wait(&m_mutex, timeoutMs);
	}
}

void ConnectionPool::release(const QString& connectionName)
{
	QMutexLocker locker(&m_mutex);
	if (m_connectionMap.contains(connectionName)) {
		m_connectionMap[connectionName].inUse = false;
		m_connectionMap[connectionName].lastUsedTimestamp = QDateTime::currentMSecsSinceEpoch();
		m_condition.wakeOne();
	}
}


void ConnectionPool::setMaxConnectionCount(int count)
{
	m_maxConnectionCount = count;
}

int ConnectionPool::getMaxConnectionCount() const
{
	return m_maxConnectionCount;
}

void ConnectionPool::onTimeoutCheck()
{
	QMutexLocker locker(&m_mutex);
	clearExpiredConnections();
}


bool ConnectionPool::isConnectionAlive(QSqlDatabase& db)
{
	if (!db.isOpen()) {
		return false;
	}
	QSqlQuery query(db);
	if (!query.exec("SELECT 1")) {
		return false;
	}
	return true;
}


void ConnectionPool::clearExpiredConnections()
{
	qint64 now = QDateTime::currentMSecsSinceEpoch();
	//QList<QString> toRemove;
	for (auto it = m_connectionMap.begin(); it != m_connectionMap.end(); ++it) {
		if (!it->inUse && (now - it->lastUsedTimestamp > m_expireTimeoutMs)) {
			it->database.close();
			//toRemove.append(it.key());
			m_connectionMap.remove(it.key());
		}
	}
	/*for (const QString& key : toRemove) {
		m_connectionMap.remove(key);
	}*/
}

QString ConnectionPool::createConnectionName()
{
	static qint64 counter = 0;
	return QString("Conn_%1_%2").arg((quintptr)QThread::currentThreadId()).arg(counter++);
}
