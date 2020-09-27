/*

***********************************************************************************
* Copyright (C) 2020 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "BootstrapDataManager.h"

#include "ApplicationSettings.h"
#include "AuthAddressManager.h"
#include "CCFileManager.h"
#include "EncryptionUtils.h"

#include <spdlog/spdlog.h>

#include <QFile>

#include "bs_communication.pb.h"
#include "bs_storage.pb.h"

using namespace Blocksettle::Communication;


BootstrapDataManager::BootstrapDataManager(const std::shared_ptr<spdlog::logger> &logger
   , const std::shared_ptr<ApplicationSettings> &appSettings
   , const std::shared_ptr<AuthAddressManager> &authAddressManager
   , const std::shared_ptr<CCFileManager> &ccFileManager)
   : appSettings_(appSettings)
   , signAddress_(bs::Address::fromAddressString(appSettings_->GetBlocksettleSignAddress()))
   , bootstapFilePath_(appSettings->bootstrapFilePath())
   , authAddressManager_(authAddressManager)
   , ccFileManager_(ccFileManager)
{
}

bool BootstrapDataManager::hasLocalFile() const
{
   return QFile(bootstapFilePath_).exists();
}

bool BootstrapDataManager::setReceivedData(const std::string& data)
{
   if (data.empty()) {
      logger_->error("[BootstrapDataManager::setReceivedData] empty data");
      return false;
   }

   ResponsePacket response;
   if (!response.ParseFromString(data)) {
      logger_->error("[BootstrapDataManager::setReceivedData] failed to parse bootstrap data");
      return false;
   }

   switch (response.responsetype()) {
      case RequestType::BootstrapSignedDataType:
         return processResponse(response.responsedata(), response.datasignature());
         break;
      default:
         logger_->error("[BootstrapDataManager::setReceivedData] undefined response type {}"
                        , static_cast<int>(response.responsetype()));
         break;
   }

   return false;
}

BootstrapFileError BootstrapDataManager::loadSavedData()
{
   auto loadError = loadFromFile(bootstapFilePath_.toStdString(), appSettings_->get<NetworkType>(ApplicationSettings::netType));
   if (loadError != BootstrapFileError::NoError) {
      QFile::remove(bootstapFilePath_);
   }
   return loadError;
}

bool BootstrapDataManager::processResponse(const std::string &response, const std::string &sig)
{
   bool sigVerified = verifySignature(BinaryData::fromString(response), BinaryData::fromString(sig), signAddress_);
   if (!sigVerified) {
      SPDLOG_LOGGER_ERROR(logger_, "signature verification failed! Rejecting CC genesis addresses reply.");
      return false;
   }

   BootstrapData data;

   if (!data.ParseFromString(response)) {
      SPDLOG_LOGGER_ERROR(logger_, "data corrupted. Could not parse.");
      return false;
   }

   if (data.is_testnet() != (appSettings_->get<NetworkType>(ApplicationSettings::netType) == NetworkType::TestNet)) {
      SPDLOG_LOGGER_ERROR(logger_, "network type mismatch in reply");
      return false;
   }

   if (data.revision() < currentRev_) {
      SPDLOG_LOGGER_ERROR(logger_, "proxy has older revision {} than we ({})"
         , data.revision(), currentRev_);
      return false;
   }

   // authAddressManager_ is updated only after login (so need to do that before revision check)
   authAddressManager_->ProcessBSAddressListResponse(data);

   if (data.revision() == currentRev_) {
      SPDLOG_LOGGER_DEBUG(logger_, "having the same revision already");
      return true;
   }

   ccFileManager_->ProcessGenAddressesResponse(data);

   return saveToFile(bootstapFilePath_.toStdString(), response, sig);
}

bool BootstrapDataManager::saveToFile(const std::string &path, const std::string &response, const std::string &sig)
{
   Blocksettle::Storage::CCDefinitions msg;
   msg.set_response(response);
   msg.set_signature(sig);
   auto data = msg.SerializeAsString();

   QFile f(QString::fromStdString(path));
   if (!f.open(QIODevice::WriteOnly)) {
      SPDLOG_LOGGER_ERROR(logger_, "failed to open file {} for writing", path);
      return false;
   }

   auto writeSize = f.write(response.data(), static_cast<int>(response.size()));
   if (static_cast<int>(response.size()) != writeSize) {
      SPDLOG_LOGGER_ERROR(logger_, "failed to write to {}", path);
      return false;
   }

   return true;
}

bool BootstrapDataManager::isTestNet() const
{
   return appSettings_->get<NetworkType>(ApplicationSettings::netType) != NetworkType::MainNet;
}


bool BootstrapDataManager::verifySignature(const BinaryData &data, const BinaryData &sign, const bs::Address &signAddress)
{
   return ArmorySigner::Signer::verifyMessageSignature(data, signAddress.prefixed(), sign);
}

BootstrapFileError BootstrapDataManager::loadFromFile(const std::string &path, NetworkType netType)
{
   QFile f(QString::fromStdString(path));
   if (!f.exists()) {
      SPDLOG_LOGGER_DEBUG(logger_, "no bootstrap file to load at {}", path);
      return BootstrapFileError::ReadError;
   }
   if (!f.open(QIODevice::ReadOnly)) {
      SPDLOG_LOGGER_ERROR(logger_, "failed to open file {} for reading", path);
      return BootstrapFileError::ReadError;
   }

   const auto buf = f.readAll();
   if (buf.isEmpty()) {
      SPDLOG_LOGGER_ERROR(logger_, "failed to read from {}", path);
      return BootstrapFileError::ReadError;
   }

   Blocksettle::Storage::CCDefinitions msg;
   bool result = msg.ParseFromArray(buf.data(), buf.size());
   if (!result) {
      SPDLOG_LOGGER_ERROR(logger_, "failed to parse storage file");
      return BootstrapFileError::InvalidFormat;
   }

   result = verifySignature(BinaryData::fromString(msg.response()), BinaryData::fromString(msg.signature()), signAddress_);
   if (!result) {
      SPDLOG_LOGGER_ERROR(logger_, "signature verification failed for {}", path);
      return BootstrapFileError::InvalidSign;
   }

   BootstrapData data;
   if (!data.ParseFromString(msg.response())) {
      SPDLOG_LOGGER_ERROR(logger_, "failed to parse {}", path);
      return BootstrapFileError::InvalidFormat;
   }

   if (data.is_testnet() != (netType == NetworkType::TestNet)) {
      SPDLOG_LOGGER_ERROR(logger_, "wrong network type in {}", path);
      return BootstrapFileError::InvalidFormat;
   }

   ccFileManager_->ProcessGenAddressesResponse(data);

   return BootstrapFileError::NoError;
}
