#ifndef EASYQTSQL_TRANSACTION_H
#define EASYQTSQL_TRANSACTION_H

/*
 * The MIT License (MIT)
 * Copyright 2018 Alexey Kramin
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
*/

#ifndef EASY_QT_SQL_MAIN
#include <QtSql>
#include "EasyQtSql_DBException.h"
#include "EasyQtSql_NonQueryResult.h"
#include "EasyQtSql_InsertQuery.h"
#include "EasyQtSql_DeleteQuery.h"
#include "EasyQtSql_UpdateQuery.h"
#include "EasyQtSql_PreparedQuery.h"

#endif
#include "EasyQtSql_Util.h"

namespace {

static QString var2str(const QVariant &v)
{
    if (v.type() == QVariant::Map || v.type() == QVariant::List || v.type() == QVariant::StringList) {
        return QJsonDocument::fromVariant(v).toJson(QJsonDocument::Compact);
    } else {
        return v.toString();
    }
}

static QVariant str2var(const QString &str)
{
    QJsonParseError err;
    auto jsonDoc = QJsonDocument::fromJson(str.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError) {
        return QVariant();
    }
    return jsonDoc.toVariant();
}

static QStringList sqlArrayToList(const QString &val)
{
    QStringList result;
    QString word;
    bool inQuote = false;
    bool inEscape = false;
    QChar c;
    for (int i = 1; i < val.count() - 1; i ++ ) {
        c = val.at(i);
        if (!inQuote) {
            if (c == '"'){
                inQuote = true;
            } else if (c == ',') {
                result.append(word);
                word.clear();
            } else {
                word.append(c);
            }
        } else {
            if (inEscape) {
                word.append(c);
                inEscape = false;
            } else {
                if (c == '\\'){
                    inEscape = true;
                } else if (c == '"') {
                    inQuote = false;
                } else {
                    word.append(c);
                }
            }
        }
    }
    if (!word.isEmpty()) result.append(word);
    return result;
}

static QVariant formatSqlValue(const QVariant &val, const QString &format)
{
    if (!val.isValid() || val.isNull()) {
        return QVariant();
    }
    QString fmt = format;
    if (val.type() == QVariant::ULongLong || val.type() == QVariant::LongLong) {
        fmt = "string";
    }
    if (fmt.isEmpty()) {
        return val;
    }
    if (val.type() == QVariant::Date) {
        return QVariant(val.toDate().toString(fmt));
    } else if (val.type() == QVariant::DateTime) {
        return QVariant(val.toDateTime().toString(fmt));
    } else if (val.type() ==  QVariant::Time) {
        return QVariant(val.toTime().toString(fmt));
    } else if (val.type() == QVariant::String) {
        if (fmt == "array") {
            return QVariant(sqlArrayToList(val.toString()));
        } else if (fmt == "json") {
            return str2var(val.toString());
        }
    } else if (val.type() == QVariant::ULongLong || val.type() == QVariant::LongLong) {
        if (fmt == "string") {
            return QVariant(val.toString());
        }
    }
    return val;
}

} // end of anonymous namespace

/*!
\brief QSqlDatabase wrapper.

\code
void test()
{
   QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE");
   db.setDatabaseName(":memory:");

   try
   {
      Database sdb(db);

      sdb.execNonQuery("CREATE TABLE table (a int, b int, c int, d text)");
   }
   catch (const DBException &e)
   {
      //you can handle all the errors at one point

      qDebug() << e.lastError << e.lastQuery;
   }
}
\endcode
*/
class Database
{
    Q_DISABLE_COPY(Database)

public:

    /*!
    * \param db QSqlDatabase to use
    *
    * Creates an Database object, tries to open <em>db</em> connection if not opened.
    *
    * \throws DBException
    */
    explicit Database (const QSqlDatabase &db = QSqlDatabase())
    {
        m_db = db.isValid() ? db : QSqlDatabase::database();

        if (!m_db.isOpen())
        {
            if (!m_db.open())
            {
#ifdef DB_EXCEPTIONS_ENABLED
                throw DBException(m_db);
#endif
            }
        }
    }

    Database(Database&& other)
    {
        m_db = other.m_db;
        other.m_db = QSqlDatabase();
    }

