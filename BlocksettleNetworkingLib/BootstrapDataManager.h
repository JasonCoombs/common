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

enum class BootstrapFileError : int {
   NoError,
   ReadError,
   InvalidFormat,
   InvalidSign,
};

class BootstrapDataManager
{
public:
   BootstrapDataManager(const std::shared_ptr<spdlog::logger> &logger
      , const std::shared_ptr<ApplicationSettings> &appSettings
      , const std::shared_ptr<AuthAddressManager> &authAddressManager
      , const std::shared_ptr<CCFileManager> &ccFileManager);

   bool hasLocalFile() const;
   void setReceivedData(const BinaryData &data);
   BootstrapFileError loadSavedData();

   static bool verifySignature(const BinaryData &data, const BinaryData &signatureStr
      , const bs::Address &signAddress);

private:
   void processResponse(const std::string& response, const std::string &sig);
   bool saveToFile(const std::string &path, const std::string &data, const std::string &sig);
   bool isTestNet() const;
   BootstrapFileError loadFromFile(const std::string &path, NetworkType netType);

   std::shared_ptr<spdlog::logger>        logger_;
   std::shared_ptr<ApplicationSettings>   appSettings_;
   const bs::Address                      signAddress_;
   QString                                bootstapFilePath_;
   int                                    currentRev_{};
   std::shared_ptr<AuthAddressManager>    authAddressManager_;
   std::shared_ptr<CCFileManager>         ccFileManager_;

};

#endif // BOOTSTRAP_DATA_MANAGER_H
