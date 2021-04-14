/*

***********************************************************************************
* Copyright (C) 2018 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __JSON_TOOLS_H__
#define __JSON_TOOLS_H__

#include <QString>
#include <QVariantMap>

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

class QJsonValue;

namespace JsonTools
{
   // converted set to true if double was extracted successfully
   double GetDouble(const QJsonValue& value, bool& converted);
   double GetDouble(const QJsonValue& value);
   int64_t GetInt64(const QJsonValue& value);

   QString  GetStringProperty(const QVariantMap& settingsMap, const QString& propertyName);
   double   GetDoubleProperty(const QVariantMap& settingsMap, const QString& propertyName, bool *converted = nullptr);
   uint64_t GetUIntProperty(const QVariantMap& settingsMap, const QString& propertyName, bool *converted = nullptr);

   double GetDouble(const nlohmann::json& jsonObject, const std::string& propertyName, bool *converted = nullptr);
   double GetDoubleProperty(const nlohmann::json& jsonObject, const std::string& propertyName, bool *converted = nullptr);
   double GetDoubleFromObject(const nlohmann::json& jsonObject, bool *converted = nullptr);

   enum class FieldsLoadingRule
   {
      NonEmptyOnly,
      EmptyAllowed
   };

   bool LoadStringFields(const QVariantMap& data, std::vector<std::pair<QString, std::string*>>& fields
                         , std::string &errorMessage, const FieldsLoadingRule loadingRule = FieldsLoadingRule::NonEmptyOnly);
   bool LoadIntFields(const QVariantMap& data, std::vector<std::pair<QString, int64_t*>>& fields
                      , std::string &errorMessage, const FieldsLoadingRule loadingRule = FieldsLoadingRule::NonEmptyOnly);
   bool LoadDoubleFields(const QVariantMap& data, std::vector<std::pair<QString, double*>>& fields
                         , std::string &errorMessage, const FieldsLoadingRule loadingRule = FieldsLoadingRule::NonEmptyOnly);
}

#endif // __JSON_TOOLS_H__