    Database& operator=(Database&& other)
    {
        if (this == &other) return *this;

        m_db = other.m_db;
        other.m_db = QSqlDatabase();

        return *this;
    }

    /*!
    * \brief Returns information about the last error that occurred on the underlying database.
    */
    QSqlError lastError() const
    {
        return m_db.lastError();
    }

    /*!
   \brief Executes non-query SQL statement (DELETE, INSERT, UPDATE, CREATE, ALTER, etc.)
   \param query SQL statement string
   \throws DBException
   */
    NonQueryResult execNonQuery(const QString &sql) const
    {
        QSqlQuery q = m_db.exec(sql);

#ifdef DB_EXCEPTIONS_ENABLED

        QSqlError lastError = q.lastError();

        if (lastError.isValid())
            throw DBException(q);

#endif

        return NonQueryResult(q);
    }

    NonQueryResult execNonQuery_batch(const QString &sql, const QVariantList &data) const
    {
        QSqlQuery q(m_db);
        q.prepare(sql);
        foreach (const QVariant &i, data) {
            q.addBindValue(i.toList());
        }
        q.execBatch();

#ifdef DB_EXCEPTIONS_ENABLED

        QSqlError lastError = q.lastError();

        if (lastError.isValid())
            throw DBException(q);

#endif

        return NonQueryResult(q);
    }

    /*!
   \brief Executes SELECT query
   \param query SQL statement string
   \throws DBException
   */
    QueryResult execQuery(const QString &sql) const
    {
        QSqlQuery q = m_db.exec(sql);

#ifdef DB_EXCEPTIONS_ENABLED

        QSqlError lastError = q.lastError();

        if (lastError.isValid())
            throw DBException(q);

#endif

        return QueryResult(q);
    }

    /*!
   \brief Creates INSERT query wrapper
   \param table Table to insert into with list of columns
   */
    InsertQuery insertInto(const QString &table) const
    {
        InsertQuery query(table, m_db);

        return query;
    }

    /*!
   \brief Creates DELETE query wrapper
   \param table Table to delete from
   */
    DeleteQuery deleteFrom(const QString &table) const
    {
        DeleteQuery query(table, m_db);

        return query;
    }

    /*!
   \brief Creates UPDATE query wrapper
   \param table Table to update
   */
    UpdateQuery update(const QString &table) const
    {
        UpdateQuery query(table, m_db);

        return query;
    }

    /*!
   \brief Prepares SQL statement
   \param sql SQL statement string
   \param forwardOnly Configure underlying QSqlQuery as forwardOnly
   */
    PreparedQuery prepare(const QString &sql, bool forwardOnly = true) const
    {
        PreparedQuery query(sql, m_db, forwardOnly);

        return query;
    }

    /*!
    * \brief Returns a reference to the wrapped QSqlDatabase object
    */
    QSqlDatabase &qSqlDatabase()
    {
        return m_db;
    }

    /*!
    \brief Executes <em>query</em> and applies function <em>f</em> to each result row.
    \param query SQL query string (SELECT statement)
    \param f Function (lambda) to apply to
    \returns num rows handled with function <em>f</em>

    \code
    Database db;
    db.each("SELECT * FROM table", [](const QueryResult &res)
    {
       qDebug() << res.toMap();
    });
    \endcode
    */
    template<typename Func>
    int each (const QString &query, Func&& f) const
    {
        QueryResult res = execQuery(query);

        return Util::each(res, f);
    }

    /*!
    \brief Executes <em>query</em> and applies function <em>f</em> to the first result row.
    \param query SQL query string (SELECT statement)
    \param f Function (lambda) to apply to
    \returns num rows handled with function <em>f</em>

    \code
    Database db;
    db.first("SELECT * FROM table", [](const QueryResult &res)
    {
       qDebug() << res.toMap();
    });
    \endcode
    */
    template<typename Func>
    int first (const QString &query, Func&& f) const
    {
        QueryResult res = execQuery(query);

        return Util::first(res, f);
    }

