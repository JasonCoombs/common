/*

***********************************************************************************
* Copyright (C) 2019 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "DatabaseCreator.h"

#include <set>
#include <QString>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QThread>

#include <disable_warnings.h>
#include <spdlog/logger.h>
#include <enable_warnings.h>

using namespace Chat;

DatabaseCreator::DatabaseCreator(const QSqlDatabase& db, const LoggerPtr& loggerPtr, QObject* parent /* = nullptr */)
   : QObject(parent)
{
   db_ = db;
   loggerPtr_ = loggerPtr;
}

void DatabaseCreator::rebuildDatabase()
{
   if (createMissingTables())
   {
      emit rebuildDone();
      return;
   }

   emit rebuildError();
}

bool DatabaseCreator::createMissingTables()
{
   QStringList existingTables;
   auto result = true;

   try {
      existingTables = db_.tables();
   }
   catch (std::exception&)
   {
      loggerPtr_->error("[DatabaseCreator::createMissingTables]: Failed to create tables in {}, Error: {}", db_.connectionName().toStdString(), db_.lastError().text().toStdString());
      return false;
   }

   std::set<QString> tableSet;
   tableSet.insert(existingTables.begin(), existingTables.end());

   for (const auto& reqTable : requiredTables_)
   {
      if (tableSet.find(reqTable) == tableSet.end())
      {
         if (!tablesMap_.contains(reqTable))
         {
            loggerPtr_->debug("[DatabaseCreator::createMissingTables] Required table '{}' not found in tables description", reqTable.toStdString());
            result = false;
            break;
         }

         QString createCmd = buildCreateCmd(reqTable, tablesMap_.value(reqTable));
         loggerPtr_->debug("[DatabaseCreator::createMissingTables] Build create cmd : {}\n", createCmd.toStdString());

         loggerPtr_->debug("[DatabaseCreator::createMissingTables] creating table {}", reqTable.toStdString());

         QSqlQuery createQuery;
         const auto rc = ExecuteQuery(createCmd, createQuery);

         if (!rc)
         {
            loggerPtr_->warn("[DatabaseCreator::createMissingTables] failed to create table {}", reqTable.toStdString());
         }

         result &= rc;
      }
      // NOTE: checkColumns use SQL command DESCRIBE to get table columns, and
      //       that command is not supported by sqlite, which is current DB
      //       driver for chat
      // else
      // {
      //    loggerPtr_->debug("[DatabaseCreator::createMissingTables] table '{}' exists. Checking...", reqTable.toStdString());
      //    if (!checkColumns(reqTable))
      //    {
      //       return false;
      //    }
      // }
   }
   return result;
}

QString DatabaseCreator::buildCreateCmd(const QString& tableName, const TableStructure& structure)
{
   const QString cmd = QLatin1String("CREATE TABLE IF NOT EXISTS %1 (%2);");

   QStringList queryParts;

   for (const auto& colDescription : structure.tableColumns)
   {
      QStringList column{ colDescription.columnName, colDescription.columnType };
      queryParts << column.join(QLatin1Char(' '));
   }

   for (const auto& foreignKey : structure.foreignKeys)
   {
      QStringList parts{
         QStringLiteral("FOREIGN KEY (%1)").arg(foreignKey.columnKey),
         QStringLiteral("REFERENCES %1(%2)")
               .arg(foreignKey.foreignTable)
               .arg(foreignKey.foreignColumn),
         QStringLiteral("%1").arg(foreignKey.foreignReferentialAction)
      };

      queryParts << parts.join(QLatin1Char(' '));
   }

   for (const auto& uniqueCondition : structure.uniqueConditions)
   {
      queryParts << QStringLiteral("UNIQUE (%1, %2)")
         .arg(uniqueCondition.firstColumn)
         .arg(uniqueCondition.secondColumn);
   }

   return cmd
      .arg(tableName)
      .arg(queryParts.join(QLatin1String(", ")));
}

bool DatabaseCreator::checkColumns(const QString& tableName) const
{
   auto cmd = QStringLiteral("DESCRIBE %1").arg(tableName);

   QStringList tableColumns;
   QSqlQuery infoQuery;
   if (ExecuteQuery(cmd, infoQuery))
   {
      while (infoQuery.next())
      {
         tableColumns << infoQuery.value(0).toString();
      }
   }
   else
   {
      // describe failed, check if you can list columns from sqlite
      cmd = QStringLiteral("PRAGMA table_info(%1)").arg(tableName);

      if (!ExecuteQuery(cmd, infoQuery))
      {
         throw std::runtime_error("Can't get table info (table: " + tableName.toStdString() + ")");
      }

      while (infoQuery.next())
      {
         tableColumns << infoQuery.value(1).toString();
      }
   }

   const auto& tableStruct = tablesMap_.value(tableName);

   for (const auto& columnItem : tableStruct.tableColumns)
   {
      loggerPtr_->debug("[DatabaseCreator::checkColumns] Check column: {}", columnItem.columnName.toStdString());

      if (!tableColumns.contains(columnItem.columnName))
      {
         loggerPtr_->debug("[DatabaseCreator::checkColumns] Column: {} not exists... Creating with type : {}",
            columnItem.columnName.toStdString(),
            columnItem.columnType.toStdString());

         QString alterCmd = QStringLiteral("ALTER TABLE %1 ADD COLUMN %2 %3;")
            .arg(tableName)
            .arg(columnItem.columnName)
            .arg(columnItem.columnType);

         QSqlQuery alterQuery;
         if (!ExecuteQuery(alterCmd, alterQuery))
         {
            loggerPtr_->debug("[DatabaseCreator::checkColumns] Can't alter table (table: {})", columnItem.columnName.toStdString());
            throw std::runtime_error("Can't alter table (table: " + tableName.toStdString() + ")");
         }
      }
      else
      {
         loggerPtr_->debug("[DatabaseCreator::checkColumns] Column: {}  already exists!", columnItem.columnName.toStdString());
      }
   }

   return true;
}

bool DatabaseCreator::ExecuteQuery(const QString& queryCmd, QSqlQuery& query) const
{
   const QSqlQuery q(db_);
   query = q;

   if (!query.prepare(QLatin1String(queryCmd.toLatin1())))
   {
      loggerPtr_->debug("[DatabaseCreator::ExecuteQuery] Cannot prepare query: {}", queryCmd.toStdString());
      return false;
   }

   if (!query.exec())
   {
      loggerPtr_->error("[DatabaseCreator::ExecuteQuery]: Requested query execution error: Query: {}, Error: {}",
         query.executedQuery().toStdString(), query.lastError().text().toStdString());
      return false;
   }

   return true;
}
