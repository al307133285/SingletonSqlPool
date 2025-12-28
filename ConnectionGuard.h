#pragma once

#include <QSqlDatabase>
#include "ConnectionPool.h"

class ConnectionGuard
{
public:
    ConnectionGuard(const QString& databaseName = "account")
        :m_databaseName(databaseName), db(ConnectionPool::instance(databaseName)->acquire())
    {}

    ~ConnectionGuard()
    {
        if (db.isValid()) {
            ConnectionPool::instance(m_databaseName)->release(db.connectionName());
        }
    }

    QSqlDatabase& database()
    {
        return db;
    }

private:
    QString m_databaseName;
    QSqlDatabase db;
};