    /*!
    \brief Executes <em>query</em> and applies function <em>f</em> to <em>count</em> result rows starting from index <em>start</em>.
    \param query SQL query string (SELECT statement)
    \param start Start index
    \param count Row count to handle
    \param f Function (lambda) to apply to
    \returns num rows handled with function <em>f</em>

    \code
    Database db;
    db.range("SELECT * FROM table", 3, 10, [](const QueryResult &res)
    {
       qDebug() << res.toMap();
    });
    \endcode
    */
    template<typename Func>
    int range(const QString &query, int start, int count, Func&& f) const
    {
        QueryResult res = execQuery(query);

        return Util::range(res, start, count, f);
    }

    /*!
    \brief Executes <em>query</em> and applies function <em>f</em> to <em>topCount</em> result rows.
    \param query SQL query string (SELECT statement)
    \param topCount Row count to handle
    \param f Function (lambda) to apply to
    \returns num rows handled with function <em>f</em>

    \code
    Database db;
    db.top("SELECT * FROM table", 10, [](const QueryResult &res)
    {
       qDebug() << res.toMap();
    });
    \endcode
    */
    template<typename Func>
    int top(const QString &query, int topCount, Func&& f) const
    {
        QueryResult res = execQuery(query);

        return Util::top(res, topCount, f);
    }

    /*!
    \brief Executes <em>query</em> and returns scalar value converted to T.
    \param query SQL query string (SELECT statement)
    \sa QueryResult::scalar
    */
    template<typename T>
    T scalar(const QString &query) const
    {
        QueryResult res = execQuery(query);

        res.next();

        return res.scalar<T>();
    }

    /*!
    \brief Executes <em>query</em> and returns scalar value.
    \param query SQL query string (SELECT statement)
    \sa QueryResult::scalar
    */
    QVariant scalar(const QString &query) const
    {
        QueryResult res = execQuery(query);

        res.next();

        return res.scalar();
    }

