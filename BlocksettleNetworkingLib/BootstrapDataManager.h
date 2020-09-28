/*

***********************************************************************************
* Copyright (C) 2020 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef BOOTSTRAP_DATA_MANAGER_H
#define BOOTSTRAP_DATA_MANAGER_H

#include <functional>
#include <memory>
#include <vector>

#include <QString>
#include <QVariant>
#include <QPointer>

#include "Address.h"
#include "BtcDefinitions.h"

namespace spdlog {
   class logger;
}
namespace Blocksettle {
   namespace Communication {
      class BootstrapData;
   }
}

class ApplicationSettings;
class AuthAddressManager;
class CCFileManager;

class BootstrapDataManager
{
public:
   BootstrapDataManager(const std::shared_ptr<spdlog::logger> &logger
      , const std::shared_ptr<ApplicationSettings> &appSettings
      , const std::shared_ptr<AuthAddressManager> &authAddressManager
      , const std::shared_ptr<CCFileManager> &ccFileManager);

   bool hasLocalFile() const;

   bool loadFromLocalFile();
   bool setReceivedData(const std::string& data);

   std::string getProxyKey() const;
   std::string getChatKey() const;
   std::string getCCTrackerKey() const;

private:
   bool loadData(const std::string& data);
   bool processBootstrapData(const std::string& data);
   bool saveToLocalFile(const std::string &data);

   std::shared_ptr<spdlog::logger>        logger_;
   std::shared_ptr<ApplicationSettings>   appSettings_;

   std::shared_ptr<AuthAddressManager>    authAddressManager_;
   std::shared_ptr<CCFileManager>         ccFileManager_;

   int         currentRev_ = 0;
   std::string proxyKey_;
   std::string chatKey_;
   std::string ccTrackerKey_;
};

#endif // BOOTSTRAP_DATA_MANAGER_H