    /**
     * @brief 以事务的方式在线程中执行一组SQL操作
     * @code
     * magic([
     *   { "type": "insert", "param": { "table": "xx", "data": {} } },
     *   { "type": "batch_insert", "param": { "table": "xx", "fields": ["x", "y", "z"], data: [] } },
     *   { "type": "update", "param": { "table": "xx", "where": "", data: {} } },
     *   { "type": "delete", "param": { "table": "xx", "where": "" } },
     *   { "type": "select", "param": { "sql": "" } },
     *   { "type": "select1", "param": { "sql": "" } },
     *   { "type": "select_value", "param": { "sql": "" } },
     *   { "type": "exec", "param": { "sql": "" } },
     *   { "type": "batch_exec", "param": { "sql": "", data: [ [], ] } }
     * ])
     * @remarks
     * 1. 若需要拿某一个条目的返回值(例如lastInertId),可以使用${0}拿到某一条目的返回值，其中0为第0个步骤的执行结果
     * magic([
     *   { "type": "insert", "param": { "table": "gwm_partnumber", "data": {} } },
     *   { "type": "batch_insert", "param": { "magic": "replace_value", "table": "xx", "fields": [], data: [ { "partnumber_id": "${0}" } ] } },
     * ])
     * 2. batch_insert时支持使用$ALL{0}替换data数据，其中0为第0个步骤的执行结果
     * magic([
     *   { "type": "select", "param": { "sql": "" } },
     *   { "type": "batch_insert", "param": { "table": "xx", data: "$ALL{0}" } }
     * ])
     * 3. batch_insert支持remove_keys和update_map
     * remove_keys：移除掉data中的key
     * update_map: 更新data中的值
     * @endcode
     */
    QVariant magic(const QVariantList &v, bool showDebugInfo = false)
    {
        QVariantList stepData;

        static QRegularExpression replaceValueRegex("\\$\\{(\\d+)\\}");
        static QRegularExpression trueCondRegex("^\\$\\{(\\d+)\\}$");
        static QRegularExpression falseCondRegex("^!\\$\\{(\\d+)\\}$");

        std::function<void(QString &)> replaceStrFunc;
        replaceStrFunc = [&stepData](QString &str) {
            while (true) {
                auto match = replaceValueRegex.match(str);
                if (match.hasMatch()) {
                    int stepIndex = match.captured(1).toInt();
                    str.replace(match.captured(0), stepData.at(stepIndex).toString());
                } else {
                    break;
                }
            }
        };

        std::function<void(QVariantMap &)> replaceVarMapFunc;
        replaceVarMapFunc = [&replaceStrFunc, &stepData](QVariantMap &m){
            for (auto iter = m.begin(); iter != m.end(); iter++) {
                if (iter.value().type() == QVariant::String) {
                    QString str = iter.value().toString();
                    replaceStrFunc(str);
                    iter->setValue(str);
                }
            }
        };
        auto replaceVarListFunc = [&replaceVarMapFunc, &stepData](QVariantList &list){
            for (auto & i : list) {
                QVariantMap iMap = i.toMap();
                replaceVarMapFunc(iMap);
                i = iMap;
            }
        };
        int index = -1;
        foreach (const auto &i, v) {
            index++;
            QVariantMap iMap = i.toMap();
            const QString cond = iMap.value("cond").toString();
            const QString type = iMap.value("type").toString();
            const QVariantMap param = iMap.value("param").toMap();
            const QString magicStrategy = param.value("magic").toString();
            const QVariantMap formatMap = param.value("format").toMap();

            if (!cond.isEmpty()) {
                // 若cond有值，检查是否要执行此SQL
                auto match1 = trueCondRegex.match(cond);
                auto match2 = falseCondRegex.match(cond);
                if (match1.hasMatch()) {
                    if (showDebugInfo) {
                        qDebug().noquote() << "true cond matched";
                    }
                    int stepIndex = match1.captured(1).toInt();
                    if (stepIndex < stepData.count()) {
                        if (!stepData.at(stepIndex).toBool()) {
                            stepData.push_back(QVariant());
                            continue;
                        }
                    }
                } else if (match2.hasMatch()) {
                    if (showDebugInfo) {
                        qDebug().noquote() << "false cond matched";
                    }
                    int stepIndex = match2.captured(1).toInt();
                    if (stepIndex < stepData.count()) {
                        if (stepData.at(stepIndex).toBool()) {
                            stepData.push_back(QVariant());
                            continue;
                        }
                    }
                }
            }
            if (showDebugInfo) {
                qDebug().noquote() << "[MAGIC^] index:" << index << "type:" << type;
            }
            if (type == "insert") {
                QString table = param.value("table").toString();
                QVariantMap data = param.value("data").toMap();
                if (magicStrategy == "replace_value") {
                    replaceVarMapFunc(data);
                }
                if (showDebugInfo) {
                    qDebug().noquote() << "[TABLE]" << table;
                    qDebug().noquote() << "[DATA]" << var2str(data);
                }
                NonQueryResult res = insertInto(table).exec2(data);
                stepData.push_back(res.lastInsertId());
            } else if (type == "batch_insert") {
                QString table = param.value("table").toString();
                QVariantList data;
                QVariant::Type dataType = param.value("data").type();
                if (dataType == QVariant::String) {
                    QString temp = param.value("data").toString();
                    static QRegularExpression re("\\$ALL\\{(\\d+)\\}");
                    auto match = re.match(temp);
                    if (match.hasMatch()) {
                        int stepIndex = match.captured(1).toInt();
                        if (stepIndex < stepData.count()) {
                            data = stepData.at(stepIndex).toList();
                        }
                    }
                } else {
                    data = param.value("data").toList();
                }
                if (data.isEmpty()) {
                    stepData.push_back(0);
                    continue;
                }
                QStringList removeKeys = param.value("remove_keys").toStringList();
                if (!removeKeys.isEmpty()) {
                    for (auto & i : data) {
                        QVariantMap iMap = i.toMap();
                        foreach (const auto & k, removeKeys) {
                            iMap.remove(k);
                        }
                        i  = iMap;
                    }
                }
                QVariantMap updateMap = param.value("update_map").toMap();
                if (!updateMap.isEmpty()) {
                    for (auto & i : data) {
                        QVariantMap iMap = i.toMap();
                        for (auto iter = updateMap.begin(); iter != updateMap.end(); iter++) {
                            iMap.insert(iter.key(), iter.value());
                        }
                        i  = iMap;
                    }
                }
                if (magicStrategy == "replace_value") {
                    replaceVarListFunc(data);
                }
                QStringList fields = param.value("fields").toStringList();
                if (fields.isEmpty()) {
                    fields = data.first().toMap().keys();
                }
                if (showDebugInfo) {
                    qDebug().noquote() << "[TABLE]" << table;
                    qDebug().noquote() << "[FIELDS]" << var2str(fields);
                    qDebug().noquote() << "[DATA]" << var2str(data);
                }
                NonQueryResult res = insertInto(table).exec3(fields, data);
                stepData.push_back(res.numRowsAffected());
            } else if (type == "update") {
                QString table = param.value("table").toString();
                QString where = param.value("where").toString();
                QVariantMap data = param.value("data").toMap();
                if (magicStrategy == "replace_value") {
                    replaceVarMapFunc(data);
                    replaceStrFunc(where);
                }
                if (where.isEmpty()) {
                    where = "1=0";
                }
                if (showDebugInfo) {
                    qDebug().noquote() << "[TABLE]" << table;
                    qDebug().noquote() << "[WHERE]" << where;
                    qDebug().noquote() << "[DATA]" << var2str(data);
                }
                NonQueryResult res = update(table).set(data).where(where);
                stepData.push_back(res.numRowsAffected());
            } else if (type == "delete") {
                QString table = param.value("table").toString();
                QString where = param.value("where").toString();
                if (magicStrategy == "replace_value") {
                    replaceStrFunc(where);
                }
                if (where.isEmpty()) {
                    where = "1=0";
                }
                NonQueryResult res = deleteFrom(table).where(where);
                stepData.push_back(res.numRowsAffected());
            } else if (type == "select") {
                QString sql = param.value("sql").toString();
                if (magicStrategy == "replace_value") {
                    replaceStrFunc(sql);
                }
                if (showDebugInfo) {
                    qDebug().noquote() << "[SQL]" << sql;
                }
                QueryResult res = execQuery(sql);
                QVariantList tableData;
                while (res.next()) {
                    QVariantMap tmp = res.toMap();
                    for (auto iter = tmp.begin(); iter != tmp.end(); iter++) {
                        iter->setValue(formatSqlValue(iter.value(), formatMap.value(iter.key()).toString()));
                    }
                    tableData.push_back(tmp);
                }
                stepData.push_back(tableData);
            } else if (type == "select1") {
                QString sql = param.value("sql").toString();
                if (magicStrategy == "replace_value") {
                    replaceStrFunc(sql);
                }
                if (showDebugInfo) {
                    qDebug().noquote() << "[SQL]" << sql;
                }
                QueryResult res = execQuery(sql);
                QVariantMap data;
                if (res.next()) {
                    data = res.toMap();
                    for (auto iter = data.begin(); iter != data.end(); iter++) {
                        iter->setValue(formatSqlValue(iter.value(), formatMap.value(iter.key()).toString()));
                    }
                }
                stepData.push_back(data);
            } else if (type == "select_value") {
                QString sql = param.value("sql").toString();
                if (magicStrategy == "replace_value") {
                    replaceStrFunc(sql);
                }
                if (showDebugInfo) {
                    qDebug().noquote() << "[SQL]" << sql;
                }
                QueryResult res = execQuery(sql);
                QVariant data;
                if (res.next()) {
                    data = formatSqlValue(res.value(0), param.value("format").toString());
                }
                stepData.push_back(data);
            } else if (type == "exec") {
                QString sql = param.value("sql").toString();
                if (magicStrategy == "replace_value") {
                    replaceStrFunc(sql);
                }
                if (showDebugInfo) {
                    qDebug().noquote() << "[SQL]" << sql;
                }
                NonQueryResult res = execNonQuery(sql);
                stepData.push_back(res.numRowsAffected());
            } else if (type == "exec_insert") {
                QString sql = param.value("sql").toString();
                if (magicStrategy == "replace_value") {
                    replaceStrFunc(sql);
                }
                if (showDebugInfo) {
                    qDebug().noquote() << "[SQL]" << sql;
                }
                NonQueryResult res = execNonQuery(sql);
                stepData.push_back(res.lastInsertId());
            } else if (type == "batch_exec") {
                QString sql = param.value("sql").toString();
                QVariantList data = param.value("data").toList();
                if (showDebugInfo) {
                    qDebug().noquote() << "[SQL]" << sql;
                    qDebug().noquote() << "[DATA]" << var2str(data);
                }
                NonQueryResult res = execNonQuery_batch(sql, data);
                stepData.push_back(res.numRowsAffected());
            }
            if (showDebugInfo) {
                qDebug().noquote() << "[MAGIC$] index:" << index << "result:" << var2str(stepData);
            }
        }
        return stepData.last();
    }

protected:
    QSqlDatabase m_db;
};

/*!
\brief QSqlDatabase transaction wrapper.

Features:
 - Automatic rollback of non-expclicitely commited transactions
 - Helper methods: Transaction::execNonQuery, Transaction::execQuery, Transaction::insertInto, Transaction::deleteFrom, Transaction::update, Transaction::prepare.

\code
void test()
{
   QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE");
   db.setDatabaseName(":memory:");
   db.open();

   try
   {
      Transaction t(db);

      t.execNonQuery("CREATE TABLE table (a int, b int, c int, d text)");

      t.insertInto("table (a, b, c, d)")
         .values(1, 2, 3, "row1")
         .values(4, 5, 6, "row2")
         .values(7, 8, 9, "row3")
         .exec();

      PreparedQuery query = t.prepare("SELECT a, b, c, d FROM table");

      QueryResult res = query.exec();
      while (res.next())
      {
         QVariantMap map = res.toMap();
         qDebug() << map;
      }

      t.update("table")
         .set("a", 111)
         .set("b", 222)
         .where("c = ? OR c = ?", 3, 6);

      res = query.exec();
      while (res.next())
      {
         QVariantMap map = res.toMap();
         qDebug() << map;
      }

      t.commit(); //the transaction will be rolled back on exit from the scope (when calling the destructor) if you do not explicitly commit

   catch (const DBException &e)
   {
      //you can handle all the errors at one point
      //the transaction will be automatically rolled back on exception

      qDebug() << e.lastError << e.lastQuery;
   }
}
\endcode
*/
class  Transaction : public Database
{
    Q_DISABLE_COPY(Transaction)

public:

    explicit Transaction (const QSqlDatabase &db = QSqlDatabase())
        : Database(db)
        , m_commited(false)
        , m_started(false)
    {
        m_started = m_db.transaction();

#ifdef DB_EXCEPTIONS_ENABLED
        if (!m_started)
        {
            throw DBException(m_db);
        }
#endif
    }

    Transaction (Transaction&& other)
        : Database(std::move(other))
    {
        m_commited = other.m_commited;
        m_started  = other.m_started;

        other.m_commited = false;
        other.m_started  = false;
    }

    Transaction& operator=(Transaction&& other)
    {
        m_started  = other.m_started;
        m_commited = other.m_commited;

        other.m_commited = false;
        other.m_started  = false;

        return static_cast<Transaction&>(Database::operator=(std::move(other)));
    }

    ~Transaction()
    {
        if (m_db.isValid() && !m_commited)
        {
            m_db.rollback();
        }
    }

    /*!
   \brief Commits transaction

   The transaction will be rolled back on calling the destructor if not explicitly commited

   \throws DBException
   */
    bool commit()
    {
        if (m_db.isValid() && !m_commited)
        {
            m_commited = m_db.commit();

#ifdef DB_EXCEPTIONS_ENABLED

            if (!m_commited)
                throw DBException(m_db);

#endif

        }

        return m_commited;
    }

    /*!
   \brief Rolls back transaction
   */
    bool rollback()
    {
        bool res = false;

        if (m_db.isValid() && !m_commited)
        {
            res = m_db.rollback();

            m_commited = false;
        }

        return res;
    }

    /*!
   \brief Returns true if the transaction has been started successfully. Otherwise it returns false.
   */
    bool started() const
    {
        return m_started;
    }

    /*!
   \brief Returns true if the transaction has been commited successfully. Otherwise it returns false.
   */
    bool commited() const
    {
        return m_commited;
    }

private:
    bool m_commited = false;
    bool m_started = false;
};

#endif // EASYQTSQL_TRANSACTION_H
